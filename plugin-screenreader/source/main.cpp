#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "screenreader/tts.hpp"

#ifdef SR_HAVE_XYORAS_SPEECH
#include "xyoras/platform.hpp"
#include "xyoras/diagnostics.hpp"
#define SR_CHECKPOINT(msg) xyoras::diag::Checkpoint(msg)
#else
#define SR_CHECKPOINT(msg) ((void)0)
#endif

#include <cstdio>
#include <cstring>
#include <string>

namespace CTRPluginFramework
{
    // ---------------------------------------------------------------------
    // Hook target
    //
    // The address of the host process function that renders one menu item.
    // For the bundled testapp this is `sr_draw_item(int index, const char *text)`;
    // read it out of testapp/testapp.elf with:
    //     arm-none-eabi-nm testapp.elf | grep sr_draw_item
    //
    // It is read at runtime from sdmc:/screenreader.cfg so you can retarget the
    // plugin without rebuilding it -- which is the whole point once you start
    // pointing it at addresses you found in Ghidra.
    // ---------------------------------------------------------------------
    static const char *CONFIG_PATH = "screenreader.cfg";

    static u32      g_hookAddr = 0;

    // Constructed inside main(), NOT at file scope. A CTRPF Hook acquires a
    // HookContext from the framework's pool in its constructor, and at static
    // initialisation time -- which runs before the framework is up -- that
    // aborts the plugin before main() is ever reached. The symptom is a "Fatal
    // plugin error: abort()" with no checkpoints written at all.
    static Hook    *g_drawItemHook = nullptr;

    static u32  ReadHookAddrFromConfig(void)
    {
        File    file;

        if (File::Open(file, CONFIG_PATH, File::READ) != 0)
            return 0;

        std::string contents;
        contents.resize(static_cast<size_t>(file.GetSize()));
        if (contents.empty() || file.Read(&contents[0], contents.size()) != 0)
            return 0;

        // Format: one `hook_addr=0x00100abc` line. Comments start with '#'.
        const char *needle = "hook_addr";
        size_t pos = contents.find(needle);
        if (pos == std::string::npos)
            return 0;

        pos = contents.find('=', pos);
        if (pos == std::string::npos)
            return 0;

        // sscanf's %x wants unsigned int; u32 is long unsigned int here, so
        // read into the type the format actually specifies.
        unsigned int addr = 0;
        if (std::sscanf(contents.c_str() + pos + 1, " %x", &addr) != 1)
            return 0;

        return static_cast<u32>(addr);
    }

    // ---------------------------------------------------------------------
    // The hook itself.
    //
    // MITM mode: this function replaces the target, and we call the original
    // through the hook context so the host still draws its menu normally.
    // Signature must match the target exactly.
    // ---------------------------------------------------------------------
    static void     DrawItemHook(int index, const char *text)
    {
        if (text != nullptr && Process::CheckAddress(reinterpret_cast<u32>(text), MEMPERM_READ))
            ScreenReader::Say(std::string(text));

        HookContext::GetCurrent().OriginalFunction<void, int, const char *>(index, text);
    }

    static const char *HookResultToString(HookResult res)
    {
        switch (res)
        {
        case HookResult::Success:               return "success";
        case HookResult::InvalidContext:        return "invalid context";
        case HookResult::InvalidAddress:        return "address not reachable";
        case HookResult::AddressAlreadyHooked:  return "address already hooked";
        case HookResult::TooManyHooks:          return "too many hooks";
        case HookResult::HookParamsError:       return "bad hook params";
        case HookResult::TargetInstructionCannotBeHandledAutomatically:
                                                return "PC-relative target instruction";
        default:                                return "unknown";
        }
    }

    static bool     InstallHook(void)
    {
        g_hookAddr = ReadHookAddrFromConfig();

        if (g_hookAddr == 0)
        {
            OSD::Notify("screenreader: no hook_addr in sdmc:/screenreader.cfg");
            return false;
        }

        if (!Process::CheckAddress(g_hookAddr, MEMPERM_READ))
        {
            OSD::Notify(Utils::Format("screenreader: %08X not mapped", g_hookAddr));
            return false;
        }

        if (g_drawItemHook == nullptr)
            g_drawItemHook = new Hook();

        g_drawItemHook->InitializeForMitm(g_hookAddr, reinterpret_cast<u32>(DrawItemHook));

        HookResult res = g_drawItemHook->Enable();
        if (res != HookResult::Success)
        {
            OSD::Notify(Utils::Format("screenreader: hook failed (%s)", HookResultToString(res)));
            return false;
        }

        OSD::Notify(Utils::Format("screenreader: hooked %08X", g_hookAddr));
        return true;
    }

    // Called before the host process starts. Safe place for code edits.
    void    PatchProcess(FwkSettings &settings)
    {
        (void)settings;
    }

    // Called when the host process exits.
    void    OnProcessExit(void)
    {
        if (g_drawItemHook != nullptr && g_drawItemHook->IsEnabled())
            g_drawItemHook->Disable();
        ScreenReader::Exit();
    }

    void    InitMenu(PluginMenu &menu)
    {
        menu.Append(new MenuEntry("Re-install hook", nullptr, [](MenuEntry *entry)
        {
            (void)entry;
            if (g_drawItemHook != nullptr && g_drawItemHook->IsEnabled())
                g_drawItemHook->Disable();
            InstallHook();
        }, "Re-read sdmc:/screenreader.cfg and re-apply the hook.\n"
           "Use after editing the address without relaunching."));

        menu.Append(new MenuEntry("Test speech", nullptr, [](MenuEntry *entry)
        {
            (void)entry;
            ScreenReader::Say("Screen reader online");
        }, "Push a string through the speech path without needing the hook to fire."));

        menu.Append(new MenuEntry("Flush speech queue", nullptr, [](MenuEntry *entry)
        {
            (void)entry;
            ScreenReader::Flush();
        }, "Drop anything queued but not yet spoken."));
    }

    int     main(void)
    {
        SR_CHECKPOINT("screenreader: plugin main entered");

        PluginMenu *menu = new PluginMenu("3DS Screen Reader", 0, 1, 0,
                                          "Reads menu items aloud by hooking the host's\n"
                                          "text-draw function.");

        menu->SynchronizeWithFrame(true);

        // MUST precede speech init. stdio does not work inside an injected
        // process until this runs, and eSpeak reads all its voice data through
        // stdio -- without it espeak_Initialize hangs or aborts rather than
        // failing cleanly. Learned the hard way; see ../plugin/source/main.cpp.
#ifdef SR_HAVE_XYORAS_SPEECH
        const bool sdmcOk = xyoras::platform::MountSdmc();
        SR_CHECKPOINT(sdmcOk ? "screenreader: sdmc mounted"
                             : "screenreader: sdmc mount FAILED");
#endif

        // ESpeak when built with XYORAS_ROOT set (eSpeak NG -> BCWAV -> CSND);
        // falls back to drawing captured text on the OSD otherwise, so an
        // audio problem never looks like a broken hook.
        ScreenReader::Init(ScreenReader::Backend::ESpeak);

        SR_CHECKPOINT(ScreenReader::IsAvailable() ? "screenreader: speech started"
                                                  : "screenreader: speech FAILED to start");

        InitMenu(*menu);
        SR_CHECKPOINT(InstallHook() ? "screenreader: hook installed"
                                    : "screenreader: hook FAILED");

        menu->Run();

        delete menu;

        ScreenReader::Exit();
        return 0;
    }
}
