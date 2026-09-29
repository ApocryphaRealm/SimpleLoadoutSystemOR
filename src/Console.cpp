#include "Console.h"

#include "Reflect.h"

namespace console
{
	namespace
	{
		constexpr const wchar_t* kKismetSystemPath = L"/Script/Engine.KismetSystemLibrary";
		constexpr const wchar_t* kPlayerClassPath = L"/Game/Dev/PlayerBlueprints/BP_OblivionPlayerCharacter.BP_OblivionPlayerCharacter_C";

		UE::UObject*   g_kismetCdo = nullptr;
		UE::UFunction* g_fnExec = nullptr;
		std::int32_t   g_offWorld = -1, g_offCommand = -1, g_offPlayer = -1, g_frame = 0;
		UE::UClass*    g_playerClass = nullptr;
		std::int32_t   g_offController = -1;
		std::string    g_problem;

		UE::UObject* PlayerController()
		{
			if (!g_playerClass) {
				g_playerClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPlayerClassPath);
				if (g_playerClass) {
					g_offController = reflect::Offset(g_playerClass, "Controller");
				}
			}
			if (!g_playerClass || g_offController < 0) {
				return nullptr;
			}
			for (UE::UObject* pawn : reflect::Instances(g_playerClass)) {
				auto** ctrl = reflect::At<UE::UObject*>(pawn, g_offController);
				if (ctrl && *ctrl && reflect::IsLive(*ctrl)) {
					return *ctrl;
				}
			}
			return nullptr;
		}
	}

	bool Ready()
	{
		if (g_fnExec) {
			return true;
		}
		if (!reflect::Ok()) {
			return false;
		}
		auto* k = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kKismetSystemPath);
		if (!k) {
			return false;
		}
		g_kismetCdo = k->GetDefaultObject(false);
		auto* fn = g_kismetCdo ? g_kismetCdo->FindFunction(UE::FName(L"ExecuteConsoleCommand", UE::EFindName::Find)) : nullptr;
		if (!fn) {
			g_problem = "ExecuteConsoleCommand not found on KismetSystemLibrary";
			return false;
		}
		auto* st = reinterpret_cast<UE::UStruct*>(fn);
		g_offWorld = reflect::Offset(st, "WorldContextObject");
		g_offCommand = reflect::Offset(st, "Command");
		g_offPlayer = reflect::Offset(st, "SpecificPlayer");
		g_frame = st->propertiesSize;
		if (g_offWorld < 0 || g_offCommand < 0 || g_offPlayer < 0 || g_frame < 32 || g_frame > 64) {
			g_problem = std::format("ExecuteConsoleCommand's frame is not the expected shape (world @{}, command @{}, player @{}, frame {})", g_offWorld, g_offCommand, g_offPlayer, g_frame);
			logger::error("console: {}", g_problem);
			return false;
		}
		g_fnExec = fn;
		logger::info("console: ExecuteConsoleCommand resolved (frame {} bytes: world @{}, command @{}, player @{})", g_frame, g_offWorld, g_offCommand, g_offPlayer);
		return true;
	}

	bool Run(const std::string& a_command)
	{
		if (!Ready()) {
			return false;
		}
		UE::UObject* ctrl = PlayerController();
		if (!ctrl) {
			g_problem = "no player controller yet";
			return false;
		}
		// The FString's buffer points at our own wide string for the duration of the call (taken by const reference).
		std::wstring w(a_command.begin(), a_command.end());
		struct FStr { wchar_t* data; std::int32_t num; std::int32_t max; };
		alignas(16) std::uint8_t params[64]{};
		*reinterpret_cast<UE::UObject**>(params + g_offWorld) = ctrl;
		*reinterpret_cast<FStr*>(params + g_offCommand) = FStr{ w.data(), static_cast<std::int32_t>(w.size() + 1), static_cast<std::int32_t>(w.size() + 1) };
		*reinterpret_cast<UE::UObject**>(params + g_offPlayer) = ctrl;
		logger::debug("console: {}", a_command);
		g_kismetCdo->ProcessEvent(g_fnExec, params);
		return true;
	}

	std::string Status()
	{
		return g_fnExec ? "ready" : (g_problem.empty() ? "not resolved yet" : g_problem);
	}
}
