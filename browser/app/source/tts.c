#include "tts.h"

#include <espeak-ng/speak_lib.h>

#include <stdio.h>
#include <string.h>

// Main-thread stack.
//
// eSpeak needs a lot of stack, but synthesis runs on the worker thread below,
// which carries its own 512 KB. Only espeak_Initialize runs on the caller's
// thread, and that is comparatively shallow.
//
// This was 512 KB and it broke launching from hbmenu: the loader set the new
// process's stack pointer to 0x00080000 -- exactly 512*1024 -- with nothing
// mapped below it, so libctru's initSystem faulted on its first push, before
// main() ever ran. 128 KB is ample for init and stays inside what the 3dsx
// loader will actually give us.
unsigned int __stacksize__ = 128 * 1024;

// eSpeak looks for espeak-ng-data inside this directory.
static const char *kDataParent = "sdmc:/xyoras-access";

// A high channel deliberately. Hosts that play their own sounds (Universal-
// Updater has a Sound class; hbmenu has none) take the low ones, and two users
// on a single channel means whoever writes last wins.
#define TTS_CHANNEL 7

// Audio is submitted in chunks as eSpeak produces it rather than accumulating a
// whole utterance first, so speech starts after the first chunk instead of
// after the whole sentence.
//
// The ring is deliberately generous: 8 x 8192 samples is about three seconds of
// queued audio at 22 kHz. A smaller ring underruns whenever synthesis hitches,
// and an underrun on the DSP is audible as a pop rather than a pause.
#define CHUNK_SAMPLES 8192
#define NUM_BUFFERS   8

static s16         *s_buf[NUM_BUFFERS];
static ndspWaveBuf  s_wb[NUM_BUFFERS];
static int          s_fill;
static size_t       s_fillPos;

static LightLock     s_lock;
static LightEvent    s_wake;
static Thread        s_worker;
static volatile bool s_running;

static char          s_pending[512];
static volatile bool s_hasPending;

// Bumped by every tts_say() and tts_stop(). The synth callback compares the
// generation it began with against the current one and aborts if superseded, so
// holding the d-pad cancels mid-word instead of queueing a backlog.
static volatile u32 s_generation;
static u32          s_synthGen;

// Samples over which an utterance ramps up at its start and down at its end.
// eSpeak begins and ends at whatever amplitude the waveform happens to be at,
// and a step change in a speaker cone is a click. ~6 ms at 22 kHz is long
// enough to remove it and far too short to hear as a fade.
#define DECLICK_SAMPLES 128

static size_t   s_utterPos;      // samples written so far this utterance

static bool     s_ready;
static char     s_error[128];
static unsigned s_lastSamples;

// Hands the current chunk to the DSP, waiting for that buffer to finish playing
// if it is still in use. Returns false if the utterance was superseded while
// waiting, which unwinds synthesis promptly.
static bool FlushChunk(void)
{
    if (s_fillPos == 0)
        return true;

    ndspWaveBuf *wb = &s_wb[s_fill];

    while (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING)
    {
        if (s_synthGen != s_generation)
            return false;
        svcSleepThread(1000000ULL);   // 1 ms
    }

    if (s_synthGen != s_generation)
        return false;

    DSP_FlushDataCache(s_buf[s_fill], CHUNK_SAMPLES * sizeof(s16));

    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = s_buf[s_fill];
    wb->nsamples   = (u32)s_fillPos;
    ndspChnWaveBufAdd(TTS_CHANNEL, wb);

    s_lastSamples += (unsigned)s_fillPos;
    s_fill = (s_fill + 1) % NUM_BUFFERS;
    s_fillPos = 0;
    return true;
}

static int SynthCallback(short *wav, int numsamples, espeak_EVENT *events)
{
    (void)events;

    if (s_synthGen != s_generation)
        return 1;   // non-zero aborts synthesis

    if (wav == NULL)
    {
        // Ramp the tail down so the utterance ends at silence rather than
        // stopping dead at whatever amplitude it reached.
        if (s_fillPos > 0)
        {
            const size_t n = (s_fillPos < DECLICK_SAMPLES) ? s_fillPos : DECLICK_SAMPLES;
            s16 *w = s_buf[s_fill] + s_fillPos - n;
            for (size_t k = 0; k < n; ++k)
            {
                w[k] = (s16)((int)w[k] * (int)(n - 1 - k) / (int)n);
            }
        }

        FlushChunk();   // push whatever is left
        return 0;
    }

    int done = 0;
    while (done < numsamples)
    {
        const size_t room = CHUNK_SAMPLES - s_fillPos;
        size_t take = (size_t)(numsamples - done);
        if (take > room)
            take = room;

        memcpy(s_buf[s_fill] + s_fillPos, wav + done, take * sizeof(s16));

        // Ramp the first few milliseconds up from silence.
        if (s_utterPos < DECLICK_SAMPLES)
        {
            s16 *w = s_buf[s_fill] + s_fillPos;
            for (size_t k = 0; k < take && (s_utterPos + k) < DECLICK_SAMPLES; ++k)
            {
                w[k] = (s16)((int)w[k] * (int)(s_utterPos + k) / DECLICK_SAMPLES);
            }
        }

        s_utterPos += take;
        s_fillPos += take;
        done += (int)take;

        if (s_fillPos == CHUNK_SAMPLES && !FlushChunk())
            return 1;
    }
    return 0;
}

// Sets the channel mix to a uniform gain on the front pair.
static void SetChannelGain(float g)
{
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = g;   // front left
    mix[1] = g;   // front right
    ndspChnSetMix(TTS_CHANNEL, mix);
}

// Silences the channel before dropping its buffers.
//
// Interrupting speech mid-word stops the waveform at an arbitrary amplitude,
// and that step is a click -- the one you hear on every cursor move when
// arrowing quickly. Ramping the mix down over a few DSP frames first removes
// it. This runs on the worker, so the ~16 ms it costs never touches input or
// drawing, and it is skipped entirely when nothing is playing.
static void StopChannelQuietly(void)
{
    if (ndspChnIsPlaying(TTS_CHANNEL))
    {
        for (int step = 3; step >= 0; --step)
        {
            SetChannelGain((float)step / 4.0f);
            svcSleepThread(4000000ULL);   // 4 ms, a little over one DSP frame
        }
    }

    ndspChnWaveBufClear(TTS_CHANNEL);
    SetChannelGain(1.0f);
}

static void WorkerMain(void *arg)
{
    (void)arg;

    while (s_running)
    {
        LightEvent_Wait(&s_wake);
        if (!s_running)
            break;

        char text[sizeof(s_pending)];
        bool have = false;

        LightLock_Lock(&s_lock);
        if (s_hasPending)
        {
            memcpy(text, s_pending, sizeof(text));
            s_hasPending = false;
            s_synthGen = s_generation;
            have = true;
        }
        LightLock_Unlock(&s_lock);

        if (!have)
            continue;

        // Stop whatever is playing and take the buffers back before writing a
        // single sample of the new utterance. memset matters: after a clear the
        // wavebuf structs still hold stale QUEUED/PLAYING status, and FlushChunk
        // would wait forever on them.
        StopChannelQuietly();
        for (int i = 0; i < NUM_BUFFERS; ++i)
            memset(&s_wb[i], 0, sizeof(s_wb[i]));

        s_fill = 0;
        s_fillPos = 0;
        s_utterPos = 0;
        s_lastSamples = 0;

        espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0,
                     espeakCHARS_UTF8, NULL, NULL);
        espeak_Synchronize();
    }
}

bool tts_init(void)
{
    s_error[0] = 0;

    for (int i = 0; i < NUM_BUFFERS; ++i)
    {
        s_buf[i] = (s16 *)linearAlloc(CHUNK_SAMPLES * sizeof(s16));
        if (s_buf[i] == NULL)
        {
            snprintf(s_error, sizeof(s_error), "linearAlloc failed");
            return false;
        }
        memset(&s_wb[i], 0, sizeof(s_wb[i]));
    }

    // libctru reference-counts ndspInit, so a host that already brought audio up
    // returns success here rather than failing.
    Result res = ndspInit();
    if (R_FAILED(res))
    {
        snprintf(s_error, sizeof(s_error),
                 "ndspInit %08lX - need sdmc:/3ds/dspfirm.cdc (may be empty)",
                 (unsigned long)res);
        return false;
    }

    // Not implicitly 1.0. Without this the channel plays into silence while
    // every status indicator looks perfectly healthy.
    ndspSetMasterVol(1.0f);

    ndspChnReset(TTS_CHANNEL);
    ndspChnInitParams(TTS_CHANNEL);
    ndspChnSetInterp(TTS_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(TTS_CHANNEL, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetPaused(TTS_CHANNEL, false);

    SetChannelGain(1.0f);

    int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, kDataParent, 0);
    if (rate <= 0)
    {
        snprintf(s_error, sizeof(s_error),
                 "espeak_Initialize %d - want %s/espeak-ng-data", rate, kDataParent);
        return false;
    }

    ndspChnSetRate(TTS_CHANNEL, (float)rate);
    espeak_SetSynthCallback(SynthCallback);
    espeak_SetParameter(espeakRATE, 180, 0);

    LightLock_Init(&s_lock);
    LightEvent_Init(&s_wake, RESET_ONESHOT);

    s_running = true;

    // Synthesis happens on this thread, so it carries eSpeak's stack needs.
    // Slightly below the caller so speech never starves drawing or input.
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_worker = threadCreate(WorkerMain, NULL, 512 * 1024, prio + 1, -1, false);
    if (s_worker == NULL)
    {
        s_running = false;
        snprintf(s_error, sizeof(s_error), "threadCreate failed");
        return false;
    }

    s_ready = true;
    return true;
}

void tts_exit(void)
{
    if (s_running)
    {
        s_running = false;
        s_generation++;             // unblock a synth waiting on a full buffer
        LightEvent_Signal(&s_wake);
        threadJoin(s_worker, U64_MAX);
        threadFree(s_worker);
        s_worker = NULL;
    }

    if (s_ready)
    {
        espeak_Terminate();
        ndspExit();
        s_ready = false;
    }

    for (int i = 0; i < NUM_BUFFERS; ++i)
    {
        if (s_buf[i])
        {
            linearFree(s_buf[i]);
            s_buf[i] = NULL;
        }
    }
}

void tts_stop(void)
{
    if (!s_ready)
        return;

    s_generation++;
    ndspChnWaveBufClear(TTS_CHANNEL);
}

void tts_say(const char *text)
{
    if (!s_ready || text == NULL || *text == 0)
        return;

    LightLock_Lock(&s_lock);
    s_generation++;                 // supersede anything in flight
    snprintf(s_pending, sizeof(s_pending), "%s", text);
    s_hasPending = true;
    LightLock_Unlock(&s_lock);

    // The worker does the actual stop, immediately before it starts the new
    // utterance. Bumping the generation above already makes the in-flight synth
    // abort at its next callback, so the delay is a callback interval and the
    // two threads never fight over the buffers.
    LightEvent_Signal(&s_wake);
}

bool        tts_ready(void)        { return s_ready; }
const char *tts_last_error(void)   { return s_error; }
unsigned    tts_last_samples(void) { return s_lastSamples; }
