#pragma once

// ============================================================================================================
// The game's own rebuild of the inventory menu's item list. The UE inventory menu shows what the Gamebryo side last
// pushed into UVInventoryMenuViewModel::SetInventory; the push is queued as a pairing message by ONE function on the
// TES thread (game+0x676EBF0 on 2026-09-29, found by byte pattern): it looks the legacy InventoryMenu up itself
// (GetMenuByClass(0x3EA), nothing to do when the menu is closed), frees the old item array, walks the player's
// inventory into a new one and queues the push - the same function the game runs when the menu opens or after its
// own equip. It takes no arguments, so a switch calls it on the TES thread right after its moves and the menu's
// rows follow through the game's own binding.
// ============================================================================================================

namespace push
{
	bool Install();     // at the first frame: the rebuild function found; false (logged) when the pattern is missing
	bool Rebuild();     // TES thread only: the game's rebuild-and-push; false when it is not found
}
