// CPU-only unit tests for src/fsr4_logic.h: the FSR 4 failure gate, the provider selection state (select, switch,
// retry, label text) and the mapping from EDPE's TemporalFrameInputs to FSR 4 context flags and dispatch values.
// No GPU, no FidelityFX SDK and no game needed.
#include "fsr4_logic.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edpe;
using namespace edpe::fsr4;

static int g_failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                        \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

static bool approx(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

static void testFailureGate() {
    FailureGate g;
    for (int i = 1; i < kMaxConsecutiveFailures; ++i) CHECK(!g.onFailure());  // failures 1..119 keep FSR 4
    CHECK(g.onFailure());                                                      // the 120th disables it
    CHECK(kMaxConsecutiveFailures == 120);

    // A success in the middle restarts the count.
    FailureGate h;
    for (int i = 0; i < 100; ++i) h.onFailure();
    h.onSuccess();
    CHECK(h.consecutive == 0);
    for (int i = 1; i < kMaxConsecutiveFailures; ++i) CHECK(!h.onFailure());
    CHECK(h.onFailure());

    h.reset();
    CHECK(h.consecutive == 0);
}

static void testProviderState() {
    ProviderState s;
    CHECK(!s.running());
    CHECK(!s.needsInit());
    CHECK(s.label() == ProviderLabel::Fsr31);

    // Selecting FSR 4: needs a (lazy) init; a failed init falls back and is not retried by itself.
    CHECK(s.setWanted(true));
    CHECK(!s.setWanted(true));  // idempotent
    CHECK(s.needsInit());
    CHECK(s.label() == ProviderLabel::Fsr31FsrFourUnavailable);
    s.onInitResult(false);
    CHECK(!s.needsInit());
    CHECK(!s.running());
    CHECK(s.label() == ProviderLabel::Fsr31FsrFourUnavailable);

    // Explicit retry re-arms the init; a successful init runs FSR 4.
    CHECK(s.retry());
    CHECK(s.needsInit());
    s.onInitResult(true);
    CHECK(s.running());
    CHECK(s.label() == ProviderLabel::Fsr4);
    CHECK(!s.retry());  // nothing to retry while running

    // Back to FSR 3.1.2 and again to FSR 4: keeps the engine initialized (no new init needed).
    CHECK(s.setWanted(false));
    CHECK(!s.running());
    CHECK(s.label() == ProviderLabel::Fsr31);
    CHECK(!s.retry());  // not wanted
    CHECK(s.setWanted(true));
    CHECK(s.running());          // still active from before
    CHECK(!s.needsInit());

    // Backend re-initialization forgets the previous engine.
    s.onBackendInitialize();
    CHECK(!s.active && !s.initTried);
    CHECK(s.wanted);             // the selection itself survives
    CHECK(s.needsInit());

    s.onShutdown();
    CHECK(!s.active && !s.initTried);
}

static void testProviderFailurePath() {
    ProviderState s;
    s.setWanted(true);
    s.onInitResult(true);
    CHECK(s.running());

    // The first failure asks for one log line; later failures do not.
    auto first = s.onEvaluateFailure();
    CHECK(first.logFirstFailure && !first.disabled);
    for (int i = 2; i < kMaxConsecutiveFailures; ++i) {
        auto o = s.onEvaluateFailure();
        CHECK(!o.logFirstFailure && !o.disabled);
        CHECK(s.running());
    }
    auto last = s.onEvaluateFailure();  // 120th consecutive failure
    CHECK(last.disabled);
    CHECK(!s.running());
    CHECK(!s.needsInit());  // not retried on its own
    CHECK(s.label() == ProviderLabel::Fsr31FsrFourUnavailable);

    // Selecting FSR 3.1.2 and then FSR 4 again, or an explicit retry, starts over.
    CHECK(s.retry());
    CHECK(s.needsInit());
    s.onInitResult(true);
    CHECK(s.running());
    auto again = s.onEvaluateFailure();
    CHECK(again.logFirstFailure);  // the log-once flag was reset by the retry

    // A success clears the failure streak, so scattered failures never disable FSR 4.
    ProviderState t;
    t.setWanted(true);
    t.onInitResult(true);
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 100; ++i) CHECK(!t.onEvaluateFailure().disabled);
        t.onEvaluateSuccess();
    }
    CHECK(t.running());
}

static void testProviderLabelText() {
    char buf[64];
    formatProviderLabel(ProviderLabel::Fsr4, "4.1.2", buf, sizeof(buf));
    CHECK(std::strcmp(buf, "FSR 4.1.2") == 0);
    formatProviderLabel(ProviderLabel::Fsr4, nullptr, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "FSR 4") == 0);
    formatProviderLabel(ProviderLabel::Fsr31, "4.1.2", buf, sizeof(buf));
    CHECK(std::strcmp(buf, "FSR 3.1.2") == 0);
    formatProviderLabel(ProviderLabel::Fsr31FsrFourUnavailable, "4.1.2", buf, sizeof(buf));
    CHECK(std::strcmp(buf, "FSR 3.1.2 (FSR 4 unavailable)") == 0);

    char tiny[8];  // truncation must stay NUL terminated
    formatProviderLabel(ProviderLabel::Fsr31FsrFourUnavailable, nullptr, tiny, sizeof(tiny));
    CHECK(tiny[sizeof(tiny) - 1] == '\0');
    formatProviderLabel(ProviderLabel::Fsr31, nullptr, nullptr, 0);  // must not crash
}

static void testContextFlags() {
    TemporalFrameInputs in{};
    in.colorIsHdr = false;
    in.depthInverted = true;
    in.depthInfinite = true;
    ContextFlagsPlan p = buildContextFlagsPlan(in);
    CHECK(p.autoExposure);
    CHECK(p.depthInverted && p.depthInfinite);
    CHECK(!p.hdr && p.nonLinearColorspace);  // tonemapped 8-bit input is declared non-linear

    in.colorIsHdr = true;
    p = buildContextFlagsPlan(in);
    CHECK(p.hdr && !p.nonLinearColorspace);  // linear pre-tonemap input uses the HDR flag
    CHECK(p.autoExposure);

    in.depthInverted = false;
    in.depthInfinite = false;
    p = buildContextFlagsPlan(in);
    CHECK(!p.depthInverted && !p.depthInfinite);
}

static void testDispatchPlan() {
    TemporalFrameInputs in{};
    in.jitterX = 0.25f;
    in.jitterY = -0.375f;
    in.motionScaleX = 1.0f;
    in.motionScaleY = 1.0f;
    in.depthInverted = true;
    in.cameraNow.depthB = 0.025f;
    in.cameraNow.scaleY = 1.7320508f;  // cot(fov/2) = sqrt(3): a vertical field of view of 60 degrees
    in.frameTimeMs = 11.1f;
    in.colorIsHdr = false;

    DispatchPlan p = buildDispatchPlan(in, false);
    // Jitter is passed through unchanged: EDPE's projection jitter already uses the SDK recipe (2*jx/w, -2*jy/h).
    CHECK(approx(p.jitterX, 0.25f) && approx(p.jitterY, -0.375f));
    CHECK(approx(p.motionScaleX, 1.0f) && approx(p.motionScaleY, 1.0f));
    CHECK(approx(p.frameTimeMs, 11.1f));
    CHECK(approx(p.preExposure, 1.0f));
    CHECK(p.nonLinearSrgb);                                   // LDR input
    CHECK(!p.reset);
    CHECK(p.cameraNear == FLT_MAX && approx(p.cameraFar, 0.025f));  // reversed infinite depth
    CHECK(approx(p.fovVertical, 2.0f * std::atan(1.0f / 1.7320508f)));
    CHECK(approx(p.fovVertical, 1.0471976f, 1e-4f));            // 60 degrees in radians

    // Reset comes from either the frame or a pending reset (new context or recreated shared textures).
    in.reset = false;
    CHECK(buildDispatchPlan(in, true).reset);
    in.reset = true;
    CHECK(buildDispatchPlan(in, false).reset);

    // Missing or zero values take the documented defaults.
    TemporalFrameInputs d{};
    d.depthInverted = true;
    DispatchPlan dp = buildDispatchPlan(d, false);
    CHECK(approx(dp.frameTimeMs, 16.6f));        // no frame time: 16.6 ms
    CHECK(approx(dp.cameraFar, 0.025f));         // no depthB: the measured Elite value
    CHECK(approx(dp.fovVertical, 1.0f));         // no projection scale: 1 rad
    CHECK(!dp.enableSharpening && approx(dp.sharpness, 0.0f));

    // Non-inverted depth swaps near and far.
    d.depthInverted = false;
    d.cameraNow.depthB = 0.1f;
    dp = buildDispatchPlan(d, false);
    CHECK(approx(dp.cameraNear, 0.1f) && dp.cameraFar == FLT_MAX);

    // Sharpening: enabled above zero, clamped to 1.
    d.sharpness = 0.5f;
    dp = buildDispatchPlan(d, false);
    CHECK(dp.enableSharpening && approx(dp.sharpness, 0.5f));
    d.sharpness = 1.5f;
    CHECK(approx(buildDispatchPlan(d, false).sharpness, 1.0f));
    d.sharpness = -0.2f;
    dp = buildDispatchPlan(d, false);
    CHECK(!dp.enableSharpening && approx(dp.sharpness, 0.0f));

    // HDR input drops the non-linear sRGB dispatch flag.
    d.colorIsHdr = true;
    CHECK(!buildDispatchPlan(d, false).nonLinearSrgb);
}

int main() {
    testFailureGate();
    testProviderState();
    testProviderFailurePath();
    testProviderLabelText();
    testContextFlags();
    testDispatchPlan();
    if (g_failures) {
        std::printf("fsr4_logic_test: %d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("fsr4_logic_test PASSED\n");
    return 0;
}
