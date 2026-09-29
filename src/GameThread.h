#pragma once

// ============================================================================================================
// The game thread, reached through the game's own XINPUT1_3!XInputGetState import: the game reads the pad on its
// main thread every frame, so chaining that import slot (the previous target is kept and called - Steam Input's,
// or another plugin's gate, so either can install first) gives a per-frame callback on the game thread and a place
// to run queued work there. Never loads an XInput DLL (gate oblivion-plugin-never-loads-xinput). The pad RULES of
// the loadout bar (D-pad Up onto the bar, Left/Right/A/B while it has focus) are applied to the read here too.
// A second thread also reads through the import (seen 2026-09-29); it is passed through untouched.
// ============================================================================================================

#include <Xinput.h>   // types and constants only

namespace gamethread
{
	bool Install();   // once, at OBSE's post-load (on the game's main thread)

	using Task = std::function<void()>;
	void Post(Task a_task);                              // runs on the next read, on the game thread
	bool Call(Task a_task, std::chrono::milliseconds a_timeout);   // runs it and waits (from another thread)

	using FrameCallback = void (*)();
	void SetFrameCallback(FrameCallback a_callback);     // every read, before the tasks

	// The bar's pad rules: sees the raw pad and rewrites what the game gets. Returns true when it changed anything.
	using PadRule = bool (*)(XINPUT_GAMEPAD& a_pad, WORD a_pressed, WORD a_released);
	void SetPadRule(PadRule a_rule);

	// rule 64: steps laid over the real pad, one after another (a step with nothing set is a pause)
	struct Step
	{
		WORD buttons = 0;
		int  ms = 100;
	};
	void        Queue(const std::vector<Step>& a_steps);
	std::size_t Queued();

	struct Status
	{
		bool          installed = false;
		std::uint64_t reads = 0;
		std::uint64_t rewritten = 0;
		std::string   previousTarget;
	};
	Status GetStatus();
}
