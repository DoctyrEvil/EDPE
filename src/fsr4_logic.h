#pragma once

// Pure, header-only logic for the FSR 4 path (no D3D, no FidelityFX SDK, no globals), so it can be unit tested on the
// CPU without a GPU or the game (tests/fsr4_logic_test.cpp). The engine and the AMD backend call these helpers; the
// per-frame helpers are small and inline, and compute exactly what the engine computed inline before.

#include "temporal_inputs.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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
    bool probed = false;      // initialization has run at least once since the backend started
    bool capable = false;     // the last initialization succeeded: this GPU and runtime can run FSR 4
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
        probed = true;
        capable = ok;
        active = ok;
    }

    // Backend (re)initialization: forget everything about a previous engine.
    void onBackendInitialize() {
        active = false;
        initTried = false;
        failLogged = false;
        probed = false;
        capable = false;
        gate.reset();
    }

    void onShutdown() {
        active = false;
        initTried = false;
        probed = false;
        capable = false;
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
// Provider versions: FSR 4 runs only on a 4.x provider (Fsr4Engine)
// ---------------------------------------------------------------------------------------------------------------

// AMD's runtime offers a list of upscaler providers for a device (for example "4.1.2", "3.1.6", "2.3.4"). FSR 4 must
// never run a 3.x or 2.x provider under its name, so the engine picks the highest 4.x one and refuses otherwise.

struct Version {
    unsigned major = 0, minor = 0, patch = 0;
};

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads the first "digits.digits[.digits]" run in `name` ("4.1.2", "FSR 4.1", "AMD FSR v4.1.1 (ML)"), whatever precedes
// it. A missing patch reads as 0. Returns false, leaving `out` untouched, for names with no such run (null, "", "4",
// "unknown", numbers longer than six digits).
inline bool parseVersion(const char* name, Version* out = nullptr) {
    if (!name) return false;
    for (const char* p = name; *p; ++p) {
        if (!isDigit(*p)) continue;
        // Skip the middle of a number and the tail of a dotted run that began earlier.
        if (p > name && (isDigit(p[-1]) || p[-1] == '.')) continue;
        unsigned parts[3] = {0, 0, 0};
        int count = 0;
        bool ok = true;
        const char* q = p;
        while (count < 3 && isDigit(*q)) {
            unsigned n = 0;
            int digits = 0;
            while (isDigit(*q)) {
                if (++digits > 6) { ok = false; break; }
                n = n * 10u + static_cast<unsigned>(*q - '0');
                ++q;
            }
            if (!ok) break;
            parts[count++] = n;
            if (*q == '.' && isDigit(q[1])) ++q; else break;
        }
        if (ok && count >= 2) {
            if (out) { out->major = parts[0]; out->minor = parts[1]; out->patch = parts[2]; }
            return true;
        }
    }
    return false;
}

inline int compareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

// Index of the highest offered provider whose major version is at least 4, or -1 when there is none. Names that do not
// parse are ignored; of two equal versions the first wins. `picked` receives the winner when given.
inline int pickFsr4Provider(const std::vector<std::string>& names, Version* picked = nullptr) {
    int best = -1;
    Version bestVersion;
    for (size_t i = 0; i < names.size(); ++i) {
        Version v;
        if (!parseVersion(names[i].c_str(), &v) || v.major < 4) continue;
        if (best < 0 || compareVersions(v, bestVersion) > 0) {
            best = static_cast<int>(i);
            bestVersion = v;
        }
    }
    if (best >= 0 && picked) *picked = bestVersion;
    return best;
}

// "4.1.2, 3.1.6, 2.3.4": each offered name as major.minor.patch in the order given. A name that is not a version is
// shown as it came (at most 24 characters, "?" when empty) so a log never hides what the runtime said.
inline std::string formatProviderList(const std::vector<std::string>& names) {
    std::string text;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) text += ", ";
        Version v;
        if (parseVersion(names[i].c_str(), &v)) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%u.%u.%u", v.major, v.minor, v.patch);
            text += buf;
        } else if (names[i].empty()) {
            text += "?";
        } else {
            text += names[i].substr(0, 24);
        }
    }
    return text;
}

// Why FSR 4 is unavailable when no 4.x provider is offered. Shown in the menu next to the greyed-out entry.
inline std::string providerRefusalText(const std::vector<std::string>& offered) {
    std::string text = offered.empty()
        ? std::string("AMD's runtime offers no FSR version for this GPU")
        : "AMD's runtime offers only FSR " + formatProviderList(offered) + " for this GPU";
    text += "; FSR 4 needs a Radeon RX 7000 or 9000 series GPU (use AMD FSR 3.1.2)";
    return text;
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
