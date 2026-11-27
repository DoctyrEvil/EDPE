// ---- scripted capture: frame generation cycle recorded by PresentMon ---------------------------------------------------
// Included into edpe_debug.cpp inside its anonymous namespace. A capture picks the AMD provider, then walks a schedule
// of frame generation off/on phases (the same call the menu combo makes), logging every transition with the QPC time in
// milliseconds. AMD's PresentMon runs as a child process attached to this process and writes its CSV with --qpc_time_ms,
// so each PresentMon row joins exactly to the events (tools/analyze_capture.py). No hand timing is needed.
//
// edpe_debug.ini keys ([Debug]):
//   CaptureProvider=4           4 = AMD FSR 4.1.2, 31 = AMD FSR 3.1.2
//   CaptureSequence=off:15:m1,off:20:m0,off:20:m1,on:15:m0,off:20:m0,off:20:m1
//                               phases as state:seconds[:menu]; m1 = F5 menu open, m0 = closed (the module posts F5 to the
//                               game window to change it), no suffix = leave the menu alone
//   CaptureSettle=3             seconds with frame generation off before the schedule
//   PresentMonPath=C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe

struct CapPhase {
    bool fg = false;
    double seconds = 0;
    int menu = -1;  // -1 leave, 0 closed, 1 open
    std::string name;
};

struct Capture {
    bool active = false;
    bool finishing = false;
    std::vector<CapPhase> phases;
    size_t current = 0;
    double phaseStartMs = 0;
    double markerMs = 0;
    double finishDeadlineMs = 0;
    int provider = EDPE_DEBUG_PROVIDER_FSR4;
    std::wstring dir;
    std::ofstream events;
    std::ofstream summary;
    HANDLE pm = nullptr;
    std::vector<double> dt;
    double lastTickMs = 0;
    unsigned long long frames = 0;
    int savedBackend = 0, savedMode = 0, savedFsr4 = 0, savedFg = 0;
    std::string statusText;
} g_cap;

std::string capLocalTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[48];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

void capEvent(const char* name, const std::string& detail) {
    if (!g_cap.events.is_open()) return;
    int configured = 0, active = 0, paused = 0;
    float rfps = 0, pfps = 0;
    g_host->getFrameGenState(&configured, &active, &paused, &rfps, &pfps);
    char line[512];
    std::snprintf(line, sizeof(line), "%.3f,%s,%s,%d,%d,%d,%d,%s,%.1f,%.1f,%s\n", nowMs(), capLocalTime().c_str(), name,
                  configured, active, paused, g_host->menuVisible() ? 1 : 0, providerLabel().c_str(), rfps, pfps, detail.c_str());
    g_cap.events << line;
    g_cap.events.flush();
}

std::vector<CapPhase> parseSequence(const std::string& text) {
    std::vector<CapPhase> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t comma = text.find(',', pos);
        std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? text.size() : comma + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        std::string state = item.substr(0, colon);
        const double secs = std::atof(item.c_str() + colon + 1);
        if (secs <= 0) continue;
        CapPhase p;
        p.fg = (state == "on");
        p.seconds = secs;
        p.name = state;
        const size_t colon2 = item.find(':', colon + 1);
        if (colon2 != std::string::npos && item.size() > colon2 + 2 && item[colon2 + 1] == 'm') {
            p.menu = item[colon2 + 2] == '1' ? 1 : 0;
            p.name += (p.menu == 1 ? "+menu" : "+nomenu");
        }
        out.push_back(p);
    }
    return out;
}

std::string capIniString(const wchar_t* key, const wchar_t* def) {
    wchar_t b[1024]{};
    GetPrivateProfileStringW(L"Debug", key, def, b, 1024, (moduleDir() + L"edpe_debug.ini").c_str());
    std::string s;
    for (const wchar_t* c = b; *c; ++c) s.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    return s;
}

std::wstring capIniWide(const wchar_t* key, const wchar_t* def) {
    wchar_t b[1024]{};
    GetPrivateProfileStringW(L"Debug", key, def, b, 1024, (moduleDir() + L"edpe_debug.ini").c_str());
    return b;
}

// Finds the game's main window (the largest visible top-level window of this process).
struct CapWindowSearch {
    DWORD pid = 0;
    HWND best = nullptr;
    long area = 0;
};

BOOL CALLBACK capEnumWindow(HWND hwnd, LPARAM lp) {
    auto* s = reinterpret_cast<CapWindowSearch*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != s->pid || !IsWindowVisible(hwnd)) return TRUE;
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    const long area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > s->area) { s->area = area; s->best = hwnd; }
    return TRUE;
}

// The menu is toggled by F5; post the key to the game window so EDPE handles it as a normal key press.
void capSetMenu(int wanted) {
    if (wanted < 0 || (g_host->menuVisible() ? 1 : 0) == wanted) return;
    CapWindowSearch s;
    s.pid = GetCurrentProcessId();
    EnumWindows(capEnumWindow, reinterpret_cast<LPARAM>(&s));
    if (!s.best) {
        g_host->log(L"EDPE debug: capture could not find the game window to toggle the menu");
        return;
    }
    PostMessageW(s.best, WM_KEYDOWN, VK_F5, 1);
    PostMessageW(s.best, WM_KEYUP, VK_F5, static_cast<LPARAM>(0xC0000001));
}

void capApplyPhase() {
    const CapPhase& p = g_cap.phases[g_cap.current];
    g_host->setFrameGenBackend(p.fg ? 1 : 0);
    capSetMenu(p.menu);
    g_cap.phaseStartMs = nowMs();
    g_cap.dt.clear();
    char d[96];
    std::snprintf(d, sizeof(d), "phase=%zu name=%s seconds=%.0f", g_cap.current, p.name.c_str(), p.seconds);
    capEvent("phase_start", d);
    MessageBeep(MB_OK);
}

void capWriteSummaryLine(const CapPhase& p) {
    char line[256];
    std::snprintf(line, sizeof(line), "phase %zu fg=%-3s seconds=%.0f in-process frames=%zu median=%.2f ms p99=%.2f ms max=%.2f ms\n",
                  g_cap.current, p.name.c_str(), p.seconds, g_cap.dt.size(), percentile(g_cap.dt, 0.5),
                  percentile(g_cap.dt, 0.99), g_cap.dt.empty() ? 0.0 : *std::max_element(g_cap.dt.begin(), g_cap.dt.end()));
    g_cap.summary << line;
}

void startCapture(int provider) {
    if (g_cap.active || g_run.state == RunState::Running) return;
    const std::wstring pmPath = capIniWide(L"PresentMonPath", L"C:\\Program Files\\AMD\\CNext\\CNext\\PresentMon-x64.exe");
    if (GetFileAttributesW(pmPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_host->log(L"EDPE debug: capture not started, PresentMon not found (set PresentMonPath in edpe_debug.ini)");
        g_cap.statusText = "PresentMon not found";
        return;
    }
    if (provider == EDPE_DEBUG_PROVIDER_FSR4 && !g_host->isFsr4Available()) {
        g_host->log(L"EDPE debug: capture not started, FSR 4 is unavailable");
        g_cap.statusText = "FSR 4 unavailable";
        return;
    }
    g_cap.phases = parseSequence(capIniString(L"CaptureSequence", L"off:15:m1,off:20:m0,off:20:m1,on:15:m0,off:20:m0,off:20:m1"));
    if (g_cap.phases.empty()) {
        g_host->log(L"EDPE debug: capture not started, CaptureSequence is empty or invalid");
        return;
    }
    CapPhase settle;
    settle.fg = false;
    settle.seconds = std::max(1, static_cast<int>(GetPrivateProfileIntW(L"Debug", L"CaptureSettle", 3, (moduleDir() + L"edpe_debug.ini").c_str())));
    settle.name = "off";
    g_cap.phases.insert(g_cap.phases.begin(), settle);
    double total = 0;
    for (const auto& p : g_cap.phases) total += p.seconds;

    g_cap.provider = provider;
    g_host->getState(&g_cap.savedBackend, &g_cap.savedMode, &g_cap.savedFsr4);
    int cfg = 0, act = 0, pau = 0;
    float rf = 0, pf = 0;
    g_host->getFrameGenState(&cfg, &act, &pau, &rf, &pf);
    g_cap.savedFg = cfg;

    const std::string stamp = fileStamp();
    g_cap.dir = moduleDir() + L"edpe-capture-" + std::wstring(stamp.begin(), stamp.end()) +
                (provider == EDPE_DEBUG_PROVIDER_FSR4 ? L"-fsr4" : L"-fsr31") + L"\\";
    CreateDirectoryW(g_cap.dir.c_str(), nullptr);

    g_cap.events.open(g_cap.dir + L"events.csv");
    g_cap.events << "qpc_ms,local_time,event,fg_configured,fg_active,paused_for_menu,menu_open,provider,rendered_fps,presented_fps,detail\n";
    g_cap.summary.open(g_cap.dir + L"summary.txt");
    g_cap.summary << "EDPE capture " << timestamp() << "  build " << g_cfg.build << "  provider "
                  << (provider == EDPE_DEBUG_PROVIDER_FSR4 ? "FSR 4.1.2" : "FSR 3.1.2") << "\n";

    g_host->selectProvider(provider);
    g_host->setFrameGenBackend(0);

    wchar_t cmd[2048];
    swprintf_s(cmd,
               L"\"%s\" --process_id %lu --output_file \"%spresentmon.csv\" --session_name EDPEDbg%lu --timed %d "
               L"--terminate_after_timed --no_console_stats --qpc_time_ms",
               pmPath.c_str(), GetCurrentProcessId(), g_cap.dir.c_str(), GetCurrentProcessId(),
               static_cast<int>(total) + 8);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        g_host->log(L"EDPE debug: capture not started, PresentMon failed to launch");
        g_cap.events.close();
        g_cap.summary.close();
        return;
    }
    CloseHandle(pi.hThread);
    g_cap.pm = pi.hProcess;

    g_cap.active = true;
    g_cap.finishing = false;
    g_cap.current = 0;
    g_cap.frames = 0;
    g_cap.lastTickMs = 0;
    g_cap.markerMs = 0;
    char d[160];
    std::snprintf(d, sizeof(d), "presentmon_pid=%lu total_seconds=%.0f provider=%d", pi.dwProcessId, total, provider);
    capEvent("capture_start", d);
    capApplyPhase();
    g_host->log(L"EDPE debug: capture started (frame generation cycle recorded by PresentMon)");
}

void capFinish() {
    // Restore what the capture changed.
    g_host->setFrameGenBackend(g_cap.savedFg);
    g_host->setState(g_cap.savedBackend, g_cap.savedMode, g_cap.savedFsr4);
    capEvent("capture_end", "state restored");
    g_cap.finishing = true;
    g_cap.finishDeadlineMs = nowMs() + 10000.0;
}

// DWM's own view of the display: refresh rate and composition rate (Hz) plus its refresh counter. Loaded at run time so
// the module needs no extra import library.
std::string capDwmText() {
    typedef HRESULT(WINAPI * GetTimingFn)(HWND, void*);
    static GetTimingFn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (HMODULE m = LoadLibraryW(L"dwmapi.dll")) fn = reinterpret_cast<GetTimingFn>(GetProcAddress(m, "DwmGetCompositionTimingInfo"));
    }
    if (!fn) return "";
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (FAILED(fn(nullptr, &ti))) return "";
    char buf[96];
    std::snprintf(buf, sizeof(buf), " dwm_refresh_hz=%.2f dwm_compose_hz=%.2f",
                  ti.rateRefresh.uiDenominator ? double(ti.rateRefresh.uiNumerator) / ti.rateRefresh.uiDenominator : 0.0,
                  ti.rateCompose.uiDenominator ? double(ti.rateCompose.uiNumerator) / ti.rateCompose.uiDenominator : 0.0);
    return buf;
}

void tickCapture() {
    if (!g_cap.active) return;
    const double now = nowMs();

    if (g_cap.finishing) {
        const bool done = !g_cap.pm || WaitForSingleObject(g_cap.pm, 0) == WAIT_OBJECT_0;
        if (done || now >= g_cap.finishDeadlineMs) {
            if (g_cap.pm) {
                if (!done) TerminateProcess(g_cap.pm, 0);
                CloseHandle(g_cap.pm);
                g_cap.pm = nullptr;
            }
            g_cap.events.close();
            g_cap.summary.close();
            g_cap.active = false;
            g_cap.statusText = "capture finished";
            g_host->log(L"EDPE debug: capture finished (events.csv, summary.txt and presentmon.csv in edpe-capture-<stamp>)");
        }
        return;
    }

    ++g_cap.frames;
    if (g_cap.lastTickMs > 0) g_cap.dt.push_back(now - g_cap.lastTickMs);
    g_cap.lastTickMs = now;

    if (now - g_cap.markerMs >= 1000.0) {
        g_cap.markerMs = now;
        capEvent("marker", "frames=" + std::to_string(g_cap.frames) + capDwmText());
    }

    char st[160];
    const CapPhase& cur = g_cap.phases[g_cap.current];
    std::snprintf(st, sizeof(st), "capture: phase %zu/%zu fg=%s (%.0f s left)", g_cap.current + 1, g_cap.phases.size(),
                  cur.name.c_str(), std::max(0.0, cur.seconds - (now - g_cap.phaseStartMs) / 1000.0));
    g_cap.statusText = st;

    if (now - g_cap.phaseStartMs >= cur.seconds * 1000.0) {
        capWriteSummaryLine(cur);
        ++g_cap.current;
        if (g_cap.current >= g_cap.phases.size()) {
            capFinish();
        } else {
            capApplyPhase();
        }
    }
}
