#include "process_event_hook.h"

#include "inventory_scroll.h"
#include "log.h"

#include "SDK/Basic.hpp"

#include <MinHook.h>

namespace {

using ProcessEventFn = void (*)(const SDK::UObject*, SDK::UFunction*, void*);
ProcessEventFn g_orig_process_event = nullptr;

void HookedProcessEvent(const SDK::UObject* obj, SDK::UFunction* func, void* parms)
{
    inventory_scroll::OnPreProcessEvent(obj, func, parms);
    g_orig_process_event(obj, func, parms);
    inventory_scroll::OnPostProcessEvent(obj, func, parms);
}

} // namespace

namespace process_event_hook {

bool Install()
{
    if (MH_Initialize() != MH_OK) {
        LOG("MH_Initialize failed");
        return false;
    }

    const auto target = reinterpret_cast<void*>(
        SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent);

    if (MH_CreateHook(target, &HookedProcessEvent, reinterpret_cast<void**>(&g_orig_process_event)) != MH_OK) {
        LOG("MH_CreateHook(ProcessEvent) failed");
        MH_Uninitialize();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        LOG("MH_EnableHook(ProcessEvent) failed");
        MH_RemoveHook(target);
        MH_Uninitialize();
        return false;
    }

    LOG("ProcessEvent hook installed at %p", target);
    return true;
}

void Uninstall()
{
    const auto target = reinterpret_cast<void*>(
        SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent);
    MH_DisableHook(target);
    MH_RemoveHook(target);
    MH_Uninitialize();
}

}
