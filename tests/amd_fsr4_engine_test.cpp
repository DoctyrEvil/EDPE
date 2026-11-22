// FSR 4 engine smoke test. Needs a D3D11 device and, to do anything, the signed FidelityFX DLLs staged beside
// the executable (amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll). Skips (exit 0) when the
// engine cannot initialize or the GPU/driver does not offer FSR 4, so it is safe on machines without them.
//
// Feeds EDPE-style inputs: 8-bit typeless color, typeless depth/stencil with a float SRV, R16G16 motion vectors,
// and an 8-bit destination. A flat color must survive the round trip through the D3D12 bridge.
#include "amd_fsr4_engine.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace edpe;

#define REQUIRE(cond, msg)                                              \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__);         \
            return 1;                                                   \
        }                                                               \
    } while (0)

static float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) { f /= static_cast<float>(base); r += f * static_cast<float>(index % base); index /= base; }
    return r;
}

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_1;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        std::printf("SKIP: no hardware D3D11 device\n");
        return 0;
    }

    Fsr4Engine engine;
    if (!engine.initialize(dev.Get())) {
        std::wprintf(L"SKIP: FSR 4 engine not available: %ls\n", engine.lastError());
        return 0;
    }

    const UINT rw = 960, rh = 540, ow = 1920, oh = 1080;
    auto makeTex = [&](UINT w, UINT h, DXGI_FORMAT fmt, UINT bind) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind;
        ComPtr<ID3D11Texture2D> t;
        return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)) ? t : ComPtr<ID3D11Texture2D>();
    };

    auto color = makeTex(rw, rh, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    auto depth = makeTex(rw, rh, DXGI_FORMAT_R32G8X24_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
    auto motion = makeTex(rw, rh, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    auto dest = makeTex(ow, oh, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    REQUIRE(color && depth && motion && dest, "test texture creation");

    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> colorRtv, motionRtv;
    REQUIRE(SUCCEEDED(dev->CreateRenderTargetView(color.Get(), &rd, &colorRtv)), "color RTV");
    REQUIRE(SUCCEEDED(dev->CreateRenderTargetView(motion.Get(), nullptr, &motionRtv)), "motion RTV");

    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv;
    REQUIRE(SUCCEEDED(dev->CreateDepthStencilView(depth.Get(), &dd, &dsv)), "depth DSV");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> depthSrv;
    REQUIRE(SUCCEEDED(dev->CreateShaderResourceView(depth.Get(), &sd, &depthSrv)), "depth SRV");

    const float clr[4] = {0.3f, 0.4f, 0.5f, 1.0f};
    const float zero[4] = {0, 0, 0, 0};

    TemporalFrameInputs in{};
    in.color = color.Get();
    in.depth = depth.Get();
    in.depthSrv = depthSrv.Get();
    in.motionVectors = motion.Get();
    in.renderWidth = rw; in.renderHeight = rh; in.outputWidth = ow; in.outputHeight = oh;
    in.frameTimeMs = 16.6f;
    in.colorIsHdr = false;
    in.depthInverted = true;
    in.depthInfinite = true;
    in.cameraNow.scaleY = 1.7320508f;
    in.cameraNow.depthB = 0.025f;

    bool anySuccess = false;
    for (int f = 0; f < 40; ++f) {
        ctx->ClearRenderTargetView(colorRtv.Get(), clr);
        ctx->ClearRenderTargetView(motionRtv.Get(), zero);
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.25f, 0);
        in.jitterX = halton((f % 8) + 1, 2) - 0.5f;
        in.jitterY = halton((f % 8) + 1, 3) - 0.5f;
        in.reset = (f == 0);
        const bool ok = engine.evaluate(ctx.Get(), in, dest.Get());
        if (!ok) {
            const std::wstring err = engine.lastError();
            if (!anySuccess && err.find(L"ffxCreateContext") != std::wstring::npos) {
                std::wprintf(L"SKIP: this GPU/driver does not offer FSR 4: %ls\n", err.c_str());
                return 0;
            }
            std::wprintf(L"FAIL: evaluate returned false on frame %d: %ls\n", f, err.c_str());
            return 1;
        }
        anySuccess = true;
    }

    // Read back the center pixel of the destination.
    D3D11_TEXTURE2D_DESC sdsc{};
    dest->GetDesc(&sdsc);
    sdsc.Usage = D3D11_USAGE_STAGING; sdsc.BindFlags = 0; sdsc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(dev->CreateTexture2D(&sdsc, nullptr, &staging)), "staging texture");
    ctx->CopyResource(staging.Get(), dest.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    REQUIRE(SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)), "map staging");
    const unsigned char* px = static_cast<const unsigned char*>(m.pData) + m.RowPitch * (oh / 2) + 4 * (ow / 2);
    const int r = px[0], g = px[1], b = px[2];
    ctx->Unmap(staging.Get(), 0);

    std::printf("FSR 4 provider %s; center pixel RGB = %d %d %d (expected about 77 102 128)\n",
                engine.providerVersion(), r, g, b);
    REQUIRE(std::abs(r - 77) <= 10 && std::abs(g - 102) <= 10 && std::abs(b - 128) <= 10,
            "output center pixel does not match the flat input color");
    std::printf("PASS\n");
    return 0;
}
