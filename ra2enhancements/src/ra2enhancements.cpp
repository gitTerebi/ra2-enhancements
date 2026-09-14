#include <windows.h>
#include "patch.h"
#include "features.h"

static void InstallAll()
{
    static bool s_done;
    if (s_done || !HostIsGame())
        return;
    s_done = true;
    InstallPatch("game speed", InstallCampaignSpeed);
    InstallPatch("shift queue", InstallQueueBatch);
    InstallPatch("select voice", InstallSelectVoice);
    InstallPatch("healer escort", InstallHealerEscort);
    InstallPatch("quick save", InstallQuickSave);
    InstallPatch("mission start save name", InstallMissionSaveName);
    InstallPatch("hotkey commands", InstallCommands);
}

EXPORT_FUNC(RA2Enhancements_Install)
{
    InstallAll();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!IsDebuggerPresent())
            InstallAll();
    }
    return TRUE;
}
