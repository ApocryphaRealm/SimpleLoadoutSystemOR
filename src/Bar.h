#pragma once

// ============================================================================================================
// The loadout row inside the game's inventory menu. The menu's main part (WBP_OriginalMenu_InventoryMainPart_C, read
// 2026-09-29) is a CanvasPanel holding the category tabs (inv_tabs) and a VerticalBox (inv_verticalbox: a Spacer,
// then inv_mainContent - whose own header row carries the category title and the sort icons - then the status bars
// and stats). The owner's place for the row: below the tabs, above the title / sort row. Only reflected
// (BlueprintCallable) functions are reachable from here, and a panel's insert-at-index is not one, so the row is
// placed on the CANVAS: the Spacer grows by the row's height (USpacer::SetSize), which pushes the content box and the
// list down, and our HorizontalBox of TextBlocks is added to the canvas (AddChildToCanvas) over the gap, with the
// vertical box's own anchors and offsets. The labels copy the font of the content box's title.
// Widgets are built on the game thread the frame after the menu activates and are dropped when it deactivates.
// ============================================================================================================

namespace bar
{
	void OnInventory(UE::UObject* a_menu, bool a_open);   // from menus (game thread)
	void Tick();                                          // game thread: builds when a build is pending

	struct Snapshot
	{
		bool        built = false;
		int         buttons = 0;
		int         cursor = 0;
		bool        focused = false;
		std::string layout;     // what was measured and where the row went
		std::string problem;
	};
	Snapshot GetSnapshot();
}
