// EDPE debug module ABI (version 1). Shared by the host hook (copied into the EDPE tree under test as
// src/edpe_debug_api.h) and the module (edpe_debug.dll). Plain C types only, so the two sides need no common
// runtime. The host passes the table; the module returns its own table. Both carry a version and a size.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EDPE_DEBUG_API_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

// Option ids. FSR31 and FSR4 go through selectProvider; the others through selectBackend (their value is the EDPE
// UpscalerBackend enum: 0 None/bypass, 1 NVIDIA DLSS, 3 Taa; the AMD FSR backend is 2).
#define EDPE_DEBUG_PROVIDER_BYPASS 0
#define EDPE_DEBUG_PROVIDER_DLSS 1
#define EDPE_DEBUG_PROVIDER_TAA 3
#define EDPE_DEBUG_PROVIDER_FSR31 31
#define EDPE_DEBUG_PROVIDER_FSR4 4

struct EdpeDebugHost {
    uint32_t version;
    uint32_t size;

    // The host's Dear ImGui context and allocators. The module compiles its own copy of ImGui from the same tree
    // and must call ImGui::SetAllocatorFunctions and ImGui::SetCurrentContext with these before drawing.
    void* imguiContext;
    void* (*imguiAlloc)(size_t size, void* user);
    void (*imguiFree)(void* ptr, void* user);
    void* imguiAllocUser;
    // The host ImGui context that is current right now. EDPE rebuilds its overlay (and context) on some swapchain events, so
    // the module must ask every frame and not keep the pointer it was given at load.
    void* (*currentImguiContext)(void);

    // Provider control. All run on the present thread, inside the host's frame hook.
    int (*selectProvider)(int provider);  // EDPE_DEBUG_PROVIDER_*; selects the AMD FSR backend. 1 = applied.
    int (*selectBackend)(int backend);    // 0 bypass, 1 DLSS, 2 AMD FSR (keeps its provider), 3 TAA. 1 = applied.
    int (*selectMode)(int mode);          // 1 Native AA ... 5 Ultra Performance. The scene resolution is fixed at game start.
    void (*getState)(int* backend, int* mode, int* fsr4Wanted);
    void (*setState)(int backend, int mode, int fsr4Wanted);  // restores what getState returned

    // Read-only state.
    int (*isFsr4Available)(void);
    int (*isBackendAvailable)(int backend);
    const wchar_t* (*backendUnavailableReason)(int backend);
    void (*getRenderSize)(int* renderW, int* renderH, int* outputW, int* outputH);  // last evaluated frame, 0 if none
    int (*isFsr4Running)(void);
    int (*setFrameGenBackend)(int backend);          // 0 off, 1 AMD FSR frame generation (what the menu combo does, not saved to the ini)
    void (*getFrameGenState)(int* configured, int* active, int* pausedForMenu, float* renderedFps, float* presentedFps);
    int (*menuVisible)(void);
    int (*isFrameGenActive)(void);                   // frame generation is running (the self-test must not switch backends then)
    int (*hasEvaluated)(void);                       // the upscaler has produced at least one frame
    void (*describeProvider)(char* buffer, uint32_t size);  // e.g. "FSR 4.1.2", "FSR 3.1.2 (FSR 4 unavailable)"
    const wchar_t* (*fsr4UnavailableReason)(void);
    float (*gpuTimeMs)(void);                        // GPU time of the active backend, last measured

    void (*log)(const wchar_t* message);             // writes to edpe.log
};

struct EdpeDebugModule {
    uint32_t version;
    uint32_t size;
    // Called once per presented frame. menuVisible is nonzero when the EDPE menu is open.
    void (*frame)(int menuVisible);
    void (*shutdown)(void);
    // Called once per presented frame whether or not any EDPE UI is showing. Drives the self-test and captures.
    void (*tick)(void);
};

// Exported by edpe_debug.dll. Returns nonzero on success.
typedef int (*EdpeDebugInitFn)(const struct EdpeDebugHost* host, struct EdpeDebugModule* out);
#define EDPE_DEBUG_INIT_NAME "EdpeDebugInit"

#ifdef __cplusplus
}
#endif
