// Host side of the EDPE debug module. See tools/edpe_debug/include/edpe_debug_api.h.
#include "debug_hook.h"
#include "debug_dred.h"

#include "edpe_debug_api.h"
#include "config.h"
#include "fg_integration.h"
#include "ui.h"
#include "log.h"
#include "temporal_inputs.h"
#include "upscaler_backend.h"

#include <windows.h>

#include <filesystem>
#include <string>

#include "imgui.h"

namespace {

edpe::UpscalerManager& mgr() { return edpe::UpscalerManager::instance(); }

int hostSelectProvider(int provider) {
    auto& m = mgr();
    if (!m.isBackendAvailable(edpe::UpscalerBackend::AmdFsr)) return 0;
    if (m.getBackend() != edpe::UpscalerBackend::AmdFsr) m.setBackend(edpe::UpscalerBackend::AmdFsr);
    m.setFsr4Selected(provider == EDPE_DEBUG_PROVIDER_FSR4);
    return 1;
}

void* hostCurrentImguiContext() { return ImGui::GetCurrentContext(); }

int hostSelectBackend(int backend) {
    auto& m = mgr();
    const auto b = static_cast<edpe::UpscalerBackend>(backend);
    if (b != edpe::UpscalerBackend::None && !m.isBackendAvailable(b)) return 0;
    return m.setBackend(b) ? 1 : 0;
}

int hostIsBackendAvailable(int backend) {
    const auto b = static_cast<edpe::UpscalerBackend>(backend);
    return (b == edpe::UpscalerBackend::None || mgr().isBackendAvailable(b)) ? 1 : 0;
}

const wchar_t* hostBackendUnavailableReason(int backend) {
    return mgr().getBackendUnavailableReason(static_cast<edpe::UpscalerBackend>(backend));
}

void hostGetRenderSize(int* rw, int* rh, int* ow, int* oh) {
    const auto& d = mgr().getDiagnostics();
    if (rw) *rw = static_cast<int>(d.renderWidth);
    if (rh) *rh = static_cast<int>(d.renderHeight);
    if (ow) *ow = static_cast<int>(d.outputWidth);
    if (oh) *oh = static_cast<int>(d.outputHeight);
}

int hostSelectMode(int mode) {
    if (mode < 1 || mode > 5) return 0;
    return mgr().setMode(static_cast<edpe::UpscaleMode>(mode)) ? 1 : 0;
}

void hostGetState(int* backend, int* mode, int* fsr4Wanted) {
    auto& m = mgr();
    if (backend) *backend = static_cast<int>(m.getBackend());
    if (mode) *mode = static_cast<int>(m.getMode());
    if (fsr4Wanted) *fsr4Wanted = m.isFsr4Selected() ? 1 : 0;
}

void hostSetState(int backend, int mode, int fsr4Wanted) {
    auto& m = mgr();
    m.setFsr4Selected(fsr4Wanted != 0);
    m.setBackend(static_cast<edpe::UpscalerBackend>(backend));
    m.setMode(static_cast<edpe::UpscaleMode>(mode));
}

int hostIsFsr4Available() { return mgr().isFsr4Available() ? 1 : 0; }
int hostIsFsr4Running() { return mgr().isFsr4Running() ? 1 : 0; }
int hostHasEvaluated() { return mgr().hasEvaluated() ? 1 : 0; }
int hostSetFrameGenBackend(int backend) {
    if (backend < 0 || backend > 2) return 0;
    auto& cfg = edpe::Config::instance();
    cfg.frameGenBackend = backend;
    edpe::FgRuntime::instance().applyConfig(cfg.frameGenBackend, cfg.frameGenMultiplier, cfg.frameGenDynamicMfg,
                                            cfg.frameGenDynamicTargetFps, cfg.frameGenReflexBoost);
    return 1;
}

void hostGetFrameGenState(int* configured, int* active, int* pausedForMenu, float* renderedFps, float* presentedFps) {
    const auto t = edpe::FgRuntime::instance().telemetry();
    if (configured) *configured = static_cast<int>(t.configuredBackend);
    if (active) *active = t.active ? 1 : 0;
    if (pausedForMenu) *pausedForMenu = t.pausedForMenu ? 1 : 0;
    if (renderedFps) *renderedFps = t.renderedFps;
    if (presentedFps) *presentedFps = t.presentedFps;
}

int hostMenuVisible() { return UiMenuVisible() ? 1 : 0; }
int hostIsFrameGenActive() { return edpe::FgRuntime::instance().telemetry().active ? 1 : 0; }
void hostDescribeProvider(char* buffer, uint32_t size) {
    if (!buffer || size == 0) return;
    snprintf(buffer, size, "%s", mgr().describeAmdProvider().c_str());
}
const wchar_t* hostFsr4UnavailableReason() { return mgr().getFsr4UnavailableReason(); }
float hostGpuTimeMs() { return mgr().getBackendGpuTimeMs(mgr().getBackend()); }
void hostLog(const wchar_t* message) { EdpeLog(message); }

struct Loaded {
    bool tried = false;
    HMODULE dll = nullptr;
    EdpeDebugModule module{};
    bool ok = false;
} g;

std::filesystem::path hostDirectory() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&hostDirectory), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void tryLoad() {
    g.tried = true;
    const auto file = hostDirectory() / L"edpe_debug.dll";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return;

    g.dll = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g.dll) {
        EdpeLog(L"EDPE debug: edpe_debug.dll found but failed to load");
        return;
    }
    auto init = reinterpret_cast<EdpeDebugInitFn>(GetProcAddress(g.dll, EDPE_DEBUG_INIT_NAME));
    if (!init) {
        EdpeLog(L"EDPE debug: edpe_debug.dll has no EdpeDebugInit export");
        return;
    }

    EdpeDebugHost host{};
    host.version = EDPE_DEBUG_API_VERSION;
    host.size = sizeof(host);
    host.imguiContext = ImGui::GetCurrentContext();
    ImGui::GetAllocatorFunctions(&host.imguiAlloc, &host.imguiFree, &host.imguiAllocUser);
    host.currentImguiContext = hostCurrentImguiContext;
    host.selectProvider = hostSelectProvider;
    host.selectBackend = hostSelectBackend;
    host.selectMode = hostSelectMode;
    host.isBackendAvailable = hostIsBackendAvailable;
    host.backendUnavailableReason = hostBackendUnavailableReason;
    host.getRenderSize = hostGetRenderSize;
    host.getState = hostGetState;
    host.setState = hostSetState;
    host.isFsr4Available = hostIsFsr4Available;
    host.isFsr4Running = hostIsFsr4Running;
    host.hasEvaluated = hostHasEvaluated;
    host.isFrameGenActive = hostIsFrameGenActive;
    host.setFrameGenBackend = hostSetFrameGenBackend;
    host.getFrameGenState = hostGetFrameGenState;
    host.menuVisible = hostMenuVisible;
    host.describeProvider = hostDescribeProvider;
    host.fsr4UnavailableReason = hostFsr4UnavailableReason;
    host.gpuTimeMs = hostGpuTimeMs;
    host.log = hostLog;

    g.module.size = sizeof(g.module);
    if (!init(&host, &g.module) || g.module.version != EDPE_DEBUG_API_VERSION ||
        g.module.size < sizeof(g.module) || !g.module.frame) {
        EdpeLog(L"EDPE debug: module refused to initialize or reports a different API version");
        return;
    }
    g.ok = true;
    EdpeLog(L"EDPE debug: edpe_debug.dll loaded (developer test build)");
}

} // namespace

namespace {

// Timeline markers for edpe.log, whose own lines carry no time: once a second (and at once when frame generation
// turns on or off) log local time, frame count, frame generation state and the active AMD provider. Only runs where
// the hook runs (the EDPE menu or the performance overlay is showing).
void logTimeline(bool menuVisible) {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    static LARGE_INTEGER last{};
    static unsigned long long frames = 0;
    static int lastFg = -1;
    static int lastMenu = -1;
    ++frames;

    const auto tel = edpe::FgRuntime::instance().telemetry();
    const int fg = tel.active ? 1 : 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const int menu = menuVisible ? 1 : 0;
    const bool changed = (fg != lastFg) || (menu != lastMenu);
    if (!changed && last.QuadPart != 0 && now.QuadPart - last.QuadPart < freq.QuadPart) return;
    last = now;
    lastFg = fg;
    lastMenu = menu;

    const std::string provider = mgr().describeAmdProvider();
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t bounce[200]{};
    EdpeDebugBounceSummary(bounce, 200);
    wchar_t msg[480];
    swprintf_s(msg, L"EDPE debug: marker %02d:%02d:%02d.%03d frames=%llu fg=%d menu=%d%ls provider=%hs %ls", st.wHour, st.wMinute,
               st.wSecond, st.wMilliseconds, frames, fg, menu, changed ? L" (changed)" : L"", provider.c_str(), bounce);
    EdpeLog(msg);
}

} // namespace

void EdpeDebugTick(bool devMode) {
    if (!devMode) return;
    if (!g.tried) tryLoad();
    logTimeline(UiMenuVisible());
    EdpeDredPoll();
    if (g.ok && g.module.tick) g.module.tick();
}

void EdpeDebugFrame(bool devMode, bool menuVisible) {
    if (!devMode) return;
    if (!g.tried) tryLoad();
    if (g.ok) g.module.frame(menuVisible ? 1 : 0);
}
