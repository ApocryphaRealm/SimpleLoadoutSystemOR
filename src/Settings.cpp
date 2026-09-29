#include "Settings.h"

#include "Strings.h"

namespace settings
{
	namespace
	{
		Values g_values;

		std::filesystem::path ThisModule()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&ThisModule), &self) &&
				GetModuleFileNameW(self, buf, MAX_PATH)) {
				return std::filesystem::path(buf);
			}
			return std::filesystem::path(L"OBSE") / L"Plugins" / L"SimpleLoadoutSystem.dll";
		}

		// Reading only - never WritePrivateProfileString (rule 16); the plugin never writes its INI.
		int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
		{
			const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
			return std::clamp(v, a_min, a_max);
		}

		std::string ReadString(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key)
		{
			wchar_t buf[256]{};
			GetPrivateProfileStringW(a_section, a_key, L"", buf, 256, a_ini.c_str());
			const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
			std::string out(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
			if (n > 1) {
				WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), n, nullptr, nullptr);
			}
			return out;
		}
	}

	std::filesystem::path PluginFolder()
	{
		return ThisModule().parent_path();
	}

	void Load()
	{
		const auto ini = PluginFolder() / L"SimpleLoadoutSystem.ini";
		Values v;
		v.names.assign(kMaxLoadouts, std::string());
		v.iniFound = std::filesystem::exists(ini);
		if (v.iniFound) {
			v.count = ReadInt(ini, L"General", L"iLoadoutCount", v.count, 1, kMaxLoadouts);
			v.logLevel = ReadInt(ini, L"Debug", L"uLogLevel", v.logLevel, 0, 4);
			for (int i = 0; i < kMaxLoadouts; ++i) {
				v.names[static_cast<std::size_t>(i)] = ReadString(ini, L"General", (L"sLoadoutName" + std::to_wstring(i + 1)).c_str());
			}
		}
		g_values = v;
		if (v.iniFound) {
			logger::info("settings: {} read (iLoadoutCount={}, uLogLevel={})", ini.string(), v.count, v.logLevel);
		} else {
			logger::warn("settings: {} not found - compiled defaults in use (iLoadoutCount={}, uLogLevel={})", ini.string(), v.count, v.logLevel);
		}
	}

	const Values& Get()
	{
		return g_values;
	}

	std::string Name(int a_loadout)
	{
		const auto& n = g_values.names;
		if (a_loadout >= 0 && a_loadout < static_cast<int>(n.size()) && !n[static_cast<std::size_t>(a_loadout)].empty()) {
			return n[static_cast<std::size_t>(a_loadout)];
		}
		return std::format("{} {}", TR("SLS_Loadout", "Loadout"), a_loadout + 1);   // rule 66: the word from the eleven files
	}

	std::string DefaultIniText()
	{
		std::string s = "; Simple Loadout System - settings. There is no in-game settings page; edit this file with the game closed.\n\n"
		                "[General]\n; How many loadout buttons sit above the inventory's categories (1-10).\niLoadoutCount=5\n\n"
		                "; The buttons' names. An empty name shows as \"Loadout N\".\n";
		for (int i = 1; i <= kMaxLoadouts; ++i) {
			s += "sLoadoutName" + std::to_string(i) + "=\n";
		}
		s += "\n[Debug]\n; Log detail: 0 trace, 1 debug, 2 info, 3 warnings, 4 errors.\nuLogLevel=2\n";
		return s;
	}
}
