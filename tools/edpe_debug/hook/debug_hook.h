// Developer-only hook: loads edpe_debug.dll from beside dxgi.dll when [Overlay] DEV=1 and calls it once per frame.
// Not part of any pull request. Applied to a throwaway worktree by tools/edpe_debug/apply_hook.py.
#pragma once

#include <cstddef>

// Call once per presented frame from the UI frame, between ImGui::NewFrame and ImGui::Render.
void EdpeDebugFrame(bool devMode, bool menuVisible);

// Defined by the hook's anchored edit in context_census.cpp: one line describing the CB map-bounce object.
void EdpeDebugBounceSummary(wchar_t* out, size_t count);

// Call once per presented frame before the UI early-return, so the module keeps running with the menu closed.
void EdpeDebugTick(bool devMode);
