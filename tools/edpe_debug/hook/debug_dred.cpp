// Developer-only DRED logging. See debug_dred.h.
#include "debug_dred.h"

#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <vector>
#include <wrl/client.h>

namespace {

struct Tracked {
    ID3D12Device* device12 = nullptr;  // weak: the bridge owns it and unregisters before it releases it
    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    bool reported = false;
};

std::mutex g_mutex;
std::vector<Tracked> g_tracked;

void logf(const wchar_t* fmt, ...) {
    wchar_t buf[512]{};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    EdpeLog(buf);
}

void applySettings(ID3D12DeviceRemovedExtendedDataSettings1* settings, const wchar_t* path) {
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    logf(L"EDPE DRED: auto breadcrumbs, page fault and breadcrumb context forced on (%ls)", path);
}

void enableFactory(ID3D12DeviceFactory* factory) {
    if (!factory) return;
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
    if (SUCCEEDED(factory->GetConfigurationInterface(CLSID_D3D12DeviceRemovedExtendedData, IID_PPV_ARGS(&settings)))) {
        applySettings(settings.Get(), L"device factory");
    } else {
        EdpeLog(L"EDPE DRED: the device factory does not expose the DRED settings interface");
    }
}

void enableSystem(HMODULE d3d12) {
    using PFN_D3D12GetInterface = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    if (!d3d12) return;
    const auto getInterface = reinterpret_cast<PFN_D3D12GetInterface>(GetProcAddress(d3d12, "D3D12GetInterface"));
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
    if (getInterface && SUCCEEDED(getInterface(CLSID_D3D12DeviceRemovedExtendedData, IID_PPV_ARGS(&settings)))) {
        applySettings(settings.Get(), L"system runtime");
    } else {
        EdpeLog(L"EDPE DRED: the system runtime does not expose the DRED settings interface");
    }
}

void registerDevice(ID3D12Device* device12, ID3D11Device* device11) {
    if (!device12) return;
    std::lock_guard lock(g_mutex);
    Tracked t;
    t.device12 = device12;
    t.device11 = device11;
    g_tracked.push_back(std::move(t));
    logf(L"EDPE DRED: tracking D3D12 device %p (%zu tracked)", static_cast<void*>(device12), g_tracked.size());
}

void unregisterDevice(ID3D12Device* device12) {
    if (!device12) return;
    std::lock_guard lock(g_mutex);
    for (auto it = g_tracked.begin(); it != g_tracked.end(); ++it) {
        if (it->device12 == device12) {
            g_tracked.erase(it);
            return;
        }
    }
}

const wchar_t* opName(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return L"SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return L"BeginEvent";
    case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return L"EndEvent";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return L"DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return L"DrawIndexedInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return L"ExecuteIndirect";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return L"Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return L"CopyBufferRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return L"CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return L"CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return L"CopyTiles";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return L"ResolveSubresource";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return L"ClearRenderTargetView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return L"ClearUnorderedAccessView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return L"ClearDepthStencilView";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return L"ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return L"ExecuteBundle";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return L"Present";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return L"ResolveQueryData";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return L"BeginSubmission";
    case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return L"EndSubmission";
    default: return L"(other)";
    }
}

void reportBreadcrumbs(ID3D12DeviceRemovedExtendedData1* dred) {
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 out{};
    if (FAILED(dred->GetAutoBreadcrumbsOutput1(&out))) {
        EdpeLog(L"EDPE DRED: no auto breadcrumbs output");
        return;
    }
    int nodes = 0;
    for (const D3D12_AUTO_BREADCRUMB_NODE1* n = out.pHeadAutoBreadcrumbNode; n; n = n->pNext) {
        ++nodes;
        const UINT done = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
        // Only lists that did not finish are interesting; the last completed op and the next ones are the suspects.
        if (done >= n->BreadcrumbCount) continue;
        logf(L"EDPE DRED: unfinished command list '%ls' on queue '%ls': %u of %u ops completed",
             n->pCommandListDebugNameW ? n->pCommandListDebugNameW : L"?",
             n->pCommandQueueDebugNameW ? n->pCommandQueueDebugNameW : L"?", done, n->BreadcrumbCount);
        const UINT first = done > 3 ? done - 3 : 0;
        for (UINT i = first; i < n->BreadcrumbCount && i < done + 4; ++i) {
            logf(L"EDPE DRED:   op %u %ls%ls", i, opName(n->pCommandHistory[i]), i == done ? L"   <== first op not completed" : L"");
        }
    }
    logf(L"EDPE DRED: %d breadcrumb node(s) inspected", nodes);
}

void reportPageFault(ID3D12DeviceRemovedExtendedData1* dred) {
    D3D12_DRED_PAGE_FAULT_OUTPUT1 out{};
    if (FAILED(dred->GetPageFaultAllocationOutput1(&out)) || out.PageFaultVA == 0) {
        EdpeLog(L"EDPE DRED: no GPU page fault recorded");
        return;
    }
    logf(L"EDPE DRED: GPU page fault at VA 0x%llX", static_cast<unsigned long long>(out.PageFaultVA));
    for (const D3D12_DRED_ALLOCATION_NODE1* n = out.pHeadExistingAllocationNode; n; n = n->pNext) {
        logf(L"EDPE DRED:   live allocation '%ls' type %d", n->ObjectNameW ? n->ObjectNameW : L"?", static_cast<int>(n->AllocationType));
    }
    for (const D3D12_DRED_ALLOCATION_NODE1* n = out.pHeadRecentFreedAllocationNode; n; n = n->pNext) {
        logf(L"EDPE DRED:   recently freed '%ls' type %d", n->ObjectNameW ? n->ObjectNameW : L"?", static_cast<int>(n->AllocationType));
    }
}

void report(Tracked& t, HRESULT reason12, HRESULT reason11) {
    t.reported = true;
    logf(L"EDPE DRED: device removed: D3D12 %p reason 0x%08X, D3D11 reason 0x%08X", static_cast<void*>(t.device12),
         static_cast<unsigned>(reason12), static_cast<unsigned>(reason11));
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (SUCCEEDED(t.device12->QueryInterface(IID_PPV_ARGS(&dred)))) {
        reportBreadcrumbs(dred.Get());
        reportPageFault(dred.Get());
    } else {
        EdpeLog(L"EDPE DRED: ID3D12DeviceRemovedExtendedData1 is not available on this device");
    }
    EdpeLogFlush();  // the game aborts soon after a removal; keep what was just written
}

struct Registrar {
    Registrar() {
        g_edpeDredFns.enableFactory = enableFactory;
        g_edpeDredFns.enableSystem = enableSystem;
        g_edpeDredFns.registerDevice = registerDevice;
        g_edpeDredFns.unregisterDevice = unregisterDevice;
    }
} g_registrar;

} // namespace

void EdpeDredPoll() {
    std::lock_guard lock(g_mutex);
    for (auto& t : g_tracked) {
        if (t.reported || !t.device12) continue;
        const HRESULT r12 = t.device12->GetDeviceRemovedReason();
        const HRESULT r11 = t.device11 ? t.device11->GetDeviceRemovedReason() : S_OK;
        if (FAILED(r12) || FAILED(r11)) report(t, r12, r11);
    }
}
