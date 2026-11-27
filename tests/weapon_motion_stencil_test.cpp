// Tests the dynamic viewmodel stencil rule and the draw-hook fast-path precheck (DrawHookFastPath).
//
// 1. viewmodelStencilRefMatches / viewmodelStencilRefPossible over every 8-bit stencil reference.
// 2. On a WARP device, WeaponMotionTracker::hasViewmodelStencilRef for null inputs and for every reference, and
//    with a full pipeline state that otherwise passes isOpaqueVisibleViewmodelDraw: whenever the precheck says no,
//    the full check says no too (the fast path never skips a draw the full path would capture), and the full check
//    follows the shared rule in both modes.

#include "weapon_motion.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;
using namespace edpe;

namespace {

int fail(int code, const char* msg) {
    std::fprintf(stderr, "weapon_motion_stencil_test FAILED (%d): %s\n", code, msg);
    return code;
}

constexpr char kPsSrc[] = "float4 main() : SV_Target { return float4(1, 0, 0, 1); }";

}  // namespace

int main() {
    // 1. Pure rule.
    static_assert(!viewmodelStencilRefMatches(0u, true) && !viewmodelStencilRefMatches(0u, false));
    static_assert(viewmodelStencilRefMatches(kWeaponStencilBit, true));
    static_assert(viewmodelStencilRefMatches(kWeaponStencilBit, false));
    static_assert(!viewmodelStencilRefMatches(kCockpitStencilBit, true));
    static_assert(viewmodelStencilRefMatches(kCockpitStencilBit, false));
    static_assert(!viewmodelStencilRefMatches(kWeaponStencilBit | kCockpitStencilBit, true));
    for (unsigned ref = 0; ref < 256; ++ref) {
        const bool foot = viewmodelStencilRefMatches(ref, true);
        const bool ship = viewmodelStencilRefMatches(ref, false);
        if (viewmodelStencilRefPossible(ref) != (foot || ship)) return fail(1, "Possible must be Matches(foot) || Matches(ship)");
        if (viewmodelStencilRefPossible(ref) != ((ref & kDynamicViewmodelStencilMask) != 0u)) {
            return fail(2, "Possible must equal a non-zero masked reference");
        }
    }

    // 2. Device checks.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 &device, nullptr, &ctx))) {
        return fail(3, "WARP device creation failed");
    }

    if (WeaponMotionTracker::hasViewmodelStencilRef(nullptr)) return fail(4, "Null context must be rejected");
    ctx->OMSetDepthStencilState(nullptr, kWeaponStencilBit);
    if (WeaponMotionTracker::hasViewmodelStencilRef(ctx.Get())) return fail(5, "No bound state must be rejected");

    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &color)) ||
        FAILED(device->CreateRenderTargetView(color.Get(), nullptr, &rtv))) {
        return fail(6, "Render target creation failed");
    }
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> dsv;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &depth)) ||
        FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv))) {
        return fail(7, "Depth target creation failed");
    }

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    dd.StencilEnable = TRUE;
    dd.StencilReadMask = 0xFF;
    dd.StencilWriteMask = 0xFF;
    dd.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    dd.BackFace = dd.FrontFace;
    ComPtr<ID3D11DepthStencilState> ds;
    if (FAILED(device->CreateDepthStencilState(&dd, &ds))) return fail(8, "Depth-stencil state creation failed");

    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(D3DCompile(kPsSrc, sizeof(kPsSrc) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob,
                          nullptr)) ||
        FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
        return fail(9, "Pixel shader creation failed");
    }

    ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
    ctx->OMSetRenderTargets(1, rtvs, dsv.Get());
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);

    for (unsigned ref = 0; ref < 256; ++ref) {
        ctx->OMSetDepthStencilState(ds.Get(), ref);
        const bool precheck = WeaponMotionTracker::hasViewmodelStencilRef(ctx.Get());
        if (precheck != viewmodelStencilRefPossible(ref)) return fail(10, "Precheck must follow the shared rule");
        for (const bool onFoot : {true, false}) {
            const bool full = WeaponMotionTracker::isOpaqueVisibleViewmodelDraw(ctx.Get(), depth.Get(), onFoot);
            if (full && !precheck) return fail(11, "Precheck rejected a draw the full check accepts");
            if (full != viewmodelStencilRefMatches(ref, onFoot)) {
                return fail(12, "Full check must follow the shared rule when the rest of the state passes");
            }
        }
    }

    std::printf("weapon_motion_stencil_test PASSED\n");
    return 0;
}
