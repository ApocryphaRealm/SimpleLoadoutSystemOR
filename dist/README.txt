Simple Loadout System for Oblivion Remastered
=============================================
Version 1.0.0

Loadout buttons inside the game's own inventory menu for The Elder Scrolls IV: Oblivion Remastered,
loaded by OBSE64 - the Skyrim mod "Simple Loadout System for Controller" carried to the remaster.

WHAT YOU GET
------------
  * A row of loadout boxes (five by default, up to ten) inside the inventory, below the category tabs
    and above the category title. It is part of the menu: the D-pad reaches it, the mouse clicks it.
  * Press a box and everything you are wearing and wielding goes into that loadout's own storage
    (out of your inventory and its weight) - or, if that loadout already holds gear, its gear comes
    back and is equipped. The list updates at once.
  * While a loadout is active, what you wear IS the loadout: equip and unequip freely; the next
    switch away stores whatever you have on at that moment. Quest items are only taken off, never
    stored. Press the active box again to deselect it (everything worn goes into storage).
  * The storage is ten chests in a sealed cell of the mod's own plugin, so it lives in your save.

CONTROLS
--------
  * Controller: from the top row of the item list press D-pad UP into the loadout row; LEFT / RIGHT
    move along it; A selects; DOWN or B return to the list; LT / RT change the category from the row.
  * Mouse: click a box.

INSTALLATION
------------
  * Two parts ship in this download, both under OblivionRemastered\:
      Binaries\Win64\OBSE\Plugins\  SimpleLoadoutSystem.dll, .pdb, .ini and the SimpleLoadoutSystem\ folder
      Content\Dev\ObvData\Data\     SimpleLoadoutSystem.esp (the storage)
  * Drop the OblivionRemastered folder over the game's own, or install with a mod manager. Mod Organizer 2
    users need Root Builder for the OBSE plugin, as for every OBSE64 mod - and the .esp must reach the real
    Data folder too (the game's plugin loader does not read through MO2's virtual folder), which Root Builder
    does when the file sits under Root\OblivionRemastered\Content\Dev\ObvData\Data\.
  * Enable SimpleLoadoutSystem.esp in the load order (Plugins.txt). The plugin adds one cell and ten
    chests; it changes nothing of the game's.
  * Start the game through OBSE64. Requires OBSE64 and Address Library for OBSE Plugins.

SETTINGS
--------
  SimpleLoadoutSystem.ini beside the DLL: the number of boxes (iLoadoutCount, 1-10), each box's name
  (sName1..sName10; empty shows "Loadout N" in the game's language), and the log level.

DEBUGGING
---------
  Send the log with any bug report:
  Documents\My Games\Oblivion Remastered\OBSE\Logs\SimpleLoadoutSystem.log
  Set uLogLevel=1 in the INI to capture every store / equip call. SimpleLoadoutSystem.selfcheck.txt
  beside the DLL says what the plugin found (the storage, the game's threads, the menu).
  The debug symbols (.pdb) ship in this download, so a crash log names this mod's functions.

REQUIREMENTS
------------
  * OBSE64 (Nexus 282), 0.2.2 or newer
  * Address Library for OBSE Plugins (Nexus 4475), the database for your game version

LICENCE
-------
  GPL-3.0-or-later (LICENSE, NOTICE.md). Built on CommonLibOB64 (GPL-3.0) and MinHook (BSD-2-Clause).
  Source: https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR
