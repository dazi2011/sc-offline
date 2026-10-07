#include "menu.h"
#include "common.h"
#include <string>
#include <unordered_map>
#include <share.h>
#include "travel.h"
#include "version.h"
#include <algorithm>
#include <d3d11.h>
#include <wincodec.h>
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "spawner.h"
#include "cvars.h"
#include "build.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_win32.h"
#include "third_party/imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND                    g_game;
static HWND                    g_wnd;
static ID3D11Device*           g_device;
static ID3D11DeviceContext*    g_context;
static IDXGISwapChain*         g_swap;
static ID3D11RenderTargetView* g_rtv;
static UINT                    g_resizeW, g_resizeH;

constexpr int kMenuW = 600, kMenuH = 900;

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void ReleaseRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDevice() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static LRESULT CALLBACK MenuWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { g_resizeW = LOWORD(l); g_resizeH = HIWORD(l); }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    }
    return DefWindowProcW(h, msg, w, l);
}

static bool OurProcessHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static bool g_clipped = false;

static void KeepCursorInMenu() {
    if (GetForegroundWindow() != g_wnd) {
        if (g_clipped) { ClipCursor(nullptr); g_clipped = false; }
        return;
    }
    RECT r = {};
    GetWindowRect(g_wnd, &r);
    ClipCursor(&r);
    g_clipped = true;
}

static void ShowMenu(bool show) {
    if (show) {
        RECT r = {};
        GetWindowRect(g_game, &r);
        SetWindowPos(g_wnd, HWND_TOPMOST, r.left + 60, r.top + 60, kMenuW, kMenuH, SWP_SHOWWINDOW);
        SetForegroundWindow(g_wnd);
        SetCursorPos(r.left + 60 + kMenuW / 2, r.top + 60 + kMenuH / 2);
        KeepCursorInMenu();
    } else {
        ClipCursor(nullptr);
        g_clipped = false;
        ShowWindow(g_wnd, SW_HIDE);
        SetForegroundWindow(g_game);
    }
}

static bool ContainsNoCase(const char* s, const char* needle, size_t n) {
    for (; *s; ++s) {
        size_t i = 0;
        while (i < n && s[i] && tolower(static_cast<unsigned char>(s[i])) == tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

// Chinese names for systems and places, read once from data\\place_names_zh.txt ("key|name").
// Keys are entity names, localization body keys (Stanton1a, Stanton1_L1) or "system:<name>".
static std::unordered_map<std::string, std::string> g_zhPlaces;
static bool g_zhPlacesLoaded = false;

static std::string LowerKey(const char* s) {
    std::string k(s);
    for (char& c : k) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return k;
}

static const char* ZhLookup(const std::string& key) {
    if (!g_zhPlacesLoaded) {
        g_zhPlacesLoaded = true;
        char path[MAX_PATH];
        if (DataFilePath(path, sizeof(path), "place_names_zh.txt"))
            if (FILE* f = _fsopen(path, "r", _SH_DENYNO)) {
                char line[512];
                while (fgets(line, sizeof(line), f)) {
                    line[strcspn(line, "\r\n")] = 0;
                    char* sep = strchr(line, '|');
                    if (line[0] == '#' || !sep || sep == line || !sep[1]) continue;
                    *sep = 0;
                    g_zhPlaces[LowerKey(line)] = sep + 1;
                }
                fclose(f);
            }
    }
    const auto it = g_zhPlaces.find(key);
    return it == g_zhPlaces.end() ? nullptr : it->second.c_str();
}

static const char* ZhSystem(const char* system) {
    const char* zh = ZhLookup("system:" + LowerKey(system));
    return zh ? zh : system;
}

static const char* ZhPlace(const char* name, const char* entity) {
    if (const char* zh = ZhLookup(LowerKey(entity))) return zh;
    if (_strnicmp(entity, "OOC_", 4) == 0) {
        // OOC_Stanton_1a_Ariel -> Stanton1a, OOC_Stanton1_L1 -> Stanton1_L1
        const char* a = entity + 4;
        const char* b = strchr(a, '_');
        if (b) {
            const char* c = strchr(b + 1, '_');
            const std::string first(a, b), second(b + 1, c ? c : b + 1 + strlen(b + 1));
            if (const char* zh = ZhLookup(LowerKey((first + second).c_str()))) return zh;
            if (const char* zh = ZhLookup(LowerKey((first + "_" + second).c_str()))) return zh;
        }
    }
    return name;
}

static bool MatchesFilter(const char* name, const char* filter) {
    for (const char* p = filter; *p; ) {
        p += strspn(p, " _");
        const size_t n = strcspn(p, " _");
        if (n && !ContainsNoCase(name, p, n)) return false;
        p += n;
    }
    return true;
}

// =============================================================================================
// Look: dark Tegridy green. Soil-green behind everything, leaf green only on things you click,
// tractor yellow for warnings. An optional background image sits under a
// green wash so text stays readable.
// =============================================================================================

static ImVec4 Hex(uint32_t rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

static const ImVec4 kSoil      = Hex(0x0E1811);   // deepest green, under the image
static const ImVec4 kPanel     = Hex(0x172A1B);   // inputs, lists, status strip
static const ImVec4 kPanelUp   = Hex(0x1F3523);   // hover
static const ImVec4 kLine      = Hex(0x2F4B32);   // dividers, scrollbars
static const ImVec4 kText      = Hex(0xE6EFDD);
static const ImVec4 kMuted     = Hex(0x9FB59A);
static const ImVec4 kLeaf      = Hex(0x86C64B);   // accent
static const ImVec4 kLeafDeep  = Hex(0x3E6B27);

constexpr float kBodySize    = 17.0f;
constexpr float kHeadingSize = 20.0f;

static void ApplyTheme() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(16, 12);
    st.FramePadding = ImVec2(9, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(8, 4);
    st.ScrollbarSize = 11;
    st.WindowRounding = 0;
    st.ChildRounding = 4;
    st.FrameRounding = 4;
    st.PopupRounding = 4;
    st.GrabRounding = 4;
    st.TabRounding = 4;
    st.ScrollbarRounding = 4;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 0;
    st.TabBorderSize = 0;
    st.SeparatorTextBorderSize = 1;
    st.SeparatorTextPadding = ImVec2(0, 6);
    st.SeparatorTextAlign = ImVec2(0, 0.5f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kMuted;
    c[ImGuiCol_WindowBg]             = ImVec4(0, 0, 0, 0);          // the background is drawn underneath
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = Hex(0x132216, 0.98f);
    c[ImGuiCol_Border]               = kLine;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = Hex(0x172A1B, 0.88f);
    c[ImGuiCol_FrameBgHovered]       = Hex(0x1F3523, 0.95f);
    c[ImGuiCol_FrameBgActive]        = Hex(0x26412A, 1.0f);
    c[ImGuiCol_TitleBg]              = Hex(0x0B140D, 0.95f);
    c[ImGuiCol_TitleBgActive]        = Hex(0x0B140D, 0.95f);
    c[ImGuiCol_TitleBgCollapsed]     = Hex(0x0B140D, 0.95f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = kLine;
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x3C5E3F);
    c[ImGuiCol_ScrollbarGrabActive]  = kLeafDeep;
    c[ImGuiCol_CheckMark]            = kLeaf;
    c[ImGuiCol_SliderGrab]           = kLeafDeep;
    c[ImGuiCol_SliderGrabActive]     = kLeaf;
    c[ImGuiCol_Button]               = Hex(0x1F3523, 0.92f);
    c[ImGuiCol_ButtonHovered]        = Hex(0x2A4730);
    c[ImGuiCol_ButtonActive]         = kLeafDeep;
    c[ImGuiCol_Header]               = Hex(0x3E6B27, 0.55f);
    c[ImGuiCol_HeaderHovered]        = Hex(0x1F3523, 0.95f);
    c[ImGuiCol_HeaderActive]         = kLeafDeep;
    c[ImGuiCol_Separator]            = kLine;
    c[ImGuiCol_SeparatorHovered]     = kLeafDeep;
    c[ImGuiCol_SeparatorActive]      = kLeaf;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab]                  = Hex(0x0B140D, 0.70f);
    c[ImGuiCol_TabHovered]           = kPanelUp;
    c[ImGuiCol_TabSelected]          = Hex(0x172A1B, 0.95f);
    c[ImGuiCol_TabSelectedOverline]  = kLeaf;
    c[ImGuiCol_TabDimmed]            = Hex(0x0B140D, 0.70f);
    c[ImGuiCol_TabDimmedSelected]    = Hex(0x172A1B, 0.95f);
    c[ImGuiCol_TableHeaderBg]        = Hex(0x172A1B, 0.95f);
    c[ImGuiCol_TableBorderStrong]    = kLine;
    c[ImGuiCol_TableBorderLight]     = Hex(0x2F4B32, 0.6f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = Hex(0x172A1B, 0.35f);
    c[ImGuiCol_TextSelectedBg]       = Hex(0x3E6B27, 0.7f);
    c[ImGuiCol_NavCursor]            = kLeaf;
}

// Bahnschrift (DIN-style, ships with Windows 10+), then Segoe UI, then ImGui's own font.
static void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    char dir[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(dir, sizeof(dir));
    static const char* const kFonts[] = { "bahnschrift.ttf", "segoeui.ttf" };
    ImFont* font = nullptr;
    for (const char* name : kFonts) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\Fonts\\%s", dir, name);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        if ((font = io.Fonts->AddFontFromFileTTF(path, kBodySize)) != nullptr) break;
    }
    // Chinese glyphs: data\font_zh.ttf/.ttc if present, then the macOS system fonts through
    // Wine's Z: drive, then Windows' own. ImGui 1.92 rasterizes glyphs on demand, so no ranges.
    char zh[MAX_PATH] = "";
    const char* zhFonts[] = { nullptr, nullptr, "Z:\\System\\Library\\Fonts\\Hiragino Sans GB.ttc",
                              "Z:\\System\\Library\\Fonts\\STHeiti Medium.ttc", nullptr, nullptr };
    char own1[MAX_PATH], own2[MAX_PATH], win1[MAX_PATH], win2[MAX_PATH];
    if (DataFilePath(own1, sizeof(own1), "font_zh.ttf")) zhFonts[0] = own1;
    if (DataFilePath(own2, sizeof(own2), "font_zh.ttc")) zhFonts[1] = own2;
    snprintf(win1, sizeof(win1), "%s\\Fonts\\msyh.ttc", dir); zhFonts[4] = win1;
    snprintf(win2, sizeof(win2), "%s\\Fonts\\simhei.ttf", dir); zhFonts[5] = win2;
    for (const char* path : zhFonts) {
        if (!path || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.MergeMode = font != nullptr;
        if (ImFont* added = io.Fonts->AddFontFromFileTTF(path, kBodySize, &cfg)) {
            if (!font) font = added;
            strncpy_s(zh, path, _TRUNCATE);
            break;
        }
    }
    if (zh[0]) Log("[menu] Chinese font: %s", zh);
    else Log("[menu] no Chinese font found; Chinese text will show as '?'");
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = font ? kBodySize : 13.0f;
    st.FontScaleMain = font ? 1.0f : 1.3f;
}

// --- background image: data\menu_background.png / .jpg, decoded with Windows' own WIC ---------

static ID3D11ShaderResourceView* g_bgSrv = nullptr;
static UINT  g_bgW = 0, g_bgH = 0;
static bool  g_bgShow = true;
static int   g_bgDarkness = 74;       // percent of the green wash over the image
static float g_bgPosition = 0.5f;     // which part of a wide image shows in the tall menu
static char  g_bgPath[MAX_PATH] = "";

static bool DecodeImage(const char* file, std::vector<uint8_t>& pixels, UINT& w, UINT& h) {
    wchar_t wide[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, file, -1, wide, MAX_PATH)) return false;
    IWICImagingFactory*    factory = nullptr;
    IWICBitmapDecoder*     decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter*   rgba = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
           && SUCCEEDED(factory->CreateDecoderFromFilename(wide, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
           && SUCCEEDED(decoder->GetFrame(0, &frame))
           && SUCCEEDED(factory->CreateFormatConverter(&rgba))
           && SUCCEEDED(rgba->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
           && SUCCEEDED(rgba->GetSize(&w, &h))
           && w > 0 && h > 0 && w <= 8192 && h <= 8192;
    if (ok) {
        pixels.resize(static_cast<size_t>(w) * h * 4);
        ok = SUCCEEDED(rgba->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    }
    if (rgba) rgba->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return ok;
}

static void LoadBackground() {
    static const char* const kNames[] = { "menu_background.png", "menu_background.jpg", "menu_background.jpeg" };
    for (const char* name : kNames) {
        char path[MAX_PATH];
        if (!DataFilePath(path, sizeof(path), name)) return;
        if (!g_bgPath[0]) strcpy_s(g_bgPath, path);           // shown in the Menu tab if nothing loads
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        std::vector<uint8_t> pixels;
        UINT w = 0, h = 0;
        if (!DecodeImage(path, pixels, w, h)) { Log("[menu] couldn't read %s", path); continue; }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA init = { pixels.data(), w * 4, 0 };
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&desc, &init, &tex)) && tex) {
            g_device->CreateShaderResourceView(tex, nullptr, &g_bgSrv);
            tex->Release();
        }
        if (g_bgSrv) {
            g_bgW = w;
            g_bgH = h;
            strcpy_s(g_bgPath, path);
            Log("[menu] background: %s (%ux%u)", path, w, h);
        }
        return;
    }
}

static void DrawBackdrop() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), size, ImGui::GetColorU32(kSoil));
    if (!g_bgSrv || !g_bgShow || size.x <= 0 || size.y <= 0) return;
    // Cover the window, cropping whichever direction overflows.
    const float imageAspect = static_cast<float>(g_bgW) / static_cast<float>(g_bgH);
    const float viewAspect = size.x / size.y;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (imageAspect > viewAspect) {
        const float visible = viewAspect / imageAspect;
        uv0.x = g_bgPosition * (1 - visible);
        uv1.x = uv0.x + visible;
    } else {
        const float visible = imageAspect / viewAspect;
        uv0.y = (1 - visible) * 0.5f;
        uv1.y = uv0.y + visible;
    }
    dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_bgSrv)), ImVec2(0, 0), size, uv0, uv1);
    dl->AddRectFilled(ImVec2(0, 0), size, ImGui::GetColorU32(ImVec4(kSoil.x, kSoil.y, kSoil.z, g_bgDarkness / 100.0f)));
}

// --- small building blocks ------------------------------------------------------------------

static void Section(const char* title) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushFont(nullptr, kHeadingSize);
    ImGui::SeparatorText(title);
    ImGui::PopFont();
}

static void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static bool PrimaryButton(const char* label, float height = 36.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button, kLeafDeep);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hex(0x4F8732));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kLeaf);
    const bool pressed = ImGui::Button(label, ImVec2(-1, height));
    ImGui::PopStyleColor(3);
    return pressed;
}

static float Columns(int n) {
    return (ImGui::GetContentRegionAvail().x - (n - 1) * ImGui::GetStyle().ItemSpacing.x) / n;
}

static bool SearchBox(const char* id, const char* hint, char* buf, size_t n) {
    ImGui::SetNextItemWidth(-1);
    return ImGui::InputTextWithHint(id, hint, buf, n);
}

static void PrettyBuildName(char* out, size_t n, const char* name) {
    static const char* const kPrefixes[] = { "PlayerDeco_", "Turret_Automated_", "BaseBuilding_Interactables_", "BaseBuilding_",
                                             "PU_Human_Enemy_GroundCombat_NPC_", "PU_Human-", "NPC_Archetypes-", "AIShip_CrewProfiles-" };
    if (const char* slash = strrchr(name, '/')) name = slash + 1;
    for (const char* p : kPrefixes)
        if (_strnicmp(name, p, strlen(p)) == 0) { name += strlen(p); break; }
    strncpy_s(out, n, name, _TRUNCATE);
    if (char* ext = strstr(out, ".socpak")) *ext = 0;
    if (char* guid = strstr(out, "_{")) {
        char tag[8];
        snprintf(tag, sizeof(tag), " (%.4s)", guid + 2);
        *guid = 0;
        strncat_s(out, n, tag, _TRUNCATE);
    }
    for (char* c = out; *c; ++c) if (*c == '_' || *c == '-') *c = ' ';
}

// --- NPC picker, shared by the NPCs and Crew tabs -------------------------------------------

static int g_npcPick = 0;

static bool NpcPicker() {
    static char filter[64] = "";
    const int npcs = Menu_NpcCount();
    if (npcs < 0) { Hint("正在加载 NPC（需要先进入游戏世界）..."); return false; }
    if (npcs == 0) { Hint("没有找到 NPC，请检查 data\\npcs.txt。"); return false; }
    if (g_npcPick >= npcs) g_npcPick = 0;
    if (SearchBox("##npcFilter", "搜索 NPC", filter, sizeof(filter)))
        for (int i = 0; i < npcs; ++i)
            if (MatchesFilter(Menu_NpcName(i), filter)) { g_npcPick = i; break; }
    char preview[128];
    PrettyBuildName(preview, sizeof(preview), Menu_NpcName(g_npcPick));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##npc", preview, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < npcs; ++i) {
            const char* name = Menu_NpcName(i);
            if (!MatchesFilter(name, filter)) continue;
            char label[160];
            PrettyBuildName(label, sizeof(label) - 16, name);
            snprintf(label + strlen(label), 16, "##n%d", i);
            if (ImGui::Selectable(label, i == g_npcPick)) g_npcPick = i;
            ImGui::SetItemTooltip("%s", name);
            if (i == g_npcPick) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return true;
}

// =============================================================================================
// Player
// =============================================================================================

static void GearCombo(int slot, const char* label, const char* none, int& pick, const char* filter, float width) {
    const int n = Menu_GearCount(slot);
    if (pick >= n) pick = -1;
    char preview[112], id[32];
    snprintf(preview, sizeof(preview), "%s: %s", label, pick >= 0 ? Menu_GearName(slot, pick) : none);
    snprintf(id, sizeof(id), "##gear%d", slot);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) return;
    if (ImGui::Selectable(none, pick < 0)) pick = -1;
    for (int i = 0; i < n; ++i) {
        const char* name = Menu_GearName(slot, i);
        if (!MatchesFilter(name, filter)) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(name, i == pick)) pick = i;
        if (i == pick) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

static void DrawPlayerTab(bool& keepOpen) {
    Section("移动");
    static bool  noclip = false;
    static float speed = 30.0f;
    if (ImGui::Checkbox("穿墙飞行", &noclip)) Menu_SetNoclip(noclip, speed);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##noclipSpeed", &speed, 1.0f, 500.0f, "速度 %.0f", ImGuiSliderFlags_Logarithmic))
        Menu_SetNoclipSpeed(speed);
    Hint("F7 记录当前位置，F8 传送回去。“传送”标签页里还有命名地点和各类目的地。");

    Section("保护");
    static bool god = true, ammo = false;
    if (ImGui::Checkbox("无敌模式", &god)) Menu_SetGodMode(god);
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("无限弹药", &ammo)) Menu_SetInfiniteAmmo(ammo);

    Section("装备");
    static int  gear[Gear_SlotCount] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    static char gearFilter[64] = "";
    static const struct { const char* label; const char* none; } kGear[Gear_SlotCount] = {
        { "内衬服", "默认" }, { "头盔", "无" }, { "躯干护甲", "无" }, { "手臂护甲", "无" }, { "腿部护甲", "无" },
        { "背包", "无" }, { "主武器", "无" }, { "副武器", "无" }, { "弹药", "与武器匹配" }, { "手雷", "无" } };
    if (Menu_GearCount(0) < 0) { Hint("正在加载装备（需要先进入游戏世界）..."); return; }
    SearchBox("##gearFilter", "搜索装备", gearFilter, sizeof(gearFilter));
    const float half = Columns(2);
    for (int s = 0; s < Gear_SlotCount; ++s) {
        if (s % 2) ImGui::SameLine();
        GearCombo(s, kGear[s].label, kGear[s].none, gear[s], gearFilter, half);
    }
    if (PrimaryButton("穿戴装备")) { Menu_RequestEquip(gear); keepOpen = false; }
}

// =============================================================================================
// Travel
// =============================================================================================

static bool HasText(const char* filter) { return filter[strspn(filter, " _")] != 0; }

static void DrawTravelTab() {
    static TravelPlace    places[3000];
    static int            order[3000];
    static TravelBookmark marks[400];
    static char  filter[64] = "";
    static char  markName[64] = "";
    static char  selected[96] = "";       // entity name of the selected place
    static float altitude = 2000.0f;
    static bool  showMinor = false;

    static int np = 0, placesVersion = -1;
    char here[32];
    Travel_CurrentSystem(here, sizeof(here));
    const int version = Travel_PlacesVersion();
    const bool placesChanged = version != placesVersion;
    if (placesChanged) { placesVersion = version; np = Travel_GetPlaces(places, 3000); }
    const int nm = Travel_GetBookmarks(marks, 400);
    const bool searching = HasText(filter);

    // Systems that have anything in them; the one you're in first, then A-Z.
    const char* systems[48];
    int ns = 0;
    auto addSystem = [&](const char* sys) {
        if (!sys[0]) return;
        for (int i = 0; i < ns; ++i) if (_stricmp(systems[i], sys) == 0) return;
        if (ns < 48) systems[ns++] = sys;
    };
    if (here[0]) addSystem(here);
    for (int i = 0; i < np; ++i) addSystem(places[i].system);
    for (int i = 0; i < nm; ++i) addSystem(marks[i].system);
    std::sort(systems + (here[0] ? 1 : 0), systems + ns, [](const char* a, const char* b) { return _stricmp(a, b) < 0; });

    if (placesChanged) {                           // OOC_Stanton_1, 1a, 1b, 2 ... reads in orbit order
        for (int i = 0; i < np; ++i) order[i] = i;
        std::sort(order, order + np, [&](int a, int b) { return _stricmp(places[a].entity, places[b].entity) < 0; });
    }

    SearchBox("##travelFilter", "搜索地点和已保存的位置", filter, sizeof(filter));

    Section("地点");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##altitude", &altitude, 100.0f, 20000.0f, "到达时离地 %.0f 米", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("显示室内和小区域", &showMinor);
    ImGui::SetItemTooltip("电梯大厅、机库、小行星带分段等，默认隐藏。");
    const TravelPlace* pick = nullptr;
    bool go = false;
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (p.kind == Place_Minor && !showMinor) continue;
            if (_stricmp(p.system, systems[s]) == 0 && (!searching || MatchesFilter(p.name, filter) || MatchesFilter(p.entity, filter)
                                                        || MatchesFilter(ZhPlace(p.name, p.entity), filter))) ++shown;
        }
        if (!shown) continue;
        const bool isHere = here[0] && _stricmp(systems[s], here) == 0;
        char header[200];
        const bool unnamed = _strnicmp(systems[s], "SolarSystem", 11) == 0;
        snprintf(header, sizeof(header), "%s%s (%d)###sys_%s", unnamed ? "未命名星系" : ZhSystem(systems[s]),
                 isHere ? "，你在这里" : "", shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::CollapsingHeader(header, isHere ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        char table[48];
        snprintf(table, sizeof(table), "##places_%s", systems[s]);
        const float rows = static_cast<float>(shown < 10 ? shown : 10);
        const float height = (rows + 1) * ImGui::GetFrameHeight() + 4;
        if (!ImGui::BeginTable(table, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, height))) continue;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("地点", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableHeadersRow();
        for (int k = 0; k < np; ++k) {
            const TravelPlace& p = places[order[k]];
            if (_stricmp(p.system, systems[s]) != 0 || (p.kind == Place_Minor && !showMinor)) continue;
            if (searching && !MatchesFilter(p.name, filter) && !MatchesFilter(p.entity, filter)
                && !MatchesFilter(ZhPlace(p.name, p.entity), filter)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[256];
            snprintf(label, sizeof(label), "%s##%s", ZhPlace(p.name, p.entity), p.entity);
            const bool isSel = _stricmp(selected, p.entity) == 0;
            if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                strcpy_s(selected, p.entity);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { pick = &p; go = true; }
            }
            ImGui::SetItemTooltip("%s\n%s", p.name, p.entity);
            ImGui::TableNextColumn();
            static const char* const kKind[] = { "行星", "卫星", "地点", "室内" };
            const int kindIdx = p.kind >= 0 && p.kind <= 3 ? p.kind : 2;
            ImGui::TextDisabled("%s%s", kKind[kindIdx], kindIdx <= Place_Moon && p.radius <= 0 ? "，轨道" : "");
        }
        ImGui::EndTable();
    }
    if (!pick)
        for (int i = 0; i < np; ++i)
            if (_stricmp(places[i].entity, selected) == 0) { pick = &places[i]; break; }
    char goLabel[240];
    snprintf(goLabel, sizeof(goLabel), pick ? "前往 %s" : "选择一个目的地", pick ? ZhPlace(pick->name, pick->entity) : "");
    ImGui::BeginDisabled(!pick);
    if (PrimaryButton(goLabel)) go = true;
    ImGui::EndDisabled();
    if (go && pick) {
        TravelPlace request = *pick;   // the name only shows in the status line
        strncpy_s(request.name, ZhPlace(pick->name, pick->entity), _TRUNCATE);
        Travel_RequestPlace(request, altitude);
    }

    float progress = 0;
    if (Travel_Scanning(progress)) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kLeafDeep);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), "正在扫描...");
        ImGui::PopStyleColor();
    } else if (ImGui::Button("扫描游戏中的地点", ImVec2(-1, 0))) {
        Travel_RequestScan();
    }
    Hint("扫描会找出所有已加载星系中的行星、卫星、空间站、拉格朗日点、通讯阵列和跳跃点，"
         "并添加到这里。只读取不修改，需要几秒钟。");

    Section("已保存的位置");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160 - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##markName", "给这个位置起个名字", markName, sizeof(markName));
    ImGui::SameLine();
    if (ImGui::Button("保存当前位置", ImVec2(160, 0))) { Travel_RequestSaveBookmark(markName); markName[0] = 0; }
    if (!nm) Hint("还没有保存任何位置。站到想去的地方，起个名字，再点“保存当前位置”。");
    for (int s = 0; s < ns; ++s) {
        int shown = 0;
        for (int i = 0; i < nm; ++i)
            if (_stricmp(marks[i].system, systems[s]) == 0 && (!searching || MatchesFilter(marks[i].name, filter))) ++shown;
        if (!shown) continue;
        char node[200];
        snprintf(node, sizeof(node), "%s (%d)###marks_%s", ZhSystem(systems[s]), shown, systems[s]);
        if (searching) ImGui::SetNextItemOpen(true);
        if (!ImGui::TreeNodeEx(node, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        for (int i = 0; i < nm; ++i) {
            if (_stricmp(marks[i].system, systems[s]) != 0 || (searching && !MatchesFilter(marks[i].name, filter))) continue;
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(marks[i].name);
            const float buttons = 70 + 80 + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
            if (ImGui::Button("前往", ImVec2(70, 0))) Travel_RequestBookmark(i);
            ImGui::SameLine();
            if (ImGui::Button("删除", ImVec2(80, 0))) Travel_RequestDeleteBookmark(i);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    Hint("F7/F8 仍可作为快速存档位使用。列表中的地点可以跨星系传送（实验功能，需用 PU_All 启动）；F8 和已保存的位置仍只能在当前星系内使用。");
}

// =============================================================================================
// Vehicles
// =============================================================================================

static void DrawVehiclesTab(bool& keepOpen) {
    static int  selected = 0;
    static char filter[64] = "";
    static MenuSpawnOptions opt;

    Section("生成飞船");
    const int count = Menu_ShipCount();
    if (count < 0) {
        Hint("正在加载飞船列表（需要先进入游戏世界）...");
    } else if (count == 0) {
        Hint("没有找到飞船，请检查 data\\ships.txt。");
    } else {
        const MenuShip* ships = Menu_Ships();
        if (selected >= count) selected = 0;
        if (SearchBox("##shipFilter", "搜索飞船（中文名或代号）", filter, sizeof(filter)))
            for (int i = 0; i < count; ++i)
                if (MatchesFilter(ships[i].display, filter) || MatchesFilter(ships[i].name, filter)) { selected = i; break; }
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                    | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##ships", 3, flags, ImVec2(0, 230))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("飞船", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("尺寸", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableSetupColumn("长度", ImGuiTableColumnFlags_WidthFixed, 64);
            ImGui::TableHeadersRow();
            for (int i = 0; i < count; ++i) {
                if (!MatchesFilter(ships[i].display, filter) && !MatchesFilter(ships[i].name, filter)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char label[240];
                snprintf(label, sizeof(label), "%s##s%d", ships[i].display, i);
                if (ImGui::Selectable(label, i == selected, ImGuiSelectableFlags_SpanAllColumns)) selected = i;
                if (strcmp(ships[i].display, ships[i].name) != 0) ImGui::SetItemTooltip("%s", ships[i].name);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", ships[i].size);
                ImGui::TableNextColumn();
                if (ships[i].length > 0) ImGui::TextDisabled("%.0f m", ships[i].length);
            }
            ImGui::EndTable();
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##height", &opt.height, 0.0f, 500.0f, "在你上方 %.0f 米处生成");
        static const char* const kBoard[] = { "不登船", "登船并坐上驾驶座", "按名称选择座位登船", "生成后再选座位" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##board", &opt.seatMode, kBoard, 4);
        if (opt.seatMode == SeatMode_Named) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##seatName", "座位名称，例如 copilot 或 turret left", opt.seatName, sizeof(opt.seatName));
            ImGui::SetItemTooltip("每个词都必须出现在座位名称里。“船员”标签页可以查看飞船的座位名称。");
        }
        const bool boarding = opt.seatMode == SeatMode_Pilot || opt.seatMode == SeatMode_Named;
        ImGui::BeginDisabled(!boarding);
        ImGui::Checkbox("移除我座位上的 NPC", &opt.replaceNpc);
        ImGui::SameLine(0, 24);
        ImGui::Checkbox("启动电源", &opt.flightReady);
        ImGui::SetItemTooltip("坐上驾驶座后自动启动飞船电源。");
        ImGui::EndDisabled();

        char spawn[240];
        snprintf(spawn, sizeof(spawn), "生成 %s", ships[selected].display);
        if (PrimaryButton(spawn)) {
            MenuSpawnOptions send = opt;
            if (!boarding) send.flightReady = false;
            Menu_RequestSpawn(selected, send);
            if (opt.seatMode != SeatMode_PickLater) keepOpen = false;
        }
    }

    Section("当前飞船");
    static bool shipAmmo = false;
    if (ImGui::Checkbox("飞船无限弹药", &shipAmmo)) Menu_SetInfiniteShipAmmo(shipAmmo);
    ImGui::SetItemTooltip("为你所在的飞船以及“船员”标签页中选中的飞船补满弹药。");
    if (ImGui::Button("电源开 / 关", ImVec2(-1, 0))) Menu_RequestFlightReady();
    Hint("切换“船员”标签页中飞船的飞行就绪状态，相当于在驾驶座按 R。");
}

// =============================================================================================
// Crew
// =============================================================================================

// "VNCL_Mauler_Gunner_Seat_200006242347" on a VNCL_Mauler -> "Gunner Seat 3"
static void PrettySeatNames(const MenuSeat* seats, int n, const char* ship, char (*out)[64]) {
    static char base[128][64];
    for (int i = 0; i < n; ++i) {
        char name[64];
        strcpy_s(name, seats[i].name);
        if (char* cut = strrchr(name, '_'); cut && cut[1] && strspn(cut + 1, "0123456789") == strlen(cut + 1)) *cut = 0;
        const char* a = name;          // drop the leading words the seat shares with the ship's name
        const char* b = ship;
        for (;;) {
            const size_t la = strcspn(a, "_"), lb = strcspn(b, "_ ");
            if (!la || la != lb || _strnicmp(a, b, la) != 0 || !a[la]) break;
            a += la + 1;
            b += lb + (b[lb] ? 1 : 0);
        }
        strcpy_s(base[i], a);
        for (char* c = base[i]; *c; ++c) if (*c == '_') *c = ' ';
    }
    for (int i = 0; i < n; ++i) {      // number repeats: Gunner Seat 1, Gunner Seat 2, ...
        int total = 0, before = 0;
        for (int j = 0; j < n; ++j)
            if (_stricmp(base[i], base[j]) == 0) { ++total; if (j < i) ++before; }
        if (total > 1) snprintf(out[i], 64, "%s %d", base[i], before + 1);
        else strcpy_s(out[i], 64, base[i]);
    }
}

static void DrawCrewTab() {
    if (!Menu_SeatControlAvailable()) {
        Section("船员");
        Hint("当前游戏版本不支持座位控制，详情见 mod.log。");
        return;
    }
    static MenuSeat seats[128];
    static char pretty[128][64];
    static unsigned long long selectedSeat = 0;
    static bool replace = true;
    char ship[64] = "";
    const int count = Menu_GetSeats(seats, 128, ship, sizeof(ship));

    Section("飞船");
    ImGui::TextUnformatted(count < 0 ? "未选择飞船" : ship);
    ImGui::SameLine();
    const float button = 190;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button);
    if (ImGui::Button("使用我所在的飞船", ImVec2(button, 0))) Menu_TargetShipImIn();
    if (count < 0) { Hint("先生成一艘飞船，或登上一艘后点“使用我所在的飞船”。"); return; }
    if (count == 0) { Hint("正在等待飞船加载..."); return; }

    Section("座位");
    PrettySeatNames(seats, count, ship, pretty);
    int sel = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##seats", 2, flags, ImVec2(0, 250))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("座位", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("乘员", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();
        for (int i = 0; i < count; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[96];
            snprintf(label, sizeof(label), "%s##seat%llu", pretty[i], seats[i].id);
            if (ImGui::Selectable(label, seats[i].id == selectedSeat, ImGuiSelectableFlags_SpanAllColumns)) selectedSeat = seats[i].id;
            ImGui::SetItemTooltip("%s", seats[i].name);
            if (seats[i].id == selectedSeat) sel = i;
            ImGui::TableNextColumn();
            switch (seats[i].state) {
            case SeatState_You:   ImGui::TextColored(kLeaf, "你"); break;
            case SeatState_Npc:   ImGui::TextUnformatted("NPC"); break;
            case SeatState_Taken: ImGui::TextDisabled("未知"); break;
            default:              ImGui::TextDisabled("空"); break;
            }
        }
        ImGui::EndTable();
    }

    const int state = sel >= 0 ? seats[sel].state : -1;
    const float quarter = Columns(4);
    ImGui::BeginDisabled(sel < 0 || state == SeatState_You);
    if (ImGui::Button("坐这里", ImVec2(quarter, 0))) Menu_RequestSit(seats[sel].id, replace);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Npc && state != SeatState_You);
    if (ImGui::Button("起身", ImVec2(quarter, 0))) Menu_RequestStandUp(seats[sel].id);
    ImGui::SetItemTooltip("让座位上的人起身，但留在船上。");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Npc);
    if (ImGui::Button("移除 NPC", ImVec2(quarter, 0))) Menu_RequestKick(seats[sel].id);
    ImGui::SetItemTooltip("把这个 NPC 从游戏中移除。");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != SeatState_Empty);
    if (ImGui::Button("添加 NPC", ImVec2(quarter, 0))) Menu_RequestAddCrew(seats[sel].id, g_npcPick);
    ImGui::EndDisabled();
    ImGui::Checkbox("如果选中的座位上有 NPC，先移除它", &replace);

    Section("船员");
    Hint("要安排到座位上的 NPC：");
    const bool haveNpcs = NpcPicker();
    const float third = Columns(3);
    ImGui::BeginDisabled(!haveNpcs);
    if (ImGui::Button("填满空座位", ImVec2(third, 0))) Menu_RequestFillCrew(g_npcPick);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("所有 NPC 起身", ImVec2(third, 0))) Menu_RequestStandAll();
    ImGui::SameLine();
    if (ImGui::Button("移除所有 NPC", ImVec2(third, 0))) Menu_RequestClearCrew();
    Hint("添加的 NPC 只会坐在座位上，暂时不会驾驶飞船或操作炮塔。");
}

// =============================================================================================
// NPCs
// =============================================================================================

static void DrawNpcsTab(bool& keepOpen) {
    Section("生成 NPC");
    if (!NpcPicker()) return;
    static int howMany = 1;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##howMany", &howMany, 1, 10, howMany == 1 ? "1 个 NPC" : "%d 个 NPC");
    if (PrimaryButton("在我面前生成")) { Menu_RequestNpc(g_npcPick, howMany); keepOpen = false; }
    if (ImGui::Button("移除已生成的 NPC", ImVec2(-1, 0))) Menu_RequestClearNpcs();
    Hint("移除这个菜单生成的所有 NPC，包括船员。");
}

// =============================================================================================
// Build
// =============================================================================================

static void DrawBuildTab(bool& keepOpen) {
    static int  build = 0, buildTab = 0;
    static char buildFilter[64] = "";
    const int buildables = Menu_BuildCount();
    Section("物体");
    if (buildables < 0) { Hint("正在加载建造物体（需要先进入游戏世界）..."); return; }
    if (buildables == 0) { Hint("没有找到建造物体，请检查 data\\buildables.txt。"); return; }
    if (build >= buildables) build = 0;
    if (ImGui::BeginTabBar("##buildTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int c = 0; c < Menu_BuildCategoryCount(); ++c) {
            char tab[40];
            snprintf(tab, sizeof(tab), "%s##cat%d", Menu_BuildCategoryName(c), c);
            if (tab[0] >= 'a' && tab[0] <= 'z') tab[0] -= 'a' - 'A';
            if (ImGui::BeginTabItem(tab)) { buildTab = c; ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    SearchBox("##buildFilter", "搜索所有物体", buildFilter, sizeof(buildFilter));
    const bool searching = buildFilter[strspn(buildFilter, " _")] != 0;
    if (ImGui::BeginChild("##buildList", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < buildables; ++i) {
            const char* name = Menu_BuildName(i);
            if (searching ? !MatchesFilter(name, buildFilter) : Menu_BuildCategoryOf(i) != buildTab) continue;
            char label[160];
            PrettyBuildName(label, sizeof(label) - 16, name);
            if (searching) {
                char tagged[160];
                snprintf(tagged, sizeof(tagged), "%s   (%s)", label, Menu_BuildCategory(i));
                strcpy_s(label, sizeof(label) - 16, tagged);
            }
            snprintf(label + strlen(label), 16, "##%d", i);
            if (ImGui::Selectable(label, i == build)) build = i;
            ImGui::SetItemTooltip("%s", name);
        }
    }
    ImGui::EndChild();

    Section("放置");
    char picked[128];
    PrettyBuildName(picked, sizeof(picked), Menu_BuildName(build));
    ImGui::Text("已选择：%s", picked);
    float reach = Menu_BuildReach();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##reach", &reach, 5.0f, 300.0f, "放置距离 %.0f 米");
    ImGui::SetItemTooltip("物体放在前方多远处：落在你视线所指的地面上，或这个距离处正下方的地面上。");
    Menu_SetBuild(build, reach);
    const bool building = Menu_BuildModeActive();
    if (PrimaryButton(building ? "停止建造（F6）" : "开始建造（F6）")) {
        Menu_ToggleBuildMode();
        if (!building) keepOpen = false;
    }
    const float half = Columns(2);
    if (ImGui::Button("撤销上一步", ImVec2(half, 0))) Menu_BuildUndo();
    ImGui::SameLine();
    char clearLabel[48];
    snprintf(clearLabel, sizeof(clearLabel), "清空基地（%d）###clearBase", Menu_BuildPlacedCount());
    if (ImGui::Button(clearLabel, ImVec2(half, 0))) Menu_BuildClear();
    Hint("建造时：左键放置，R 旋转，[ 和 ] 调整距离，Backspace 撤销，F6 停止。");
}

// =============================================================================================
// Menu settings
// =============================================================================================

static void DrawMenuTab() {
    Section("背景");
    if (g_bgSrv) {
        ImGui::Checkbox("显示背景图片", &g_bgShow);
        ImGui::BeginDisabled(!g_bgShow);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##dark", &g_bgDarkness, 20, 95, "暗度 %d%%");
        if (static_cast<float>(g_bgW) / g_bgH > 0.7f) {
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##pos", &g_bgPosition, 0.0f, 1.0f, "图片位置");
            ImGui::SetItemTooltip("菜单比图片宽度更高，拖动滑块选择显示图片的哪一部分。");
        }
        ImGui::EndDisabled();
        Hint(g_bgPath);
    } else {
        Hint("要使用背景图片，请把它保存为下面位置的 menu_background.png（或 .jpg），然后重启游戏：");
        ImGui::TextWrapped("%s", g_bgPath[0] ? g_bgPath : "data\\menu_background.png");
    }

    Section("关于");
    // SCO_BASED_ON in Chinese.
    Hint(SCO_TITLE " 仍在开发中，基于 ChrisWareOffline 0.9.0-rc1。");
    Hint("问题反馈：github.com/scubamount/sc-offline/issues");
    Hint("中文版问题：github.com/dazi2011/sc-offline/issues");
}

// =============================================================================================
// Window
// =============================================================================================

static void DrawStatusStrip(float height) {
    char status[768];
    Menu_GetStatus(status, sizeof(status));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Hex(0x0B140D, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
    ImGui::BeginChild("##status", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 4, p.y + height), ImGui::GetColorU32(kLeaf));
    ImGui::TextWrapped("%s", status);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Set by the Squadron 42 spoiler gate's Back button; DrawMenu selects the first tab once.
static bool g_backToFirstTab = false;

static void DrawSq42Tab(bool& keepOpen) {
    static bool spoilerOk = false;
    if (!spoilerOk) {
        ImGui::SeparatorText("剧透警告");
        ImGui::TextWrapped("这个标签页可能含有《42 中队》的剧透。");
        ImGui::TextWrapped("点“确定”继续。");
        if (ImGui::Button("确定", ImVec2(120, 0))) spoilerOk = true;
        ImGui::SameLine();
        if (ImGui::Button("返回", ImVec2(120, 0))) g_backToFirstTab = true;
        return;
    }

    ImGui::SeparatorText("服装");
    const int outfits = Menu_OutfitCount();
    static int outfit = 0;
    if (outfits < 0) {
        ImGui::TextWrapped("正在加载服装...（需要先进入游戏世界）");
    } else if (outfits == 0) {
        ImGui::TextWrapped("没有找到服装，请检查 outfits.txt。");
    } else {
        static char filter[64] = "";
        if (outfit >= outfits) outfit = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##outfitFilter", "搜索服装...", filter, sizeof(filter));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##outfit", Menu_OutfitName(outfit), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < outfits; ++i) {
                const char* name = Menu_OutfitName(i);
                if (!MatchesFilter(name, filter)) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(name, i == outfit)) outfit = i;
                if (i == outfit) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("穿上 SQ42 服装", ImVec2(-1, 0))) {
            Menu_RequestWearOutfit(outfit);
            keepOpen = false;
        }
    }
    static bool visor = false;
    if (ImGui::Checkbox("SQ42 面罩 HUD（下次穿戴装备或服装时生效）", &visor))
        Menu_SetS42VisorHud(visor);

    ImGui::SeparatorText("设置");
    for (int i = 0; i < Menu_S42SettingCount(); ++i) {
        bool on = Menu_S42SettingOn(i);
        ImGui::PushID(i);
        const bool known = Menu_S42SettingKnown(i);   // greyed until the game thread has read the cvar
        ImGui::BeginDisabled(!known);
        if (ImGui::Checkbox(Menu_S42SettingLabel(i), &on)) Menu_RequestS42Setting(i, on);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", known ? Menu_S42SettingTip(i) : "Reading this setting from the game...");
        ImGui::PopID();
    }

    ImGui::SeparatorText("生成");
    {
        static int   thing = -1;
        static char  thingFilter[64] = "";
        static bool  inFront = true;
        static float ahead = 8.0f;
        const int buildables = Menu_BuildCount();
        if (buildables < 0) {
            ImGui::TextWrapped("正在加载...（需要先进入游戏世界）");
        } else if (buildables == 0) {
            ImGui::TextWrapped("没有找到可建造物体，请检查 buildables.txt。");
        } else {
            if (thing < 0) {
                thing = 0;
                for (int i = 0; i < buildables; ++i)
                    if (_stricmp(Menu_BuildCategory(i), "sq42") == 0) { thing = i; break; }
            }
            if (thing >= buildables) thing = 0;
            ImGui::Checkbox("在你面前生成", &inFront);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##thingFilter", "搜索 [sq42] 分组...", thingFilter, sizeof(thingFilter));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##sq42thing", Menu_BuildName(thing), ImGuiComboFlags_HeightLargest)) {
                for (int i = 0; i < buildables; ++i) {
                    // Only the [sq42] group, like the original; the text filter narrows it further.
                    if (_stricmp(Menu_BuildCategory(i), "sq42") != 0) continue;
                    const char* name = Menu_BuildName(i);
                    if (!MatchesFilter(name, thingFilter)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name, i == thing)) thing = i;
                    if (i == thing) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (inFront) {
                ImGui::SetNextItemWidth(200);
                ImGui::SliderFloat("前方距离（米）", &ahead, 1.0f, 50.0f, "%.0f");
            }
            if (ImGui::Button("生成它", ImVec2(-1, 0))) {
                Menu_RequestPlace(thing, inFront, ahead);
                keepOpen = false;
            }
            ImGui::SetItemTooltip("建造部分的“撤销”和“清空基地”也会移除这些物体。");
        }
    }

    ImGui::SeparatorText("飞船");
    struct Entry { const char* label; const char* cls; bool enemyWing; float height; bool sit; };
    static const struct { const char* label; const char* cls; } kSq42Ships[] = {
        { "伊德里斯-P（斯坦顿号同级舰）", "AEGS_Idris_P" },
        { "角斗士（SQ42 战斗机）",        "AEGS_Gladius" },
        { "报复者（带 S42 HUD）",   "AEGS_Retaliator" },
        { "星际远航者（第 5、7、9 章）",        "MISC_Starfarer" },
        { "复仇者潜行者（S42 残骸）",   "AEGS_Avenger_Stalker" },
        { "大黄蜂（卡尔·梅森的座驾）",     "ANVL_Hornet_F7C" },
        { "范杜尔刀锋（AI）",            "VNCL_Blade_PU_AI_VAN" },
        { "范杜尔镰刀（AI）",           "VNCL_Scythe_PU_AI_VAN" },
        { "范杜尔长柄刀（AI）",           "VNCL_Glaive_PU_AI_VAN" },
        { "范杜尔毒刺（AI）",          "VNCL_Stinger_PU_AI_VAN" },
    };
    Entry list[16];
    static_assert(sizeof(kSq42Ships) / sizeof(kSq42Ships[0]) + 2 <= sizeof(list) / sizeof(list[0]),
                  "SQ42 ship table outgrew Entry list[]");
    int   n = 0;
    for (size_t i = 0; i < sizeof(kSq42Ships) / sizeof(kSq42Ships[0]); ++i)
        list[n++] = { kSq42Ships[i].label, kSq42Ships[i].cls, false, 0.0f, true };
    for (int i = 0; i < n; ++i)
        if (strncmp(list[i].cls, "VNCL_", 5) == 0) { list[i].height = 300.0f; list[i].sit = false; }

    // Not the original's dormant "[battle]" mode (two Bengals fighting each other): these
    // spawn one UEE Bengal, the second with a Vanduul wing when an enemy side was found.
    static char bengalWing[64];
    const bool enemySide = Menu_EnemySideAvailable();
    strcpy_s(bengalWing, enemySide ? "孟加拉 + 范杜尔编队" : "孟加拉 + 编队（UEE，未找到敌方阵营）");
    list[n++] = { "孟加拉（UEE）", "RSI_Bengal_PU_AI_UEE", false,      1500.0f, false };
    list[n++] = { bengalWing,     "RSI_Bengal_PU_AI_UEE", enemySide, 1500.0f, false };

    static int   pick = 0;
    static char  sqFilter[64] = "";
    static float height = 30.0f;   // the original's fixed spawn height for your own ships
    static bool  sit = true;
    if (pick >= n) pick = 0;
    // Greys out classes this game build doesn't have, like the original, instead of failing
    // after the click with "unknown entity class".
    auto known = [](const char* cls) {
        const int count = Menu_ShipCount();
        if (count < 0) return true;   // ship list still loading: don't block
        const MenuShip* ships = Menu_Ships();
        for (int i = 0; i < count; ++i)
            if (_stricmp(ships[i].name, cls) == 0) return true;
        return false;
    };

    ImGui::TextWrapped("己方飞船会让你坐上驾驶座；范杜尔飞船在上方 300 米生成并向你发起攻击。");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##sq42shipFilter", "搜索飞船...", sqFilter, sizeof(sqFilter));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sq42ships", list[pick].label, ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < n; ++i) {
            if (sqFilter[0] && !MatchesFilter(list[i].label, sqFilter)) continue;
            ImGui::PushID(i);
            const bool have = known(list[i].cls);
            if (ImGui::Selectable(list[i].label, i == pick, have ? 0 : ImGuiSelectableFlags_Disabled)) pick = i;
            if (!have) ImGui::SetItemTooltip("%s isn't in this game build's ship list.", list[i].cls);
            if (i == pick) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const bool pickKnown = known(list[pick].cls);
    if (list[pick].sit) {
        ImGui::SliderFloat("离我的高度（米）", &height, 0.0f, 500.0f, "%.0f");
        ImGui::Checkbox("让我坐上驾驶座", &sit);
    }
    ImGui::BeginDisabled(!pickKnown);
    const bool spawnClicked = ImGui::Button("生成", ImVec2(-1, 42));
    ImGui::EndDisabled();
    if (spawnClicked) {
        Menu_RequestSpawnClass(list[pick].cls,
                               list[pick].sit ? height : list[pick].height,
                               list[pick].sit && sit, list[pick].sit && sit,
                               list[pick].enemyWing);
        keepOpen = false;
    }

    ImGui::SeparatorText("控制台");
    static char cmd[256] = "";
    const bool consoleReady = Menu_ConsoleReady();
    ImGui::BeginDisabled(!consoleReady);
    const float runWidth = ImGui::CalcTextSize("执行").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-(runWidth + ImGui::GetStyle().ItemSpacing.x));
    bool run = ImGui::InputTextWithHint("##console",
                                        "控制台命令，例如 i_target_selector.targeting2_enabled 1",
                                        cmd, sizeof(cmd), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    run |= ImGui::Button("执行");
    ImGui::EndDisabled();
    if (run && cmd[0]) {
        Menu_RunConsole(cmd);
        cmd[0] = 0;
    }
    if (!consoleReady) ImGui::TextDisabled("还没有找到游戏的控制台。");
    ImGui::TextWrapped("在游戏自带的控制台中执行，结果写在游戏日志里，不在这里显示。");
}

static bool DrawMenu() {
    bool keepOpen = true;
    DrawBackdrop();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin(SCO_TITLE "###main", &keepOpen,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    const float statusHeight = ImGui::GetTextLineHeight() * 2 + 18;
    const float bodyHeight = -(statusHeight + ImGui::GetStyle().ItemSpacing.y);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
        auto body = [&](auto draw) {
            if (ImGui::BeginChild("##body", ImVec2(0, bodyHeight))) draw();
            ImGui::EndChild();
        };
        const ImGuiTabItemFlags firstTab = g_backToFirstTab ? ImGuiTabItemFlags_SetSelected : 0;
        g_backToFirstTab = false;
        if (ImGui::BeginTabItem("玩家", nullptr, firstTab)) { body([&] { DrawPlayerTab(keepOpen); }); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("传送"))   { body([&] { DrawTravelTab(); });           ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("载具")) { body([&] { DrawVehiclesTab(keepOpen); }); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("船员"))     { body([&] { DrawCrewTab(); });             ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("NPC"))     { body([&] { DrawNpcsTab(keepOpen); });     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("建造"))    { body([&] { DrawBuildTab(keepOpen); });    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("42 中队")) { body([&] { DrawSq42Tab(keepOpen); }); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("菜单"))     { body([&] { DrawMenuTab(); });             ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    DrawStatusStrip(statusHeight);
    ImGui::End();
    return keepOpen;
}

static DWORD WINAPI MenuThread(LPVOID) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MenuWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"starcitzenofflinemods_menu";
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"sc-offline", WS_POPUP,
                            100, 100, kMenuW, kMenuH, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_wnd || !CreateDevice()) return 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui::StyleColorsDark();
    ApplyTheme();
    LoadFonts();
    ImGui_ImplWin32_Init(g_wnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadBackground();
    if (SUCCEEDED(com)) CoUninitialize();

    bool visible = false, wasDown = false;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const bool down = (GetAsyncKeyState('M') & 0x8000) != 0;
        const bool typing = visible && GetForegroundWindow() == g_wnd && ImGui::GetIO().WantTextInput;
        if (down && !wasDown && !typing && OurProcessHasFocus()) { visible = !visible; ShowMenu(visible); }
        wasDown = down;
        if (visible && !IsWindowVisible(g_wnd)) { visible = false; ClipCursor(nullptr); g_clipped = false; }
        if (!visible) { Sleep(50); continue; }
        KeepCursorInMenu();

        if (g_resizeW && g_resizeH) {
            ReleaseRenderTarget();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const bool keepOpen = DrawMenu();
        ImGui::Render();
        const float clear[4] = { kSoil.x, kSoil.y, kSoil.z, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);

        if (!keepOpen) { visible = false; ShowMenu(false); }
    }
}

void Menu_Start(HWND gameWindow) {
    g_game = gameWindow;
    if (HANDLE t = CreateThread(nullptr, 0, MenuThread, nullptr, 0, nullptr)) CloseHandle(t);
}
