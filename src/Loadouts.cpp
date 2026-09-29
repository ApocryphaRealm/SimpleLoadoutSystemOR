#include "Loadouts.h"

#include "GameThread.h"
#include "Settings.h"

namespace loadouts
{
	namespace
	{
		// build-esp.py: SLSOR_Storage01..10 are REFR 0x801 + slot of the vanilla chest ChestHouseTreasuryMiddle02
		// (Oblivion.esm 0x000A496D, DATA flags 0 - never respawns); SLSOR_ActiveLoadout is GLOB 0x800.
		constexpr std::uint32_t kChestBase = 0x000A496D;
		constexpr std::uint32_t kGlobalObject = 0x800;
		constexpr std::uint32_t kFirstStorageObject = 0x801;

		std::array<RE::TESObjectREFR*, settings::kMaxLoadouts> g_storage{};
		RE::TESGlobal*    g_active = nullptr;
		int               g_found = 0;
		std::atomic<bool> g_busy{ false };
		std::string       g_storageNote;

		bool IsGear(RE::TESBoundObject* a_object)
		{
			// The owner's rule (the Skyrim plan, default 2): armour, clothing (rings and amulets are CLOT), weapons,
			// shields (ARMO), ammunition. Torches, spells and everything else are left alone. Nameless items are
			// other mods' hidden pieces and are never touched.
			if (!a_object) {
				return false;
			}
			const auto t = a_object->GetFormType();
			if (t != RE::FormType::Armor && t != RE::FormType::Clothing && t != RE::FormType::Weapon && t != RE::FormType::Ammo) {
				return false;
			}
			const char* n = RE::TESFullName::GetFullName(a_object);
			return n && *n;
		}

		RE::BSSimpleList<RE::ItemChange*>* Items(RE::TESObjectREFR* a_ref)
		{
			auto* changes = a_ref ? a_ref->extra.GetExtraData<RE::ExtraContainerChanges>() : nullptr;
			return changes && changes->changes ? changes->changes->list : nullptr;
		}

		bool IsWorn(RE::ExtraDataList* a_list, bool& a_left)
		{
			if (!a_list) {
				return false;
			}
			a_left = a_list->GetExtraData(RE::EXTRA_DATA_TYPE::WornLeft) != nullptr;
			return a_left || a_list->GetExtraData(RE::EXTRA_DATA_TYPE::Worn) != nullptr;
		}

		bool HasOwnData(RE::ExtraDataList* a_list)
		{
			// what makes one copy of an item differ from another: health, charge, a poison, ownership, a soul
			for (RE::BSExtraData* x = a_list ? a_list->head : nullptr; x; x = x->next) {
				switch (x->type.get()) {
				case RE::EXTRA_DATA_TYPE::Worn:
				case RE::EXTRA_DATA_TYPE::WornLeft:
				case RE::EXTRA_DATA_TYPE::Count:
				case RE::EXTRA_DATA_TYPE::QuickKey:
					break;
				default:
					return true;
				}
			}
			return false;
		}

		struct WornEntry
		{
			RE::ItemChange*    item;
			RE::ExtraDataList* extra;
			std::int32_t       count;
			bool               left;
			bool               quest;
		};

		std::vector<WornEntry> WornEntries(RE::PlayerCharacter* a_player)
		{
			std::vector<WornEntry> out;
			auto* items = Items(a_player);
			if (!items) {
				return out;
			}
			for (RE::ItemChange* item : *items) {
				if (!item || !item->object || !item->extraData || item->count <= 0 || !IsGear(item->object)) {
					continue;
				}
				for (RE::ExtraDataList* xl : *item->extraData) {
					bool left = false;
					if (!IsWorn(xl, left)) {
						continue;
					}
					// ammunition is worn as a whole stack; everything else one piece per list
					const std::int32_t n = item->object->GetFormType() == RE::FormType::Ammo ? item->count : 1;
					out.push_back({ item, xl, n, left, item->object->GetQuestObject() });
				}
			}
			return out;
		}

		std::string NameOf(RE::TESForm* a_form)
		{
			const char* n = a_form ? RE::TESFullName::GetFullName(a_form) : nullptr;
			return n && *n ? std::string(n) : (a_form ? std::format("0x{:08X}", a_form->GetFormID()) : std::string("?"));
		}

		// Everything worn comes off: into the old loadout's container when there is one (quest items only come off and
		// stay), otherwise it stays in the inventory (it belonged to no loadout).
		int StoreWorn(RE::PlayerCharacter* a_player, int a_into, int& a_questKept)
		{
			RE::TESObjectREFR* storage = a_into >= 0 ? g_storage[static_cast<std::size_t>(a_into)] : nullptr;
			int moved = 0;
			for (const auto& w : WornEntries(a_player)) {
				const std::string name = NameOf(w.item->object);
				if (storage && !w.quest) {
					// Removing a worn item unequips it; the piece keeps its own ExtraDataList (health, charge, name).
					a_player->RemoveItem(w.item->object, w.extra, w.count, false, false, storage, nullptr, nullptr, false, false);
					logger::debug("store: {} x{} -> {}", name, w.count, settings::Name(a_into));
					++moved;
				} else {
					if (w.quest && storage) {
						++a_questKept;
					}
					a_player->RemoveWornItem(w.item->object, w.count, w.extra, w.left, true);
					logger::debug("unequip: {} x{} (stays in the inventory{})", name, w.count, w.quest ? ", quest item" : "");
				}
			}
			return moved;
		}

		struct Piece
		{
			RE::TESBoundObject* object;
			std::int32_t        count;
			bool                ownData;
		};

		// The loadout's container empties into the inventory, then every piece is equipped from where it now sits.
		int Restore(RE::PlayerCharacter* a_player, int a_from)
		{
			auto* storage = g_storage[static_cast<std::size_t>(a_from)];
			auto* items = Items(storage);
			if (!storage || !items) {
				return 0;
			}
			std::vector<Piece> pieces;
			for (RE::ItemChange* item : *items) {
				if (!item || !item->object || item->count <= 0) {
					continue;
				}
				std::int32_t listed = 0;
				if (item->extraData) {
					for (RE::ExtraDataList* xl : *item->extraData) {
						if (!xl) {
							continue;
						}
						pieces.push_back({ item->object, 1, HasOwnData(xl) });
						++listed;
					}
				}
				if (item->count > listed) {
					pieces.push_back({ item->object, item->count - listed, false });
				}
			}
			// The move may merge or free the ExtraDataList it is given (the Skyrim mod's 1.0.0 crash), so nothing is
			// kept across it: the whole container is moved, then each piece is found again in the player's inventory.
			for (RE::ItemChange* item : *items) {
				if (!item || !item->object || item->count <= 0) {
					continue;
				}
				storage->RemoveItem(item->object, nullptr, item->count, false, false, a_player, nullptr, nullptr, false, false);
			}
			int restored = 0;
			auto* inv = Items(a_player);
			for (const auto& p : pieces) {
				RE::ExtraDataList* extra = nullptr;
				if (p.ownData && inv) {
					for (RE::ItemChange* item : *inv) {
						if (!item || item->object != p.object || !item->extraData) {
							continue;
						}
						for (RE::ExtraDataList* xl : *item->extraData) {
							bool left = false;
							if (xl && !IsWorn(xl, left) && HasOwnData(xl)) {
								extra = xl;
								break;
							}
						}
						if (extra) {
							break;
						}
					}
				}
				const bool ok = a_player->AddWornItem(p.object, p.count, extra, true);
				logger::debug("equip: {} x{}{} -> {}", NameOf(p.object), p.count, extra ? " (own data)" : "", ok ? "worn" : "REFUSED");
				restored += ok ? 1 : 0;
			}
			return restored;
		}

		void SetActive(int a_active)
		{
			if (g_active) {
				g_active->value = static_cast<float>(a_active);
			}
		}

		void Switch(int a_to)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				g_busy = false;
				return;
			}
			const int from = Active();
			int questKept = 0;
			const int stored = StoreWorn(player, from, questKept);
			const int restored = a_to >= 0 ? Restore(player, a_to) : 0;
			SetActive(a_to);
			logger::info("switch {} -> {}: {} piece(s) stored, {} restored{}", from >= 0 ? settings::Name(from) : "none",
				a_to >= 0 ? settings::Name(a_to) : "none", stored, restored,
				questKept ? std::format(", {} quest item(s) kept in the inventory", questKept) : "");
			g_busy = false;
		}

		// Our records by their on-disk object index: the plugin's runtime load index is whatever Plugins.txt makes it,
		// so every index is tried until the reference with our base object turns up.
		template <class T>
		T* Find(std::uint32_t a_object, std::uint8_t& a_index)
		{
			if (auto* f = RE::TESForm::LookupByID<T>((static_cast<std::uint32_t>(a_index) << 24) | a_object); f && a_index != 0) {
				return f;
			}
			for (std::uint32_t i = 1; i < 0xFF; ++i) {
				auto* f = RE::TESForm::LookupByID<T>((i << 24) | a_object);
				if (!f) {
					continue;
				}
				if constexpr (std::is_same_v<T, RE::TESObjectREFR>) {
					if (!f->data.objectReference || f->data.objectReference->GetFormID() != kChestBase) {
						continue;
					}
				} else {
					auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>((i << 24) | kFirstStorageObject);
					if (!ref || !ref->data.objectReference || ref->data.objectReference->GetFormID() != kChestBase) {
						continue;
					}
				}
				a_index = static_cast<std::uint8_t>(i);
				return f;
			}
			return nullptr;
		}
	}

	void Init()
	{
		std::uint8_t index = 0;
		g_found = 0;
		for (int i = 0; i < settings::kMaxLoadouts; ++i) {
			g_storage[static_cast<std::size_t>(i)] = Find<RE::TESObjectREFR>(kFirstStorageObject + static_cast<std::uint32_t>(i), index);
			g_found += g_storage[static_cast<std::size_t>(i)] ? 1 : 0;
		}
		g_active = Find<RE::TESGlobal>(kGlobalObject, index);
		g_storageNote = g_found == settings::kMaxLoadouts && g_active
		                    ? std::format("{} containers and the global in SimpleLoadoutSystem.esp (load index 0x{:02X})", g_found, index)
		                    : std::format("only {} of {} containers, global {} - is SimpleLoadoutSystem.esp enabled?", g_found, settings::kMaxLoadouts, g_active ? "found" : "MISSING");
		logger::info("storage: {}", g_storageNote);
	}

	int StorageReady() { return g_found; }

	int Active()
	{
		if (!g_active) {
			return -1;
		}
		const int v = static_cast<int>(g_active->value);
		return v >= 0 && v < settings::kMaxLoadouts ? v : -1;
	}

	bool Request(int a_loadout, std::string& a_why)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			a_why = "no player";
			return false;
		}
		if (a_loadout >= settings::Get().count) {
			a_why = "no such loadout";
			return false;
		}
		if (g_found < settings::Get().count || !g_active) {
			a_why = "the loadout storage is missing - is SimpleLoadoutSystem.esp enabled?";
			return false;
		}
		if (player->IsInCombat(false)) {
			a_why = "in combat";
			return false;
		}
		if (g_busy.exchange(true)) {
			a_why = "a switch is already running";
			return false;
		}
		gamethread::Post([a_loadout] { Switch(a_loadout); });
		return true;
	}

	std::vector<WornPiece> Worn()
	{
		std::vector<WornPiece> out;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return out;
		}
		for (const auto& w : WornEntries(player)) {
			out.push_back({ w.item->object->GetFormID(), NameOf(w.item->object), w.count, w.left, w.quest, HasOwnData(w.extra) });
		}
		return out;
	}

	json Contents()
	{
		json j;
		j["active"] = Active();
		j["storage"] = g_storageNote;
		json worn = json::array();
		for (const auto& w : Worn()) {
			worn.push_back({ { "name", w.name }, { "formId", std::format("{:08X}", w.formID) }, { "count", w.count }, { "left", w.left }, { "quest", w.quest }, { "extra", w.extra } });
		}
		j["worn"] = worn;
		json storage = json::array();
		for (int i = 0; i < settings::Get().count; ++i) {
			json items = json::array();
			if (auto* list = Items(g_storage[static_cast<std::size_t>(i)])) {
				for (RE::ItemChange* item : *list) {
					if (item && item->object && item->count > 0) {
						items.push_back({ { "name", NameOf(item->object) }, { "count", item->count } });
					}
				}
			}
			storage.push_back({ { "name", settings::Name(i) }, { "found", g_storage[static_cast<std::size_t>(i)] != nullptr }, { "items", items } });
		}
		j["loadouts"] = storage;
		return j;
	}
}
