// The TestBench driving tool (rules 31 and 64): sls.loadouts. Runs on TestBench's listener thread; every read of
// game state is handed to the game thread (gamethread::Call) and waited for.
#include "Bar.h"
#include "Console.h"
#include "GameThread.h"
#include "Loadouts.h"
#include "Menus.h"
#include "Settings.h"
#include "TesThread.h"
#include "TestBenchAPI.h"

namespace tool
{
	namespace
	{
		TestBenchAPI::ITestBenchInterface001* g_tb = nullptr;

		void Write(void* a_sink, TestBenchAPI::WriteFn a_write, const json& a_j) { a_write(a_sink, a_j.dump().c_str()); }

		WORD ButtonBit(const std::string& a_name)
		{
			static const std::pair<const char*, WORD> kButtons[] = {
				{ "a", XINPUT_GAMEPAD_A }, { "b", XINPUT_GAMEPAD_B }, { "x", XINPUT_GAMEPAD_X }, { "y", XINPUT_GAMEPAD_Y },
				{ "lb", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "rb", XINPUT_GAMEPAD_RIGHT_SHOULDER },
				{ "up", XINPUT_GAMEPAD_DPAD_UP }, { "down", XINPUT_GAMEPAD_DPAD_DOWN },
				{ "left", XINPUT_GAMEPAD_DPAD_LEFT }, { "right", XINPUT_GAMEPAD_DPAD_RIGHT },
				{ "start", XINPUT_GAMEPAD_START }, { "back", XINPUT_GAMEPAD_BACK },
			};
			for (const auto& [n, bit] : kButtons) {
				if (a_name == n) {
					return bit;
				}
			}
			return 0;
		}

		json State()
		{
			json j = loadouts::Contents();
			j["version"] = SLS_VERSION;
			j["inventory_open"] = menus::InventoryOpen();
			j["menu_mode"] = menus::AnyMenuOpen();
			const auto g = gamethread::GetStatus();
			j["game_thread"] = { { "installed", g.installed }, { "reads", g.reads }, { "rewritten", g.rewritten }, { "chained_after", g.previousTarget } };
			const auto t = testhread::GetStatus();
			j["tes_thread"] = { { "installed", t.installed }, { "thread", t.thread }, { "calls", t.calls }, { "queued", t.queued } };
			j["queued_steps"] = gamethread::Queued();
			j["loadout_count"] = settings::Get().count;
			const auto b = bar::GetSnapshot();
			j["bar"] = { { "built", b.built }, { "buttons", b.buttons }, { "cursor", b.cursor }, { "focused", b.focused }, { "list_row", bar::ListRow() }, { "layout", b.layout }, { "problem", b.problem } };
			return j;
		}

		void Tool(void*, const char* a_args, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			json args = json::parse(a_args ? a_args : "{}", nullptr, false);
			if (args.is_discarded()) {
				Write(a_sink, a_write, { { "ok", false }, { "error", "args are not JSON" } });
				return;
			}
			const std::string op = args.value("op", "state");
			if (op == "state") {
				json out;
				if (!gamethread::Call([&] { out = State(); }, 3s)) {
					Write(a_sink, a_write, { { "ok", false }, { "error", "the game thread did not answer in 3 s (no controller reads - is the game paused or minimised?)" } });
					return;
				}
				out["ok"] = true;
				Write(a_sink, a_write, out);
				return;
			}
			if (op == "storage") {
				json out;
				if (!gamethread::Call([&] { loadouts::Init(); out = State(); }, 5s)) {
					Write(a_sink, a_write, { { "ok", false }, { "error", "the game thread did not answer in 5 s" } });
					return;
				}
				out["ok"] = true;
				Write(a_sink, a_write, out);
				return;
			}
			if (op == "console") {
				const std::string cmd = args.value("command", "");
				bool ok = false;
				const bool answered = gamethread::Call([&] { ok = console::Run(cmd); }, 5s);
				Write(a_sink, a_write, { { "ok", answered && ok }, { "console", console::Status() }, { "command", cmd } });
				return;
			}
			if (op == "unequip" || op == "store" || op == "take" || op == "equip" || op == "wear") {
				const auto id = static_cast<std::uint32_t>(std::stoul(args.value("formId", "0"), nullptr, 16));
				const int slot = args.value("slot", 0);
				std::string result;
				const bool answered = gamethread::Call([&] {
					result = op == "unequip" ? loadouts::Unequip(id) : op == "store" ? loadouts::Store(id, slot) : op == "take" ? loadouts::Take(slot) : op == "wear" ? loadouts::Wear(id) : loadouts::Equip(id);
				}, 5s);
				Write(a_sink, a_write, { { "ok", answered }, { "result", answered ? result : "the game thread did not answer in 5 s" } });
				return;
			}
			if (op == "select" || op == "deselect") {
				const int slot = op == "select" ? args.value("slot", 0) : -1;
				std::string why;
				bool ok = false;
				if (!gamethread::Call([&] { ok = loadouts::Request(slot, why); }, 3s)) {
					Write(a_sink, a_write, { { "ok", false }, { "error", "the game thread did not answer in 3 s" } });
					return;
				}
				Write(a_sink, a_write, { { "ok", ok }, { "error", why }, { "requested", slot } });
				return;
			}
			if (op == "press") {
				// steps: [{buttons:["up"], ms:120}, {ms:300} (a pause)]
				std::vector<gamethread::Step> steps;
				for (const auto& s : args.value("steps", json::array())) {
					gamethread::Step st;
					for (const auto& b : s.value("buttons", json::array())) {
						const WORD bit = ButtonBit(b.get<std::string>());
						if (!bit) {
							Write(a_sink, a_write, { { "ok", false }, { "error", "unknown button " + b.get<std::string>() } });
							return;
						}
						st.buttons |= bit;
					}
					st.ms = s.value("ms", 120);
					steps.push_back(st);
				}
				gamethread::Queue(steps);
				Write(a_sink, a_write, { { "ok", true }, { "queued", steps.size() } });
				return;
			}
			Write(a_sink, a_write, { { "ok", false }, { "error", "op: state (default) | select {slot} | deselect | press {steps:[{buttons:[a,b,x,y,lb,rb,up,down,left,right,start,back], ms}]}" } });
		}
	}

	bool Register()
	{
		if (g_tb) {
			return true;
		}
		HMODULE tb = ::GetModuleHandleW(L"TestBench.dll");
		auto get = tb ? reinterpret_cast<void* (*)(unsigned)>(::GetProcAddress(tb, "TestBench_GetInterface")) : nullptr;
		g_tb = get ? static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1)) : nullptr;
		if (!g_tb) {
			return false;
		}
		g_tb->RegisterTool("sls.loadouts",
			R"({"description":"Simple Loadout System driver. op: state (default: active loadout, worn pieces, each container's items, storage, menu state) | select {slot 0-based} | deselect | press {steps:[{buttons:[a|b|x|y|lb|rb|up|down|left|right|start|back], ms}]} laid over the real pad on the game thread","inputSchema":{"type":"object","properties":{"op":{"type":"string"},"slot":{"type":"integer"},"steps":{"type":"array"}}},"readOnly":false})",
			&Tool, nullptr);
		logger::info("TestBench tool registered: sls.loadouts");
		return true;
	}
}
