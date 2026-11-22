#include "amd_fsr4_engine.h"

#include "d3d12_bridge.h"
#include "log.h"

#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(EDPE_ENABLE_FFX_FSR4) && EDPE_ENABLE_FFX_FSR4
#include <ffx_api.h>
#include <ffx_api_loader.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_upscale.h>
#endif

namespace edpe {

namespace {

std::wstring iniPathNextToExecutable() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (!len || len >= MAX_PATH) return L"edpe.ini";
    return (std::filesystem::path(modulePath).parent_path() / L"edpe.ini").wstring();
}

// Directory of the module that contains this code (dxgi.dll in the game, the test exe in tests).
std::filesystem::path moduleDirectory() {
    wchar_t path[MAX_PATH]{};
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&moduleDirectory), &self) &&
        self && GetModuleFileNameW(self, path, MAX_PATH) > 0) {
        return std::filesystem::path(path).parent_path();
    }
    return std::filesystem::path();
}

// Integer option from the environment (tests) or [Upscaler] in edpe.ini.
int envOrIni(const wchar_t* envName, const wchar_t* key, int defaultValue) {
    wchar_t buf[32]{};
    if (GetEnvironmentVariableW(envName, buf, 32) > 0 && buf[0] != L'\0') return _wtoi(buf);
    return static_cast<int>(GetPrivateProfileIntW(L"Upscaler", key, defaultValue, iniPathNextToExecutable().c_str()));
}

HMODULE loadSiblingDll(const wchar_t* dllName) {
    if (HMODULE already = GetModuleHandleW(dllName)) return already;
    const auto dir = moduleDirectory();
    if (!dir.empty()) {
        const auto candidate = dir / dllName;
        if (HMODULE loaded = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            return loaded;
        }
    }
    return LoadLibraryW(dllName);
}

// Concrete (non-typeless, non-sRGB) format for shared textures. sRGB maps to UNORM: CopyResource between
// the two is a bit copy within one typeless family, and FSR is told about the encoding through its flags.
DXGI_FORMAT concreteColorFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return f;
    }
}

DXGI_FORMAT concreteMotionFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    default: return f;
    }
}

// Shader resource view format that exposes the depth plane as a float.
DXGI_FORMAT depthSrvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// Converts the scene depth (typeless depth/stencil copy) into an R32_FLOAT texture FSR can read from D3D12.
const char kDepthConvertCs[] = R"(
Texture2D<float> gDepth : register(t0);
RWTexture2D<float> gOut : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    gOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    gOut[id.xy] = gDepth.Load(int3(id.xy, 0));
}
)";

} // namespace

#if defined(EDPE_ENABLE_FFX_FSR4) && EDPE_ENABLE_FFX_FSR4

namespace {

void ffxMessageToLog(uint32_t type, const wchar_t* message) {
    wchar_t buf[512]{};
    std::swprintf(buf, 512, L"EDPE FSR4: [ffx %ls] %ls",
                  type == FFX_API_MESSAGE_TYPE_ERROR ? L"error" : L"warning", message ? message : L"");
    EdpeLog(buf);
}

struct Fsr4Slot {
    SharedBridgeTexture color;
    SharedBridgeTexture depth;
    SharedBridgeTexture motion;
    SharedBridgeTexture output;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> depthUav;
};

} // namespace

struct Fsr4Engine::Impl {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D11D3D12Bridge bridge;
    HMODULE loaderDll = nullptr;
    HMODULE providerDll = nullptr;
    ffxFunctions ffx{};
    ffxContext upscaleCtx = nullptr;
    bool ready = false;
    bool resetPending = true;
    wchar_t error[256] = L"";
    char providerVer[64] = "unavailable";

    // Active configuration; any change recreates the resources and the FFX context.
    uint32_t renderW = 0, renderH = 0, outputW = 0, outputH = 0;
    DXGI_FORMAT colorFmt = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT motionFmt = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT outputFmt = DXGI_FORMAT_UNKNOWN;
    bool hdr = false;
    bool depthInverted = true;
    bool depthInfinite = true;
    std::array<Fsr4Slot, D3D11D3D12Bridge::kAllocatorCount> slots;

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> depthCs;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ownDepthSrv;
    ID3D11Texture2D* ownDepthSrvSource = nullptr;
    unsigned evalCount = 0;

    // ---- optional profiling: [Upscaler] Fsr4Profile=1 (or env EDPE_FSR4_PROFILE=1) ----
    // D3D12 side: GPU timestamps inside the command list. D3D11 side: timestamps around copy-in, the wait on
    // the D3D12 queue and copy-out. Plus CPU time of evaluate and of beginCommandList. One summary line per
    // `profileInterval` frames in edpe.log.
    struct Stat {
        double sum = 0.0, max = 0.0;
        unsigned n = 0;
        void add(double v) { sum += v; if (v > max) max = v; ++n; }
        double avg() const { return n ? sum / n : 0.0; }
        void clear() { sum = 0.0; max = 0.0; n = 0; }
    };
    struct D3D11Prof {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, 4> ts;
        bool inFlight = false;
    };
    static constexpr size_t kProfRing = 8;
    bool profile = false;
    bool flushEachFrame = true;
    unsigned profileInterval = 300;
    std::array<D3D11Prof, kProfRing> d11prof;
    size_t d11ProfWrite = 0;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> qHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> qReadback;
    std::array<bool, D3D11D3D12Bridge::kAllocatorCount> slotHasTs{};
    Stat sD11In, sD11Wait, sD11Out, sD11Total, sD12Total, sD12Fsr, sD12BarIn, sD12BarOut, sCpuEval, sCpuBegin;
    unsigned cpuWaits = 0, profFrames = 0;

    void initProfiling() {
        profile = envOrIni(L"EDPE_FSR4_PROFILE", L"Fsr4Profile", 0) != 0;
        profileInterval = static_cast<unsigned>(std::max(10, envOrIni(L"EDPE_FSR4_PROFILE_INTERVAL", L"Fsr4ProfileInterval", 300)));
        flushEachFrame = envOrIni(L"EDPE_FSR4_FLUSH", L"Fsr4Flush", 1) != 0;
        slotHasTs.fill(false);
        if (!profile) return;
        D3D11_QUERY_DESC dq{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC tq{D3D11_QUERY_TIMESTAMP, 0};
        for (auto& p : d11prof) {
            p = D3D11Prof{};
            device->CreateQuery(&dq, &p.disjoint);
            for (auto& t : p.ts) device->CreateQuery(&tq, &t);
        }
        D3D12_QUERY_HEAP_DESC hd{};
        hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        hd.Count = D3D11D3D12Bridge::kAllocatorCount * 4;
        if (FAILED(bridge.d3d12Device()->CreateQueryHeap(&hd, IID_PPV_ARGS(&qHeap)))) {
            profile = false;
            setError(L"profiling disabled: CreateQueryHeap failed");
            return;
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = static_cast<UINT64>(hd.Count) * sizeof(uint64_t);
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(bridge.d3d12Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                  IID_PPV_ARGS(&qReadback)))) {
            profile = false;
            qHeap.Reset();
            setError(L"profiling disabled: readback buffer creation failed");
            return;
        }
        wchar_t buf[200]{};
        std::swprintf(buf, 200, L"EDPE FSR4: profiling on (summary every %u frames, flush=%d)", profileInterval,
                      flushEachFrame ? 1 : 0);
        EdpeLog(buf);
    }

    void pollD3D11Profile(ID3D11DeviceContext* ctx) {
        for (auto& p : d11prof) {
            if (!p.inFlight) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            UINT64 t[4]{};
            if (ctx->GetData(p.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
            bool ok = true;
            for (int i = 0; i < 4; ++i) {
                if (ctx->GetData(p.ts[i].Get(), &t[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) { ok = false; break; }
            }
            if (!ok) continue;
            p.inFlight = false;
            if (dj.Disjoint || dj.Frequency == 0 || t[1] < t[0] || t[2] < t[1] || t[3] < t[2]) continue;
            const double toMs = 1000.0 / static_cast<double>(dj.Frequency);
            sD11In.add(static_cast<double>(t[1] - t[0]) * toMs);
            sD11Wait.add(static_cast<double>(t[2] - t[1]) * toMs);
            sD11Out.add(static_cast<double>(t[3] - t[2]) * toMs);
            sD11Total.add(static_cast<double>(t[3] - t[0]) * toMs);
        }
    }

    void readD3D12Slot(uint32_t slot) {
        if (!profile || !qReadback || slot >= slotHasTs.size() || !slotHasTs[slot]) return;
        const UINT64 freq = bridge.timestampFrequency();
        if (!freq) return;
        const D3D12_RANGE range{static_cast<SIZE_T>(slot) * 4 * sizeof(uint64_t),
                                static_cast<SIZE_T>(slot + 1) * 4 * sizeof(uint64_t)};
        uint64_t* data = nullptr;
        if (FAILED(qReadback->Map(0, &range, reinterpret_cast<void**>(&data))) || !data) return;
        const uint64_t* q = data + static_cast<size_t>(slot) * 4;
        if (q[1] >= q[0] && q[2] >= q[1] && q[3] >= q[2]) {
            const double toMs = 1000.0 / static_cast<double>(freq);
            sD12BarIn.add(static_cast<double>(q[1] - q[0]) * toMs);
            sD12Fsr.add(static_cast<double>(q[2] - q[1]) * toMs);
            sD12BarOut.add(static_cast<double>(q[3] - q[2]) * toMs);
            sD12Total.add(static_cast<double>(q[3] - q[0]) * toMs);
        }
        const D3D12_RANGE none{0, 0};
        qReadback->Unmap(0, &none);
    }

    void maybeReport() {
        if (!profile || ++profFrames < profileInterval) return;
        profFrames = 0;
        wchar_t buf[760]{};
        std::swprintf(buf, 760,
            L"EDPE FSR4 profile (avg/max ms, n=%u): d11 copyIn+depth %.3f/%.3f | d11 wait(d12+sync) %.3f/%.3f | "
            L"d11 copyOut %.3f/%.3f | d11 total %.3f/%.3f | d12 total %.3f/%.3f (barriersIn %.3f, fsr %.3f, barriersOut %.3f) | "
            L"sync gap %.3f | cpu evaluate %.3f/%.3f | cpu beginCmdList %.3f/%.3f (cpuWaits %u)",
            sD11Total.n, sD11In.avg(), sD11In.max, sD11Wait.avg(), sD11Wait.max, sD11Out.avg(), sD11Out.max,
            sD11Total.avg(), sD11Total.max, sD12Total.avg(), sD12Total.max, sD12BarIn.avg(), sD12Fsr.avg(),
            sD12BarOut.avg(), sD11Wait.avg() - sD12Total.avg(), sCpuEval.avg(), sCpuEval.max, sCpuBegin.avg(),
            sCpuBegin.max, cpuWaits);
        EdpeLog(buf);
        for (Stat* s : {&sD11In, &sD11Wait, &sD11Out, &sD11Total, &sD12Total, &sD12Fsr, &sD12BarIn, &sD12BarOut,
                        &sCpuEval, &sCpuBegin}) {
            s->clear();
        }
        cpuWaits = 0;
    }

    void setError(const wchar_t* msg) {
        // Log only when the message changes so a persistent failure does not flood edpe.log.
        const bool changed = std::wcscmp(error, msg) != 0;
        std::swprintf(error, 256, L"%ls", msg);
        if (!changed) return;
        wchar_t buf[320]{};
        std::swprintf(buf, 320, L"EDPE FSR4: %ls", msg);
        EdpeLog(buf);
    }

    void setErrorF(const wchar_t* what, long code) {
        wchar_t msg[256]{};
        std::swprintf(msg, 256, L"%ls (0x%08lX)", what, code);
        setError(msg);
    }

    bool loadDlls() {
        if (ffx.CreateContext && ffx.Dispatch && ffx.Query && ffx.Configure && ffx.DestroyContext) return true;
        if (!providerDll) providerDll = loadSiblingDll(L"amd_fidelityfx_upscaler_dx12.dll");
        if (!loaderDll) loaderDll = loadSiblingDll(L"amd_fidelityfx_loader_dx12.dll");
        if (!loaderDll) { setError(L"amd_fidelityfx_loader_dx12.dll not found beside dxgi.dll"); return false; }
        if (!providerDll) { setError(L"amd_fidelityfx_upscaler_dx12.dll not found beside dxgi.dll"); return false; }
        ffxLoadFunctions(&ffx, loaderDll);
        if (!ffx.CreateContext || !ffx.DestroyContext || !ffx.Configure || !ffx.Query || !ffx.Dispatch) {
            setError(L"FidelityFX loader is missing ffx_api entry points");
            return false;
        }
        ffxConfigureDescGlobalDebug dbg{};
        dbg.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
        dbg.effectId = FFX_API_EFFECT_ID_UPSCALE;
        dbg.fpMessage = ffxMessageToLog;
        dbg.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
        ffx.Configure(nullptr, &dbg.header);
        return true;
    }

    bool ensureDepthShader() {
        if (depthCs) return true;
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3DCompile(kDepthConvertCs, sizeof(kDepthConvertCs) - 1, "edpe_fsr4_depth", nullptr, nullptr,
                                "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
        if (FAILED(hr)) { setErrorF(L"depth convert shader compile failed", hr); return false; }
        hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &depthCs);
        if (FAILED(hr)) { setErrorF(L"CreateComputeShader(depth convert) failed", hr); return false; }
        return true;
    }

    void destroyContext() {
        if (upscaleCtx) {
            bridge.waitGpuIdle();
            ffx.DestroyContext(&upscaleCtx, nullptr);
            upscaleCtx = nullptr;
        }
    }

    void destroyResources() {
        destroyContext();
        for (auto& s : slots) {
            s.depthUav.Reset();
            s.color.reset();
            s.depth.reset();
            s.motion.reset();
            s.output.reset();
        }
        renderW = renderH = outputW = outputH = 0;
        colorFmt = motionFmt = outputFmt = DXGI_FORMAT_UNKNOWN;
    }

    bool ensureResources(const TemporalFrameInputs& in, const D3D11_TEXTURE2D_DESC& colorDesc,
                         const D3D11_TEXTURE2D_DESC& motionDesc, const D3D11_TEXTURE2D_DESC& destDesc) {
        const DXGI_FORMAT cf = concreteColorFormat(colorDesc.Format);
        const DXGI_FORMAT mf = concreteMotionFormat(motionDesc.Format);
        const DXGI_FORMAT of = concreteColorFormat(destDesc.Format);
        if (renderW == in.renderWidth && renderH == in.renderHeight && outputW == in.outputWidth &&
            outputH == in.outputHeight && colorFmt == cf && motionFmt == mf && outputFmt == of && hdr == in.colorIsHdr &&
            depthInverted == in.depthInverted && depthInfinite == in.depthInfinite && slots[0].color.isValid()) {
            return true;
        }

        destroyResources();
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& s : slots) {
            if (!bridge.createSharedTexture(in.renderWidth, in.renderHeight, cf, D3D11_BIND_SHADER_RESOURCE, s.color) ||
                !bridge.createSharedTexture(in.renderWidth, in.renderHeight, DXGI_FORMAT_R32_FLOAT,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, s.depth) ||
                !bridge.createSharedTexture(in.renderWidth, in.renderHeight, mf, D3D11_BIND_SHADER_RESOURCE, s.motion) ||
                !bridge.createSharedTexture(in.outputWidth, in.outputHeight, of,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, s.output)) {
                setError(L"failed to create shared textures (format or UAV not supported for D3D12 sharing)");
                destroyResources();
                return false;
            }
            const HRESULT hr = device->CreateUnorderedAccessView(s.depth.d3d11.Get(), nullptr, &s.depthUav);
            if (FAILED(hr)) { setErrorF(L"CreateUnorderedAccessView(depth) failed", hr); destroyResources(); return false; }
        }

        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = bridge.d3d12Device();

        ffxCreateContextDescUpscaleVersion ver{};
        ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        ver.header.pNext = &backend.header;
        ver.version = FFX_UPSCALER_VERSION;

        ffxCreateContextDescUpscale desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        desc.header.pNext = &ver.header;
        desc.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        if (in.depthInverted) desc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
        if (in.depthInfinite) desc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
        // FSR 4 wants linear color. HDR (linear, pre-tonemap) input uses the HDR flag; tonemapped LDR input
        // is declared non-linear (sRGB), which the SDK documents as supported but possibly lower quality.
        desc.flags |= in.colorIsHdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        desc.maxRenderSize = {in.renderWidth, in.renderHeight};
        desc.maxUpscaleSize = {in.outputWidth, in.outputHeight};
        desc.fpMessage = ffxMessageToLog;

        const ffxReturnCode_t rc = ffx.CreateContext(&upscaleCtx, &desc.header, nullptr);
        if (rc != FFX_API_RETURN_OK || !upscaleCtx) {
            setErrorF(L"ffxCreateContext(upscale) failed", static_cast<long>(rc));
            upscaleCtx = nullptr;
            destroyResources();
            return false;
        }

        ffxQueryGetProviderVersion pv{};
        pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (ffx.Query(&upscaleCtx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) {
            std::snprintf(providerVer, sizeof(providerVer), "%s", pv.versionName);
        }

        renderW = in.renderWidth; renderH = in.renderHeight;
        outputW = in.outputWidth; outputH = in.outputHeight;
        colorFmt = cf; motionFmt = mf; outputFmt = of;
        hdr = in.colorIsHdr; depthInverted = in.depthInverted; depthInfinite = in.depthInfinite;
        resetPending = true;

        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        wchar_t buf[320]{};
        std::swprintf(buf, 320,
                      L"EDPE FSR4: context ready provider=%S render=%ux%u output=%ux%u colorFmt=%u hdr=%d agility=%d (%.0f ms)",
                      providerVer, renderW, renderH, outputW, outputH, static_cast<unsigned>(cf), hdr ? 1 : 0,
                      bridge.usingAgilitySdk() ? 1 : 0, ms);
        EdpeLog(buf);
        return true;
    }

    ID3D11ShaderResourceView* depthSrvFor(const TemporalFrameInputs& in) {
        if (in.depthSrv) return in.depthSrv;
        if (!in.depth) return nullptr;
        if (ownDepthSrv && ownDepthSrvSource == in.depth) return ownDepthSrv.Get();
        D3D11_TEXTURE2D_DESC d{};
        in.depth->GetDesc(&d);
        const DXGI_FORMAT sf = depthSrvFormat(d.Format);
        if (sf == DXGI_FORMAT_UNKNOWN) return nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = sf;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        ownDepthSrv.Reset();
        if (FAILED(device->CreateShaderResourceView(in.depth, &sd, &ownDepthSrv))) return nullptr;
        ownDepthSrvSource = in.depth;
        return ownDepthSrv.Get();
    }

    bool evaluate(ID3D11DeviceContext* context, const TemporalFrameInputs& in, ID3D11Texture2D* dest) {
        if (!ready) return false;
        const wchar_t* reason = nullptr;
        if (!in.isValid(&reason)) { setError(reason ? reason : L"invalid temporal inputs"); return false; }
        if (!in.color || !in.motionVectors) { setError(L"color or motion vector texture missing"); return false; }

        Microsoft::WRL::ComPtr<ID3D11Device> evalDevice;
        context->GetDevice(&evalDevice);
        if (evalDevice.Get() != device.Get()) { setError(L"evaluation device differs from bridge device"); return false; }

        D3D11_TEXTURE2D_DESC colorDesc{}, motionDesc{}, destDesc{};
        in.color->GetDesc(&colorDesc);
        in.motionVectors->GetDesc(&motionDesc);
        dest->GetDesc(&destDesc);
        if (colorDesc.Width != in.renderWidth || colorDesc.Height != in.renderHeight ||
            motionDesc.Width != in.renderWidth || motionDesc.Height != in.renderHeight ||
            destDesc.Width != in.outputWidth || destDesc.Height != in.outputHeight) {
            setError(L"input or destination texture size does not match the reported render/output size");
            return false;
        }
        if (colorDesc.SampleDesc.Count != 1 || destDesc.SampleDesc.Count != 1) { setError(L"multisampled texture"); return false; }

        ID3D11ShaderResourceView* depthSrv = depthSrvFor(in);
        if (!depthSrv) { setError(L"depth shader resource view unavailable"); return false; }
        if (!ensureDepthShader()) return false;
        if (!ensureResources(in, colorDesc, motionDesc, destDesc)) return false;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext4> ctx4;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&ctx4)))) { setError(L"ID3D11DeviceContext4 unavailable"); return false; }

        const auto cpuT0 = std::chrono::steady_clock::now();
        if (profile) pollD3D11Profile(context);

        uint32_t slotIndex = 0;
        bool didCpuWait = false;
        const auto beginT0 = std::chrono::steady_clock::now();
        ID3D12GraphicsCommandList* cl = bridge.beginCommandList(slotIndex, &didCpuWait);
        if (profile) {
            sCpuBegin.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beginT0).count());
            if (didCpuWait) ++cpuWaits;
        }
        if (!cl) { setError(L"bridge command list unavailable"); return false; }
        Fsr4Slot& slot = slots[slotIndex];
        readD3D12Slot(slotIndex);  // previous use of this slot has finished (allocator wait above)

        // ---- D3D12 recording ----
        if (profile) cl->EndQuery(qHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 4 + 0);
        D3D11D3D12Bridge::transitionResource(cl, slot.color.d3d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D11D3D12Bridge::transitionResource(cl, slot.depth.d3d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D11D3D12Bridge::transitionResource(cl, slot.motion.d3d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D11D3D12Bridge::transitionResource(cl, slot.output.d3d12.Get(), D3D12_RESOURCE_STATE_COMMON,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (profile) cl->EndQuery(qHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 4 + 1);
        ffxDispatchDescUpscale du{};
        du.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        du.commandList = cl;
        du.color = ffxApiGetResourceDX12(slot.color.d3d12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.depth = ffxApiGetResourceDX12(slot.depth.d3d12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.motionVectors = ffxApiGetResourceDX12(slot.motion.d3d12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.output = ffxApiGetResourceDX12(slot.output.d3d12.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        du.jitterOffset = {in.jitterX, in.jitterY};
        du.motionVectorScale = {in.motionScaleX, in.motionScaleY};
        du.renderSize = {in.renderWidth, in.renderHeight};
        du.upscaleSize = {in.outputWidth, in.outputHeight};
        du.enableSharpening = in.sharpness > 0.0f;
        du.sharpness = std::clamp(in.sharpness, 0.0f, 1.0f);
        du.frameTimeDelta = in.frameTimeMs > 0.0f ? in.frameTimeMs : 16.6f;
        du.preExposure = 1.0f;
        du.reset = in.reset || resetPending;
        const float nearPlane = (in.cameraNow.depthB > 0.0f) ? in.cameraNow.depthB : 0.025f;
        du.cameraNear = in.depthInverted ? FLT_MAX : nearPlane;
        du.cameraFar = in.depthInverted ? nearPlane : FLT_MAX;
        du.cameraFovAngleVertical =
            (in.cameraNow.scaleY > 0.0f) ? (2.0f * std::atan(1.0f / in.cameraNow.scaleY)) : 1.0f;
        du.viewSpaceToMetersFactor = 1.0f;
        du.flags = in.colorIsHdr ? 0u : static_cast<uint32_t>(FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB);

        const ffxReturnCode_t rc = ffx.Dispatch(&upscaleCtx, &du.header);
        if (rc != FFX_API_RETURN_OK) {
            cl->Close();
            setErrorF(L"ffxDispatch(upscale) failed", static_cast<long>(rc));
            return false;
        }
        if (profile) cl->EndQuery(qHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 4 + 2);

        D3D11D3D12Bridge::transitionResource(cl, slot.color.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_COMMON);
        D3D11D3D12Bridge::transitionResource(cl, slot.depth.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_COMMON);
        D3D11D3D12Bridge::transitionResource(cl, slot.motion.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_COMMON);
        D3D11D3D12Bridge::transitionResource(cl, slot.output.d3d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_COMMON);

        if (profile) {
            cl->EndQuery(qHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 4 + 3);
            cl->ResolveQueryData(qHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 4, 4, qReadback.Get(),
                                 static_cast<UINT64>(slotIndex) * 4 * sizeof(uint64_t));
            slotHasTs[slotIndex] = true;
        }

        // ---- D3D11 side: fill the shared inputs (executed before the fence signal below) ----
        D3D11Prof* pf = nullptr;
        if (profile) {
            D3D11Prof& cand = d11prof[d11ProfWrite];
            if (!cand.inFlight && cand.disjoint) {
                pf = &cand;
                context->Begin(pf->disjoint.Get());
                context->End(pf->ts[0].Get());
            }
        }
        auto closeProf = [&](bool completed) {
            if (!pf) return;
            context->End(pf->disjoint.Get());
            if (completed) {
                pf->inFlight = true;
                d11ProfWrite = (d11ProfWrite + 1) % kProfRing;
            }
            pf = nullptr;
        };
        context->CopyResource(slot.color.d3d11.Get(), in.color);
        context->CopyResource(slot.motion.d3d11.Get(), in.motionVectors);
        {
            // Preserve the compute state the game or EDPE may have bound.
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> prevCs;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> prevSrv;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> prevUav;
            context->CSGetShader(&prevCs, nullptr, nullptr);
            context->CSGetShaderResources(0, 1, &prevSrv);
            context->CSGetUnorderedAccessViews(0, 1, &prevUav);

            context->CSSetShader(depthCs.Get(), nullptr, 0);
            ID3D11ShaderResourceView* srv = depthSrv;
            context->CSSetShaderResources(0, 1, &srv);
            ID3D11UnorderedAccessView* uav = slot.depthUav.Get();
            const UINT initial = 0;
            context->CSSetUnorderedAccessViews(0, 1, &uav, &initial);
            context->Dispatch((in.renderWidth + 7) / 8, (in.renderHeight + 7) / 8, 1);

            ID3D11UnorderedAccessView* nullUav = nullptr;
            context->CSSetUnorderedAccessViews(0, 1, &nullUav, &initial);
            ID3D11ShaderResourceView* nullSrv = nullptr;
            context->CSSetShaderResources(0, 1, &nullSrv);
            const UINT restoreInitial = static_cast<UINT>(-1);
            ID3D11UnorderedAccessView* restoreUav = prevUav.Get();
            context->CSSetShader(prevCs.Get(), nullptr, 0);
            ID3D11ShaderResourceView* restoreSrv = prevSrv.Get();
            context->CSSetShaderResources(0, 1, &restoreSrv);
            context->CSSetUnorderedAccessViews(0, 1, &restoreUav, &restoreInitial);
        }

        if (pf) context->End(pf->ts[1].Get());
        const uint64_t done = bridge.submitAfterD3D11Signal(ctx4.Get(), slotIndex, flushEachFrame);
        if (done == 0) { closeProf(false); setError(L"bridge submit failed"); return false; }
        if (!bridge.waitOnD3D11(ctx4.Get(), done)) { closeProf(false); setError(L"D3D11 wait on D3D12 fence failed"); return false; }
        if (pf) context->End(pf->ts[2].Get());

        context->CopyResource(dest, slot.output.d3d11.Get());
        if (pf) context->End(pf->ts[3].Get());
        closeProf(true);

        if (profile) {
            sCpuEval.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuT0).count());
            maybeReport();
        }
        resetPending = false;
        ++evalCount;
        if (evalCount == 1) {
            EdpeLog(L"EDPE FSR4: first frame upscaled through the D3D12 bridge");
        }
        return true;
    }
};

Fsr4Engine::Fsr4Engine() : impl_(std::make_unique<Impl>()) {}
Fsr4Engine::~Fsr4Engine() { shutdown(); }

bool Fsr4Engine::enabledByConfig() {
    return GetPrivateProfileIntW(L"Upscaler", L"Fsr4", 0, iniPathNextToExecutable().c_str()) != 0;
}

bool Fsr4Engine::initialize(ID3D11Device* device) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    shutdown();
    if (!device) { impl_->setError(L"D3D11 device is null"); return false; }
    impl_->device = device;
    if (!impl_->loadDlls()) return false;

    // Use the side-loaded Agility SDK runtime if D3D12\D3D12Core.dll is staged next to the proxy.
    std::wstring agilityDir;
    const auto dir = moduleDirectory() / L"D3D12";
    std::error_code ec;
    if (std::filesystem::exists(dir / L"D3D12Core.dll", ec)) {
        agilityDir = dir.wstring();
        if (agilityDir.back() != L'\\') agilityDir += L'\\';
    }
    if (!impl_->bridge.initialize(device, agilityDir.empty() ? nullptr : agilityDir.c_str())) {
        impl_->setError(L"D3D11/D3D12 bridge initialization failed");
        return false;
    }
    impl_->ready = true;
    impl_->initProfiling();
    wchar_t buf[256]{};
    std::swprintf(buf, 256, L"EDPE FSR4: engine initialized (agilitySdk=%d)", impl_->bridge.usingAgilitySdk() ? 1 : 0);
    EdpeLog(buf);
    return true;
}

void Fsr4Engine::shutdown() {
    if (!impl_) return;
    impl_->ready = false;
    impl_->destroyResources();
    impl_->depthCs.Reset();
    impl_->qHeap.Reset();
    impl_->qReadback.Reset();
    for (auto& p : impl_->d11prof) p = Fsr4Engine::Impl::D3D11Prof{};
    impl_->slotHasTs.fill(false);
    impl_->ownDepthSrv.Reset();
    impl_->ownDepthSrvSource = nullptr;
    impl_->bridge.shutdown();
    impl_->device.Reset();
    impl_->evalCount = 0;
    impl_->resetPending = true;
}

bool Fsr4Engine::isReady() const { return impl_ && impl_->ready; }
const wchar_t* Fsr4Engine::lastError() const { return impl_ ? impl_->error : L""; }
const char* Fsr4Engine::providerVersion() const { return impl_ ? impl_->providerVer : "unavailable"; }

bool Fsr4Engine::evaluate(ID3D11DeviceContext* context, const TemporalFrameInputs& inputs,
                          ID3D11Texture2D* destination) {
    if (!impl_ || !context || !destination) return false;
    return impl_->evaluate(context, inputs, destination);
}

void Fsr4Engine::resetHistory() {
    if (impl_) impl_->resetPending = true;
}

#else // FSR 4 support not compiled in: inert stub so callers need no #if.

struct Fsr4Engine::Impl {
    wchar_t error[64] = L"FSR 4 support not compiled into EDPE";
};
Fsr4Engine::Fsr4Engine() : impl_(std::make_unique<Impl>()) {}
Fsr4Engine::~Fsr4Engine() = default;
bool Fsr4Engine::enabledByConfig() { return false; }
bool Fsr4Engine::initialize(ID3D11Device*) { return false; }
void Fsr4Engine::shutdown() {}
bool Fsr4Engine::isReady() const { return false; }
const wchar_t* Fsr4Engine::lastError() const { return impl_ ? impl_->error : L""; }
const char* Fsr4Engine::providerVersion() const { return "unavailable"; }
bool Fsr4Engine::evaluate(ID3D11DeviceContext*, const TemporalFrameInputs&, ID3D11Texture2D*) { return false; }
void Fsr4Engine::resetHistory() {}

#endif

} // namespace edpe
