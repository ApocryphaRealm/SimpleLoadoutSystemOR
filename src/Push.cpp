#include "Push.h"

#include "Scan.h"

namespace push
{
	namespace
	{
		// 0x18 bytes into the function (after its register saves): the frame set-up, the stack cookie and the
		// GetMenuByClass(kInventoryMenu = 0x3EA) call - one match in the image (2026-09-29)
		constexpr const char* kPattern = "48 8D A8 98 FE FF FF 48 81 EC 40 02 00 00 0F 29 70 C8 0F 29 78 B8 44 0F 29 40 A8 44 0F 29 48 98 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 F0 00 00 00 B9 EA 03 00 00 E8";
		constexpr std::size_t kPatternOffset = 0x18;

		using Rebuild_t = void (*)();

		Rebuild_t g_rebuild = nullptr;
	}

	bool Install()
	{
		if (g_rebuild) {
			return true;
		}
		std::uint8_t* site = scan::First(kPattern);
		if (!site) {
			logger::error("push: the inventory list rebuild was not found in this game build - the list refreshes only on reopening the menu");
			return false;
		}
		g_rebuild = reinterpret_cast<Rebuild_t>(site - kPatternOffset);
		logger::info("push: the game's inventory list rebuild (game+0x{:X}) runs after each switch",
			reinterpret_cast<std::uintptr_t>(g_rebuild) - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
		return true;
	}

	bool Rebuild()
	{
		if (!g_rebuild) {
			return false;
		}
		g_rebuild();
		return true;
	}
}
