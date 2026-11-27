"""Apply the EDPE debug hook to an EDPE worktree. Usage: apply_hook.py <edpe-tree> [--forensics]

Copies the hook sources into src/ and makes small, anchored edits (CMakeLists.txt source list, the D3D12 bridge DRED
calls, the ui.cpp include and frame calls, and a bounce-state summary function in context_census.cpp). Exits non-zero
if an anchor is missing or ambiguous, so a drifted tree fails loudly instead of building without the hook.

--forensics adds a guard in PerfTracker that logs corrupt history indices and recovers. It is off by default because
it can hide the memory corruption it was written to catch.

Never run this against a branch you intend to push.
"""
import shutil
import sys
from pathlib import Path

args = [a for a in sys.argv[1:] if not a.startswith("--")]
forensics = "--forensics" in sys.argv[1:]
if len(args) != 1:
    sys.exit(__doc__)
tree = Path(args[0]).resolve()
here = Path(__file__).resolve().parent

for src, dst in [
    (here / "include" / "edpe_debug_api.h", tree / "src" / "edpe_debug_api.h"),
    (here / "hook" / "debug_hook.h", tree / "src" / "debug_hook.h"),
    (here / "hook" / "debug_hook.cpp", tree / "src" / "debug_hook.cpp"),
    (here / "hook" / "debug_dred.h", tree / "src" / "debug_dred.h"),
    (here / "hook" / "debug_dred.cpp", tree / "src" / "debug_dred.cpp"),
]:
    shutil.copyfile(src, dst)


def edit(rel, pairs):
    path = tree / rel
    text = path.read_bytes().decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    for old, new in pairs:
        if text.count(old) != 1:
            sys.exit("anchor count %d (need 1) in %s: %r" % (text.count(old), rel, old[:70]))
        text = text.replace(old, new, 1)
    path.write_bytes(text.replace("\n", nl).encode("utf-8"))


edit("CMakeLists.txt", [("    src/version.cpp\n", "    src/version.cpp\n    src/debug_hook.cpp\n    src/debug_dred.cpp\n")])
# DRED (Device Removed Extended Data) for the bridge's D3D12 devices: forced on before each device is created, devices
# registered after creation, polled once per frame from EdpeDebugTick. The calls are inline no-ops until debug_dred.cpp
# is linked, so test targets that compile d3d12_bridge.cpp are unaffected.
edit("src/d3d12_bridge.cpp", [
    ('#include "log.h"\n#include "renderdoc_support.h"\n',
     '#include "debug_dred.h"\n#include "log.h"\n#include "renderdoc_support.h"\n'),
    ("                const HRESULT dhr = deviceFactory_->CreateDevice(\n",
     "                EdpeDredEnableForFactory(deviceFactory_.Get());\n"
     "                const HRESULT dhr = deviceFactory_->CreateDevice(\n"),
    ("        hr = createDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12_));\n",
     "        EdpeDredEnableForSystem(d3d12Module_);\n"
     "        hr = createDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12_));\n"),
    ("    queue_->GetTimestampFrequency(&timestampFrequency_);\n",
     "    queue_->GetTimestampFrequency(&timestampFrequency_);\n"
     "    EdpeDredRegister(device12_.Get(), d3d11Device);\n"),
    ("void D3D11D3D12Bridge::shutdown() {\n",
     "void D3D11D3D12Bridge::shutdown() {\n    EdpeDredUnregister(device12_.Get());\n"),
])
edit("src/ui.cpp", [
    ('#include "ui.h"\n', '#include "ui.h"\n#include "debug_hook.h"\n'),
    ("    } // if (draw_menu)\n\n\n    ImGui::Render();\n",
     "    } // if (draw_menu)\n\n    EdpeDebugFrame(cfg.devMode, draw_menu);\n\n    ImGui::Render();\n"),
    ("    if (!show_menu && !show_perf && !show_debug_view) {\n",
     "    EdpeDebugTick(cfg.devMode);\n    if (!show_menu && !show_perf && !show_debug_view) {\n"),
])
# Bounce-state summary, written into every timeline marker by the hook (always applied: it only adds a function).
edit("src/context_census.cpp", [
    ("void ContextCensusOnPresent(IDXGISwapChain* swap_chain, unsigned long long frame, UINT flags) {\n",
     "void EdpeDebugBounceSummary(wchar_t* out, size_t count) {\n"
     "    swprintf_s(out, count, L\"bounce(mode=%d auto=%d tripped=%d reason=%d bounced=%llu flushed=%llu open=%zu)\",\n"
     "               static_cast<int>(s_cb_map_bounce.mode()), static_cast<int>(s_cb_map_bounce.autoState()),\n"
     "               s_cb_map_bounce.isTripped() ? 1 : 0, static_cast<int>(s_cb_map_bounce.lastTripReason()),\n"
     "               static_cast<unsigned long long>(s_cb_map_bounce.bouncedMapCount()),\n"
     "               static_cast<unsigned long long>(s_cb_map_bounce.flushedUnmapCount()), s_cb_map_bounce.openSlotCount());\n"
     "}\n\n"
     "void ContextCensusOnPresent(IDXGISwapChain* swap_chain, unsigned long long frame, UINT flags) {\n"),
])
# Forensics (opt-in, --forensics): PerfTracker::onPresentStart once wrote through a garbage historyIndex_. The guard logs the
# corrupt values and the bounce state and recovers instead of crashing, so the log survives.
if forensics:
  edit("src/perf_overlay.cpp", [
    ('#include "perf_overlay.h"\n', '#include "perf_overlay.h"\n#include "debug_hook.h"\n'),
    ("    frametimeHistory_[historyIndex_] = sample;\n",
     "    if (historyIndex_ >= kFrametimeHistorySize || historyCount_ > kFrametimeHistorySize) {\n"
     "        wchar_t bounce[200]{}, cmsg[512]{};\n"
     "        EdpeDebugBounceSummary(bounce, 200);\n"
     "        swprintf_s(cmsg, L\"EDPE debug: PerfTracker memory is corrupt this=%p historyIndex=0x%zx historyCount=0x%zx %ls\",\n"
     "                   static_cast<void*>(this), historyIndex_, historyCount_, bounce);\n"
     "        EdpeLog(cmsg);\n"
     "        EdpeLogFlush();\n"
     "        historyIndex_ = 0;\n"
     "        historyCount_ = 0;\n"
     "    }\n"
     "    frametimeHistory_[historyIndex_] = sample;\n"),
])
print("hook applied to", tree)
