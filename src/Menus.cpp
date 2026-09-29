#include "Menus.h"

#include "PEHook.h"

namespace menus
{
	namespace
	{
		constexpr const wchar_t* kInventoryPath = L"/Game/UI/Original/GameMenuLayer/Inventory/WBP_OriginalMenu_Inventory.WBP_OriginalMenu_Inventory_C";

		std::atomic<UE::UClass*>    g_class{ nullptr };
		std::atomic<bool>           g_open{ false };
		std::atomic<UE::UObject*>   g_widget{ nullptr };
		std::atomic<UE::UFunction*> g_fnActivated{ nullptr };
		std::atomic<UE::UFunction*> g_fnDeactivated{ nullptr };
		std::atomic<Listener>       g_listener{ nullptr };

		bool InMenuMode()
		{
			auto* im = RE::InterfaceManager::GetInstance(false, false);
			return im && im->menuMode != 1;
		}

		void OnEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (!a_obj || !a_fn) {
				return;
			}
			bool activated = a_fn == g_fnActivated.load();
			bool deactivated = a_fn == g_fnDeactivated.load();
			if (!activated && !deactivated) {
				const std::string n = pe::FunctionName(a_fn);
				if (n == "BP_OnActivated") {
					g_fnActivated.store(a_fn);
					activated = true;
				} else if (n == "BP_OnDeactivated") {
					g_fnDeactivated.store(a_fn);
					deactivated = true;
				} else {
					return;
				}
			}
			g_open.store(activated);
			g_widget.store(activated ? a_obj : nullptr);
			logger::info("menus: inventory {}", activated ? "activated" : "deactivated");
			if (auto l = g_listener.load()) {
				l(a_obj, activated);
			}
		}
	}

	void Tick()
	{
		if (g_class.load()) {
			return;
		}
		auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kInventoryPath);
		if (!cls || !pe::Watch(cls, &OnEvent)) {
			return;   // not opened yet this session
		}
		g_class.store(cls);
		// the class loads as the menu opens for the first time: it is the menu on screen now
		if (InMenuMode()) {
			g_open.store(true);
		}
		logger::info("menus: watching the inventory ({})", InMenuMode() ? "open now" : "closed");
	}

	bool AnyMenuOpen()
	{
		return InMenuMode();
	}

	bool InventoryOpen()
	{
		if (!InMenuMode()) {
			g_open.store(false);   // back in gameplay: clears a missed deactivation
			g_widget.store(nullptr);
		}
		return g_open.load();
	}

	UE::UObject* InventoryWidget()
	{
		return InventoryOpen() ? g_widget.load() : nullptr;
	}

	void SetListener(Listener a_listener)
	{
		g_listener.store(a_listener);
	}
}
