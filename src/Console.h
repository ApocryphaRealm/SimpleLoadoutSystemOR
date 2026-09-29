#pragma once

// ============================================================================================================
// The engine's console, from C++: UKismetSystemLibrary::ExecuteConsoleCommand through ProcessEvent with a hand-laid
// parameter frame (the Ultimate Combat bridge's first build proved the shape: world context 8 bytes, Command an
// FString of 16, SpecificPlayer 8). The world context and player are the player pawn's Controller. Game thread only.
// A console command is the game's OWN path for equipping (player.equipitem / unequipitem keep the item and its data
// in the inventory) - used where an engine function reached directly took the game down (2026-09-29).
// ============================================================================================================

namespace console
{
	bool Ready();                               // resolves lazily on the game thread; false until everything is found
	bool Run(const std::string& a_command);     // game thread
	std::string Status();
}
