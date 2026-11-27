p = r"C:\Users\<user>\Development\GPU_Performance_Analysis\work\EDPE-test\src\fg_integration.cpp"
raw = open(p, encoding="utf-8", newline="").read()
crlf = "\r\n" in raw
t = raw.replace("\r\n", "\n")


def rep(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:70])
    t = t.replace(a, b)


rep('''struct CreateChildParams {
    HWND parent = nullptr;''', '''// The overlay child window makes DWM compose the game window. After the child is destroyed DWM does not always hand
// the window back to Independent Flip until the window changes (minimize and restore did it), which left
// compositor-paced presents and judder. A no-op frame change asks DWM to re-evaluate the window. Runs once per
// overlay teardown, on the window's own thread.
void requestPresentationReevaluation(HWND parent) {
    if (!parent || !IsWindow(parent)) return;
    SetWindowPos(parent, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(parent, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

struct CreateChildParams {
    HWND parent = nullptr;''')

rep('''        if (child && IsWindow(child)) {
            DestroyWindow(child);
        }
        if (outResult) *outResult = 1;
        return true;
    }
    return false;''', '''        if (child && IsWindow(child)) {
            DestroyWindow(child);
        }
        requestPresentationReevaluation(hwnd);
        if (outResult) *outResult = 1;
        return true;
    }
    return false;''')

rep('''        if (ownerTid == GetCurrentThreadId()) {
            DestroyWindow(child);
        } else if (parent && IsWindow(parent)) {''', '''        if (ownerTid == GetCurrentThreadId()) {
            DestroyWindow(child);
            requestPresentationReevaluation(parent);
        } else if (parent && IsWindow(parent)) {''')

open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n") if crlf else t)
print("edited")
