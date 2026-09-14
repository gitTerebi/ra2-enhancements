#include "patch.h"
#include "features.h"
#include <wchar.h>

struct TitleProfile
{
    const char *name;
    DWORD scenario;
    DWORD mapFileOffset;
    DWORD formatPush;
    DWORD format;
    DWORD formatCall;
    DWORD swprintf;
};

static const TitleProfile kTitles[] = {
    {"gamemd", 0x00A8B230, 0x125C, 0x0055DC77, 0x0082A05C, 0x0055DC7D, 0x007CA564},
};

enum { kTitleChars = 56 };

static const TitleProfile *g_title;

static const char *BaseName(const char *path)
{
    const char *slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

static bool ClientMissionName(const char *map, wchar_t *out, int outChars)
{
    char ini[MAX_PATH];
    if (!BesideExe("INI\\BattleClient.ini", ini, sizeof(ini)))
        return false;
    enum { kSectionsSize = 0x10000 };
    char *sections = (char *)malloc(kSectionsSize);
    if (!sections)
        return false;
    bool found = false;
    if (GetPrivateProfileSectionNamesA(sections, kSectionsSize, ini))
    {
        for (const char *s = sections; *s && !found; s += strlen(s) + 1)
        {
            char scenario[MAX_PATH];
            GetPrivateProfileStringA(s, "Scenario", "", scenario, sizeof(scenario), ini);
            if (_stricmp(BaseName(scenario), BaseName(map)) != 0)
                continue;
            char description[256];
            GetPrivateProfileStringA(s, "Description", "", description, sizeof(description), ini);
            found = *description && MultiByteToWideChar(CP_UTF8, 0, description, -1, out, outChars) > 0;
        }
    }
    free(sections);
    return found;
}

static void MapStem(const char *map, wchar_t *out, int outChars)
{
    if (!MultiByteToWideChar(CP_ACP, 0, BaseName(map), -1, out, outChars))
        *out = 0;
    wchar_t *dot = wcsrchr(out, L'.');
    if (dot)
        *dot = 0;
}

static int __cdecl FormatMissionTitle(wchar_t *buffer, const wchar_t *format, const wchar_t *name)
{
    wchar_t found[MAX_PATH];
    if (!*name)
    {
        const char *map = (const char *)(*(DWORD *)g_title->scenario + g_title->mapFileOffset);
        if (!ClientMissionName(map, found, _countof(found)))
            MapStem(map, found, _countof(found));
        name = found;
        SpeedLog("  mission start save: the scenario has no name, so the title uses \"%ls\"", name);
    }
    return _snwprintf_s(buffer, kTitleChars, _TRUNCATE, format, name);
}

void InstallMissionSaveName()
{
    const TitleProfile *t = ForThisGame(kTitles);
    if (!t)
        return;

    BYTE push[5] = {0x68};
    memcpy(push + 1, &t->format, 4);
    if (!BytesAt(t->formatPush, push, sizeof(push)) || !CallGoesTo(t->formatCall, t->swprintf))
    {
        SpeedLog("  mission start save name: site %X unexpected, skipped", t->formatCall);
        return;
    }
    g_title = t;
    if (WriteCall(t->formatCall, FormatMissionTitle))
        SpeedLog("  mission start save name ready at %X", t->formatCall);
}
