#pragma once

// ============================================================================================================
// Whether the game's inventory menu is open. WBP_OriginalMenu_Inventory_C (native VInventoryMenu) is a CommonUI
// activatable widget; BP_OnActivated / BP_OnDeactivated pass through ProcessEvent and are watched with pe::Watch
// (the Improved Wheel Menu's Menus.cpp, proven in game 2026-09-29). The class loads the first time the menu opens,
// so the watch lands then, and the menu is taken as open at that moment. The Gamebryo menu mode (1 = gameplay)
// clears the state if a deactivation was missed.
// ============================================================================================================

namespace menus
{
	using Listener = void (*)(UE::UObject* a_menu, bool a_open);

	void Tick();                      // game thread: finds the class once it is loaded and watches it
	bool InventoryOpen();             // any thread
	UE::UObject* InventoryWidget();   // the live menu widget while it is open (nullptr otherwise)
	bool AnyMenuOpen();               // the game is in menu mode
	void SetListener(Listener a_listener);   // told on the game thread as the menu activates / deactivates
}
