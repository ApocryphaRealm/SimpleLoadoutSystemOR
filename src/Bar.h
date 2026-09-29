#pragma once

// ============================================================================================================
// The loadout row inside the game's inventory menu - PART OF THE MENU'S OWN WIDGET TREE, never drawn over it (the
// owner, 2026-09-29: "the loadouts buttons should be part of the inventory interface, not drawn over the top of it").
// The menu's main part (WBP_OriginalMenu_InventoryMainPart_C, read 2026-09-29) is a CanvasPanel holding the category
// tabs (inv_tabs) and a VerticalBox (inv_verticalbox: a Spacer, then inv_mainContent - whose own vertical layout is
// the header row with the category title and the sort icons, a separator and the item list - then the status bars
// and stats). The owner's place for the row: below the tabs, above the title / sort row. So the row (a
// HorizontalBox of brown boxes built from the game's own prefabs, the Tween Menu's way) is inserted FIRST in
// inv_mainContent's vertical layout: its children come off and go back on behind the row with the slot settings they
// had (a panel's insert-at-index is not reflected; add-to-end is). The game's layout then pushes the title row and
// the list down by the row's height, and the row can never be wider than the content box.
// Widgets are built on the game thread the frame after the menu activates and are dropped when it deactivates.
// ============================================================================================================

#include "GameThread.h"

namespace bar
{
	void OnInventory(UE::UObject* a_menu, bool a_open);   // from menus (game thread)
	void Tick();                                          // game thread: builds when a build is pending

	// The controller rules (gamethread::SetPadRule): D-pad Up with the list on its top row focuses the row (the game
	// never sees the press); while focused, Left / Right move, A chooses, Down or B hand the list back; every other
	// press passes through. Returns true when the pad state was changed.
	bool PadRule(XINPUT_GAMEPAD& a_pad, WORD a_pressed, WORD a_released);

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
	int ListRow();   // game thread: the list's selected row (-1 none, -2 unreadable)
}
