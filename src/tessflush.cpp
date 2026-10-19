#include "tessflush.h"
#include <d3d11.h>
#include <dxgi.h>

// The device context's vtable slots for the draws, the render target changes and the hull shader
// point at detours: every context of the same class, the game's among them, then comes through
// them. Each detour passes the call on unchanged; the draws first flush the context when a hull
// shader is bound and this pass (since the last render target change) hasn't been flushed yet.
namespace {

// ID3D11DeviceContext vtable slots, in d3d11.h's order (IUnknown 0-2, ID3D11DeviceChild 3-6).
constexpr int kDrawIndexed = 12, kDraw = 13, kDrawIndexedInstanced = 20, kDrawInstanced = 21,
              kOMSetRenderTargets = 33, kOMSetRenderTargetsAndUAVs = 34, kDrawIndexedInstancedIndirect = 39,
              kDrawInstancedIndirect = 40, kHSSetShader = 60;
constexpr int kSlots = 61;

// The originals, per vtable: the immediate and the deferred contexts may not share one.
struct Table { void** vtbl; void* orig[kSlots]; };
constexpr int kMaxTables = 4;
Table         g_tables[kMaxTables];
volatile LONG g_tableCount = 0;

void* Orig(void* obj, int slot) {
    void** vtbl = *static_cast<void***>(obj);
    const LONG n = g_tableCount;
    for (LONG i = 0; i < n; ++i)
        if (g_tables[i].vtbl == vtbl) return g_tables[i].orig[slot];
    return nullptr;
}

bool Patch(void* obj, int slot, void* detour) {
    void** vtbl = *static_cast<void***>(obj);
    Table* t = nullptr;
    for (LONG i = 0; i < g_tableCount; ++i)
        if (g_tables[i].vtbl == vtbl) t = &g_tables[i];
    if (!t) {
        if (g_tableCount >= kMaxTables) return false;
        t = &g_tables[g_tableCount];
        t->vtbl = vtbl;
        MemoryBarrier();
        InterlockedIncrement(&g_tableCount);
    }
    if (vtbl[slot] == detour) return true;
    t->orig[slot] = vtbl[slot];
    MemoryBarrier();
    DWORD old = 0;
    if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(&vtbl[slot], detour);
    DWORD ignored = 0;
    VirtualProtect(&vtbl[slot], sizeof(void*), old, &ignored);
    return true;
}

// Per context: a hull shader is bound; this pass has been flushed.
struct State { void* volatile ctx; bool hs; bool flushed; };
State          g_state[16];
volatile LONG64 g_flushes = 0;

State& For(void* ctx) {
    for (State& s : g_state)
        if (s.ctx == ctx) return s;
    for (State& s : g_state)
        if (!s.ctx && InterlockedCompareExchangePointer(&s.ctx, ctx, nullptr) == nullptr) return s;
    return g_state[0];   // more contexts than slots: they share one, which at worst flushes more often
}

using Ctx = ID3D11DeviceContext;
#define ORIG(T, obj, slot) reinterpret_cast<T>(Orig(obj, slot))

void BeforeDraw(Ctx* c) {
    State& s = For(c);
    if (!s.hs || s.flushed) return;
    s.flushed = true;
    InterlockedIncrement64(&g_flushes);
    c->Flush();
}

void STDMETHODCALLTYPE HSSetShader_(Ctx* c, ID3D11HullShader* hs, ID3D11ClassInstance* const* ci, UINT n) {
    For(c).hs = hs != nullptr;
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, ID3D11HullShader*, ID3D11ClassInstance* const*, UINT), c, kHSSetShader)(c, hs, ci, n);
}
void STDMETHODCALLTYPE OMSetRenderTargets_(Ctx* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    For(c).flushed = false;
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*), c, kOMSetRenderTargets)(c, n, rtv, dsv);
}
void STDMETHODCALLTYPE OMSetRenderTargetsAndUAVs_(Ctx* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv,
                                                  UINT uavStart, UINT uavs, ID3D11UnorderedAccessView* const* uav, const UINT* counts) {
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) For(c).flushed = false;
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT,
                                  ID3D11UnorderedAccessView* const*, const UINT*), c, kOMSetRenderTargetsAndUAVs)(c, n, rtv, dsv, uavStart, uavs, uav, counts);
}
void STDMETHODCALLTYPE DrawIndexed_(Ctx* c, UINT count, UINT start, INT base) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, UINT, INT), c, kDrawIndexed)(c, count, start, base);
}
void STDMETHODCALLTYPE Draw_(Ctx* c, UINT count, UINT start) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, UINT), c, kDraw)(c, count, start);
}
void STDMETHODCALLTYPE DrawIndexedInstanced_(Ctx* c, UINT count, UINT instances, UINT start, INT base, UINT startInstance) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, UINT, UINT, INT, UINT), c, kDrawIndexedInstanced)(c, count, instances, start, base, startInstance);
}
void STDMETHODCALLTYPE DrawInstanced_(Ctx* c, UINT count, UINT instances, UINT start, UINT startInstance) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, UINT, UINT, UINT, UINT), c, kDrawInstanced)(c, count, instances, start, startInstance);
}
void STDMETHODCALLTYPE DrawIndexedInstancedIndirect_(Ctx* c, ID3D11Buffer* args, UINT offset) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, ID3D11Buffer*, UINT), c, kDrawIndexedInstancedIndirect)(c, args, offset);
}
void STDMETHODCALLTYPE DrawInstancedIndirect_(Ctx* c, ID3D11Buffer* args, UINT offset) {
    BeforeDraw(c);
    ORIG(void(STDMETHODCALLTYPE*)(Ctx*, ID3D11Buffer*, UINT), c, kDrawInstancedIndirect)(c, args, offset);
}

int PatchContext(Ctx* c) {
    const struct { int slot; void* fn; } list[] = {
        { kHSSetShader, reinterpret_cast<void*>(&HSSetShader_) },
        { kOMSetRenderTargets, reinterpret_cast<void*>(&OMSetRenderTargets_) },
        { kOMSetRenderTargetsAndUAVs, reinterpret_cast<void*>(&OMSetRenderTargetsAndUAVs_) },
        { kDrawIndexed, reinterpret_cast<void*>(&DrawIndexed_) },
        { kDraw, reinterpret_cast<void*>(&Draw_) },
        { kDrawIndexedInstanced, reinterpret_cast<void*>(&DrawIndexedInstanced_) },
        { kDrawInstanced, reinterpret_cast<void*>(&DrawInstanced_) },
        { kDrawIndexedInstancedIndirect, reinterpret_cast<void*>(&DrawIndexedInstancedIndirect_) },
        { kDrawInstancedIndirect, reinterpret_cast<void*>(&DrawInstancedIndirect_) },
    };
    int patched = 0;
    for (const auto& e : list) patched += Patch(c, e.slot, e.fn) ? 1 : 0;
    return patched;
}

// Wine on macOS (ntdll's wine_get_host_version says Darwin): D3D11 there is translated to Metal, by
// D3DMetal or DXMT. Neither reports an Apple adapter (D3DMetal says "AMD Compatibility Mode" with
// NVIDIA's vendor id, for DLSS), so the host is what tells.
bool WineOnMac() {
    using HostVersion = void(CDECL*)(const char** sysname, const char** release);
    const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    const auto fn = ntdll ? reinterpret_cast<HostVersion>(GetProcAddress(ntdll, "wine_get_host_version")) : nullptr;
    if (!fn) return false;
    const char* sys = nullptr;
    const char* rel = nullptr;
    fn(&sys, &rel);
    return sys && strcmp(sys, "Darwin") == 0;
}

void AdapterInfo(ID3D11Device* dev, UINT& vendor, char* name, size_t n) {
    vendor = 0;
    name[0] = 0;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) && dxgi &&
        SUCCEEDED(dxgi->GetAdapter(&adapter)) && adapter) {
        DXGI_ADAPTER_DESC d = {};
        if (SUCCEEDED(adapter->GetDesc(&d))) {
            vendor = d.VendorId;
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, static_cast<int>(n), nullptr, nullptr);
        }
    }
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
}

DWORD WINAPI InstallThread(LPVOID param) {
    const bool force = param != nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* imm = nullptr;
    ID3D11DeviceContext* def = nullptr;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &dev, nullptr, &imm);
    if (FAILED(hr) || !dev || !imm) {
        Log("[gpu] tessellation flush: no device to hook (0x%08lX); off", static_cast<unsigned long>(hr));
    } else {
        UINT vendor = 0;
        char name[128];
        AdapterInfo(dev, vendor, name, sizeof(name));
        if (!force && !WineOnMac()) {
            Log("[gpu] tessellation flush: off (not Wine on macOS; adapter \"%s\", vendor 0x%04X)", name, vendor);
        } else {
            int patched = PatchContext(imm);
            if (SUCCEEDED(dev->CreateDeferredContext(0, &def)) && def) patched += PatchContext(def);
            Log("[gpu] tessellation flush: on (%s; adapter \"%s\", vendor 0x%04X), %d slots in %ld vtable(s); "
                "SC_OFFLINE_TESS_FLUSH=0 turns it off", force ? "forced" : "Wine on macOS", name, vendor, patched,
                static_cast<long>(g_tableCount));
        }
    }
    if (def) def->Release();
    if (imm) imm->Release();
    if (dev) dev->Release();
    return 0;
}

}  // namespace

bool TessFlush_WineOnMac() { return WineOnMac(); }

void TessFlush_Install() {
    char env[8] = "";
    const DWORD len = GetEnvironmentVariableA("SC_OFFLINE_TESS_FLUSH", env, sizeof(env));
    const bool set = len > 0 && len < sizeof(env);
    if (set && strcmp(env, "0") == 0) { Log("[gpu] tessellation flush: off (SC_OFFLINE_TESS_FLUSH=0)"); return; }
    const bool force = set && strcmp(env, "1") == 0;
    if (HANDLE t = CreateThread(nullptr, 0, InstallThread, force ? reinterpret_cast<LPVOID>(1) : nullptr, 0, nullptr)) CloseHandle(t);
}
