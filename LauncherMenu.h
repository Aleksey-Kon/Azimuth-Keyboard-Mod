#pragma once
#include <windows.h>

namespace gamepadmod
{
    struct MenuCallbacks
    {
        void (*Select)(int);
        int (*Active)();
        void (*Edit)(HWND);
        void (*Reload)();
        void (*ResetInput)();
        void (*Log)(const char*);
    };

    using ImportHook = bool(*)(const char*, void*, void**);
    bool InstallLauncherMenu(ImportHook hook, MenuCallbacks callbacks);
    bool IsLauncherInputCaptured() noexcept;
}
