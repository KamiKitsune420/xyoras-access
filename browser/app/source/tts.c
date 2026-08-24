#include "tts.h"

#include <espeak-ng/speak_lib.h>

#include <stdio.h>
#include <string.h>

// ~8 seconds at 22 kHz. Longer than any filename we will read; anything past
// this is dropped rather than allowed to run off the buffer.
#define MAX_SAMPLES (22050 * 8)

// eSpeak recurses deeply and uses large stack frames. libctru's default 32 KB
// main-thread stack overflows a few seconds in: the stack pointer runs below the
// heap base, a corrupted return address is read back off it, and the app faults
// at PC 0 with a NoExecuteFault. Defined here so every host that links this
// module inherits it, rather than each one rediscovering the crash.
unsigned int __stacksize__ = 512 * 1024;

// eSpeak looks for espeak-ng-data inside this directory.
static const char *kDataParent = "sdmc:/xyoras-access";

static s16        *s_pcm;        // linear memory: DSP reads it by physical address
static size_t      s_samples;
static ndspWaveBuf s_wavebuf;
static bool        s_ready;
static char        s_error[128];

static int SynthCallback(short *wav, int numsamples, espeak_EVENT *events)
{
    (void)events;

    if (wav == NULL || numsamples <= 0)
        return 0;   // wav == NULL marks end of utterance

    size_t room = MAX_SAMPLES - s_samples;
    size_t take = (size_t)numsamples < room ? (size_t)numsamples : room;
    if (take == 0)
        return 0;

    memcpy(s_pcm + s_samples, wav, take * sizeof(s16));
    s_samples += take;
    return 0;
}

bool tts_init(void)
{
    s_error[0] = '\0';

    s_pcm = (s16 *)linearAlloc(MAX_SAMPLES * sizeof(s16));
    if (s_pcm == NULL)
    {
        snprintf(s_error, sizeof(s_error), "linearAlloc failed");
        return false;
    }

    Result res = ndspInit();
    if (R_FAILED(res))
    {
        // Overwhelmingly this means sdmc:/3ds/dspfirm.cdc is missing. libctru
        // refuses to start ndsp without it and reports only a bare DSP result
        // code. Azahar HLEs the DSP, so an EMPTY file is sufficient.
        snprintf(s_error, sizeof(s_error),
                 "ndspInit %08lX - need sdmc:/3ds/dspfirm.cdc (may be empty)",
                 (unsigned long)res);
        return false;
    }

    // Not implicitly 1.0. Without this the channel plays into silence while
    // every status indicator looks perfectly healthy.
    ndspSetMasterVol(1.0f);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    ndspChnReset(0);
    ndspChnInitParams(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetPaused(0, false);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;   // front left
    mix[1] = 1.0f;   // front right
    ndspChnSetMix(0, mix);

    int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, kDataParent, 0);
    if (rate <= 0)
    {
        snprintf(s_error, sizeof(s_error),
                 "espeak_Initialize %d - want %s/espeak-ng-data", rate, kDataParent);
        return false;
    }

    ndspChnSetRate(0, (float)rate);
    espeak_SetSynthCallback(SynthCallback);
    espeak_SetParameter(espeakRATE, 180, 0);

    s_ready = true;
    return true;
}

void tts_exit(void)
{
    if (s_ready)
    {
        espeak_Terminate();
        ndspExit();
        s_ready = false;
    }
    if (s_pcm)
    {
        linearFree(s_pcm);
        s_pcm = NULL;
    }
}

void tts_stop(void)
{
    if (s_ready)
        ndspChnWaveBufClear(0);
}

void tts_say(const char *text)
{
    if (!s_ready || text == NULL || *text == '\0')
        return;

    ndspChnWaveBufClear(0);
    s_samples = 0;

    espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0,
                 espeakCHARS_UTF8, NULL, NULL);
    espeak_Synchronize();

    if (s_samples == 0)
        return;

    DSP_FlushDataCache(s_pcm, s_samples * sizeof(s16));

    memset(&s_wavebuf, 0, sizeof(s_wavebuf));
    s_wavebuf.data_vaddr = s_pcm;
    s_wavebuf.nsamples   = (u32)s_samples;
    ndspChnWaveBufAdd(0, &s_wavebuf);
}

bool        tts_ready(void)        { return s_ready; }
const char *tts_last_error(void)   { return s_error; }
unsigned    tts_last_samples(void) { return (unsigned)s_samples; }
