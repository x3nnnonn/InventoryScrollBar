#pragma once

#include <cstdint>

namespace SDK {
    class UObject;
    class UFunction;
}

namespace inventory_scroll {
    inline constexpr float kMaxViewportHeight = 720.f;
    inline constexpr std::int32_t kMinVisibleRowsAtFullSize = 8;

    bool IsGameplayReady();
    void RefreshOpenInventoryLayouts();
    void OnPreProcessEvent(const SDK::UObject* obj, SDK::UFunction* func, void* parms);
    void OnPostProcessEvent(const SDK::UObject* obj, SDK::UFunction* func, void* parms);
}
