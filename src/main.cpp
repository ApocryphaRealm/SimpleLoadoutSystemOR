// Simple Loadout System (Oblivion Remastered) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Bar.h"
#include "GameThread.h"
#include "Loadouts.h"
#include "Menus.h"
#include "Push.h"
#include "Reflect.h"
#include "Settings.h"
#include "TesThread.h"

namespace tool { bool Register(); }

namespace
{
	// The plain-text self-check beside the log: what installed and what the storage looks like (memory: guards-and-a-
	// selfcheck-report-in-every-build). Rewritten as the state changes, so a test can read it without the log.
	void WriteSelfCheck()
	{
		const auto g = gamethread::GetStatus();
		const auto path = settings::PluginFolder() / L"SimpleLoadoutSystem.selfcheck.txt";
		const std::string text = std::format(
			"Simple Loadout System {} self-check\nINI: {}\nloadouts: {}\ngame thread: {} (reads {}, chained after {})\n"
			"reflection: {}\nstorage containers found: {} of {}\nactive loadout: {}\ninventory menu: {}\n",
			SLS_VERSION, settings::Get().iniFound ? "read" : "NOT FOUND (defaults)", settings::Get().count,
			g.installed ? "installed" : "NOT installed", g.reads, g.previousTarget.empty() ? "-" : g.previousTarget,
			reflect::Ok() ? "proven" : "not yet proven", loadouts::StorageReady(), settings::kMaxLoadouts,
			loadouts::Active() >= 0 ? settings::Name(loadouts::Active()) : "none", menus::InventoryOpen() ? "open" : "closed");
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
			std::fwrite(text.data(), 1, text.size(), f);
			std::fclose(f);
		}
	}

	void OnFrame()
	{
		static auto next = std::chrono::steady_clock::now();
		static bool toolRegistered = false;
		static bool storageInit = false;
		static bool selfCheckWritten = false;
		const auto now = std::chrono::steady_clock::now();
		if (now < next) {
			return;
		}
		next = now + 200ms;
		// Engine objects are looked for on the GAME thread, from the controller read - never from a thread of our own
		// (a start-up crashed in UObjectArray, 2026-09-29).
		reflect::SelfCheck();
		menus::Tick();
		bar::Tick();
		// The storage is looked for again every 5 s until it is all there (rule 17: a first miss is not permanent -
		// the plugin's records may load after the player object exists), a dozen times at most.
		static int storageTries = 0;
		static auto nextStorageTry = std::chrono::steady_clock::now();
		static bool tesTried = false;
		if (!tesTried) {
			tesTried = true;
			testhread::Install();   // the switch queue on the Gamebryo thread (TesThread.h)
			push::Install();        // the game's inventory list rebuild, run after a switch (Push.h)
			WriteSelfCheck();
		}
		if (!storageInit && RE::PlayerCharacter::GetSingleton() && now >= nextStorageTry) {
			++storageTries;
			nextStorageTry = now + 5s;
			loadouts::Init();
			storageInit = loadouts::StorageReady() == settings::kMaxLoadouts || storageTries >= 12;
			WriteSelfCheck();
		}
		if (!toolRegistered) {
			toolRegistered = tool::Register();
		}
		if (!selfCheckWritten && gamethread::GetStatus().installed) {
			selfCheckWritten = true;
			WriteSelfCheck();
		}
	}

	void OnMenu(UE::UObject* a_menu, bool a_open)
	{
		bar::OnInventory(a_menu, a_open);
		WriteSelfCheck();
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		menus::SetListener(&OnMenu);
		gamethread::SetFrameCallback(&OnFrame);
		gamethread::SetPadRule(&bar::PadRule);   // D-pad Up from the list's top row hands the game's focus to the row
		gamethread::Install();
		WriteSelfCheck();
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	settings::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(settings::Get().logLevel, 0, 4));
		logger::set_level(level, level);
	}
	logger::info("Simple Loadout System {} loaded (Oblivion Remastered). Log level {} - set [Debug] uLogLevel=1 in "
				 "SimpleLoadoutSystem.ini for more detail when reporting a problem.", SLS_VERSION, settings::Get().logLevel);
	if (auto* messaging = OBSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(&OnMessage)) {
		logger::error("OBSE messaging unavailable - nothing will be installed");
	}
	return true;
}
