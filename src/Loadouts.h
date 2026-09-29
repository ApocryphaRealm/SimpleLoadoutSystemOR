#pragma once

// ============================================================================================================
// The loadouts themselves - the Skyrim mod's model on the Gamebryo side of the remaster. Everything the player wears
// or holds while a loadout is active IS that loadout: switching away moves the worn gear into the loadout's own
// container (one persistent reference per loadout in SimpleLoadoutSystem.esp - a vanilla non-respawning chest base,
// so the engine never resets its contents - in an interior cell nothing ever enters), out of the inventory and its
// weight; selecting a loadout brings its gear back and equips it. Unequipping inside a loadout just leaves the item in
// the inventory. The active loadout lives in the plugin's global SLSOR_ActiveLoadout, which the game's save keeps.
// Every call here that touches the inventory runs on the game thread (gamethread::Post).
// ============================================================================================================

namespace loadouts
{
	void Init();                // data loaded: find the storage references and the global; self-check them
	int  StorageReady();        // how many containers were found
	int  Active();              // -1 when none (read from the global)

	// Select a loadout (0-based), or -1 to deselect. False - with the reason - when it cannot happen now (in combat,
	// the storage missing, a switch running). The move itself is posted to the game thread.
	bool Request(int a_loadout, std::string& a_why);
	void SetOnSwitched(std::function<void()> a_fn);   // run on the UE game thread after each switch (the row refreshes)

	json Contents();            // the driving tool: active, worn pieces, each container's items, the storage state

	struct WornPiece
	{
		std::uint32_t formID;
		std::string   name;
		std::int32_t  count;
		bool          left;
		bool          quest;
		bool          extra;    // the piece has its own extra data (health, charge, a name)
		bool          locked;   // ExtraCannotWear on it: the console's NoUnequip lock - never ours (2026-09-29)
	};
	std::vector<WornPiece> Worn();   // game thread

	// The spike's single calls, one engine function each (game thread), so a crash names its call:
	std::string Unequip(std::uint32_t a_formID);          // the vtable's RemoveWornItem on the first worn list of that item
	std::string Store(std::uint32_t a_formID, int a_slot); // RemoveItem of one piece into that loadout's container
	std::string Take(int a_slot);                          // the container's items back to the player (not equipped)
	std::string Equip(std::uint32_t a_formID);             // EquipObject (address library) on a carried piece
	std::string Wear(std::uint32_t a_formID);              // the vtable's AddWornItem on a carried piece
	// A test's mass spawn (the owner, 2026-09-29: over-encumber the player for Weightless): AddObjectToContainer on the
	// TES thread - container changes from any other thread hit the engine's thread trap. Posted; the log reports.
	bool AddItem(std::uint32_t a_formID, int a_count, std::string& a_why);
}
