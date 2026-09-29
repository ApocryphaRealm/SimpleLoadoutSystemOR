#include "GameThread.h"

#include <condition_variable>
#include <deque>

namespace gamethread
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using Clock = std::chrono::steady_clock;

		XInputGetState_t           g_previous = nullptr;
		std::atomic<bool>          g_installed{ false };
		std::atomic<std::uint64_t> g_reads{ 0 }, g_rewritten{ 0 }, g_otherThreadReads{ 0 };
		std::string                g_previousTarget;
		DWORD                      g_gameThread = 0;
		std::atomic<FrameCallback> g_frameCallback{ nullptr };
		std::atomic<PadRule>       g_padRule{ nullptr };

		std::mutex       g_taskLock;
		std::deque<Task> g_tasks;

		std::mutex        g_stepLock;
		std::deque<Step>  g_steps;
		bool              g_stepRunning = false;
		Clock::time_point g_stepUntil{};
		Step              g_step{};

		WORD  g_prevRaw = 0;
		WORD  g_prevOut = 0;
		DWORD g_packetOffset = 0;

		void RunTasks()
		{
			for (;;) {
				Task t;
				{
					std::scoped_lock l(g_taskLock);
					if (g_tasks.empty()) {
						return;
					}
					t = std::move(g_tasks.front());
					g_tasks.pop_front();
				}
				t();
			}
		}

		bool Inject(XINPUT_GAMEPAD& a_pad)
		{
			std::scoped_lock l(g_stepLock);
			const auto now = Clock::now();
			if (g_stepRunning && now >= g_stepUntil) {
				g_stepRunning = false;
			}
			if (!g_stepRunning && !g_steps.empty()) {
				g_step = g_steps.front();
				g_steps.pop_front();
				g_stepRunning = true;
				g_stepUntil = now + std::chrono::milliseconds(std::max(1, g_step.ms));
			}
			if (!g_stepRunning) {
				return false;
			}
			a_pad.wButtons |= g_step.buttons;
			return true;
		}

		DWORD WINAPI Chained(DWORD a_user, XINPUT_STATE* a_state)
		{
			if (GetCurrentThreadId() != g_gameThread) {
				if (g_otherThreadReads.fetch_add(1, std::memory_order_relaxed) == 0) {
					logger::info("gamethread: thread {} also reads the controller through the game's import - passed through untouched", GetCurrentThreadId());
				}
				return g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			}
			if (a_user == 0) {
				if (auto cb = g_frameCallback.load(std::memory_order_acquire)) {
					cb();
				}
				RunTasks();
			}
			DWORD rc = g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			if (!a_state || a_user != 0) {
				return rc;
			}
			if (rc != ERROR_SUCCESS) {
				XINPUT_GAMEPAD probe{};
				if (!Inject(probe)) {
					return rc;
				}
				*a_state = XINPUT_STATE{};   // no pad connected, but a test step runs: hand over the step alone
				a_state->Gamepad = probe;
				rc = ERROR_SUCCESS;
			} else {
				Inject(a_state->Gamepad);
			}
			g_reads.fetch_add(1, std::memory_order_relaxed);
			const WORD raw = a_state->Gamepad.wButtons;
			const WORD pressed = raw & ~g_prevRaw;
			const WORD released = ~raw & g_prevRaw;
			g_prevRaw = raw;
			if (auto rule = g_padRule.load(std::memory_order_acquire)) {
				if (rule(a_state->Gamepad, pressed, released)) {
					g_rewritten.fetch_add(1, std::memory_order_relaxed);
				}
			}
			// the game only processes a state whose packet number moved: move it whenever what we hand over changes
			if (a_state->Gamepad.wButtons != g_prevOut) {
				++g_packetOffset;
				g_prevOut = a_state->Gamepad.wButtons;
			}
			a_state->dwPacketNumber += g_packetOffset;
			return rc;
		}

		std::string ModuleOf(const void* a_p)
		{
			HMODULE m = nullptr;
			wchar_t w[MAX_PATH]{};
			if (a_p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(a_p), &m) && m &&
				GetModuleFileNameW(m, w, MAX_PATH)) {
				return std::filesystem::path(w).filename().string();
			}
			return "an unknown module";
		}
	}

	bool Install()
	{
		if (g_installed.load()) {
			return true;
		}
		static bool s_tried = false;
		if (s_tried) {
			return false;
		}
		s_tried = true;
		g_gameThread = GetCurrentThreadId();   // post-load runs on the game's main thread
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			logger::error("gamethread: the game has no import table");
			return false;
		}
		for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
			const char* dll = reinterpret_cast<const char*>(base + desc->Name);
			if (_strnicmp(dll, "xinput", 6) != 0) {
				continue;
			}
			auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
			auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
				                       ? IMAGE_ORDINAL64(names->u1.Ordinal) == 2
				                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "XInputGetState") == 0;
				if (!match) {
					continue;
				}
				auto* slot = reinterpret_cast<XInputGetState_t*>(&slots->u1.Function);
				DWORD old = 0;
				if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
					logger::error("gamethread: the import slot could not be made writable ({})", GetLastError());
					return false;
				}
				g_previous = *slot;
				*slot = &Chained;
				VirtualProtect(slot, sizeof(void*), old, &old);
				g_previousTarget = ModuleOf(reinterpret_cast<const void*>(g_previous));
				g_installed.store(true);
				logger::info("gamethread: the game's {} XInputGetState import is chained (previously {} in {})", dll,
					reinterpret_cast<const void*>(g_previous), g_previousTarget);
				return true;
			}
		}
		logger::error("gamethread: the game imports no XInputGetState");
		return false;
	}

	void Post(Task a_task)
	{
		std::scoped_lock l(g_taskLock);
		g_tasks.push_back(std::move(a_task));
	}

	bool Call(Task a_task, std::chrono::milliseconds a_timeout)
	{
		auto done = std::make_shared<std::atomic<bool>>(false);
		auto cv = std::make_shared<std::condition_variable>();
		auto m = std::make_shared<std::mutex>();
		Post([=, t = std::move(a_task)] {
			t();
			{
				std::scoped_lock l(*m);
				done->store(true);
			}
			cv->notify_all();
		});
		std::unique_lock l(*m);
		return cv->wait_for(l, a_timeout, [&] { return done->load(); });
	}

	void SetFrameCallback(FrameCallback a_callback)
	{
		g_frameCallback.store(a_callback, std::memory_order_release);
	}

	void SetPadRule(PadRule a_rule)
	{
		g_padRule.store(a_rule, std::memory_order_release);
	}

	void Queue(const std::vector<Step>& a_steps)
	{
		std::scoped_lock l(g_stepLock);
		for (const auto& s : a_steps) {
			g_steps.push_back(s);
		}
	}

	std::size_t Queued()
	{
		std::scoped_lock l(g_stepLock);
		return g_steps.size() + (g_stepRunning ? 1 : 0);
	}

	Status GetStatus()
	{
		return { g_installed.load(), g_reads.load(), g_rewritten.load(), g_previousTarget };
	}
}
