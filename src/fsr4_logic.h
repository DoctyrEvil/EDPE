#pragma once

// Pure, header-only logic for the FSR 4 path (no D3D, no FidelityFX SDK, no globals), so it can be unit tested on the
// CPU without a GPU or the game (tests/fsr4_logic_test.cpp). The engine and the AMD backend call these helpers; the
// per-frame helpers are small and inline, and compute exactly what the engine computed inline before.

#include "temporal_inputs.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace edpe {
namespace fsr4 {

// ---------------------------------------------------------------------------------------------------------------
// Failure gate and provider selection state (AmdUpscalerBackend)
// ---------------------------------------------------------------------------------------------------------------

// FSR 4 is disabled (until it is selected again) after this many evaluations in a row fail.
constexpr int kMaxConsecutiveFailures = 120;

struct FailureGate {
    int consecutive = 0;
    void reset() { consecutive = 0; }
    void onSuccess() { consecutive = 0; }
    // Returns true when this failure reaches the threshold and FSR 4 should be disabled.
    bool onFailure() { return ++consecutive >= kMaxConsecutiveFailures; }
};

enum class ProviderLabel { Fsr31, Fsr31FsrFourUnavailable, Fsr4 };

struct ProviderState {
    bool wanted = false;      // selected: ini Fsr4=1 at startup, or the menu
    bool active = false;      // the FSR 4 engine initialized and has not been disabled by failures
    bool initTried = false;   // initialization attempted since the last selection or retry
    bool failLogged = false;  // the first fallback has been logged
    FailureGate gate;

    bool running() const { return wanted && active; }
    // True when the next evaluation should (lazily) try to initialize the engine.
    bool needsInit() const { return wanted && !active && !initTried; }

    // Changes the selection. Returns false (and changes nothing) when the value is unchanged. A fresh selection of
    // FSR 4 retries a failed initialization.
    bool setWanted(bool value) {
        if (value == wanted) return false;
        wanted = value;
        failLogged = false;
        gate.reset();
        if (value) initTried = false;
        return true;
    }

    // Explicit user retry after FSR 4 failed to initialize or was disabled by failures.
    bool retry() {
        if (!wanted || active) return false;
        initTried = false;
        failLogged = false;
        gate.reset();
        return true;
    }

    void onInitResult(bool ok) {
        initTried = true;
        active = ok;
    }

    // Backend (re)initialization: forget everything about a previous engine.
    void onBackendInitialize() {
        active = false;
        initTried = false;
        failLogged = false;
        gate.reset();
    }

    void onShutdown() {
        active = false;
        initTried = false;
    }

    void onEvaluateSuccess() { gate.onSuccess(); }

    struct FailureOutcome {
        bool logFirstFailure = false;  // log the "falling back" line (once per selection)
        bool disabled = false;         // the threshold was reached; FSR 4 is now off until reselected
    };
    FailureOutcome onEvaluateFailure() {
        FailureOutcome out;
        if (!failLogged) {
            failLogged = true;
            out.logFirstFailure = true;
        }
        if (gate.onFailure()) {
            active = false;
            initTried = true;  // do not retry until FSR 4 is selected again
            out.disabled = true;
        }
        return out;
    }

    ProviderLabel label() const {
        return running() ? ProviderLabel::Fsr4
                         : (wanted ? ProviderLabel::Fsr31FsrFourUnavailable : ProviderLabel::Fsr31);
    }
};

// Text shown in the menu and the performance HUD. `fsr4Version` is the provider version, for example "4.1.2".
inline void formatProviderLabel(ProviderLabel label, const char* fsr4Version, char* buffer, size_t size) {
    if (!buffer || size == 0) return;
    switch (label) {
    case ProviderLabel::Fsr4:
        std::snprintf(buffer, size, "FSR %s", (fsr4Version && fsr4Version[0]) ? fsr4Version : "4");
        break;
    case ProviderLabel::Fsr31FsrFourUnavailable:
        std::snprintf(buffer, size, "FSR 3.1.2 (FSR 4 unavailable)");
        break;
    default:
        std::snprintf(buffer, size, "FSR 3.1.2");
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Input mapping (Fsr4Engine): EDPE TemporalFrameInputs to FSR 4 context flags and dispatch values
// ---------------------------------------------------------------------------------------------------------------

// Which FSR 4 context creation flags the inputs need. The engine maps these onto the FidelityFX flag bits.
struct ContextFlagsPlan {
    bool autoExposure = true;
    bool depthInverted = false;
    bool depthInfinite = false;
    // FSR 4 wants linear color: HDR (linear, pre-tonemap) input uses the HDR flag; tonemapped LDR input is declared
    // non-linear (sRGB), which the SDK documents as supported.
    bool hdr = false;
    bool nonLinearColorspace = false;
};

inline ContextFlagsPlan buildContextFlagsPlan(const TemporalFrameInputs& in) {
    ContextFlagsPlan p;
    p.autoExposure = true;
    p.depthInverted = in.depthInverted;
    p.depthInfinite = in.depthInfinite;
    p.hdr = in.colorIsHdr;
    p.nonLinearColorspace = !in.colorIsHdr;
    return p;
}

struct DispatchPlan {
    float jitterX = 0.0f, jitterY = 0.0f;               // render pixels, unchanged from EDPE (SDK recipe already matches)
    float motionScaleX = 1.0f, motionScaleY = 1.0f;
    bool enableSharpening = false;
    float sharpness = 0.0f;
    float frameTimeMs = 16.6f;
    float preExposure = 1.0f;
    bool reset = false;
    float cameraNear = 0.0f, cameraFar = 0.0f;
    float fovVertical = 1.0f;
    float viewSpaceToMeters = 1.0f;
    bool nonLinearSrgb = false;                         // dispatch flag NON_LINEAR_COLOR_SRGB (LDR input)
};

inline DispatchPlan buildDispatchPlan(const TemporalFrameInputs& in, bool resetPending) {
    DispatchPlan p;
    p.jitterX = in.jitterX;
    p.jitterY = in.jitterY;
    p.motionScaleX = in.motionScaleX;
    p.motionScaleY = in.motionScaleY;
    p.enableSharpening = in.sharpness > 0.0f;
    p.sharpness = std::clamp(in.sharpness, 0.0f, 1.0f);
    p.frameTimeMs = in.frameTimeMs > 0.0f ? in.frameTimeMs : 16.6f;
    p.preExposure = 1.0f;
    p.reset = in.reset || resetPending;
    // EDPE passes no near/far planes, only depthB (measured 0.025): reversed infinite depth has an infinite near.
    const float nearPlane = (in.cameraNow.depthB > 0.0f) ? in.cameraNow.depthB : 0.025f;
    p.cameraNear = in.depthInverted ? FLT_MAX : nearPlane;
    p.cameraFar = in.depthInverted ? nearPlane : FLT_MAX;
    p.fovVertical = (in.cameraNow.scaleY > 0.0f) ? (2.0f * std::atan(1.0f / in.cameraNow.scaleY)) : 1.0f;
    p.viewSpaceToMeters = 1.0f;
    p.nonLinearSrgb = !in.colorIsHdr;
    return p;
}

} // namespace fsr4
} // namespace edpe
