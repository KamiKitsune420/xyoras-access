// An accessible SD-card browser for the 3DS: everything under the cursor is
// spoken aloud, so it can be used without seeing the screen.
//
// This is the route that works without a console. Reading Nintendo's Home Menu
// needs a 3GX plugin, and Home Menu fails the plugin loader's title mask, so a
// plugin can never attach to it. A homebrew launcher with speech built in
// addresses the same problem -- a blind user who cannot navigate the system
// unaided -- and runs today.
//
// See "AI docks/15-home-menu-screen-reader.md".

#include <3ds.h>

#include "tts.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Stack size now lives in tts.c, so the speech module carries its own
// requirement into whatever host links it.

#define MAX_ENTRIES  512
#define NAME_MAX_LEN 256
#define VISIBLE_ROWS 20

typedef struct
{
    char name[NAME_MAX_LEN];
    bool is_dir;
} Entry;

static Entry s_entries[MAX_ENTRIES];
static int   s_count;
static int   s_cursor;
static int   s_scroll;
static char  s_cwd[512] = "sdmc:/";

static int CompareEntries(const void *a, const void *b)
{
    const Entry *ea = (const Entry *)a;
    const Entry *eb = (const Entry *)b;

    // Directories first, then case-insensitive by name -- predictable order
    // matters more when you cannot see the list.
    if (ea->is_dir != eb->is_dir)
        return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

static void ReadCurrentDir(void)
{
    s_count  = 0;
    s_cursor = 0;
    s_scroll = 0;

    DIR *dir = opendir(s_cwd);
    if (dir == NULL)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_count < MAX_ENTRIES)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        // snprintf rather than strncpy: always NUL-terminates, and does not
        // warn about truncating a name that exactly fills the buffer.
        snprintf(s_entries[s_count].name, NAME_MAX_LEN, "%s", ent->d_name);
        s_entries[s_count].is_dir = (ent->d_type == DT_DIR);
        s_count++;
    }
    closedir(dir);

    qsort(s_entries, s_count, sizeof(Entry), CompareEntries);
}

// What the cursor is on, said the way a screen reader should: what it is, its
// name, and where it sits in the list, so you always know how far you have got.
static void AnnounceCursor(bool includeLocation)
{
    char phrase[600];

    if (s_count == 0)
    {
        snprintf(phrase, sizeof(phrase), "%s. Empty folder.",
                 includeLocation ? s_cwd : "");
        tts_say(phrase);
        return;
    }

    const Entry *e = &s_entries[s_cursor];

    if (includeLocation)
    {
        snprintf(phrase, sizeof(phrase), "%s. %s %s. %d of %d.",
                 s_cwd, e->is_dir ? "Folder" : "File", e->name,
                 s_cursor + 1, s_count);
    }
    else
    {
        snprintf(phrase, sizeof(phrase), "%s %s. %d of %d.",
                 e->is_dir ? "Folder" : "File", e->name,
                 s_cursor + 1, s_count);
    }
    tts_say(phrase);
}

static void EnterSelected(void)
{
    if (s_count == 0 || !s_entries[s_cursor].is_dir)
        return;

    size_t len = strlen(s_cwd);
    if (len + strlen(s_entries[s_cursor].name) + 2 >= sizeof(s_cwd))
        return;

    if (len > 0 && s_cwd[len - 1] != '/')
        strcat(s_cwd, "/");
    strcat(s_cwd, s_entries[s_cursor].name);

    ReadCurrentDir();
    AnnounceCursor(true);
}

static void GoUp(void)
{
    // "sdmc:/" is the root; there is nowhere above it.
    char *lastSlash = strrchr(s_cwd, '/');
    if (lastSlash == NULL || lastSlash == s_cwd + 5)
    {
        tts_say("Already at the top.");
        return;
    }

    *lastSlash = '\0';
    ReadCurrentDir();
    AnnounceCursor(true);
}

static void Redraw(void)
{
    printf("\x1b[2J");   // clear
    printf("\x1b[1;1H%.48s", s_cwd);
    printf("\x1b[2;1Hup/down move  A open  B back  R repeat  START exit");

    if (s_count == 0)
    {
        printf("\x1b[4;3H(empty)");
    }
    else
    {
        if (s_cursor < s_scroll)
            s_scroll = s_cursor;
        if (s_cursor >= s_scroll + VISIBLE_ROWS)
            s_scroll = s_cursor - VISIBLE_ROWS + 1;

        for (int row = 0; row < VISIBLE_ROWS; ++row)
        {
            int i = s_scroll + row;
            if (i >= s_count)
                break;

            printf("\x1b[%d;1H%c %c %.42s", 4 + row,
                   (i == s_cursor) ? '>' : ' ',
                   s_entries[i].is_dir ? '/' : ' ',
                   s_entries[i].name);
        }
    }

    if (s_count > 0)
        printf("\x1b[26;1H%d of %d", s_cursor + 1, s_count);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    // Draw before speech comes up: espeak_Initialize hangs rather than failing
    // when its voice data is missing, and a hang before the first frame looks
    // exactly like the app never started.
    printf("\x1b[1;1H3DS accessible file browser");
    printf("\x1b[3;1Hstarting speech...");
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();

    bool speech = tts_init();

    if (!speech)
    {
        printf("\x1b[2J");
        printf("\x1b[1;1Hspeech FAILED - browser still usable");
        printf("\x1b[3;1H%.50s", tts_last_error());
        printf("\x1b[5;1HPress START to exit.");
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    ReadCurrentDir();
    Redraw();
    AnnounceCursor(true);

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown();

        if (down & KEY_START)
            break;

        bool moved = false;

        if (down & KEY_DOWN)
        {
            if (s_count > 0) { s_cursor = (s_cursor + 1) % s_count; moved = true; }
        }
        if (down & KEY_UP)
        {
            if (s_count > 0) { s_cursor = (s_cursor + s_count - 1) % s_count; moved = true; }
        }
        if (down & KEY_A)      EnterSelected();
        if (down & KEY_B)      GoUp();
        if (down & KEY_R)      AnnounceCursor(false);

        if (moved)
            AnnounceCursor(false);

        if (down & (KEY_DOWN | KEY_UP | KEY_A | KEY_B))
            Redraw();

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    tts_exit();
    gfxExit();
    return 0;
}
