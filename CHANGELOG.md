# Changelog - Simple Loadout System for Oblivion Remastered

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## Unversioned - 2026-09-29 - in progress

The loadout switch works end to end, in game (the owner, 09:47): five brown loadout boxes below the inventory's
category tabs, D-pad Up from the top row into them, Left / Right along them, Down / B back to the top row, LT / RT
change the category from the row, A stores what is worn into the loadout's chest and brings the chosen loadout's gear
back and equips it; the item list updates in place. The switch runs on the game's TES thread (its equipment path
traps every other thread), pieces come off through Actor::UnequipObject and go on through Actor::EquipObject without
the NoUnequip lock, and the rows follow the game's own list rebuild.

Stage 1 (the spike): the plugin loads, chains the game's controller read for a game-thread frame, watches the
inventory menu, reads the worn gear, finds the ten storage containers and the active-loadout global in
SimpleLoadoutSystem.esp, and drives select / deselect through the TestBench tool sls.loadouts. No bar yet.
