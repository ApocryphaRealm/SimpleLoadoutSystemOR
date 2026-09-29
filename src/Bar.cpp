#include "Bar.h"

#include "Loadouts.h"
#include "Settings.h"
#include "Ue.h"

#include <set>

namespace bar
{
	namespace
	{
		constexpr const wchar_t* kMainPartPath = L"/Game/UI/Original/GameMenuLayer/Inventory/MainPart/WBP_OriginalMenu_InventoryMainPart.WBP_OriginalMenu_InventoryMainPart_C";
		// The Controls page's gamepad rebind row: the brown box, its lit state and its label are the templates (the
		// Tween Menu's boxes, the owner 2026-09-29: "each one of the loadouts should be a nav box, just like the tween
		// menu, the brown control page box").
		constexpr const wchar_t* kRowPackage = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget";
		constexpr const wchar_t* kRowAsset = L"WBP_Modern_Settings_GamepadRebindWidget_C";
		constexpr const wchar_t* kRowClass = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C";
		constexpr const wchar_t* kBoxTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.RebindBackground";
		constexpr const wchar_t* kFocusTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.FocusBackground";
		constexpr const wchar_t* kLabelTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.RebindLabel";
		constexpr const wchar_t* kTileClass = L"/Game/UI/Original/Prefabs/WBP_OriginalImageTile.WBP_OriginalImageTile_C";
		constexpr const wchar_t* kTextClass = L"/Game/UI/Modern/Prefabs/WBP_AltarTextBlock.WBP_AltarTextBlock_C";
		constexpr const wchar_t* kButtonClass = L"/Game/UI/Modern/Prefabs/Buttons/WBP_ModernPrefab_InvisibleButton.WBP_ModernPrefab_InvisibleButton_C";   // the category tabs' click surface - a VNavigableButton
		// The game's own navigation (module VCommonUIEnhancedInputNavigation, read from the SDK dump 2026-09-29): every
		// navigable widget implements IVEnhancedInputNavigable, the list is a VAltarNavigableListView, our buttons are
		// VNavigableButtons, and the global subsystem's NavigateToWidget moves the focus between them. The list's
		// BP_OnNavigateUp and the buttons' BP_OnNavigate* / OnFocus / BP_OnClicked events pass through ProcessEvent.
		constexpr const wchar_t* kNavigableInterface = L"/Script/VCommonUIEnhancedInputNavigation.VEnhancedInputNavigable";
		constexpr const wchar_t* kNavSubsystemClass = L"/Script/VCommonUIEnhancedInputNavigation.VUINavigationGlobalSubsystem";

		constexpr double kBoxH = 44.0, kGap = 8.0;   // Slate units; the boxes share the content box's width

		struct Entry
		{
			UE::UObject* sizeBox = nullptr;
			UE::UObject* overlay = nullptr;
			UE::UObject* tile = nullptr;
			UE::UObject* label = nullptr;
			UE::UObject* button = nullptr;   // the mouse's click surface over the box
		};

		std::mutex   g_lock;
		Snapshot     g_snap;
		bool         g_pending = false;
		int          g_tries = 0;
		UE::UObject* g_row = nullptr;          // our HorizontalBox, first in the content box
		UE::UObject* g_contentBox = nullptr;   // inv_cont_verticalbox
		UE::UObject* g_list = nullptr;         // the VModernListView inside the wrapper (its selected row)
		UE::UObject* g_listWrapper = nullptr;  // WBP_OriginalMenu_InventoryListView_C - the navigable list the game focuses
		std::vector<Entry> g_entries;
		int          g_cursor = 0;
		bool         g_focused = false;

		struct Colors
		{
			std::array<float, 4> focus{ 0.072f, 0.038f, 0.028f, 1.0f };     // FocusColor: dark text on the lit box
			std::array<float, 4> unfocus{ 1.0f, 0.939f, 0.847f, 1.0f };     // UnfocusColor: light text on the brown box
		} g_colors;

		void Problem(std::string a_why)
		{
			std::scoped_lock l(g_lock);
			if (g_snap.problem != a_why) {
				logger::warn("bar: {}", a_why);
				g_snap.problem = std::move(a_why);
			}
		}

		// ---- the Tween Menu's widget helpers (TweenMenuOR Menu.cpp) ----------------------------------------------
		UE::UObject* PlayerController()
		{
			static UE::UObject* cached = nullptr;
			if (!cached || !reflect::IsLive(cached)) {
				cached = ue::FirstOf(ue::Class(L"/Script/Engine.PlayerController"));
			}
			return cached;
		}

		bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
		{
			auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
			if (!fn) {
				return false;
			}
			auto* st = reinterpret_cast<UE::UStruct*>(fn);
			const auto fields = reflect::Fields(st);
			if (fields.empty()) {
				return false;
			}
			std::vector<std::uint8_t> params(static_cast<std::size_t>(st->propertiesSize), 0);
			std::memcpy(params.data() + fields.front().second, a_bytes, std::min<std::size_t>(a_size, params.size() - fields.front().second));
			a_obj->ProcessEvent(fn, params.data());
			return true;
		}

		// a real widget (CreateWidget: constructed AND initialised), owned by the player
		UE::UObject* Create(const wchar_t* a_classPath)
		{
			static auto* lib = ue::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			auto* cls = ue::Class(a_classPath);
			auto* pc = PlayerController();
			if (!lib || !cls || !pc) {
				return nullptr;
			}
			ue::Call c(lib->GetDefaultObject(false), L"Create");
			c.Set("WorldContextObject", pc);
			c.Set("WidgetType", cls);
			c.Set("OwningPlayer", pc);
			c.Run();
			return c.Get<UE::UObject*>("ReturnValue");
		}

		void CopyProperty(UE::UObject* a_to, UE::UObject* a_from, std::string_view a_name)
		{
			const auto to = reflect::Offset(a_to->GetClass(), a_name);
			const auto from = reflect::Offset(a_from->GetClass(), a_name);
			const auto size = reflect::Size(a_from->GetClass(), a_name);
			if (to >= 0 && from >= 0 && size > 0 && size == reflect::Size(a_to->GetClass(), a_name)) {
				std::memcpy(reinterpret_cast<std::uint8_t*>(a_to) + to, reinterpret_cast<std::uint8_t*>(a_from) + from, static_cast<std::size_t>(size));
			}
		}

		UE::UClass* LoadClass(const wchar_t* a_package, const wchar_t* a_asset)
		{
			static auto* lib = ue::Class(L"/Script/Engine.KismetSystemLibrary");
			ue::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"LoadClassAsset_Blocking");
			auto* soft = static_cast<std::uint8_t*>(c ? c.At("AssetClass") : nullptr);
			if (!soft) {
				return nullptr;
			}
			new (soft + 0x08) UE::FName(a_package);
			new (soft + 0x10) UE::FName(a_asset);
			c.Run();
			auto* cls = c.Get<UE::UClass*>("ReturnValue");
			logger::info("bar: {} loaded by path ({})", pe::Utf8(UE::FString(a_asset)), cls ? "ok" : "FAILED");
			return cls;
		}

		void SetLit(Entry& a_e, bool a_lit)
		{
			static auto* box = ue::Find(kBoxTemplate);
			static auto* focus = ue::Find(kFocusTemplate);
			auto* from = a_lit ? focus : box;
			const auto off = from ? reflect::Offset(from->GetClass(), "Brush") : -1;
			const auto size = from ? reflect::Size(from->GetClass(), "Brush") : -1;
			if (off >= 0 && size > 0 && a_e.tile) {
				CallFirst(a_e.tile, L"SetBrush", reinterpret_cast<std::uint8_t*>(from) + off, static_cast<std::size_t>(size));   // FSlateBrush
			}
			struct { std::array<float, 4> c; std::uint8_t rule; std::uint8_t pad[7]; } color{ a_lit ? g_colors.focus : g_colors.unfocus, 0, {} };
			if (a_e.label) {
				CallFirst(a_e.label, L"SetColor", &color, sizeof(color));
			}
		}

		void SetLabel(Entry& a_e, const std::string& a_utf8)
		{
			if (!a_e.label) {
				return;
			}
			const int n = MultiByteToWideChar(CP_UTF8, 0, a_utf8.c_str(), -1, nullptr, 0);
			std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
			if (n > 1) {
				MultiByteToWideChar(CP_UTF8, 0, a_utf8.c_str(), -1, w.data(), n);
			}
			if (const auto t = reflect::Offset(a_e.label->GetClass(), "Text"); t >= 0) {
				auto* text = reflect::At<UE::FText>(a_e.label, t);
				text->~FText();   // FText cannot be assigned: the default text goes, ours is made in its place
				new (text) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
			}
		}

		// ---- the menu's widgets -------------------------------------------------------------------------------------
		UE::UObject* Prop(UE::UObject* a_obj, const char* a_name)
		{
			if (!a_obj) {
				return nullptr;
			}
			auto** p = reflect::At<UE::UObject*>(a_obj, reflect::Offset(a_obj->GetClass(), a_name));
			return p && *p && reflect::IsLive(*p) ? *p : nullptr;
		}

		std::string ClassName(UE::UObject* a_o)
		{
			return a_o ? pe::Utf8(a_o->GetClass()->GetFName().ToString()) : std::string();
		}

		struct TArr { UE::UObject** data; std::int32_t num, max; };

		std::vector<UE::UObject*> Children(UE::UObject* a_panel)
		{
			std::vector<UE::UObject*> out;
			const auto offSlots = a_panel ? reflect::Offset(a_panel->GetClass(), "Slots") : -1;
			auto* slots = offSlots >= 0 ? reflect::At<TArr>(a_panel, offSlots) : nullptr;
			for (std::int32_t i = 0; slots && slots->data && i < slots->num && i < 64; ++i) {
				if (auto* c = Prop(slots->data[i], "Content")) {
					out.push_back(c);
				}
			}
			return out;
		}

		UE::UObject* FindByClass(UE::UObject* a_widget, const char* a_classPart, int a_depth)
		{
			if (!a_widget || a_depth < 0) {
				return nullptr;
			}
			if (ClassName(a_widget).find(a_classPart) != std::string::npos) {
				return a_widget;
			}
			for (UE::UObject* c : Children(a_widget)) {
				if (auto* f = FindByClass(c, a_classPart, a_depth - 1)) {
					return f;
				}
			}
			return nullptr;
		}

		UE::UObject* TreeRoot(UE::UObject* a_userWidget)
		{
			return Prop(Prop(a_userWidget, "WidgetTree"), "RootWidget");
		}

		struct Margin { float left, top, right, bottom; };
		struct ChildSize { float value; std::uint8_t rule; };

		struct SlotSettings   // a vertical box slot's settings, kept across the remove / re-add
		{
			ChildSize    size{ 1.0f, 0 };
			Margin       padding{};
			std::uint8_t halign = 0, valign = 0;
		};

		SlotSettings ReadSlot(UE::UObject* a_slot)
		{
			SlotSettings s;
			if (!a_slot) {
				return s;
			}
			auto* cls = a_slot->GetClass();
			if (auto* v = reflect::At<ChildSize>(a_slot, reflect::Offset(cls, "Size"))) { s.size = *v; }
			if (auto* v = reflect::At<Margin>(a_slot, reflect::Offset(cls, "Padding"))) { s.padding = *v; }
			if (auto* v = reflect::At<std::uint8_t>(a_slot, reflect::Offset(cls, "HorizontalAlignment"))) { s.halign = *v; }
			if (auto* v = reflect::At<std::uint8_t>(a_slot, reflect::Offset(cls, "VerticalAlignment"))) { s.valign = *v; }
			return s;
		}

		void ApplySlot(UE::UObject* a_slot, const SlotSettings& a_s)
		{
			if (!a_slot) {
				return;
			}
			CallFirst(a_slot, L"SetSize", &a_s.size, sizeof(a_s.size));
			CallFirst(a_slot, L"SetPadding", &a_s.padding, sizeof(a_s.padding));
			CallFirst(a_slot, L"SetHorizontalAlignment", &a_s.halign, sizeof(a_s.halign));
			CallFirst(a_slot, L"SetVerticalAlignment", &a_s.valign, sizeof(a_s.valign));
		}

		UE::UObject* AddTo(UE::UObject* a_panel, const wchar_t* a_fn, UE::UObject* a_child)
		{
			ue::Call add(a_panel, a_fn);
			add.Set("Content", a_child);
			add.Run();
			return add.Get<UE::UObject*>("ReturnValue");
		}

		// The list's highlighted row: the list widget's selected item and its index (UListView's BP_GetSelectedItem /
		// GetIndexForItem). -1 when the list has no selection, -2 when the calls are not there (then a HELD Up reaches
		// the row instead).
		int SelectedRow()
		{
			if (!g_list || !reflect::IsLive(g_list)) {
				return -2;
			}
			ue::Call sel(g_list, L"BP_GetSelectedItem");
			if (!sel) {
				return -2;
			}
			sel.Run();
			auto* item = sel.Get<UE::UObject*>("ReturnValue");
			if (!item) {
				return -1;
			}
			ue::Call idx(g_list, L"GetIndexForItem");
			if (!idx) {
				return -2;
			}
			idx.Set("Item", item);
			idx.Run();
			return idx.Get<std::int32_t>("ReturnValue");
		}

		void Choose();
		void FocusFromMouse(int a_index);
		void Focus(bool a_on);

		// ---- the game's navigation ----------------------------------------------------------------------------
		UE::UObject* NavSubsystem()
		{
			static UE::UObject* cached = nullptr;
			if (!cached || !reflect::IsLive(cached)) {
				cached = nullptr;
				if (auto* cls = ue::Class(kNavSubsystemClass)) {
					for (UE::UObject* o : reflect::Instances(cls)) {
						cached = o;
					}
				}
			}
			return cached;
		}

		// the IVEnhancedInputNavigable pointer inside a_obj (UE5 FImplementedInterface: class, pointer offset, by-K2), for a
		// TScriptInterface the subsystem can use; nullptr when the class does not implement it natively
		void* NavigableInterface(UE::UObject* a_obj)
		{
			struct Implemented { UE::UClass* cls; std::int32_t offset; bool byK2; };
			struct TArr { Implemented* data; std::int32_t num, max; };
			static auto* iface = ue::Class(kNavigableInterface);
			if (!a_obj || !iface) {
				return nullptr;
			}
			for (UE::UStruct* s = a_obj->GetClass(); s; s = s->superStruct) {
				auto* cls = static_cast<UE::UClass*>(s);
				const auto& arr = *reinterpret_cast<const TArr*>(&cls->interfaces);
				for (std::int32_t i = 0; arr.data && i < arr.num && i < 32; ++i) {
					if (arr.data[i].cls == iface && arr.data[i].offset > 0) {
						return reinterpret_cast<std::uint8_t*>(a_obj) + arr.data[i].offset;
					}
				}
			}
			return nullptr;
		}

		bool NavigateTo(UE::UObject* a_widget, const char* a_what)
		{
			auto* sub = NavSubsystem();
			void* iface = NavigableInterface(a_widget);
			if (!sub || !a_widget || !iface) {
				logger::warn("bar: cannot navigate to {} (subsystem {:p}, widget {:p}, interface {:p})", a_what, static_cast<void*>(sub), static_cast<void*>(a_widget), iface);
				return false;
			}
			struct ScriptInterface { UE::UObject* object; void* iface; };
			ue::Call c(sub, L"NavigateToWidget");
			c.Set("Widget", ScriptInterface{ a_widget, iface });
			const bool ok = c.Run();
			logger::info("bar: navigate to {} ({})", a_what, ok ? "asked" : "no NavigateToWidget");
			return ok;
		}

		int ListElementIndex()
		{
			ue::Call c(g_listWrapper, L"GetCurrentElementIndex");
			if (!c) {
				return -2;
			}
			c.Run();
			return c.Get<std::int32_t>("ReturnValue");
		}

		// the list's events: Up from its top row hands the focus to the row
		void OnListEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (a_obj != g_listWrapper || !a_fn || g_entries.empty()) {
				return;
			}
			const std::string n = pe::FunctionName(a_fn);
			static std::set<std::string> seen;
			if (seen.insert(n).second) {
				logger::info("bar: list fired {}", n);
			}
			if (n == "BP_OnNavigateUp") {
				const int row = ListElementIndex();
				logger::info("bar: list navigate-up at element {}", row);
				if (row <= 0) {
					NavigateTo(g_entries[static_cast<std::size_t>(std::clamp(g_cursor, 0, static_cast<int>(g_entries.size()) - 1))].button, "the loadout row");
				}
			}
		}

		// ProcessEvent on the invisible-button prefab: which of our buttons it is, and what fired on it. The click is
		// whichever event name the prefab uses (learned from the log on the first click; every name fires once here).
		void OnButtonEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			int index = -1;
			for (int i = 0; i < static_cast<int>(g_entries.size()); ++i) {
				if (g_entries[static_cast<std::size_t>(i)].button == a_obj) {
					index = i;
				}
			}
			if (index < 0 || !a_fn) {
				return;
			}
			const std::string n = pe::FunctionName(a_fn);
			static std::set<std::string> seen;
			if (seen.insert(n).second) {
				logger::info("bar: button {} fired {}", index + 1, n);
			}
			if (n == "BP_OnClicked") {
				FocusFromMouse(index);
				Choose();
			} else if (n == "OnFocus" || n == "BP_OnHovered" || n == "OnHovered" || n == "BP_OnFocused") {
				FocusFromMouse(index);
			} else if (n == "OnUnfocus" || n == "BP_OnUnhovered" || n == "BP_OnUnfocused") {
				if (g_cursor == index) {
					Focus(false);
				}
			} else if (n == "BP_OnNavigateLeft" || n == "BP_OnNavigateRight") {
				const int count = static_cast<int>(g_entries.size());
				const int next = (index + (n == "BP_OnNavigateLeft" ? -1 : 1) + count) % count;
				NavigateTo(g_entries[static_cast<std::size_t>(next)].button, n == "BP_OnNavigateLeft" ? "the box to the left" : "the box to the right");
			} else if (n == "BP_OnNavigateDown" || n == "BP_OnNavigateUp") {
				NavigateTo(g_listWrapper, "the item list");
				ue::Call top(g_listWrapper, L"NavigateToIndex");
				top.Set("NewIndex", std::int32_t(0));
				top.Run();
			}
		}

		void Refresh()
		{
			const int active = loadouts::Active();
			for (int i = 0; i < static_cast<int>(g_entries.size()); ++i) {
				SetLit(g_entries[static_cast<std::size_t>(i)], i == active || (g_focused && i == g_cursor));
				SetLabel(g_entries[static_cast<std::size_t>(i)], (i == active ? "* " : "") + settings::Name(i));
			}
		}

		void Build()
		{
			auto* mainPartClass = ue::Class(kMainPartPath);
			UE::UObject* mainPart = nullptr;
			for (UE::UObject* o : mainPartClass ? reflect::Instances(mainPartClass) : std::vector<UE::UObject*>{}) {
				mainPart = o;
			}
			if (!mainPart) {
				Problem("the inventory's main part is not there yet");
				return;
			}
			UE::UObject* content = Prop(mainPart, "inv_mainContent");
			UE::UObject* contentBox = FindByClass(TreeRoot(content), "VerticalBox", 2);   // inv_cont_verticalbox
			if (!contentBox) {
				Problem("the content box's vertical layout was not found");
				return;
			}
			auto* rowClass = ue::Class(kRowClass);
			if (!rowClass) {
				rowClass = LoadClass(kRowPackage, kRowAsset);
			}
			auto* labelTemplate = ue::Find(kLabelTemplate);
			if (!rowClass || !labelTemplate || !ue::Find(kBoxTemplate) || !ue::Find(kFocusTemplate)) {
				Problem("the Controls page's row widget (the box template) is not loaded");
				return;
			}
			if (auto* cdo = rowClass->GetDefaultObject(false)) {
				if (const auto f = reflect::Offset(rowClass, "FocusColor"); f >= 0) {
					std::memcpy(g_colors.focus.data(), reinterpret_cast<std::uint8_t*>(cdo) + f, 16);
				}
				if (const auto u = reflect::Offset(rowClass, "UnfocusColor"); u >= 0) {
					std::memcpy(g_colors.unfocus.data(), reinterpret_cast<std::uint8_t*>(cdo) + u, 16);
				}
			}
			auto* hboxClass = ue::Class(L"/Script/UMG.HorizontalBox");
			auto* sizeBoxClass = ue::Class(L"/Script/UMG.SizeBox");
			auto* overlayClass = ue::Class(L"/Script/UMG.Overlay");
			UE::UObject* outer = Prop(content, "WidgetTree");
			if (!hboxClass || !sizeBoxClass || !overlayClass || !outer) {
				Problem("UMG panel classes or the content's widget tree not found");
				return;
			}
			auto* row = UE::NewObject<UE::UObject>(outer, hboxClass);
			if (!row) {
				Problem("the row's HorizontalBox could not be made");
				return;
			}
			std::vector<Entry> entries;
			const int count = settings::Get().count;
			for (int i = 0; i < count; ++i) {
				Entry e;
				e.sizeBox = UE::NewObject<UE::UObject>(outer, sizeBoxClass);
				e.overlay = UE::NewObject<UE::UObject>(outer, overlayClass);
				e.tile = Create(kTileClass);
				e.label = Create(kTextClass);
				if (!e.sizeBox || !e.overlay || !e.tile || !e.label) {
					Problem(std::format("box {} could not be made (sizebox {:p}, overlay {:p}, tile {:p}, label {:p})", i + 1, static_cast<void*>(e.sizeBox), static_cast<void*>(e.overlay), static_cast<void*>(e.tile), static_cast<void*>(e.label)));
					continue;
				}
				CopyProperty(e.label, labelTemplate, "FontInfo");
				CopyProperty(e.label, labelTemplate, "Justification");
				CopyProperty(e.label, labelTemplate, "FontSizeChannel");
				SetLabel(e, settings::Name(i));
				// the box: a SizeBox of the row's size, an Overlay in it, the tile filling it and the label centred
				const float h = static_cast<float>(kBoxH);
				CallFirst(e.sizeBox, L"SetHeightOverride", &h, sizeof(h));   // the width comes from the row: every box an equal share
				if (auto* ts = AddTo(e.overlay, L"AddChildToOverlay", e.tile)) {
					const std::uint8_t fill = 0;   // HAlign_Fill / VAlign_Fill
					CallFirst(ts, L"SetHorizontalAlignment", &fill, 1);
					CallFirst(ts, L"SetVerticalAlignment", &fill, 1);
				}
				if (auto* ls = AddTo(e.overlay, L"AddChildToOverlay", e.label)) {
					const std::uint8_t centre = 2;   // HAlign_Center / VAlign_Center
					CallFirst(ls, L"SetHorizontalAlignment", &centre, 1);
					CallFirst(ls, L"SetVerticalAlignment", &centre, 1);
				}
				e.button = Create(kButtonClass);
				if (e.button) {
					// the game's navigation moves focus within a LAYER (a gameplay tag on every navigable widget): the button
					// takes the list's, so the subsystem treats them as one layer
					if (auto* listWrapper = FindByClass(contentBox, "InventoryListView", 4)) {
						CopyProperty(e.button, listWrapper, "LayerTag");
					}
					if (auto* bs = AddTo(e.overlay, L"AddChildToOverlay", e.button)) {
						const std::uint8_t fill = 0;
						CallFirst(bs, L"SetHorizontalAlignment", &fill, 1);
						CallFirst(bs, L"SetVerticalAlignment", &fill, 1);
					}
					static bool watched = false;
					if (!watched) {
						watched = pe::Watch(e.button->GetClass(), &OnButtonEvent);
					}
				} else if (i == 0) {
					logger::warn("bar: the invisible-button prefab could not be made - no mouse on the boxes");
				}
				{
					ue::Call c(e.sizeBox, L"SetContent");
					c.Set("Content", e.overlay);
					c.Run();
				}
				if (auto* rs = AddTo(row, L"AddChildToHorizontalBox", e.sizeBox)) {
					const Margin m{ i == 0 ? 0.0f : static_cast<float>(kGap), 0.0f, 0.0f, 0.0f };
					CallFirst(rs, L"SetPadding", &m, sizeof(m));
					const ChildSize fill{ 1.0f, 1 };   // ESlateSizeRule::Fill - the boxes share the row's width equally
					CallFirst(rs, L"SetSize", &fill, sizeof(fill));
				}
				entries.push_back(e);
			}
			if (entries.empty()) {
				return;
			}
			// the row goes FIRST in the content box: its children come off and go back on behind it, each with the
			// slot settings it had
			g_listWrapper = FindByClass(contentBox, "InventoryListView", 4);
			g_list = FindByClass(TreeRoot(g_listWrapper), "ListView", 3);
			if (g_listWrapper) {
				static bool watched = false;
				if (!watched) {
					watched = pe::Watch(g_listWrapper->GetClass(), &OnListEvent);
				}
			}
			const int selectedBefore = SelectedRow();
			std::vector<std::pair<UE::UObject*, SlotSettings>> old;
			for (UE::UObject* c : Children(contentBox)) {
				old.emplace_back(c, ReadSlot(Prop(c, "Slot")));
			}
			for (auto& [c, s] : old) {
				ue::Call r(c, L"RemoveFromParent");
				r.Run();
			}
			if (auto* rslot = AddTo(contentBox, L"AddChildToVerticalBox", row)) {
				SlotSettings s;
				s.padding = Margin{ 0.0f, 4.0f, 0.0f, 10.0f };
				s.halign = 0;   // HAlign_Fill: the row is exactly as wide as the content box, never wider
				ApplySlot(rslot, s);
			} else {
				Problem("AddChildToVerticalBox refused the row");
			}
			for (auto& [c, s] : old) {
				ApplySlot(AddTo(contentBox, L"AddChildToVerticalBox", c), s);
			}
			// The list came off the box and went back on, which loses the game's navigation registration for it (the
			// D-pad stopped moving the list, 2026-09-29): the list's own NavigateToItemIndex re-establishes it on the
			// row it had, and its focus comes back.
			g_list = FindByClass(TreeRoot(FindByClass(contentBox, "InventoryListView", 4)), "ListView", 3);   // the VModernListView inside the wrapper
			// Taking the list off the box DEACTIVATED it (a CommonUI activatable widget), which is what killed the D-pad
			// until the mouse re-activated it (2026-09-29): its own ActivateWidget brings its input and focus back.
			{
				ue::Call act(g_listWrapper, L"ActivateWidget");
				logger::info("bar: list {:p} was at row {}; re-activated ({})", static_cast<void*>(g_listWrapper), selectedBefore, act.Run() ? "ActivateWidget called" : "no ActivateWidget");
			}
			g_row = row;
			g_contentBox = contentBox;
			g_entries = std::move(entries);
			g_cursor = std::clamp(loadouts::Active(), 0, static_cast<int>(g_entries.size()) - 1);
			g_focused = false;
			Refresh();
			const std::string layout = std::format("{} boxes (height {}) filling the row, first in the content box, {} children re-added", g_entries.size(), kBoxH, old.size());
			logger::info("bar: built - {}", layout);
			std::scoped_lock l(g_lock);
			g_snap.built = true;
			g_snap.buttons = static_cast<int>(g_entries.size());
			g_snap.cursor = g_cursor;
			g_snap.focused = false;
			g_snap.layout = layout;
			g_snap.problem.clear();
		}

		void Drop()
		{
			if (g_row && reflect::IsLive(g_row)) {
				ue::Call r(g_row, L"RemoveFromParent");
				r.Run();
			}
			g_row = nullptr;
			g_contentBox = nullptr;
			g_list = nullptr;
			g_listWrapper = nullptr;
			g_entries.clear();
			g_focused = false;
			std::scoped_lock l(g_lock);
			g_snap.built = false;
			g_snap.buttons = 0;
			g_snap.focused = false;
		}

		WORD  g_swallow = 0;         // bits the game must not see until they are released
		std::chrono::steady_clock::time_point g_upHeldSince{};
		bool  g_upHeld = false;

		void Focus(bool a_on)
		{
			g_focused = a_on;
			Refresh();
			std::scoped_lock l(g_lock);
			g_snap.focused = a_on;
			g_snap.cursor = g_cursor;
		}

		void FocusFromMouse(int a_index)
		{
			g_cursor = std::clamp(a_index, 0, std::max(0, static_cast<int>(g_entries.size()) - 1));
			Focus(true);
		}

		void Move(int a_delta)
		{
			const int n = static_cast<int>(g_entries.size());
			if (n <= 0) {
				return;
			}
			g_cursor = (g_cursor + a_delta + n) % n;
			Refresh();
			std::scoped_lock l(g_lock);
			g_snap.cursor = g_cursor;
		}

		void Choose()
		{
			const int target = g_cursor == loadouts::Active() ? -1 : g_cursor;
			// The switch itself stays DISARMED until the engine's equip route is proven (Actor::UnequipObject took the
			// game down on 2026-09-29): the press is logged and the highlight moves, nothing moves in the inventory.
			logger::info("bar: A on {} -> would {} (switch disarmed until the equip route is proven)", settings::Name(g_cursor), target < 0 ? "deselect" : "select");
			Refresh();
		}
	}

	void OnInventory(UE::UObject*, bool a_open)
	{
		if (a_open) {
			g_pending = true;   // built on the next frames, once the menu has constructed its parts
			g_tries = 0;
		} else {
			g_pending = false;
			Drop();
		}
	}

	void Tick()
	{
		if (!g_pending) {
			return;
		}
		static auto retryAt = std::chrono::steady_clock::now();
		if (std::chrono::steady_clock::now() < retryAt) {
			return;
		}
		retryAt = std::chrono::steady_clock::now() + 100ms;
		if (!reflect::Ok()) {
			return;
		}
		Build();
		if (g_snap.built || ++g_tries >= 20) {   // two seconds of retries for a menu still constructing its parts
			g_pending = false;
		}
	}


	bool PadRule(XINPUT_GAMEPAD& a_pad, WORD a_pressed, WORD a_released)
	{
		using Clock = std::chrono::steady_clock;
		const WORD before = a_pad.wButtons;
		g_swallow &= ~a_released;
		if (!g_snap.built || g_entries.empty()) {
			g_upHeld = false;
			a_pad.wButtons &= ~g_swallow;
			return a_pad.wButtons != before;
		}
		constexpr WORD kNav = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
		if (!g_focused) {
			if (a_pressed & XINPUT_GAMEPAD_DPAD_UP) {
				const int row = SelectedRow();
				logger::info("bar: D-pad Up with the list's selected row {} (list {:p})", row, static_cast<void*>(g_list));
				if (row == 0 || row == -1) {
					g_swallow |= XINPUT_GAMEPAD_DPAD_UP;   // the game never steps onto anything above the list
					const int i = std::clamp(g_cursor, 0, static_cast<int>(g_entries.size()) - 1);
					NavigateTo(g_entries[static_cast<std::size_t>(i)].button, "the loadout row");   // the game's focus lands on the button: A clicks it, its sounds play
					Focus(true);
				} else if (row == -2) {
					g_upHeld = true;   // no index to read: a hold reaches the row
					g_upHeldSince = Clock::now();
				}
			}
			if (g_upHeld && (a_pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) && Clock::now() - g_upHeldSince >= 250ms) {
				g_upHeld = false;
				g_swallow |= XINPUT_GAMEPAD_DPAD_UP;
				Focus(true);
			}
			if (a_released & XINPUT_GAMEPAD_DPAD_UP) {
				g_upHeld = false;
			}
		} else {
			const int count = static_cast<int>(g_entries.size());
			if (a_pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
				Move((a_pressed & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : +1);
				NavigateTo(g_entries[static_cast<std::size_t>(std::clamp(g_cursor, 0, count - 1))].button, "the next box");
			}
			if (a_pressed & (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_B)) {
				Focus(false);
				NavigateTo(g_listWrapper, "the item list");
				// back to the TOP of the list, not the element it remembers (the owner, 2026-09-29: it jumped to the
				// Steel Claymore instead of starting at the top)
				ue::Call top(g_listWrapper, L"NavigateToIndex");
				top.Set("NewIndex", std::int32_t(0));
				top.Run();
			}
			// A is the game's: the focused button's own click fires BP_OnClicked
			g_swallow |= static_cast<WORD>(a_pad.wButtons & (kNav & ~XINPUT_GAMEPAD_A));
		}
		a_pad.wButtons &= ~g_swallow;
		return a_pad.wButtons != before;
	}

	Snapshot GetSnapshot()
	{
		std::scoped_lock l(g_lock);
		return g_snap;
	}

	int ListRow()
	{
		return SelectedRow();
	}
}
