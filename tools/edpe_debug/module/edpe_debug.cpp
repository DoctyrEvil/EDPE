// EDPE developer test module. Drop-in: copy edpe_debug.dll beside dxgi.dll in a developer build with [Overlay] DEV=1.
// Measures every available upscaler option (bypass, FSR 3.1.2, FSR 4.1.2, DLSS, native TAA) from inside the game
// and writes a timestamped edpe-selftest-<date>-<time>-mode<N>.txt and .json next to the DLL (never overwrites an
// earlier run). An optional provider switch stress test runs at the end.
//
// The render scale (mode) is fixed when the game starts, so one run covers one mode; the report records the mode,
// the render size and the output size. Run the sequence once per mode by changing Mode in edpe.ini and restarting.
//
// Optional edpe_debug.ini beside this DLL:
//   [Debug]
//   AutoRun=1        run the sequence once, after the upscaler has been producing frames for StartDelay frames
//   StartDelay=300
//   WarmFrames=120   frames ignored after each switch
//   HoldFrames=600   frames measured per block
//   Rounds=2         the option list is run this many times, alternating forward and reverse order, so slow
//                    scene drift shows up as a difference between rounds
//   IncludeTaa=1     0 = skip the native D3D11 TAA backend
//   RunStress=0      1 = provider switch stress test (plus a settle block) after the measurements
//   StressFrames=600
//   AutoExit=0       1 = close the game after the autorun report is written
//   Build=<text>     free-text build identifier copied into the report (the driver script writes branch@commit)
#include "edpe_debug_api.h"

#include "imgui.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

EdpeDebugHost g_hostCopy{};
const EdpeDebugHost* g_host = nullptr;
HMODULE g_self = nullptr;

// ---- settings ----------------------------------------------------------------------------------------------------

struct Settings {
    bool autoRun = false;
    bool autoExit = false;
    bool includeTaa = true;
    bool runStress = false;
    int startDelay = 300;
    int warmFrames = 120;
    int holdFrames = 600;
    int rounds = 2;
    int stressFrames = 600;
    std::string build = "unknown";
} g_cfg;

std::wstring moduleDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p(path);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash + 1);
}

void loadSettings() {
    const std::wstring ini = moduleDir() + L"edpe_debug.ini";
    auto num = [&](const wchar_t* key, int def) {
        return static_cast<int>(GetPrivateProfileIntW(L"Debug", key, def, ini.c_str()));
    };
    g_cfg.autoRun = num(L"AutoRun", 0) != 0;
    g_cfg.autoExit = num(L"AutoExit", 0) != 0;
    g_cfg.includeTaa = num(L"IncludeTaa", 1) != 0;
    g_cfg.runStress = num(L"RunStress", 0) != 0;
    g_cfg.startDelay = std::max(0, num(L"StartDelay", 300));
    g_cfg.warmFrames = std::max(1, num(L"WarmFrames", 120));
    g_cfg.holdFrames = std::max(30, num(L"HoldFrames", 600));
    g_cfg.rounds = std::clamp(num(L"Rounds", 2), 1, 10);
    g_cfg.stressFrames = std::max(60, num(L"StressFrames", 600));
    wchar_t b[256]{};
    GetPrivateProfileStringW(L"Debug", L"Build", L"unknown", b, 256, ini.c_str());
    g_cfg.build.clear();
    for (const wchar_t* c = b; *c; ++c) g_cfg.build.push_back(*c < 128 ? static_cast<char>(*c) : '?');
}

// ---- helpers -----------------------------------------------------------------------------------------------------

double nowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())));
    return v[i];
}

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    std::string s;
    for (; *w; ++w) s.push_back(*w < 128 ? static_cast<char>(*w) : '?');
    return s;
}

std::string timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[64];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
    return b;
}

std::string fileStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[32];
    std::snprintf(b, sizeof(b), "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
    return b;
}

const char* modeName(int mode) {
    switch (mode) {
    case 1: return "Native AA";
    case 2: return "Quality";
    case 3: return "Balanced";
    case 4: return "Performance";
    case 5: return "Ultra Performance";
    default: return "unknown";
    }
}

// Test ids: EDPE_DEBUG_PROVIDER_* (0 bypass, 1 DLSS, 3 TAA, 31 FSR 3.1.2, 4 FSR 4.1.2).
const char* optionName(int id) {
    switch (id) {
    case EDPE_DEBUG_PROVIDER_BYPASS: return "Bypass (no reconstruction)";
    case EDPE_DEBUG_PROVIDER_DLSS: return "NVIDIA DLSS";
    case EDPE_DEBUG_PROVIDER_TAA: return "Native TAA";
    case EDPE_DEBUG_PROVIDER_FSR31: return "AMD FSR 3.1.2";
    case EDPE_DEBUG_PROVIDER_FSR4: return "AMD FSR 4.1.2";
    default: return "?";
    }
}

// Backend enum value (EDPE UpscalerBackend) the option should leave active.
int expectedBackend(int id) {
    switch (id) {
    case EDPE_DEBUG_PROVIDER_DLSS: return 1;
    case EDPE_DEBUG_PROVIDER_TAA: return 3;
    case EDPE_DEBUG_PROVIDER_FSR31:
    case EDPE_DEBUG_PROVIDER_FSR4: return 2;
    default: return 0;
    }
}

int hostBackendOf(int id) { return expectedBackend(id); }

void selectOption(int id) {
    if (id == EDPE_DEBUG_PROVIDER_FSR31 || id == EDPE_DEBUG_PROVIDER_FSR4) g_host->selectProvider(id);
    else g_host->selectBackend(hostBackendOf(id));
}

std::string providerLabel() {
    char b[128]{};
    g_host->describeProvider(b, sizeof(b));
    return b;
}

// ---- test model --------------------------------------------------------------------------------------------------

enum class Kind { Steady, Stress, Settle };
enum class Outcome { Pending, Pass, Fail, Skip };

const char* outcomeName(Outcome o) {
    switch (o) {
    case Outcome::Pass: return "PASS";
    case Outcome::Fail: return "FAIL";
    case Outcome::Skip: return "SKIP";
    default: return "PENDING";
    }
}

struct Phase {
    std::string name;
    Kind kind = Kind::Steady;
    int id = EDPE_DEBUG_PROVIDER_FSR31;
    int round = -1;
    Outcome outcome = Outcome::Pending;
    std::string detail;
    int samples = 0;
    double medianMs = 0, p99Ms = 0, maxMs = 0, gpuMedianMs = 0;
    int switches = 0;
    int renderW = 0, renderH = 0, outW = 0, outH = 0;
};

enum class RunState { Idle, Running, Done };

struct Run {
    RunState state = RunState::Idle;
    std::vector<Phase> phases;
    size_t current = 0;
    int counter = 0;
    bool measuring = false;
    std::vector<double> dt, gpu;
    double lastFrame = 0;
    int savedBackend = 0, savedMode = 0, savedFsr4 = 0;
    bool autoRunRun = false;
    std::string started, stamp;
    int modeUsed = 4;
    double sharpness = 0;
    bool stress = false;
} g_run;

#include "capture_section.inc"

void beginPhase();

// Switching backends while frame generation is running crashed the game once (an access violation inside AMD's frame
// generation provider, in a self-test that began with Bypass), so the self-test refuses to start in that state.
bool g_fgBlocked = false;

void startRun(bool autoRun) {
    if (g_cap.active) return;
    g_fgBlocked = g_host->isFrameGenActive() != 0;
    if (g_fgBlocked) {
        g_host->log(L"EDPE debug: self-test not started, frame generation is on (turn it off first)");
        return;
    }
    g_run = Run{};
    g_run.state = RunState::Running;
    g_run.autoRunRun = autoRun;
    g_run.started = timestamp();
    g_run.stamp = fileStamp();
    g_run.stress = g_cfg.runStress;
    g_host->getState(&g_run.savedBackend, &g_run.savedMode, &g_run.savedFsr4);
    g_run.modeUsed = (g_run.savedMode >= 1 && g_run.savedMode <= 5) ? g_run.savedMode : 4;
    {
        wchar_t s[64]{};
        GetPrivateProfileStringW(L"Upscaler", L"Sharpness", L"0.0", s, 64, (moduleDir() + L"edpe.ini").c_str());
        g_run.sharpness = _wtof(s);
    }

    // Options in a fixed order; unavailable ones become a single SKIP entry with the reason.
    std::vector<int> available;
    auto consider = [&](int id, bool isAvailable, const std::string& reason) {
        if (isAvailable) { available.push_back(id); return; }
        Phase p;
        p.name = optionName(id);
        p.id = id;
        p.outcome = Outcome::Skip;
        p.detail = "unavailable: " + reason;
        g_run.phases.push_back(p);
    };
    consider(EDPE_DEBUG_PROVIDER_BYPASS, true, "");
    consider(EDPE_DEBUG_PROVIDER_FSR31, g_host->isBackendAvailable(2) != 0, narrow(g_host->backendUnavailableReason(2)));
    consider(EDPE_DEBUG_PROVIDER_FSR4, g_host->isFsr4Available() != 0, narrow(g_host->fsr4UnavailableReason()));
    consider(EDPE_DEBUG_PROVIDER_DLSS, g_host->isBackendAvailable(1) != 0, narrow(g_host->backendUnavailableReason(1)));
    if (g_cfg.includeTaa) consider(EDPE_DEBUG_PROVIDER_TAA, g_host->isBackendAvailable(3) != 0, narrow(g_host->backendUnavailableReason(3)));

    for (int r = 0; r < g_cfg.rounds; ++r) {
        std::vector<int> order = available;
        if (r % 2 == 1) std::reverse(order.begin(), order.end());
        for (int id : order) {
            Phase p;
            char n[96];
            std::snprintf(n, sizeof(n), "R%d %s", r + 1, optionName(id));
            p.name = n;
            p.id = id;
            p.round = r;
            g_run.phases.push_back(p);
        }
    }
    const bool fsr4 = std::find(available.begin(), available.end(), EDPE_DEBUG_PROVIDER_FSR4) != available.end();
    if (g_run.stress && fsr4) {
        Phase s;
        s.name = "switch stress (FSR 3.1.2 <-> 4.1.2)";
        s.kind = Kind::Stress;
        s.id = EDPE_DEBUG_PROVIDER_FSR4;
        g_run.phases.push_back(s);
        Phase t;
        t.name = "settle after stress";
        t.kind = Kind::Settle;
        t.id = EDPE_DEBUG_PROVIDER_FSR4;
        g_run.phases.push_back(t);
    } else if (g_run.stress) {
        Phase s;
        s.name = "switch stress";
        s.kind = Kind::Stress;
        s.outcome = Outcome::Skip;
        s.detail = "needs both FSR 3.1.2 and FSR 4.1.2";
        g_run.phases.push_back(s);
    }

    g_run.current = 0;
    g_run.counter = 0;
    g_run.measuring = false;
    g_run.lastFrame = 0;
    beginPhase();
    g_host->log(L"EDPE debug: self-test started");
}

void beginPhase() {
    // Skip entries need no measuring; move to the next runnable phase.
    while (g_run.current < g_run.phases.size() && g_run.phases[g_run.current].outcome == Outcome::Skip) ++g_run.current;
    if (g_run.current >= g_run.phases.size()) return;
    Phase& p = g_run.phases[g_run.current];
    g_run.counter = 0;
    g_run.measuring = false;
    g_run.dt.clear();
    g_run.gpu.clear();
    selectOption(p.kind == Kind::Stress ? EDPE_DEBUG_PROVIDER_FSR31 : p.id);
}

// ---- report ------------------------------------------------------------------------------------------------------

struct Summary {
    int id = 0;
    int blocks = 0;
    double meanMedian = 0, minMedian = 0, maxMedian = 0, meanGpu = 0, meanP99 = 0;
};

std::vector<Summary> summarize() {
    std::vector<Summary> out;
    for (const auto& p : g_run.phases) {
        if (p.kind != Kind::Steady || p.round < 0 || p.outcome == Outcome::Skip || p.outcome == Outcome::Pending) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const Summary& s) { return s.id == p.id; });
        if (it == out.end()) {
            Summary s;
            s.id = p.id;
            s.minMedian = p.medianMs;
            s.maxMedian = p.medianMs;
            out.push_back(s);
            it = out.end() - 1;
        }
        ++it->blocks;
        it->meanMedian += p.medianMs;
        it->meanGpu += p.gpuMedianMs;
        it->meanP99 += p.p99Ms;
        it->minMedian = std::min(it->minMedian, p.medianMs);
        it->maxMedian = std::max(it->maxMedian, p.medianMs);
    }
    for (auto& s : out) {
        s.meanMedian /= s.blocks;
        s.meanGpu /= s.blocks;
        s.meanP99 /= s.blocks;
    }
    return out;
}

void writeReport() {
    const std::wstring base = moduleDir();
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof(nameBuf), "edpe-selftest-%s-mode%d", g_run.stamp.c_str(), g_run.modeUsed);
    const std::wstring stem(nameBuf, nameBuf + std::strlen(nameBuf));

    int passed = 0, failed = 0, skipped = 0;
    int rw = 0, rh = 0, ow = 0, oh = 0;
    for (const auto& p : g_run.phases) {
        if (p.outcome == Outcome::Pass) ++passed;
        else if (p.outcome == Outcome::Fail) ++failed;
        else if (p.outcome == Outcome::Skip) ++skipped;
        if (p.outW > 0 && p.renderW > 0 && p.renderW < p.outW) { rw = p.renderW; rh = p.renderH; ow = p.outW; oh = p.outH; }
        else if (p.outW > 0 && rw == 0) { rw = p.renderW; rh = p.renderH; ow = p.outW; oh = p.outH; }
    }
    const auto summary = summarize();

    {
        std::ofstream t(base + stem + L".txt");
        t << "EDPE self-test  " << g_run.started << "\n";
        t << "build: " << g_cfg.build << "\n";
        char m[160];
        std::snprintf(m, sizeof(m), "mode: %d (%s)  sharpness: %.2f  render %dx%d -> output %dx%d", g_run.modeUsed,
                      modeName(g_run.modeUsed), g_run.sharpness, rw, rh, ow, oh);
        t << m << "\n";
        t << "settings: warm=" << g_cfg.warmFrames << " hold=" << g_cfg.holdFrames << " rounds=" << g_cfg.rounds
          << " stress=" << (g_run.stress ? "on" : "off") << "\n\n";
        t << "Summary (mean of per-block median frame time; spread = max-min across rounds, a drift indicator)\n";
        for (const auto& s : summary) {
            char line[200];
            std::snprintf(line, sizeof(line), "  %-28s mean %.2f ms  spread %.2f ms  p99 %.2f ms  gpu %.2f ms  (%d blocks)",
                          optionName(s.id), s.meanMedian, s.maxMedian - s.minMedian, s.meanP99, s.meanGpu, s.blocks);
            t << line << "\n";
        }
        t << "\nBlocks\n";
        for (const auto& p : g_run.phases) {
            char line[256];
            std::snprintf(line, sizeof(line), "%-5s %-38s frames=%d median=%.2f ms p99=%.2f ms max=%.2f ms gpu=%.2f ms switches=%d",
                          outcomeName(p.outcome), p.name.c_str(), p.samples, p.medianMs, p.p99Ms, p.maxMs, p.gpuMedianMs,
                          p.switches);
            t << line << "\n";
            if (!p.detail.empty()) t << "      " << p.detail << "\n";
        }
        t << "\nresult: " << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
        t << "final provider: " << providerLabel() << "\n";
    }
    {
        std::ofstream j(base + stem + L".json");
        j << "{\n  \"started\": \"" << g_run.started << "\",\n";
        j << "  \"build\": \"" << g_cfg.build << "\",\n";
        char m[200];
        std::snprintf(m, sizeof(m),
                      "  \"mode\": %d, \"modeName\": \"%s\", \"sharpness\": %.2f,\n  \"renderWidth\": %d, \"renderHeight\": %d, "
                      "\"outputWidth\": %d, \"outputHeight\": %d,\n",
                      g_run.modeUsed, modeName(g_run.modeUsed), g_run.sharpness, rw, rh, ow, oh);
        j << m;
        j << "  \"rounds\": " << g_cfg.rounds << ", \"warmFrames\": " << g_cfg.warmFrames << ", \"holdFrames\": "
          << g_cfg.holdFrames << ",\n";
        j << "  \"passed\": " << passed << ", \"failed\": " << failed << ", \"skipped\": " << skipped << ",\n";
        j << "  \"summary\": [\n";
        for (size_t i = 0; i < summary.size(); ++i) {
            const auto& s = summary[i];
            char b[300];
            std::snprintf(b, sizeof(b),
                          "    {\"option\": \"%s\", \"blocks\": %d, \"meanMedianMs\": %.3f, \"minMedianMs\": %.3f, "
                          "\"maxMedianMs\": %.3f, \"meanP99Ms\": %.3f, \"meanGpuMs\": %.3f}%s\n",
                          optionName(s.id), s.blocks, s.meanMedian, s.minMedian, s.maxMedian, s.meanP99, s.meanGpu,
                          i + 1 < summary.size() ? "," : "");
            j << b;
        }
        j << "  ],\n  \"phases\": [\n";
        for (size_t i = 0; i < g_run.phases.size(); ++i) {
            const auto& p = g_run.phases[i];
            char b[512];
            std::snprintf(b, sizeof(b),
                          "    {\"name\": \"%s\", \"outcome\": \"%s\", \"frames\": %d, \"medianMs\": %.3f, \"p99Ms\": %.3f, "
                          "\"maxMs\": %.3f, \"gpuMedianMs\": %.3f, \"switches\": %d}%s\n",
                          p.name.c_str(), outcomeName(p.outcome), p.samples, p.medianMs, p.p99Ms, p.maxMs, p.gpuMedianMs,
                          p.switches, i + 1 < g_run.phases.size() ? "," : "");
            j << b;
        }
        j << "  ]\n}\n";
    }
    g_host->log(L"EDPE debug: self-test finished, report written (edpe-selftest-<stamp>-mode<N>.txt/.json)");
}

// ---- phase evaluation --------------------------------------------------------------------------------------------

void collectStats(Phase& p) {
    p.samples = static_cast<int>(g_run.dt.size());
    p.medianMs = percentile(g_run.dt, 0.5);
    p.p99Ms = percentile(g_run.dt, 0.99);
    p.maxMs = g_run.dt.empty() ? 0 : *std::max_element(g_run.dt.begin(), g_run.dt.end());
    p.gpuMedianMs = percentile(g_run.gpu, 0.5);
    g_host->getRenderSize(&p.renderW, &p.renderH, &p.outW, &p.outH);
}

void finishSteady(Phase& p) {
    collectStats(p);
    std::string fails;
    int backend = 0, mode = 0, fsr4 = 0;
    g_host->getState(&backend, &mode, &fsr4);
    const std::string label = providerLabel();
    if (backend != expectedBackend(p.id)) fails += "backend " + std::to_string(backend) + " active, expected " + std::to_string(expectedBackend(p.id)) + "; ";
    if (p.id == EDPE_DEBUG_PROVIDER_FSR4) {
        if (!g_host->isFsr4Running() || label.rfind("FSR 4", 0) != 0) fails += "FSR 4 not active at end of block (" + label + "); ";
        if (p.gpuMedianMs <= 0.0f) fails += "no GPU time reported; ";
    } else if (p.id == EDPE_DEBUG_PROVIDER_FSR31) {
        if (g_host->isFsr4Running() || label.rfind("FSR 3.1", 0) != 0) fails += "FSR 3.1.2 not active at end of block (" + label + "); ";
    }
    const double stallLimit = std::max(100.0, 8.0 * p.medianMs);
    if (p.maxMs > stallLimit) {
        char b[96];
        std::snprintf(b, sizeof(b), "frame stall %.1f ms (limit %.1f ms); ", p.maxMs, stallLimit);
        fails += b;
    }
    p.outcome = fails.empty() ? Outcome::Pass : Outcome::Fail;
    p.detail = fails.empty() ? "active: " + (p.id == EDPE_DEBUG_PROVIDER_FSR31 || p.id == EDPE_DEBUG_PROVIDER_FSR4 ? label : std::string(optionName(p.id))) : fails;
}

void finishStress(Phase& p) {
    collectStats(p);
    const double stallLimit = 2000.0;  // context creation on a first FSR 4 switch can be slow; a hang is far longer
    p.outcome = p.maxMs <= stallLimit ? Outcome::Pass : Outcome::Fail;
    char b[160];
    std::snprintf(b, sizeof(b), "%d provider switches, longest frame %.1f ms (limit %.0f ms)", p.switches, p.maxMs, stallLimit);
    p.detail = b;
}

void finishRun() {
    g_host->setState(g_run.savedBackend, g_run.savedMode, g_run.savedFsr4);
    g_run.state = RunState::Done;
    writeReport();
    if (g_run.autoRunRun && g_cfg.autoExit) {
        g_host->log(L"EDPE debug: AutoExit=1, closing the game");
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

void tickRun() {
    const double now = nowMs();
    const double dt = g_run.lastFrame > 0 ? now - g_run.lastFrame : 0;
    g_run.lastFrame = now;

    if (g_run.state != RunState::Running) return;
    if (g_run.current >= g_run.phases.size()) { finishRun(); return; }

    Phase& p = g_run.phases[g_run.current];
    ++g_run.counter;
    const int hold = p.kind == Kind::Stress ? g_cfg.stressFrames : g_cfg.holdFrames;

    if (!g_run.measuring) {
        if (g_run.counter >= g_cfg.warmFrames) {
            g_run.measuring = true;
            g_run.counter = 0;
        }
        return;
    }

    if (dt > 0) {
        g_run.dt.push_back(dt);
        g_run.gpu.push_back(g_host->gpuTimeMs());
    }

    if (p.kind == Kind::Stress && g_run.counter % 10 == 0) {
        const int id = ((g_run.counter / 10) % 2 == 0) ? EDPE_DEBUG_PROVIDER_FSR31 : EDPE_DEBUG_PROVIDER_FSR4;
        g_host->selectProvider(id);
        ++p.switches;
    }

    if (g_run.counter >= hold) {
        if (p.kind == Kind::Stress) finishStress(p);
        else finishSteady(p);
        ++g_run.current;
        if (g_run.current >= g_run.phases.size()) { finishRun(); return; }
        beginPhase();
        if (g_run.current >= g_run.phases.size()) finishRun();
    }
}

// ---- UI ----------------------------------------------------------------------------------------------------------

void drawStatusOverlay() {
    if (g_cap.active && !g_cap.statusText.empty()) {
        ImGui::GetForegroundDrawList()->AddText(ImVec2(16, 36), IM_COL32(120, 220, 255, 255), g_cap.statusText.c_str());
    }
    if (g_run.state != RunState::Running || g_run.current >= g_run.phases.size()) return;
    const Phase& p = g_run.phases[g_run.current];
    char b[200];
    std::snprintf(b, sizeof(b), "EDPE self-test: %s (%s, %d frames)", p.name.c_str(), g_run.measuring ? "measuring" : "warming up",
                  g_run.counter);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(16, 16), IM_COL32(255, 220, 80, 255), b);
}

void drawMenu() {
    ImGui::SetNextWindowSize(ImVec2(620, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("EDPE Developer Tests")) { ImGui::End(); return; }

    ImGui::Text("Provider: %s", providerLabel().c_str());
    ImGui::Text("FSR 4 available: %s", g_host->isFsr4Available() ? "yes" : "no");
    ImGui::Separator();

    const bool running = g_run.state == RunState::Running;
    const bool fgOn = g_host->isFrameGenActive() != 0;
    if (running || fgOn) ImGui::BeginDisabled();
    if (ImGui::Button("Run self-test")) startRun(false);
    if (running || fgOn) ImGui::EndDisabled();
    ImGui::SameLine();
    if (running) ImGui::BeginDisabled();
    ImGui::Checkbox("Include switch stress at the end", &g_cfg.runStress);
    if (running) ImGui::EndDisabled();
    if (fgOn) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Frame generation is on: turn it off to run the self-test.");
    ImGui::SameLine();
    if (!running) ImGui::BeginDisabled();
    if (ImGui::Button("Abort")) {
        g_host->setState(g_run.savedBackend, g_run.savedMode, g_run.savedFsr4);
        g_run.state = RunState::Idle;
        g_host->log(L"EDPE debug: self-test aborted, settings restored");
    }
    if (!running) ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Frame generation cycle capture (scripted, recorded by PresentMon)");
    const bool capBusy = running || g_cap.active;
    if (capBusy) ImGui::BeginDisabled();
    if (ImGui::Button("Capture: FSR 4.1.2")) startCapture(EDPE_DEBUG_PROVIDER_FSR4);
    ImGui::SameLine();
    if (ImGui::Button("Capture: FSR 3.1.2")) startCapture(EDPE_DEBUG_PROVIDER_FSR31);
    if (capBusy) ImGui::EndDisabled();
    ImGui::TextDisabled("%s", g_cap.statusText.empty() ? "Schedule: CaptureSequence in edpe_debug.ini. The menu can be closed during a capture; a beep marks each phase." : g_cap.statusText.c_str());
    ImGui::Separator();
    const int options = 3 + (g_host->isBackendAvailable(1) ? 1 : 0) + (g_cfg.includeTaa ? 1 : 0);
    const int frames = g_cfg.rounds * options * (g_cfg.warmFrames + g_cfg.holdFrames) +
                       (g_cfg.runStress ? 2 * g_cfg.warmFrames + g_cfg.stressFrames + g_cfg.holdFrames : 0);
    ImGui::TextDisabled("Hold still in a static scene. About %d frames (roughly %d s at 100 fps).", frames, frames / 100);

    if (!g_run.phases.empty()) {
        ImGui::Separator();
        for (const auto& p : g_run.phases) {
            ImGui::Text("%-7s %-36s median %.2f  p99 %.2f  max %.2f  gpu %.2f ms", outcomeName(p.outcome), p.name.c_str(),
                        p.medianMs, p.p99Ms, p.maxMs, p.gpuMedianMs);
            if (!p.detail.empty()) ImGui::TextDisabled("    %s", p.detail.c_str());
        }
        if (g_run.state == RunState::Done) ImGui::TextDisabled("Report: edpe-selftest-<stamp>-mode<N>.txt / .json beside this DLL");
    }
    ImGui::End();
}

int g_evalFrames = 0;
bool g_autoRunDone = false;

// Runs once per presented frame whether or not any EDPE UI is showing.
void tick() {
    if (!g_host) return;
    if (g_host->hasEvaluated()) ++g_evalFrames; else g_evalFrames = 0;

    if (g_cfg.autoRun && !g_autoRunDone && g_run.state == RunState::Idle && g_evalFrames >= g_cfg.startDelay) {
        g_autoRunDone = true;
        startRun(true);
    }

    tickRun();
    tickCapture();
}

// Runs only while the EDPE menu or the performance overlay is drawing: UI only.
void frame(int menuVisible) {
    if (!g_host || !g_host->currentImguiContext) return;
    void* live = g_host->currentImguiContext();
    static void* lastCtx = nullptr;
    static bool loggedFirst = false;
    if (!loggedFirst || live != lastCtx) {
        wchar_t msg[160];
        swprintf_s(msg, L"EDPE debug: frame() ImGui context %p (menuVisible=%d)%ls", live, menuVisible,
                   loggedFirst ? L" changed" : L"");
        g_host->log(msg);
        loggedFirst = true;
        lastCtx = live;
    }
    if (!live) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(live));
    drawStatusOverlay();
    if (menuVisible) drawMenu();
}

void shutdown() {}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) int EdpeDebugInit(const EdpeDebugHost* host, EdpeDebugModule* out) {
    if (!host || !out || host->version != EDPE_DEBUG_API_VERSION || host->size < sizeof(EdpeDebugHost)) return 0;
    // The host table lives on the host's stack during init: keep a copy.
    g_hostCopy = *host;
    g_host = &g_hostCopy;
    ImGui::SetAllocatorFunctions(host->imguiAlloc, host->imguiFree, host->imguiAllocUser);
    loadSettings();
    out->version = EDPE_DEBUG_API_VERSION;
    out->size = sizeof(EdpeDebugModule);
    out->frame = frame;
    out->tick = tick;
    out->shutdown = shutdown;
    return 1;
}
