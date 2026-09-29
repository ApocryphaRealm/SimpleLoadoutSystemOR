#pragma once

// SimpleLoadoutSystem.ini beside the plugin - the only settings surface (as the Skyrim mod: no in-game page). Read
// once at load. The compiled defaults ARE the shipped INI's values (rule 16); tools/gen-ini.py writes the INI from
// DefaultIniText() so the two cannot drift.

namespace settings
{
	inline constexpr int kMaxLoadouts = 10;

	struct Values
	{
		int                      count = 5;    // [General] iLoadoutCount, 1-10
		std::vector<std::string> names;        // [General] sLoadoutName1..10, "Loadout N" when empty
		int                      logLevel = 2; // [Debug] uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error
		bool                     iniFound = false;
	};

	void Load();
	const Values& Get();
	std::string Name(int a_loadout);           // 0-based; "Loadout N" when the INI gives none
	std::filesystem::path PluginFolder();      // ...\OblivionRemastered\Binaries\Win64\OBSE\Plugins
	std::string DefaultIniText();
}
