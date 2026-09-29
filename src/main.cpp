// Simple Loadout System (Oblivion Remastered) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GameThread.h"
#include "Loadouts.h"
#include "Menus.h"
#include "Reflect.h"
#include "Settings.h"

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
		if (!storageInit && RE::PlayerCharacter::GetSingleton()) {
			storageInit = true;
			loadouts::Init();
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

	void OnMenu(UE::UObject*, bool)
	{
		WriteSelfCheck();
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		menus::SetListener(&OnMenu);
		gamethread::SetFrameCallback(&OnFrame);
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
