#pragma once

// Developer mode only: CPU time EDPE spends on the game's render thread, to find where EDPE costs frame time in
// CPU-bound scenes.
//
// - Present path: each EDPE step inside the Present hook, timed every frame with QueryPerformanceCounter.
// - Context hooks: inclusive time of each hooked D3D11 context method (EDPE's own work plus the forwarded driver
//   call), timed on one frame in kSampleInterval and reported as an average per sampled frame. The driver part is
//   the same whatever EDPE does, so comparing two modes (for example Bypass and FSR 4) in the same scene isolates
//   EDPE's own cost. Only the outermost hook on a thread is recorded, and nothing is recorded while EDPE's own
//   Present work runs, so nested calls are not counted twice.
//
// Enabled by setEnabled(true) (DEV=1); otherwise each scope costs one relaxed atomic load.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <cstdint>

namespace edpe::cputiming {

enum class Hook : uint32_t {
    Draw,  // Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced
    Map,
    Unmap,
    UpdateSubresource,
    VSSetConstantBuffers,
    PSSetSamplers,
    OMSetRenderTargets,  // both OMSetRenderTargets variants
    CommandList,         // ExecuteCommandList, FinishCommandList
    Count
};

enum class PresentStep : uint32_t {
    Census,        // ContextCensusOnPresent
    Ui,            // UiOnPresent
    AfterOverlay,  // ContextCensusAfterOverlay
    FrameGen,      // FgRuntime::onPresent
    Original,      // the game's Present (driver), for reference; not EDPE work
    AfterPresent,  // ContextCensusAfterPresent
    Limiter,       // FrameLimiter::waitForNextFrame
    Count
};

inline constexpr uint32_t kSampleInterval = 8;

struct State {
    std::atomic<bool> enabled{false};
    std::atomic<bool> sampleFrame{false};
    std::atomic<int64_t> hookTicks[static_cast<uint32_t>(Hook::Count)]{};
    std::atomic<uint32_t> hookCalls[static_cast<uint32_t>(Hook::Count)]{};
    std::atomic<int64_t> hookForwardTicks[static_cast<uint32_t>(Hook::Count)]{};  // forwarded driver calls
    std::atomic<int64_t> hookUpscaleTicks[static_cast<uint32_t>(Hook::Count)]{};  // EDPE upscale run from the hook
    double qpcCallNs = 0.0;  // cost of one QueryPerformanceCounter call, measured when enabled
    int64_t presentTicks[static_cast<uint32_t>(PresentStep::Count)]{};  // render thread only
    uint32_t frames = 0;
    uint32_t sampledFrames = 0;
    double qpcToMs = 0.0;
};

inline State& state() {
    static State s;
    return s;
}

inline thread_local int t_depth = 0;  // >0 inside a recorded hook or EDPE's Present work
inline thread_local bool t_inHook = false;     // inside a recorded (outermost) hook
inline thread_local bool t_inUpscale = false;  // inside an UpscaleScope
inline thread_local int64_t t_forwardTicks = 0;
inline thread_local int64_t t_upscaleTicks = 0;

inline int64_t now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline void setEnabled(bool on) {
    State& s = state();
    if (s.qpcToMs == 0.0) {
        LARGE_INTEGER f{};
        if (QueryPerformanceFrequency(&f) && f.QuadPart > 0) s.qpcToMs = 1000.0 / static_cast<double>(f.QuadPart);
    }
    if (on && s.qpcCallNs == 0.0 && s.qpcToMs > 0.0) {
        constexpr int kCalls = 4096;
        const int64_t t0 = now();
        volatile int64_t sink = 0;
        for (int i = 0; i < kCalls; ++i) sink = sink + now();
        const int64_t t1 = now();
        s.qpcCallNs = static_cast<double>(t1 - t0) * s.qpcToMs * 1.0e6 / kCalls;
    }
    s.enabled.store(on, std::memory_order_relaxed);
}

class HookScope {
public:
    explicit HookScope(Hook hook) {
        State& s = state();
        if (!s.sampleFrame.load(std::memory_order_relaxed) || t_depth != 0) return;
        hook_ = hook;
        active_ = true;
        ++t_depth;
        t_inHook = true;
        t_forwardTicks = 0;
        t_upscaleTicks = 0;
        start_ = now();
    }
    ~HookScope() {
        if (!active_) return;
        const int64_t elapsed = now() - start_;
        --t_depth;
        t_inHook = false;
        State& s = state();
        const auto i = static_cast<uint32_t>(hook_);
        s.hookTicks[i].fetch_add(elapsed, std::memory_order_relaxed);
        s.hookCalls[i].fetch_add(1, std::memory_order_relaxed);
        s.hookForwardTicks[i].fetch_add(t_forwardTicks, std::memory_order_relaxed);
        s.hookUpscaleTicks[i].fetch_add(t_upscaleTicks, std::memory_order_relaxed);
    }
    HookScope(const HookScope&) = delete;
    HookScope& operator=(const HookScope&) = delete;

private:
    Hook hook_ = Hook::Draw;
    int64_t start_ = 0;
    bool active_ = false;
};

// Wraps a forwarded driver call inside a hook, so the hook's own (EDPE) time can be told apart from the driver's.
// Ignored outside a recorded hook and inside an UpscaleScope (that time is reported as upscale).
class ForwardScope {
public:
    ForwardScope() {
        if (!t_inHook || t_inUpscale) return;
        active_ = true;
        start_ = now();
    }
    ~ForwardScope() {
        if (active_) t_forwardTicks += now() - start_;
    }
    ForwardScope(const ForwardScope&) = delete;
    ForwardScope& operator=(const ForwardScope&) = delete;

private:
    int64_t start_ = 0;
    bool active_ = false;
};

// Wraps EDPE's upscale when a hook runs it, so it is reported apart from the per-call hook cost.
class UpscaleScope {
public:
    UpscaleScope() {
        if (!t_inHook || t_inUpscale) return;
        active_ = true;
        t_inUpscale = true;
        start_ = now();
    }
    ~UpscaleScope() {
        if (!active_) return;
        t_upscaleTicks += now() - start_;
        t_inUpscale = false;
    }
    UpscaleScope(const UpscaleScope&) = delete;
    UpscaleScope& operator=(const UpscaleScope&) = delete;

private:
    int64_t start_ = 0;
    bool active_ = false;
};

// Times the Present hook step by step. Call mark(step) after each step; the time since the previous mark goes to
// that step. While active, context hooks on this thread are not recorded (EDPE's own D3D11 calls).
class PresentTimer {
public:
    PresentTimer() {
        if (!state().enabled.load(std::memory_order_relaxed)) return;
        active_ = true;
        ++t_depth;
        last_ = now();
    }
    void mark(PresentStep step) {
        if (!active_) return;
        const int64_t t = now();
        state().presentTicks[static_cast<uint32_t>(step)] += t - last_;
        last_ = t;
    }
    ~PresentTimer() {
        if (active_) --t_depth;
    }
    bool active() const { return active_; }
    PresentTimer(const PresentTimer&) = delete;
    PresentTimer& operator=(const PresentTimer&) = delete;

private:
    int64_t last_ = 0;
    bool active_ = false;
};

// Call once per presented frame, after the Present hook's work. Advances the sampling window and, every
// kLogFrames frames, fills `out` with the log line and returns true.
inline constexpr uint32_t kLogFrames = 600;

inline bool endFrame(wchar_t* out, size_t outCount) {
    State& s = state();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        s.sampleFrame.store(false, std::memory_order_relaxed);
        return false;
    }
    if (s.sampleFrame.load(std::memory_order_relaxed)) ++s.sampledFrames;
    ++s.frames;
    // The next frame (until the next Present) is sampled once every kSampleInterval frames.
    s.sampleFrame.store(s.frames % kSampleInterval == 0, std::memory_order_relaxed);
    if (s.frames < kLogFrames) return false;

    const double perFrame = s.qpcToMs / static_cast<double>(s.frames);
    const double perSampled = s.sampledFrames ? s.qpcToMs / static_cast<double>(s.sampledFrames) : 0.0;
    const double sf = s.sampledFrames ? static_cast<double>(s.sampledFrames) : 1.0;
    auto p = [&](PresentStep step) { return static_cast<double>(s.presentTicks[static_cast<uint32_t>(step)]) * perFrame; };
    double hookMs[static_cast<uint32_t>(Hook::Count)]{};
    double hookCalls[static_cast<uint32_t>(Hook::Count)]{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(Hook::Count); ++i) {
        hookMs[i] = static_cast<double>(s.hookTicks[i].exchange(0, std::memory_order_relaxed)) * perSampled;
        hookCalls[i] = static_cast<double>(s.hookCalls[i].exchange(0, std::memory_order_relaxed)) / sf;
    }
    const double edpePresent = p(PresentStep::Census) + p(PresentStep::Ui) + p(PresentStep::AfterOverlay) +
                               p(PresentStep::FrameGen) + p(PresentStep::AfterPresent) + p(PresentStep::Limiter);
    double hookTotal = 0.0;
    for (double v : hookMs) hookTotal += v;
    using H = Hook;
    auto hi = [](H h) { return static_cast<uint32_t>(h); };
    std::swprintf(out, outCount,
        L"EDPE CPU timing (%u frames, %u sampled): Present EDPE %.3f ms [census %.3f ui %.3f afterOverlay %.3f "
        L"fg %.3f afterPresent %.3f limiter %.3f] original Present %.3f ms | hooks incl. driver %.3f ms/frame "
        L"[draw %.3f (%.0f) map %.3f (%.0f) unmap %.3f (%.0f) updateSub %.3f (%.0f) vsCB %.3f (%.0f) "
        L"psSamplers %.3f (%.0f) omSetRT %.3f (%.0f) cmdList %.3f (%.0f)]",
        s.frames, s.sampledFrames, edpePresent, p(PresentStep::Census), p(PresentStep::Ui),
        p(PresentStep::AfterOverlay), p(PresentStep::FrameGen), p(PresentStep::AfterPresent),
        p(PresentStep::Limiter), p(PresentStep::Original), hookTotal,
        hookMs[hi(H::Draw)], hookCalls[hi(H::Draw)], hookMs[hi(H::Map)], hookCalls[hi(H::Map)],
        hookMs[hi(H::Unmap)], hookCalls[hi(H::Unmap)], hookMs[hi(H::UpdateSubresource)],
        hookCalls[hi(H::UpdateSubresource)], hookMs[hi(H::VSSetConstantBuffers)],
        hookCalls[hi(H::VSSetConstantBuffers)], hookMs[hi(H::PSSetSamplers)], hookCalls[hi(H::PSSetSamplers)],
        hookMs[hi(H::OMSetRenderTargets)], hookCalls[hi(H::OMSetRenderTargets)], hookMs[hi(H::CommandList)],
        hookCalls[hi(H::CommandList)]);
    // Second line: per hook, inclusive = driver (forwarded calls) + upscale (run from the hook) + EDPE's own work.
    // EDPE's own work still contains about two timer calls per call (qpc, ns each).
    if (outCount > 0) {
        wchar_t* tail = out + wcslen(out);
        size_t left = outCount - (tail - out);
        auto split = [&](H h, const wchar_t* name) {
            const uint32_t i = hi(h);
            const double fwd = static_cast<double>(s.hookForwardTicks[i].exchange(0, std::memory_order_relaxed)) * perSampled;
            const double ups = static_cast<double>(s.hookUpscaleTicks[i].exchange(0, std::memory_order_relaxed)) * perSampled;
            const int n = std::swprintf(tail, left, L" | %ls driver %.3f upscale %.3f edpe %.3f", name, fwd, ups,
                                        hookMs[i] - fwd - ups);
            if (n > 0 && static_cast<size_t>(n) < left) { tail += n; left -= n; }
        };
        split(H::Draw, L"draw");
        split(H::Map, L"map");
        split(H::Unmap, L"unmap");
        std::swprintf(tail, left, L" | qpc %.1f ns", s.qpcCallNs);
        for (uint32_t i = 0; i < static_cast<uint32_t>(Hook::Count); ++i) {
            s.hookForwardTicks[i].store(0, std::memory_order_relaxed);
            s.hookUpscaleTicks[i].store(0, std::memory_order_relaxed);
        }
    }
    for (auto& t : s.presentTicks) t = 0;
    s.frames = 0;
    s.sampledFrames = 0;
    return true;
}

}  // namespace edpe::cputiming
