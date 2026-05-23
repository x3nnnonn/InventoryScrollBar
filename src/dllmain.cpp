#include <windows.h>

#include "inventory_scroll.h"
#include "log.h"
#include "process_event_hook.h"

namespace {

HMODULE g_self = nullptr;

DWORD WINAPI MainThread(LPVOID)
{
    log_::Init();
    LOG("InventoryScrollBar starting");

    while (!inventory_scroll::IsGameplayReady()) {
        if (GetAsyncKeyState(VK_END) & 1) {
            LOG("Unload before hook install");
            log_::Shutdown();
            FreeLibraryAndExitThread(g_self, 0);
            return 0;
        }
        Sleep(500);
    }

    LOG("Game ready, instaling ProcessEevnt hook");

    if (!process_event_hook::Install()) {
        LOG("Failed to install ProcessEvent hook");
        log_::Shutdown();
        FreeLibraryAndExitThread(g_self, 1);
        return 1;
    }

    LOG("Inventory scroll mod active");

    while (!(GetAsyncKeyState(VK_END) & 1))
        Sleep(250);

    LOG("Unload requested");
    process_event_hook::Uninstall();
    log_::Shutdown();
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
