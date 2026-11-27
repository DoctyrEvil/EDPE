// Developer-only D3D12 DRED (Device Removed Extended Data) logging for the D3D11-D3D12 bridge devices.
// Not part of any pull request. Applied to a throwaway worktree by tools/edpe_debug/apply_hook.py.
//
// d3d12_bridge.cpp calls the inline helpers below. They do nothing until debug_dred.cpp is linked in, so test targets
// that compile d3d12_bridge.cpp without it still link.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

struct EdpeDredFns {
    void (*enableFactory)(ID3D12DeviceFactory*) = nullptr;
    void (*enableSystem)(HMODULE) = nullptr;
    void (*registerDevice)(ID3D12Device*, ID3D11Device*) = nullptr;
    void (*unregisterDevice)(ID3D12Device*) = nullptr;
};
inline EdpeDredFns g_edpeDredFns;

inline void EdpeDredEnableForFactory(ID3D12DeviceFactory* factory) {
    if (g_edpeDredFns.enableFactory) g_edpeDredFns.enableFactory(factory);
}
inline void EdpeDredEnableForSystem(HMODULE d3d12) {
    if (g_edpeDredFns.enableSystem) g_edpeDredFns.enableSystem(d3d12);
}
inline void EdpeDredRegister(ID3D12Device* device12, ID3D11Device* device11) {
    if (g_edpeDredFns.registerDevice) g_edpeDredFns.registerDevice(device12, device11);
}
inline void EdpeDredUnregister(ID3D12Device* device12) {
    if (g_edpeDredFns.unregisterDevice) g_edpeDredFns.unregisterDevice(device12);
}

// Once per presented frame (from EdpeDebugTick): logs the removal reason, the DRED breadcrumbs and the page-fault
// allocation the first time a registered device reports it was removed.
void EdpeDredPoll();
