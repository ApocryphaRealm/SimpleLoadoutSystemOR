#include "Loadouts.h"

#include "GameThread.h"
#include "Push.h"
#include "TesThread.h"
#include "Settings.h"

namespace loadouts
{
	namespace
	{
		// build-esp.py: SLSOR_Storage01..10 are REFR 0x802 + slot (0x801 is the cell) of the vanilla empty chest ChestClutterLower01Empty
		// (Oblivion.esm 0x000086C1, no items, DATA flags 0 - never respawns); SLSOR_ActiveLoadout is GLOB 0x800.
		constexpr std::uint32_t kChestBase = 0x000086C1;
		constexpr std::uint32_t kGlobalObject = 0x800;
		constexpr std::uint32_t kFirstStorageObject = 0x802;

		std::array<RE::TESObjectREFR*, settings::kMaxLoadouts> g_storage{};
		RE::TESGlobal*    g_active = nullptr;
		int               g_found = 0;
		std::atomic<bool> g_busy{ false };
		std::string       g_storageNote;
		std::function<void()> g_onSwitched;

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

		// the console's NoUnequip lock (player.equipitem <id> 1) on a worn piece - a switch must never leave one
		bool Locked(RE::ExtraDataList* a_list)
		{
			return a_list && a_list->GetExtraData(RE::EXTRA_DATA_TYPE::CannotWear) != nullptr;
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
		//
		// Two passes (2026-09-29): the engine will not move a WORN armour piece out of the inventory - RemoveItem took
		// the sword and left the Legion set on the player ("you cannot unequip this item") - so every piece comes off
		// first, and only then is each one found again and moved. Found again, not remembered: unequipping can free the
		// ExtraDataList the piece was worn through (logic library: an ExtraDataList handed to the engine is not yours
		// afterwards), so the second pass takes a fresh unworn list for that object, one with own data when the piece
		// had own data, or none for a plain item.
		struct ToStore
		{
			RE::TESBoundObject* object;
			std::int32_t        count;
			bool                ownData;
		};

		RE::ExtraDataList* UnwornList(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, bool a_ownData)
		{
			auto* inv = Items(a_player);
			if (!inv) {
				return nullptr;
			}
			for (RE::ItemChange* item : *inv) {
				if (!item || item->object != a_object || !item->extraData) {
					continue;
				}
				for (RE::ExtraDataList* xl : *item->extraData) {
					bool left = false;
					if (xl && !IsWorn(xl, left) && HasOwnData(xl) == a_ownData) {
						return xl;
					}
				}
			}
			return nullptr;
		}

		int StoreWorn(RE::PlayerCharacter* a_player, int a_into, int& a_questKept)
		{
			RE::TESObjectREFR* storage = a_into >= 0 ? g_storage[static_cast<std::size_t>(a_into)] : nullptr;
			std::vector<ToStore> toStore;
			for (const auto& w : WornEntries(a_player)) {
				const std::string name = NameOf(w.item->object);
				const bool ownData = HasOwnData(w.extra);
				// Actor::UnequipObject (address library): the actor's full unequip path - see Wear
				a_player->UnequipObject(w.item->object, w.count, w.extra, false, true);   // no lock (see Wear)
				if (storage && !w.quest) {
					toStore.push_back({ w.item->object, w.count, ownData });
					logger::debug("unequip: {} x{} (to be stored)", name, w.count);
				} else {
					if (w.quest && storage) {
						++a_questKept;
					}
					logger::debug("unequip: {} x{} (stays in the inventory{})", name, w.count, w.quest ? ", quest item" : "");
				}
			}
			int moved = 0;
			for (const auto& s : toStore) {
				RE::ExtraDataList* xl = UnwornList(a_player, s.object, s.ownData);
				if (!xl && s.ownData) {
					xl = UnwornList(a_player, s.object, false);   // the own data went with the worn list: any copy, then
				}
				a_player->RemoveItem(s.object, xl, s.count, false, false, storage, nullptr, nullptr, false, false);
				logger::debug("store: {} x{}{} -> {}", NameOf(s.object), s.count, xl ? "" : " (no list)", settings::Name(a_into));
				++moved;
			}
			return moved;
		}

		struct Piece
		{
			RE::TESBoundObject* object;
			std::int32_t        count;
			bool                ownData;
		};

		// The one equip call of the mod: Actor::EquipObject (address library) - the actor's full equip path, what the
		// game's own equip runs. The vtable's AddWornItem only marked the piece worn: the sword came back "worn" but the
		// actor's weapon state stayed stale, and later weapon swaps showed the old sword (2026-09-29). Both crashed off
		// the TES thread; on it they are the right calls.
		bool Wear(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, std::int32_t a_count, RE::ExtraDataList* a_extra)
		{
			// the last argument is the console's NoUnequip lock (player.equipitem <id> 1): true made every restored piece
			// impossible to take off ("you cannot unequip this item", 2026-09-29)
			a_player->EquipObject(a_object, a_count, a_extra, false, false);
			return true;
		}

		// The loadout's container empties into the inventory, then every piece is equipped from where it now sits.
		int Restore(RE::PlayerCharacter* a_player, int a_from)
		{
			auto* storage = g_storage[static_cast<std::size_t>(a_from)];
			auto* items = Items(storage);
			logger::debug("restore from {}: storage {:p}, item list {:p}", settings::Name(a_from), static_cast<void*>(storage), static_cast<void*>(items));
			if (!storage || !items) {
				return 0;
			}
			std::vector<Piece> pieces;
			std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> stacks;   // the move happens from this copy, not the list it empties
			for (RE::ItemChange* item : *items) {
				logger::debug("restore: container entry {} x{}", item && item->object ? NameOf(item->object) : "null", item ? item->count : 0);
				if (!item || !item->object || item->count <= 0) {
					continue;
				}
				stacks.emplace_back(item->object, item->count);
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
			for (const auto& [object, count] : stacks) {
				storage->RemoveItem(object, nullptr, count, false, false, a_player, nullptr, nullptr, false, false);
				logger::debug("restore: {} x{} -> the player", NameOf(object), count);
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
				const bool ok = Wear(a_player, p.object, p.count, extra);
				logger::debug("equip: {} x{}{} - EquipObject called", NameOf(p.object), p.count, extra ? " (own data)" : "");
				(void)ok;
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
			int locked = 0;
			for (const auto& w : WornEntries(player)) {
				if (Locked(w.extra)) {
					++locked;
					logger::warn("switch: {} is LOCKED (CannotWear) after the restore - it could not be taken off again", NameOf(w.item->object));
				}
			}
			SetActive(a_to);
			const bool rebuilt = push::Rebuild();   // the menu's rows follow the game's own push (Push.h)
			logger::info("switch {} -> {}: {} piece(s) stored, {} restored{}{}", from >= 0 ? settings::Name(from) : "none",
				a_to >= 0 ? settings::Name(a_to) : "none", stored, restored,
				questKept ? std::format(", {} quest item(s) kept in the inventory", questKept) : "", rebuilt ? ", list rebuild queued" : "");
			if (restored > 0) {
				logger::info("switch: {} of the {} restored piece(s) locked", locked, restored);
			}
			g_busy = false;
			if (g_onSwitched) {
				gamethread::Post(g_onSwitched);   // the menu's row lives on the UE game thread
			}
		}
	}

	void SetOnSwitched(std::function<void()> a_fn) { g_onSwitched = std::move(a_fn); }

	void Init()
	{
		// What the game actually loaded: the plugin must be in the game's Plugins.txt AND physically in its Data folder
		// (the remaster's loader reads ESPs past MO2's virtual mapping, 2026-09-29 - Root Builder copies it in). The
		// load index comes from the game's own file list.
		int index = -1;
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			std::string files;
			for (RE::TESFile* f : dh->listFiles) {
				if (!f) {
					continue;
				}
				const std::string name = std::filesystem::path(f->filename).filename().string();
				files += std::format("{}{} (0x{:02X})", files.empty() ? "" : ", ", name, f->GetCompileIndex());
				if (_stricmp(name.c_str(), "SimpleLoadoutSystem.esp") == 0) {
					index = f->GetCompileIndex();
				}
			}
			logger::info("plugins loaded by the game: {}", files);
		}
		if (index < 0) {
			logger::error("SimpleLoadoutSystem.esp is NOT in the game's plugin list - the storage cannot exist");
		}
		g_found = 0;
		g_active = nullptr;
		for (std::uint32_t object = kGlobalObject; index >= 0 && object <= kFirstStorageObject + settings::kMaxLoadouts - 1; ++object) {
			const std::uint32_t id = (static_cast<std::uint32_t>(index) << 24) | object;
			auto* form = RE::TESForm::LookupByID(id);
			logger::debug("form 0x{:08X}: {}{}", id, form ? std::format("type {}", static_cast<int>(form->GetFormType())) : "not found",
				form && form->GetFormType() == RE::FormType::Reference ? std::format(", base {}", NameOf(static_cast<RE::TESObjectREFR*>(form)->data.objectReference)) : "");
			if (!form) {
				continue;
			}
			if (object == kGlobalObject && form->GetFormType() == RE::FormType::Global) {
				g_active = static_cast<RE::TESGlobal*>(form);
			} else if (object >= kFirstStorageObject && form->GetFormType() == RE::FormType::Reference) {
				g_storage[object - kFirstStorageObject] = static_cast<RE::TESObjectREFR*>(form);
				++g_found;
			}
		}
		// By EditorID as well, in case the runtime IDs are not index<<24 | object in the remaster
		if (!g_active) {
			auto* byName = RE::TESForm::LookupByEditorID("SLSOR_ActiveLoadout");
			logger::debug("by EditorID SLSOR_ActiveLoadout: {}", byName ? std::format("0x{:08X} type {}", byName->GetFormID(), static_cast<int>(byName->GetFormType())) : "not found");
			if (byName && byName->GetFormType() == RE::FormType::Global) {
				g_active = static_cast<RE::TESGlobal*>(byName);
			}
		}
		for (int i = 0; i < settings::kMaxLoadouts && !g_storage[static_cast<std::size_t>(i)]; ++i) {
			auto* byName = RE::TESForm::LookupByEditorID(std::format("SLSOR_Storage{:02d}", i + 1));
			if (i == 0) {
				logger::debug("by EditorID SLSOR_Storage01: {}", byName ? std::format("0x{:08X} type {}", byName->GetFormID(), static_cast<int>(byName->GetFormType())) : "not found");
			}
			if (byName && byName->GetFormType() == RE::FormType::Reference) {
				g_storage[static_cast<std::size_t>(i)] = static_cast<RE::TESObjectREFR*>(byName);
				++g_found;
			}
		}
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
		if (!testhread::Installed()) {
			g_busy = false;
			a_why = "the TES thread is not hooked yet";
			return false;
		}
		testhread::Post([a_loadout] { Switch(a_loadout); });   // the engine's equipment path runs on the TES thread only
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
			out.push_back({ w.item->object->GetFormID(), NameOf(w.item->object), w.count, w.left, w.quest, HasOwnData(w.extra), Locked(w.extra) });
		}
		return out;
	}

	namespace
	{
		RE::ItemChange* Carried(RE::TESObjectREFR* a_ref, std::uint32_t a_formID)
		{
			auto* items = Items(a_ref);
			if (!items) {
				return nullptr;
			}
			for (RE::ItemChange* item : *items) {
				if (item && item->object && item->object->GetFormID() == a_formID && item->count > 0) {
					return item;
				}
			}
			return nullptr;
		}

		RE::ExtraDataList* FirstList(RE::ItemChange* a_item, bool a_worn)
		{
			if (!a_item || !a_item->extraData) {
				return nullptr;
			}
			for (RE::ExtraDataList* xl : *a_item->extraData) {
				bool left = false;
				if (xl && IsWorn(xl, left) == a_worn) {
					return xl;
				}
			}
			return nullptr;
		}
	}

	std::string Unequip(std::uint32_t a_formID)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* item = player ? Carried(player, a_formID) : nullptr;
		if (!item) {
			return "not carried";
		}
		auto* xl = FirstList(item, true);
		if (!xl) {
			return "not worn";
		}
		bool left = false;
		IsWorn(xl, left);
		logger::info("spike: RemoveWornItem {} (list {:p}, left {})", NameOf(item->object), static_cast<void*>(xl), left);
		const bool ok = player->RemoveWornItem(item->object, 1, xl, left, true);
		return ok ? "unequipped" : "RemoveWornItem returned false";
	}

	std::string Store(std::uint32_t a_formID, int a_slot)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* storage = a_slot >= 0 && a_slot < settings::kMaxLoadouts ? g_storage[static_cast<std::size_t>(a_slot)] : nullptr;
		auto* item = player ? Carried(player, a_formID) : nullptr;
		if (!item || !storage) {
			return !storage ? "no such container" : "not carried";
		}
		auto* xl = FirstList(item, false);
		if (!xl) {
			xl = FirstList(item, true);
		}
		logger::info("spike: RemoveItem {} x1 (list {:p}) -> {}", NameOf(item->object), static_cast<void*>(xl), settings::Name(a_slot));
		player->RemoveItem(item->object, xl, 1, false, false, storage, nullptr, nullptr, false, false);
		return "moved";
	}

	std::string Take(int a_slot)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* storage = a_slot >= 0 && a_slot < settings::kMaxLoadouts ? g_storage[static_cast<std::size_t>(a_slot)] : nullptr;
		auto* items = Items(storage);
		if (!player || !items) {
			return "no such container, or it is empty";
		}
		int n = 0;
		for (RE::ItemChange* item : *items) {
			if (!item || !item->object || item->count <= 0) {
				continue;
			}
			logger::info("spike: container RemoveItem {} x{} -> player", NameOf(item->object), item->count);
			storage->RemoveItem(item->object, nullptr, item->count, false, false, player, nullptr, nullptr, false, false);
			++n;
		}
		return std::format("{} stack(s) taken", n);
	}

	std::string Equip(std::uint32_t a_formID)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* item = player ? Carried(player, a_formID) : nullptr;
		if (!item) {
			return "not carried";
		}
		auto* xl = FirstList(item, false);
		logger::info("spike: EquipObject {} (list {:p})", NameOf(item->object), static_cast<void*>(xl));
		player->EquipObject(item->object, 1, xl, false, false);   // no lock (see Wear)
		return "EquipObject called";
	}

	std::string Wear(std::uint32_t a_formID)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* item = player ? Carried(player, a_formID) : nullptr;
		if (!item) {
			return "not carried";
		}
		auto* xl = FirstList(item, false);
		logger::info("spike: AddWornItem {} (list {:p})", NameOf(item->object), static_cast<void*>(xl));
		const bool ok = Wear(player, item->object, 1, xl);
		return ok ? "worn" : "AddWornItem returned false";
	}

	bool AddItem(std::uint32_t a_formID, int a_count, std::string& a_why)
	{
		auto* form = RE::TESForm::LookupByID(a_formID);
		auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
		if (!object) {
			a_why = form ? "not an item" : "no such form";
			return false;
		}
		if (a_count < 1 || a_count > 1000) {
			a_why = "count must be 1..1000";
			return false;
		}
		if (!testhread::Installed()) {
			a_why = "the TES thread is not hooked yet";
			return false;
		}
		testhread::Post([object, a_count] {
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->AddObjectToContainer(object, nullptr, a_count);
				logger::info("additem: {} x{} added to the player (TES thread)", NameOf(object), a_count);
			}
		});
		return true;
	}

	json Contents()
	{
		json j;
		j["active"] = Active();
		j["storage"] = g_storageNote;
		json worn = json::array();
		for (const auto& w : Worn()) {
			worn.push_back({ { "name", w.name }, { "formId", std::format("{:08X}", w.formID) }, { "count", w.count }, { "left", w.left }, { "quest", w.quest }, { "extra", w.extra }, { "locked", w.locked } });
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
