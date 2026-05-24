#include "inventory_scroll.h"

#include "log.h"

#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/Engine_classes.hpp"
#include "SDK/Subnautica2_classes.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_structs.hpp"
#include "SDK/WBP_Inventory_classes.hpp"
#include "SDK/WBP_Inventory_parameters.hpp"
#include "SDK/WBP_TabInventory_classes.hpp"
#include "SDK/WBP_ScrollBoxThatWorksWithController_classes.hpp"
#include "SDK/WBP_ScrollBoxThatWorksWithController_parameters.hpp"
#include "SDK/UWECommonUI_classes.hpp"

#include <unordered_map>

using namespace SDK;

namespace {

thread_local bool g_reentrant = false;

struct ScrollPatchState {
    UWBP_ScrollBoxThatWorksWithController_C* scroll_widget = nullptr;
    bool                                     scroll_installed = false;
    bool                                     patches_enabled = false;
};

std::unordered_map<UWBP_Inventory_C*, ScrollPatchState> g_states;

UFunction* g_fn_update_visible_items = nullptr;
UFunction* g_fn_get_is_slot_available = nullptr;
UFunction* g_fn_update_row_number = nullptr;
UFunction* g_fn_update_inventory_size = nullptr;
UFunction* g_fn_tab_activated = nullptr;
UFunction* g_fn_set_view_model = nullptr;
UFunction* g_fn_inventory_construct = nullptr;
UFunction* g_fn_scroll_tick = nullptr;
UFunction* g_fn_scroll_construct = nullptr;

bool IsUsable(const UObject* obj)
{
    return obj && !obj->IsDefaultObject();
}

APlayerController* LocalController()
{
    UWorld* world = UWorld::GetWorld();
    if (!world || !world->OwningGameInstance) return nullptr;
    auto& players = world->OwningGameInstance->LocalPlayers;
    if (players.Num() == 0) return nullptr;
    ULocalPlayer* local = players[0];
    return local ? local->PlayerController : nullptr;
}

bool IsGameplayReadyImpl()
{
    UWorld* world = UWorld::GetWorld();
    if (!IsUsable(world) || !world->OwningGameInstance) return false;

    APlayerController* pc = LocalController();
    if (!IsUsable(pc) || !IsUsable(pc->Pawn)) return false;
    if (!pc->Pawn->IsA(ASN2PlayerCharacter::StaticClass())) return false;

    AGameStateBase* game_state = world->GameState;
    if (IsUsable(game_state) && !game_state->HasBegunPlay()) return false;

    return true;
}

void CacheFunctions()
{
    if (g_fn_tab_activated) return;

    UClass* inv_cls = UWBP_Inventory_C::StaticClass();
    g_fn_update_visible_items = inv_cls->GetFunction("WBP_Inventory_C", "UpdateVisibleItems");
    g_fn_get_is_slot_available = inv_cls->GetFunction("WBP_Inventory_C", "GetIsSlotAvailable");
    g_fn_update_row_number = inv_cls->GetFunction("WBP_Inventory_C", "UpdateRowNumber");
    g_fn_update_inventory_size = inv_cls->GetFunction("WBP_Inventory_C", "UpdateInventorySize");
    g_fn_tab_activated = UWBP_TabInventory_C::StaticClass()->GetFunction("WBP_TabInventory_C", "BP_OnActivated");
    g_fn_set_view_model = inv_cls->GetFunction("WBP_Inventory_C", "SetViewModel");
    g_fn_inventory_construct = inv_cls->GetFunction("WBP_Inventory_C", "Construct");

    UClass* scroll_cls = UWBP_ScrollBoxThatWorksWithController_C::StaticClass();
    g_fn_scroll_tick = scroll_cls->GetFunction("WBP_ScrollBoxThatWorksWithController_C", "Tick");
    g_fn_scroll_construct = scroll_cls->GetFunction("WBP_ScrollBoxThatWorksWithController_C", "Construct");
}

bool IsManagedInventory(UWBP_Inventory_C* inv)
{
    if (!IsUsable(inv) || !IsGameplayReadyImpl()) return false;
    if (!IsUsable(inv->ViewModel) || inv->ViewModel->InventorySize <= 0) return false;

    USN2InventoryScreenViewModel* screen = inv->InventoryScreenViewModel;
    if (!IsUsable(screen)) return false;

    if (inv->ViewModel == screen->GetInventory()) return true;

    if (screen->HasOtherInventory() && inv->ViewModel == screen->GetOtherInventory()) return true;

    return false;
}

ScrollPatchState* StateFor(UWBP_Inventory_C* inv)
{
    if (!inv) return nullptr;
    return &g_states[inv];
}

bool PatchesEnabled(UWBP_Inventory_C* inv)
{
    ScrollPatchState* state = StateFor(inv);
    return state && state->patches_enabled;
}

void HideOverflowOverlay(UWBP_Inventory_C* inv)
{
    if (IsUsable(inv->NotAllItemsDisplayedOverlay))
        inv->NotAllItemsDisplayedOverlay->SetVisibility(ESlateVisibility::Collapsed);
}

int32 InventoryColumns(UWBP_Inventory_C* inv);
int32 InventorySlotCount(UWBP_Inventory_C* inv);
void ExpandGridLayout(UWBP_Inventory_C* inv);
float ComputeInventoryViewportHeight(UWBP_Inventory_C* inv);
void ApplyInventoryViewport(UWBP_Inventory_C* inv);
void ApplyFixedSlotLayout(UWBP_Inventory_C* inv);
void UpdateScrollBarVisibility(UWBP_Inventory_C* inv);

void UnlockAllSlots(UWBP_Inventory_C* inv, void* parms)
{
    auto* p = static_cast<Params::WBP_Inventory_C_GetIsSlotAvailable*>(parms);
    if (!p || !IsUsable(inv->ViewModel)) return;

    const int32 size = InventorySlotCount(inv);
    if (p->Index_0 >= 0 && p->Index_0 < size)
        p->ReturnValue = true;
}

int32 InventoryColumns(UWBP_Inventory_C* inv)
{
    if (IsUsable(inv->ViewModel) && inv->ViewModel->Columns > 0)
        return inv->ViewModel->Columns;
    if (IsUsable(inv->Items) && inv->Items->GetNumColumns() > 0)
        return inv->Items->GetNumColumns();
    return 6;
}

int32 InventorySlotCount(UWBP_Inventory_C* inv)
{
    if (!IsUsable(inv->ViewModel)) return 0;

    int32 size = inv->ViewModel->InventorySize;
    if (IsUsable(inv->ViewModel->InventoryComponent)) {
        const int32 max_items = inv->ViewModel->InventoryComponent->MaxItems;
        if (max_items > size)
            size = max_items;
    }
    return size;
}

int32 RowsForSlots(int32 slots, int32 columns)
{
    if (columns <= 0) columns = 1;
    return (slots + columns - 1) / columns;
}

void SyncWidgetRowCap(UWBP_Inventory_C* inv)
{
    const int32 slots = InventorySlotCount(inv);
    if (slots <= 0) return;

    const double rows_needed =
        static_cast<double>(RowsForSlots(slots, InventoryColumns(inv)));
    if (inv->MaxRows < rows_needed)
        inv->MaxRows = rows_needed;
}

void ExpandGridLayout(UWBP_Inventory_C* inv)
{
    const int32 slots = InventorySlotCount(inv);
    if (slots <= 0) return;

    const int32 columns = InventoryColumns(inv);
    const int32 rows = RowsForSlots(slots, columns);

    if (IsUsable(inv->Items))
        inv->Items->SetNumRows(rows);
    if (IsUsable(inv->GridBackground))
        inv->GridBackground->SetNumRows(rows);

    if (IsUsable(inv->InventoryContentSizeBox) && inv->ColumnWidth > 0.0) {
        if (!PatchesEnabled(inv)) {
            const float row_height = static_cast<float>(inv->ColumnWidth);
            const float padding = static_cast<float>(inv->RowsHeightBottomPadding);
            const float full_height = row_height * static_cast<float>(rows) + padding;
            inv->InventoryContentSizeBox->ClearMaxDesiredHeight();
            inv->InventoryContentSizeBox->SetHeightOverride(full_height);
        }
    }
}

float InventoryViewportHeight(UWBP_Inventory_C* inv)
{
    float viewport = static_cast<float>(inv->MaxBackgroundHeight);
    if (IsUsable(inv->InventoryContentSizeBox) && inv->InventoryContentSizeBox->HeightOverride > 0.f)
        viewport = inv->InventoryContentSizeBox->HeightOverride;
    if (viewport <= 0.f)
        viewport = 420.f;
    return viewport;
}

float InventoryContentHeight(UWBP_Inventory_C* inv)
{
    const int32 slots = InventorySlotCount(inv);
    if (slots <= 0 || inv->ColumnWidth <= 0.0)
        return 0.f;

    const int32 rows = RowsForSlots(slots, InventoryColumns(inv));
    return static_cast<float>(inv->ColumnWidth) * static_cast<float>(rows)
         + static_cast<float>(inv->RowsHeightBottomPadding);
}

float ComputeInventoryViewportHeight(UWBP_Inventory_C* inv)
{
    const float content = InventoryContentHeight(inv);
    const float row_h = static_cast<float>(inv->ColumnWidth);
    const int32 rows = RowsForSlots(InventorySlotCount(inv), InventoryColumns(inv));

    float base = static_cast<float>(inv->MaxBackgroundHeight);
    if (IsUsable(inv->InventoryContentSizeBox) && inv->InventoryContentSizeBox->HeightOverride > 0.f)
        base = inv->InventoryContentSizeBox->HeightOverride;
    if (base <= 0.f)
        base = 420.f;

    float viewport = base;
    if (row_h > 0.f && rows > 0) {
        const int32 visible_rows =
            rows < inventory_scroll::kMinVisibleRowsAtFullSize ? rows
                                                                 : inventory_scroll::kMinVisibleRowsAtFullSize;
        const float min_viewport =
            row_h * static_cast<float>(visible_rows) + static_cast<float>(inv->RowsHeightBottomPadding);
        if (min_viewport > viewport)
            viewport = min_viewport;
    }

    if (content > 0.f && content < viewport)
        viewport = content;

    if (viewport > inventory_scroll::kMaxViewportHeight)
        viewport = inventory_scroll::kMaxViewportHeight;

    return viewport;
}

void ApplyInventoryViewport(UWBP_Inventory_C* inv)
{
    const float viewport = ComputeInventoryViewportHeight(inv);
    const float content = InventoryContentHeight(inv);

    if (IsUsable(inv->InventoryContentSizeBox)) {
        inv->InventoryContentSizeBox->ClearMaxDesiredHeight();
        inv->InventoryContentSizeBox->SetHeightOverride(viewport);
    }

    const double layout_height =
        content > 0.f ? static_cast<double>(content) : static_cast<double>(viewport);
    if (inv->MaxBackgroundHeight < layout_height)
        inv->MaxBackgroundHeight = layout_height;
}

void ApplyFixedSlotLayout(UWBP_Inventory_C* inv)
{
    SyncWidgetRowCap(inv);

    const float cell = static_cast<float>(inv->ColumnWidth);
    if (cell > 0.f) {
        if (IsUsable(inv->Items))
            inv->Items->SetSlotMinHeight(cell);
        if (IsUsable(inv->GridBackground))
            inv->GridBackground->SetSlotMinHeight(cell);
    }

    ExpandGridLayout(inv);
    ApplyInventoryViewport(inv);
    UpdateScrollBarVisibility(inv);
}

bool InventoryNeedsScroll(UWBP_Inventory_C* inv, UScrollBox* box)
{
    if (IsUsable(box) && box->GetScrollOffsetOfEnd() > 1.f)
        return true;

    const float content = InventoryContentHeight(inv);
    const float viewport = InventoryViewportHeight(inv);
    return content > viewport + 1.f;
}

void ApplyScrollBarStyle(UWBP_ScrollBoxThatWorksWithController_C* scroll_ui, UScrollBox* box, bool needs_scroll)
{
    if (!IsUsable(scroll_ui) || !IsUsable(box)) return;

    if (needs_scroll) {
        box->WidgetBarStyle = scroll_ui->BufferedBarStyle;
        box->SetAlwaysShowScrollbar(true);
        box->SetScrollbarVisibility(ESlateVisibility::Visible);

        if (IsUsable(scroll_ui->ScrollBarOverlay))
            scroll_ui->ScrollBarOverlay->SetVisibility(ESlateVisibility::Collapsed);
        if (IsUsable(scroll_ui->FakeScrollBarTrack))
            scroll_ui->FakeScrollBarTrack->SetVisibility(ESlateVisibility::Collapsed);
    } else {
        box->WidgetBarStyle = scroll_ui->InvisibleBarStyle;
        box->SetAlwaysShowScrollbar(false);
        box->SetScrollbarVisibility(ESlateVisibility::Collapsed);
        box->SetScrollOffset(0.f);

        if (IsUsable(scroll_ui->ScrollBarOverlay))
            scroll_ui->ScrollBarOverlay->SetVisibility(ESlateVisibility::Collapsed);
        if (IsUsable(scroll_ui->FakeScrollBarTrack))
            scroll_ui->FakeScrollBarTrack->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (IsUsable(scroll_ui->SizeBox_RightStickOnScrollBar))
        scroll_ui->SizeBox_RightStickOnScrollBar->SetVisibility(ESlateVisibility::Collapsed);
}

void UpdateScrollBarVisibility(UWBP_Inventory_C* inv)
{
    ScrollPatchState* state = StateFor(inv);
    if (!state || !state->scroll_installed || !IsUsable(state->scroll_widget)) return;

    UWBP_ScrollBoxThatWorksWithController_C* scroll_ui = state->scroll_widget;
    UScrollBox* box = scroll_ui->ScrollBox;
    if (!IsUsable(box)) return;

    ApplyScrollBarStyle(scroll_ui, box, InventoryNeedsScroll(inv, box));
}

UWBP_Inventory_C* InventoryForScrollWidget(UWBP_ScrollBoxThatWorksWithController_C* scroll_ui)
{
    if (!IsUsable(scroll_ui)) return nullptr;

    for (auto& entry : g_states) {
        if (entry.second.scroll_widget == scroll_ui && entry.second.patches_enabled)
            return entry.first;
    }
    return nullptr;
}

void InstallScrollWrapper(UWBP_Inventory_C* inv)
{
    ScrollPatchState& state = g_states[inv];
    if (state.scroll_installed || !IsUsable(inv->InventoryContentSizeBox)) return;

    UWidget* content = inv->InventoryContentSizeBox->GetContent();
    if (!IsUsable(content)) {
        LOG("InventoryContentSizeBox content not ready for %p", inv);
        return;
    }

    APlayerController* pc = LocalController();
    if (!IsUsable(pc)) return;

    UWBP_ScrollBoxThatWorksWithController_C* scroll = static_cast<UWBP_ScrollBoxThatWorksWithController_C*>(
        UWidgetBlueprintLibrary::Create(inv, UWBP_ScrollBoxThatWorksWithController_C::StaticClass(), pc));
    if (!IsUsable(scroll)) {
        LOG("Create scroll widget failed");
        return;
    }

    scroll->ForceHideRightStick = true;
    scroll->AddChild(content);
    inv->InventoryContentSizeBox->SetContent(scroll);

    if (IsUsable(scroll->ScrollBox))
        scroll->ScrollBox->SetWheelScrollMultiplier(1.0f);

    state.scroll_widget = scroll;
    state.scroll_installed = true;

    ApplyInventoryViewport(inv);
    LOG("Installed inventory scroll wrapper on %p (viewport=%.0f content=%.0f)",
        inv, ComputeInventoryViewportHeight(inv), InventoryContentHeight(inv));
}

void EnableInventoryPatches(UWBP_Inventory_C* inv)
{
    if (!IsManagedInventory(inv)) return;

    ScrollPatchState& state = g_states[inv];
    if (state.patches_enabled) return;

    state.patches_enabled = true;
    LOG("Enabling inventory patches on %p", inv);

    HideOverflowOverlay(inv);
    InstallScrollWrapper(inv);
    ApplyFixedSlotLayout(inv);
}

void TryEnableInventoryPatches(UWBP_Inventory_C* inv)
{
    if (!IsManagedInventory(inv)) return;
    EnableInventoryPatches(inv);
}

void OnInventoryTabActivated(UWBP_TabInventory_C* tab)
{
    if (!IsUsable(tab) || !IsGameplayReadyImpl()) return;

    if (IsUsable(tab->Inventory))
        EnableInventoryPatches(tab->Inventory);
}

}

namespace inventory_scroll {

void RefreshOpenInventoryLayouts()
{
    for (auto& entry : g_states) {
        UWBP_Inventory_C* inv = entry.first;
        if (!entry.second.patches_enabled || !IsUsable(inv)) continue;

        ApplyFixedSlotLayout(inv);
    }
}

bool IsGameplayReady()
{
    return IsGameplayReadyImpl();
}

void OnPreProcessEvent(const UObject* obj, UFunction* func, void* /*parms*/)
{
    (void)obj;
    (void)func;
    CacheFunctions();
}

void OnPostProcessEvent(const UObject* obj, UFunction* func, void* parms)
{
    if (g_reentrant || !obj || !func) return;

    CacheFunctions();

    g_reentrant = true;

    if (obj->IsA(UWBP_TabInventory_C::StaticClass()) && func == g_fn_tab_activated) {
        OnInventoryTabActivated(const_cast<UWBP_TabInventory_C*>(static_cast<const UWBP_TabInventory_C*>(obj)));
        g_reentrant = false;
        return;
    }

    if (obj->IsA(UWBP_ScrollBoxThatWorksWithController_C::StaticClass())
        && (func == g_fn_scroll_tick || func == g_fn_scroll_construct)) {
        auto* scroll_ui = const_cast<UWBP_ScrollBoxThatWorksWithController_C*>(
            static_cast<const UWBP_ScrollBoxThatWorksWithController_C*>(obj));
        if (UWBP_Inventory_C* inv = InventoryForScrollWidget(scroll_ui))
            UpdateScrollBarVisibility(inv);
        g_reentrant = false;
        return;
    }

    if (!obj->IsA(UWBP_Inventory_C::StaticClass()) || !IsGameplayReadyImpl()) {
        g_reentrant = false;
        return;
    }

    auto* inv = const_cast<UWBP_Inventory_C*>(static_cast<const UWBP_Inventory_C*>(obj));

    if (func == g_fn_set_view_model || func == g_fn_inventory_construct)
        TryEnableInventoryPatches(inv);

    if (!IsManagedInventory(inv) || !PatchesEnabled(inv)) {
        g_reentrant = false;
        return;
    }

    if (func == g_fn_get_is_slot_available)
        UnlockAllSlots(inv, parms);

    if (func == g_fn_update_row_number || func == g_fn_update_inventory_size) {
        HideOverflowOverlay(inv);
        ApplyFixedSlotLayout(inv);
    }

    if (func == g_fn_update_visible_items) {
        HideOverflowOverlay(inv);
        ApplyFixedSlotLayout(inv);
    }

    g_reentrant = false;
}

}
