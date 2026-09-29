# Simple Loadout System (Oblivion Remastered)

Loadout buttons inside the game's inventory menu, just above the categories, for The Elder Scrolls IV: Oblivion
Remastered - the [Skyrim Simple Loadout System for Controller](https://github.com/ApocryphaRealm/SimpleLoadoutSystemForController)
carried to the remaster as an OBSE64 plugin. Pick a loadout and everything you equip while it is active becomes that
loadout; switch away and the gear goes into that loadout's own storage, out of your inventory and its weight; pick it
again and it all comes back and is equipped.

* What the player gets and where the files go: `dist/README.txt`
* What changed: `CHANGELOG.md`
* The plan and the engine facts it rests on: `4. plans\Simple Loadout System for Oblivion Remastered\PLAN.md` (project)

## Building

* [xmake](https://xmake.io) 3.0+, a C++23 compiler (MSVC), and the submodule: `git clone --recurse-submodules`.
* `xmake build SimpleLoadoutSystem` from PowerShell (from Git Bash xmake configures for mingw).
* `python tools/build-esp.py` writes `SimpleLoadoutSystem.esp` (the storage: one container per loadout, a global);
  `python tools/gen-ini.py` writes the shipped INI from the compiled defaults.

## Licence

GPL-3.0-or-later (`LICENSE`, `dist/NOTICE.md`). CommonLibOB64 is GPL-3.0.
