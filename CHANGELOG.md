# Changelog - Simple Loadout System (Oblivion Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.1 - 2026-09-30 - untested

### Fixed (installation)
- **Mod Organizer 2 dropped the storage plugin from the load order** (the owner, 2026-09-30: after loading a save, no
  loadout box showed as active and pressing one did nothing). MO2 lists - and keeps in plugins.txt - only the plugins
  at the top level of a mod's folder, and removes every other plugins.txt line when it starts or closes; this mod's
  ESP sat only under Root\ (where Root Builder copies it into the real Data folder the game reads), so the line went
  and the chests and the active-loadout global were never loaded - silently. The README now tells MO2 users to keep a
  second copy of SimpleLoadoutSystem.esp at the mod folder's top level, and what the symptom means if it happens.
  Proven in the owner's instance: with the top-level copy the line survived MO2's start and close, and the loadouts
  worked in game.

### Fixed (stability)
- The loadout boxes' widget creation runs fault-guarded and refuses a player controller that is being destroyed (gate
  rule or-world-context-calls-are-guarded, from Minimap Menu's crash on quitting to the menu, 2026-09-30).
- The "is this widget still alive" check reads the widget's slot index under a fault guard, so a widget the game has
  already garbage-collected returns "gone" instead of crashing (Apocrypha Menu Framework's crash on a loadout swap,
  2026-09-30, was this check reading freed memory).

## 1.0.0 - 2026-09-29 - working

The loadout switch works end to end, in game (the owner, 09:47): five brown loadout boxes below the inventory's
category tabs, D-pad Up from the top row into them, Left / Right along them, Down / B back to the top row, LT / RT
change the category from the row, A stores what is worn into the loadout's chest and brings the chosen loadout's gear
back and equips it; the item list updates in place. The switch runs on the game's TES thread (its equipment path
traps every other thread), pieces come off through Actor::UnequipObject and go on through Actor::EquipObject without
the NoUnequip lock, and the rows follow the game's own list rebuild.

Stage 1 (the spike): the plugin loads, chains the game's controller read for a game-thread frame, watches the
inventory menu, reads the worn gear, finds the ten storage containers and the active-loadout global in
SimpleLoadoutSystem.esp, and drives select / deselect through the TestBench tool sls.loadouts. No bar yet.
