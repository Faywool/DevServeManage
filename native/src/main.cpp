// DevServeManage native edition: Win32 + Direct2D + DirectWrite.
// No UI framework, no runtime dependency, native tray menu and flyout.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <commdlg.h>
#include <knownfolders.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "core.h"
#include "json.h"

using RectF = D2D1_RECT_F;

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace dsm;

#define IDI_APP 101
#define IDI_TRAY_IDLE 102
#define IDI_TRAY_RUNNING 103
#define IDI_TRAY_BUSY 104
#define IDI_TRAY_ERROR 105

static const wchar_t* kMainClass = L"DevServeManageMainWindow";
static const wchar_t* kTrayClass = L"DevServeManageTrayWindow";
static const wchar_t* kFlyoutClass = L"DevServeManageFlyoutWindow";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_APP_SHOW = WM_APP + 2;
static const UINT WM_APP_REFRESH = WM_APP + 3;
static const UINT WM_APP_TRAY_HOVER = WM_APP + 4;
static const UINT WM_APP_EXIT = WM_APP + 5;
static const UINT WM_APP_BUILD_DONE = WM_APP + 6;
static const UINT WM_APP_SAVE_EDIT = WM_APP + 30;
static const GUID kTrayGuid = { 0xA3F2B9C1, 0x7D4E, 0x4A2B, { 0x9C, 0x6F, 0x1E, 0x8D, 0x5B, 0x3A, 0x7C, 0x21 } };
static const UINT_PTR TIMER_TICK = 1;
static const UINT_PTR TIMER_HOVER = 2;
static const UINT_PTR TIMER_FLY_HIDE = 3;
static const UINT_PTR TIMER_SPIN = 4;

static ID2D1Factory* g_d2d = nullptr;
static IDWriteFactory* g_dwrite = nullptr;
static UINT g_taskbarCreated = 0;

struct Colors {
    D2D1_COLOR_F bg, surface, surface2, border, text, sub, faint, accent, accentHover,
        accentSoft, green, greenSoft, amber, amberSoft, red, redSoft, chip, chipHover,
        white, shadow, transparent;
};

static D2D1_COLOR_F C(UINT rgb, float a = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f, a);
}

static COLORREF ToColorRef(D2D1_COLOR_F c) {
    return RGB((int)(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
        (int)(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
        (int)(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
}

static bool IsDarkTheme() {
    HKEY key = nullptr;
    DWORD value = 1, size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&value, &size);
        RegCloseKey(key);
    }
    return value == 0;
}

static D2D1_COLOR_F MixColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static float ColorLuma(D2D1_COLOR_F c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

static D2D1_COLOR_F ReadSystemAccent() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
        if (mx - mn > 0.05f && mx > 0.15f) return D2D1::ColorF(r, g, b);
    }
    HKEY key = nullptr;
    DWORD value = 0, size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AccentColor", nullptr, nullptr, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            float r = (value & 0xFF) / 255.0f;
            float g = ((value >> 8) & 0xFF) / 255.0f;
            float b = ((value >> 16) & 0xFF) / 255.0f;
            float mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
            if (mx - mn > 0.04f) return D2D1::ColorF(r, g, b);
        } else RegCloseKey(key);
    }
    COLORREF cr = GetSysColor(COLOR_HIGHLIGHT);
    float r = GetRValue(cr) / 255.0f, g = GetGValue(cr) / 255.0f, b = GetBValue(cr) / 255.0f;
    if (r + g + b < 0.18f) return C(0x8A94A6);
    return D2D1::ColorF(r, g, b);
}

struct AppSettings {
    int themeMode = 0;             // 0 follow, 1 light, 2 dark
    bool followSystemAccent = false;
    unsigned int primaryColor = 0x2F8F83;
    bool startMinimized = false;
    bool confirmExit = true;
    bool animations = true;
};

static AppSettings g_settings;

static std::wstring ColorToHex(unsigned int rgb) {
    return Format(L"#%02X%02X%02X", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static unsigned int ParseColorHex(const std::string& text, unsigned int fallback) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(s.begin());
    if (s.size() != 6) return fallback;
    char* end = nullptr;
    unsigned long value = std::strtoul(s.c_str(), &end, 16);
    if (!end || *end) return fallback;
    return (unsigned int)value & 0xFFFFFF;
}

static dsm::Json EmptyWorkspaceNode() {
    dsm::Json workspace;
    workspace.type = dsm::Json::Type::Object;
    dsm::Json version; version.type = dsm::Json::Type::Number; version.number = 1;
    dsm::Json projects; projects.type = dsm::Json::Type::Array;
    workspace.object.emplace_back("version", std::move(version));
    workspace.object.emplace_back("projects", std::move(projects));
    return workspace;
}

static dsm::Json SettingsNode(const AppSettings& settings) {
    dsm::Json o;
    o.type = dsm::Json::Type::Object;
    dsm::Json theme; theme.type = dsm::Json::Type::Number; theme.number = settings.themeMode;
    o.object.emplace_back("themeMode", std::move(theme));
    dsm::Json follow; follow.type = dsm::Json::Type::Bool; follow.boolean = settings.followSystemAccent;
    o.object.emplace_back("followSystemAccent", std::move(follow));
    dsm::Json color; color.type = dsm::Json::Type::String; color.text = Narrow(ColorToHex(settings.primaryColor));
    o.object.emplace_back("primaryColor", std::move(color));
    dsm::Json tray; tray.type = dsm::Json::Type::Bool; tray.boolean = settings.startMinimized;
    o.object.emplace_back("startMinimized", std::move(tray));
    dsm::Json confirm; confirm.type = dsm::Json::Type::Bool; confirm.boolean = settings.confirmExit;
    o.object.emplace_back("confirmExit", std::move(confirm));
    dsm::Json anim; anim.type = dsm::Json::Type::Bool; anim.boolean = settings.animations;
    o.object.emplace_back("animations", std::move(anim));
    return o;
}

static dsm::Json LoadConfigRoot() {
    std::string text = ReadTextFile(ConfigPath());
    if (text.empty()) return {};
    dsm::Json root = dsm::Json::Parse(text);
    return root.type == dsm::Json::Type::Object ? root : dsm::Json{};
}

static dsm::Json WorkspaceNodeFromConfig(const dsm::Json& root) {
    if (auto* workspace = root.obj("workspace")) return *workspace;
    return EmptyWorkspaceNode();
}

static void LoadSettings() {
    g_settings = AppSettings{};
    std::wstring path = ConfigPath();
    if (!FileExists(path)) return;
    dsm::Json root = LoadConfigRoot();
    if (root.type != dsm::Json::Type::Object) return;
    const dsm::Json* json = root.obj("settings");
    if (!json) return;
    g_settings.themeMode = std::clamp(json->integer("themeMode", 0), 0, 2);
    g_settings.followSystemAccent = json->flag("followSystemAccent", false);
    g_settings.primaryColor = ParseColorHex(json->str("primaryColor", "#2F8F83"), 0x2F8F83);
    g_settings.startMinimized = json->flag("startMinimized", false);
    g_settings.confirmExit = json->flag("confirmExit", true);
    g_settings.animations = json->flag("animations", true);
}

static bool SaveSettings(const std::string* workspaceOverride = nullptr, std::wstring* error = nullptr) {
    dsm::Json workspace = EmptyWorkspaceNode();
    if (workspaceOverride) {
        dsm::Json parsed = dsm::Json::Parse(*workspaceOverride);
        if (parsed.type != dsm::Json::Type::Object) {
            if (error) *error = L"导入的工作区不是有效的 JSON 对象。";
            return false;
        }
        workspace = std::move(parsed);
    } else {
        workspace = WorkspaceNodeFromConfig(LoadConfigRoot());
    }

    dsm::Json root;
    root.type = dsm::Json::Type::Object;
    dsm::Json version; version.type = dsm::Json::Type::Number; version.number = 1;
    root.object.emplace_back("version", std::move(version));
    root.object.emplace_back("settings", SettingsNode(g_settings));
    root.object.emplace_back("workspace", std::move(workspace));
    return SaveConfigDocument(root.dump(2), error);
}

static bool EffectiveDark() {
    if (g_settings.themeMode == 1) return false;
    if (g_settings.themeMode == 2) return true;
    return IsDarkTheme();
}

static Colors MakeColors(bool dark) {
    Colors c{};
    if (dark) {
        c.bg = C(0x0E1015); c.surface = C(0x16181E); c.surface2 = C(0x1D2027);
        c.border = C(0x2A2E37); c.text = C(0xF1F3F7); c.sub = C(0xA7ADB9); c.faint = C(0x757C89);
        c.green = C(0x43D17A); c.greenSoft = C(0x143021); c.amber = C(0xF0B84A); c.amberSoft = C(0x34280F);
        c.red = C(0xF27B7B); c.redSoft = C(0x36181B); c.chip = C(0x22252C); c.chipHover = C(0x2C3038);
        c.white = C(0xFFFFFF); c.shadow = C(0x000000, 0.45f); c.transparent = C(0x000000, 0.0f);
    } else {
        c.bg = C(0xF3F4F6); c.surface = C(0xFFFFFF); c.surface2 = C(0xF7F8FA);
        c.border = C(0xE3E5E9); c.text = C(0x1A1C20); c.sub = C(0x565D68); c.faint = C(0x8A919E);
        c.green = C(0x15803D); c.greenSoft = C(0xDCFCE7); c.amber = C(0xB45309); c.amberSoft = C(0xFEF3C7);
        c.red = C(0xDC2626); c.redSoft = C(0xFEE2E2); c.chip = C(0xF0F1F3); c.chipHover = C(0xE7E9ED);
        c.white = C(0xFFFFFF); c.shadow = C(0x111827, 0.14f); c.transparent = C(0x000000, 0.0f);
    }
    D2D1_COLOR_F accent = g_settings.followSystemAccent ? ReadSystemAccent() : C(g_settings.primaryColor);
    float luma = ColorLuma(accent);
    if (dark && luma < 0.38f) accent = MixColor(accent, C(0xFFFFFF), 0.22f);
    if (!dark && luma > 0.74f) accent = MixColor(accent, C(0x000000), 0.20f);
    c.accent = accent;
    c.accentHover = MixColor(accent, dark ? C(0xFFFFFF) : C(0x000000), dark ? 0.20f : 0.16f);
    c.accentSoft = MixColor(c.surface, accent, dark ? 0.17f : 0.10f);
    return c;
}

// ---------------------------------------------------------------------------
// D2D / DWrite painting helpers
// ---------------------------------------------------------------------------
struct Painter {
    HWND hwnd = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    std::map<std::tuple<int, int, int>, IDWriteTextFormat*> formats;
    std::map<int, IDWriteTextFormat*> iconFormats;

    ~Painter() {
        for (auto& kv : formats) if (kv.second) kv.second->Release();
        for (auto& kv : iconFormats) if (kv.second) kv.second->Release();
        if (rt) rt->Release();
    }

    void Discard() {
        for (auto& kv : formats) if (kv.second) kv.second->Release();
        formats.clear();
        for (auto& kv : iconFormats) if (kv.second) kv.second->Release();
        iconFormats.clear();
        if (rt) { rt->Release(); rt = nullptr; }
    }

    float Dpi() const {
        float dpi = hwnd ? (float)GetDpiForWindow(hwnd) : 96.0f;
        return dpi <= 0 ? 96.0f : dpi;
    }

    bool Ensure() {
        if (!hwnd) return false;
        RECT rc{}; GetClientRect(hwnd, &rc);
        UINT w = (UINT)std::max(1L, rc.right), h = (UINT)std::max(1L, rc.bottom);
        if (rt) {
            auto sz = rt->GetPixelSize();
            if (sz.width != w || sz.height != h) rt->Resize(D2D1::SizeU(w, h));
            return true;
        }
        float dpi = Dpi();
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi, dpi);
        auto hwndProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(w, h),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY);
        if (FAILED(g_d2d->CreateHwndRenderTarget(props, hwndProps, &rt))) {
            rt = nullptr; return false;
        }
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return true;
    }

    float W() { return rt ? rt->GetSize().width : 0; }
    float H() { return rt ? rt->GetSize().height : 0; }

    bool Begin(D2D1_COLOR_F bg) {
        if (!Ensure()) return false;
        rt->BeginDraw();
        rt->Clear(bg);
        return true;
    }
    void End() {
        if (!rt) return;
        HRESULT hr = rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) Discard();
    }

    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F color) {
        if (!rt) return nullptr;
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(color, &b);
        return b;
    }

    void Fill(RectF r, D2D1_COLOR_F color) {
        auto* b = Brush(color);
        if (b) { rt->FillRectangle(r, b); b->Release(); }
    }
    void FillRound(RectF r, float radius, D2D1_COLOR_F color) {
        auto* b = Brush(color);
        if (b) { rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b); b->Release(); }
    }
    void StrokeRound(RectF r, float radius, D2D1_COLOR_F color, float width = 1.0f) {
        auto* b = Brush(color);
        if (b) { rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b, width); b->Release(); }
    }
    void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width = 1) {
        auto* b = Brush(color);
        if (b) { rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, width); b->Release(); }
    }
    void Ellipse(float x, float y, float rx, float ry, D2D1_COLOR_F color) {
        auto* b = Brush(color);
        if (b) { rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), b); b->Release(); }
    }

    IDWriteTextFormat* Fmt(float size, int weight, int align) {
        int keySize = (int)std::lround(size * 10);
        auto key = std::make_tuple(keySize, weight, align);
        auto it = formats.find(key);
        if (it != formats.end()) return it->second;
        IDWriteTextFormat* f = nullptr;
        g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, (DWRITE_FONT_WEIGHT)weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
        if (f) {
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        formats[key] = f;
        return f;
    }

    void Text(const std::wstring& text, RectF r, float size, int weight, D2D1_COLOR_F color, int align = 0) {
        auto* f = Fmt(size, weight, align);
        auto* b = Brush(color);
        if (f && b) {
            rt->DrawText(text.c_str(), (UINT32)text.size(), f, r, b,
                D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        }
        if (b) b->Release();
    }

    void TextWrapped(const std::wstring& text, RectF r, float size, int weight, D2D1_COLOR_F color, int align = 0) {        if (!rt) return;
        IDWriteTextFormat* f = nullptr;
        g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, (DWRITE_FONT_WEIGHT)weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
        if (!f) return;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        auto* b = Brush(color);
        IDWriteTextLayout* layout = nullptr;
        if (b && SUCCEEDED(g_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), f,
            r.right - r.left, r.bottom - r.top, &layout)) && layout) {
            rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout, b,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (layout) layout->Release();
        if (b) b->Release();
        f->Release();
    }

    IDWriteTextFormat* IconFmt(float size) {
        int key = (int)std::lround(size * 10);
        auto it = iconFormats.find(key);
        if (it != iconFormats.end()) return it->second;
        IDWriteTextFormat* f = nullptr;
        g_dwrite->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
        if (f) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        iconFormats[key] = f;
        return f;
    }

    void Icon(const std::wstring& glyph, RectF r, float size, D2D1_COLOR_F color) {
        auto* f = IconFmt(size);
        auto* b = Brush(color);
        if (f && b) {
            rt->DrawText(glyph.c_str(), (UINT32)glyph.size(), f, r, b,
                D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        }
        if (b) b->Release();
    }

    float Measure(const std::wstring& text, float size, int weight) {
        auto* f = Fmt(size, weight, 0);
        if (!f) return 0;
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(g_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), f, 2000, 100, &layout)) || !layout)
            return 0;
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        layout->Release();
        return m.widthIncludingTrailingWhitespace;
    }
};

struct R {
    float x = 0, y = 0, w = 0, h = 0;
    bool has(float px, float py) const { return px >= x && py >= y && px <= x + w && py <= y + h; }
    RectF d2d() const { return D2D1::RectF(x, y, x + w, y + h); }
    operator RectF() const { return d2d(); }
};
static R MakeR(float x, float y, float w, float h) { R r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }
static R Inset(R r, float d) { return MakeR(r.x + d, r.y + d, r.w - d * 2, r.h - d * 2); }

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum class Act {
    None = 0, Add, StartAll, StopAll, RestartAll, Menu, CardPrimary, CardRestart, CardBuild, CardBuildRelease,
    CardLog, CardMore, CardOpen, EmptyAdd, FlyPrimary, FlyRestart, FlyOpen, FlyMain,
    FlyStartAll, FlyStopAll, FlyExit
};

enum class IconKind {
    None = 0, Play, Stop, Restart, Document, Build, Release, More, Folder, Copy, Edit,
    Remove, Open, Home, Power, Warning, Package, Add, Menu, Settings, Info, Exit, Window
};
struct Hotspot {
    R r;
    Act action = Act::None;
    int index = -1;
};

struct UiMenuItem {
    int id = 0;
    std::wstring label;
    IconKind icon = IconKind::None;
    bool danger = false;
    bool enabled = true;
    bool separator = false;
};

struct AppState {
    HINSTANCE instance = nullptr;
    Engine engine;
    HWND main = nullptr;
    HWND tray = nullptr;
    HWND flyout = nullptr;
    HICON iconApp = nullptr;
    HICON iconTrayIdle = nullptr;
    HICON iconTrayRunning = nullptr;
    HICON iconTrayBusy = nullptr;
    HICON iconTrayError = nullptr;
    bool dark = false;
    Colors colors{};
    Painter mainPainter;
    Painter flyPainter;
    std::vector<Hotspot> mainSpots;
    std::vector<Hotspot> flySpots;
    std::vector<Snapshot> flySnaps;
    float scroll = 0;
    int hotMain = -1;
    int hotFly = -1;
    float mouseX = -1, mouseY = -1;
    float mainMouseX = -1, mainMouseY = -1;
    float flyMouseX = -1, flyMouseY = -1;
    float flyW = 450, flyH = 0;
    bool flyVisible = false;
    int flyMissTicks = 0;
    bool trayAdded = false;
    bool trayGuidMode = true;
    bool exiting = false;
    HICON currentTrayIcon = nullptr;
    int runningCount = 0;
    int totalCount = 0;
    int tick = 0;
    HMONITOR flyMonitor = nullptr;
    std::wstring lastTooltip;
    ULONGLONG flyShownAt = 0;
};

static AppState* g = nullptr;
static HANDLE g_mutex = nullptr;
static DWORD g_mainThread = 0;
static std::set<std::wstring> g_building;
static std::set<std::wstring> g_conflictPrompted;

struct BuildJobResult {
    std::wstring id;
    std::wstring name;
    std::wstring logPath;
    std::wstring outputDir;
    bool release = false;
    bool success = false;
};

struct ProjectMenuEntry {
    int id = 0;
    std::wstring label;
    std::wstring glyph;
    bool danger = false;
    bool enabled = true;
};
static std::vector<ProjectMenuEntry> g_projectMenuEntries;
static HFONT g_projectMenuFont = nullptr;
static HFONT g_projectMenuIconFont = nullptr;
static HBRUSH g_projectMenuBrush = nullptr;

// forward declarations
static void InvalidateMain();
static void PaintMain(Painter& p);
static void ShowMainWindow();
static void HideFlyout();
static void ShowFlyout();
static void UpdateTrayIcon();
static void PollEngine();
static void RequestExit();
static void RestartApplication();
static void AddFolderProject(HWND owner);
static void ToggleProject(int index, HWND owner);
static void StartProject(int index, HWND owner);
static void StopProject(int index);
static void OpenProjectLog(int index);
static void ShowProjectMenu(int index, POINT pt);
static void ShowTrayMenu(HWND owner, POINT pt);
static void HandleAction(Act action, int index);
static void AddProjectPath(const std::wstring& path, HWND owner);
static void RefreshTheme();
static void ApplyDarkTitle(HWND hwnd);
static void ShowInfoDialog(HWND owner, const std::wstring& title, const std::wstring& message);
static bool ShowConfirmDialog(HWND owner, const std::wstring& title, const std::wstring& message,
    const std::wstring& primary, const std::wstring& secondary, bool danger = false,
    bool topmost = false);
static bool ShowEditProjectDialog(HWND owner, Project& project);
static void StartBuild(const Snapshot& snapshot, bool release);
static std::wstring BuildOutputDir(const Project& project, bool release);
static void OpenFolderPath(const std::wstring& path);
static int ShowCustomMenu(HWND owner, POINT pt, const std::vector<UiMenuItem>& items,
    const RECT* clip = nullptr);
static bool IsCustomMenuOpen();
static void ShowAppMenu();
static void ShowSettingsWindow(HWND owner);
static void ShowAboutDialog(HWND owner);
static void ShowAboutWindow(HWND owner);

// ---------------------------------------------------------------------------
// Main window layout and painting
// ---------------------------------------------------------------------------
static D2D1_COLOR_F StatusColor(const Snapshot& s) {
    switch (s.status) {
    case Status::Running: return g->colors.green;
    case Status::Starting:
    case Status::Stopping: return g->colors.amber;
    case Status::Failed: return g->colors.red;
    default: return g->colors.faint;
    }
}

static bool IsActive(Status s) {
    return s == Status::Running || s == Status::Starting || s == Status::Stopping;
}

static std::wstring StatusLabel(const Snapshot& s) {
    if (s.project.kind() == Kind::WindowsService && !s.serviceInstalled) return L"服务未安装";
    if (s.status == Status::Failed) return L"启动失败";
    return StatusText(s.status);
}

static std::wstring PortsText(const Snapshot& s) {
    if (s.ports.empty()) return s.project.port > 0 ? L"预期 :" + std::to_wstring(s.project.port) : L"";
    std::wstring out;
    for (size_t i = 0; i < s.ports.size(); ++i) {
        if (i) out += L" / ";
        out += L":" + std::to_wstring(s.ports[i]);
    }
    return out;
}

static void DrawBadge(Painter& p, R r, const std::wstring& text, D2D1_COLOR_F bg, D2D1_COLOR_F fg) {
    p.FillRound(r, 5, bg);
    p.Text(text, r, 10.5f, 600, fg, 1);
}

static void DrawSpinner(Painter& p, R r, D2D1_COLOR_F color) {
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    double base = g_settings.animations ? (double)((GetTickCount64() / 14) % 360) : 24.0;
    for (int i = 0; i < 10; ++i) {
        double a = (base + i * 36.0) * 3.14159265358979323846 / 180.0;
        float r1 = 4.5f, r2 = 8.0f;
        D2D1_COLOR_F c = D2D1::ColorF(color.r, color.g, color.b, 0.12f + i * 0.085f);
        p.Line(cx + (float)std::cos(a) * r1, cy + (float)std::sin(a) * r1,
            cx + (float)std::cos(a) * r2, cy + (float)std::sin(a) * r2, c, 2.4f);
    }
}

// A small comet that travels around the rounded button edge. This is kept
// separate from DrawSpinner so the existing start-button icon animation can
// remain while build / start / restart actions also show progress.
static void DrawRunningBorder(Painter& p, R r, D2D1_COLOR_F color) {
    if (!g_settings.animations || r.w < 12 || r.h < 12) return;
    const float rad = 7.0f;
    const float x = r.x + 0.5f, y = r.y + 0.5f;
    const float w = r.w - 1.0f, h = r.h - 1.0f;
    struct Point { float x, y; };
    std::vector<Point> pts;
    pts.reserve(64);
    auto addLine = [&](float x1, float y1, float x2, float y2, int count) {
        for (int i = 0; i < count; ++i) {
            float t = (float)i / (float)count;
            pts.push_back({ x1 + (x2 - x1) * t, y1 + (y2 - y1) * t });
        }
    };
    auto addArc = [&](float cx, float cy, float radius, float a0, float a1, int count) {
        for (int i = 0; i < count; ++i) {
            float t = (float)i / (float)count;
            float a = (a0 + (a1 - a0) * t) * 3.14159265358979323846f / 180.0f;
            pts.push_back({ cx + (float)std::cos(a) * radius, cy + (float)std::sin(a) * radius });
        }
    };
    addLine(x + rad, y, x + w - rad, y, 8);
    addArc(x + w - rad, y + rad, rad, -90.0f, 0.0f, 5);
    addLine(x + w, y + rad, x + w, y + h - rad, 8);
    addArc(x + w - rad, y + h - rad, rad, 0.0f, 90.0f, 5);
    addLine(x + w - rad, y + h, x + rad, y + h, 8);
    addArc(x + rad, y + h - rad, rad, 90.0f, 180.0f, 5);
    addLine(x, y + h - rad, x, y + rad, 8);
    addArc(x + rad, y + rad, rad, 180.0f, 270.0f, 5);
    if (pts.size() < 8) return;

    const double n = (double)pts.size();
    auto pointAt = [&](double index) -> const Point& {
        double v = std::fmod(index, n);
        if (v < 0) v += n;
        return pts[(size_t)v];
    };
    double phase = std::fmod((double)(GetTickCount64() / 12), n);
    for (int pass = 0; pass < 2; ++pass) {
        for (int trail = 0; trail < 2; ++trail) {
            double start = phase + (double)trail * n / 2.0;
            for (int i = 0; i < 13; ++i) {
                const Point& a = pointAt(start - i);
                const Point& b = pointAt(start - i - 1.0);
                float fade = 1.0f - (float)i / 14.0f;
                float alpha = (pass == 0 ? 0.18f : 0.95f) * fade;
                float width = pass == 0 ? 5.0f : 2.0f;
                p.Line(a.x, a.y, b.x, b.y,
                    D2D1::ColorF(color.r, color.g, color.b, alpha), width);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Small hand-drawn vector icons. Rounded caps and joins keep the set soft.
// ---------------------------------------------------------------------------
static void RoundLine(Painter& p, float x1, float y1, float x2, float y2,
    D2D1_COLOR_F color, float width) {
    p.Line(x1, y1, x2, y2, color, width);
    p.Ellipse(x1, y1, width / 2, width / 2, color);
    p.Ellipse(x2, y2, width / 2, width / 2, color);
}

static void IconArc(Painter& p, float cx, float cy, float radius, float startDeg, float endDeg,
    D2D1_COLOR_F color, float width) {
    const int steps = 22;
    float px = cx + radius * (float)std::cos(startDeg * 3.14159265358979323846 / 180.0);
    float py = cy + radius * (float)std::sin(startDeg * 3.14159265358979323846 / 180.0);
    for (int i = 1; i <= steps; ++i) {
        float a = (startDeg + (endDeg - startDeg) * i / steps) * 3.14159265358979323846 / 180.0;
        float qx = cx + radius * (float)std::cos(a);
        float qy = cy + radius * (float)std::sin(a);
        RoundLine(p, px, py, qx, qy, color, width);
        px = qx; py = qy;
    }
}

static void FillTriangle(Painter& p, float ax, float ay, float bx, float by, float cx, float cy,
    D2D1_COLOR_F color) {
    ID2D1PathGeometry* geo = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&geo)) || !geo) return;
    geo->Open(&sink);
    if (sink) {
        sink->BeginFigure(D2D1::Point2F(ax, ay), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(bx, by));
        sink->AddLine(D2D1::Point2F(cx, cy));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        auto* b = p.Brush(color);
        if (b) {
            p.rt->FillGeometry(geo, b);
            p.rt->DrawGeometry(geo, b, 1.6f);
            b->Release();
        }
        p.Ellipse(ax, ay, 1.2f, 1.2f, color);
        p.Ellipse(bx, by, 1.2f, 1.2f, color);
        p.Ellipse(cx, cy, 1.2f, 1.2f, color);
        sink->Release();
    }
    geo->Release();
}

static void DrawIconLegacy(Painter& p, IconKind kind, R box, D2D1_COLOR_F color) {
    if (kind == IconKind::None || box.w <= 2 || box.h <= 2) return;
    float s = std::min(box.w, box.h);
    float x = box.x + (box.w - s) / 2;
    float y = box.y + (box.h - s) / 2;
    float cx = x + s / 2, cy = y + s / 2;
    float lw = std::max(1.3f, s * 0.075f);

    switch (kind) {
    case IconKind::Play:
        FillTriangle(p, x + s * 0.30f, y + s * 0.20f,
            x + s * 0.80f, y + s * 0.50f,
            x + s * 0.30f, y + s * 0.80f, color);
        break;
    case IconKind::Stop:
        p.FillRound(MakeR(cx - s * 0.33f, cy - s * 0.33f, s * 0.66f, s * 0.66f), s * 0.16f, color);
        break;
    case IconKind::Restart: {
        IconArc(p, cx, cy, s * 0.31f, -55, 235, color, lw * 1.65f);
        float ax = cx + s * 0.31f * (float)std::cos(235.0 * 3.14159265358979323846 / 180.0);
        float ay = cy + s * 0.31f * (float)std::sin(235.0 * 3.14159265358979323846 / 180.0);
        FillTriangle(p, ax - s * 0.045f, ay - s * 0.15f,
            ax + s * 0.17f, ay + s * 0.02f,
            ax - s * 0.14f, ay + s * 0.10f, color);
        break;
    }
    case IconKind::Document: {
        R page = MakeR(x + s * 0.28f, y + s * 0.16f, s * 0.46f, s * 0.68f);
        p.FillRound(page, s * 0.07f, D2D1::ColorF(color.r, color.g, color.b, 0.14f));
        p.StrokeRound(page, s * 0.07f, color, lw);
        RoundLine(p, page.x + page.w * 0.55f, page.y, page.x + page.w, page.y + page.h * 0.24f, color, lw * 0.9f);
        RoundLine(p, page.x + page.w * 0.55f, page.y, page.x + page.w * 0.55f, page.y + page.h * 0.24f, color, lw * 0.9f);
        RoundLine(p, page.x + page.w * 0.20f, page.y + page.h * 0.42f, page.x + page.w * 0.78f, page.y + page.h * 0.42f, color, lw * 0.8f);
        RoundLine(p, page.x + page.w * 0.20f, page.y + page.h * 0.60f, page.x + page.w * 0.78f, page.y + page.h * 0.60f, color, lw * 0.8f);
        RoundLine(p, page.x + page.w * 0.20f, page.y + page.h * 0.78f, page.x + page.w * 0.58f, page.y + page.h * 0.78f, color, lw * 0.8f);
        break;
    }
    case IconKind::Build: {
        RoundLine(p, x + s * 0.26f, y + s * 0.76f, x + s * 0.58f, y + s * 0.43f, color, lw * 1.55f);
        RoundLine(p, x + s * 0.51f, y + s * 0.30f, x + s * 0.60f, y + s * 0.21f, color, lw * 1.45f);
        RoundLine(p, x + s * 0.60f, y + s * 0.21f, x + s * 0.75f, y + s * 0.31f, color, lw * 1.45f);
        RoundLine(p, x + s * 0.75f, y + s * 0.31f, x + s * 0.67f, y + s * 0.45f, color, lw * 1.45f);
        break;
    }
    case IconKind::Release:
    case IconKind::Package: {
        // Rocket: a filled capsule body, a pointed nose, two fins and a flame.
        p.FillRound(MakeR(cx - s * 0.09f, y + s * 0.28f, s * 0.18f, s * 0.38f), s * 0.09f, color);
        FillTriangle(p, cx - s * 0.09f, y + s * 0.33f, cx, y + s * 0.12f, cx + s * 0.09f, y + s * 0.33f, color);
        RoundLine(p, cx - s * 0.09f, y + s * 0.52f, cx - s * 0.22f, y + s * 0.72f, color, lw * 1.1f);
        RoundLine(p, cx + s * 0.09f, y + s * 0.52f, cx + s * 0.22f, y + s * 0.72f, color, lw * 1.1f);
        FillTriangle(p, cx - s * 0.045f, y + s * 0.66f, cx, y + s * 0.88f, cx + s * 0.045f, y + s * 0.66f,
            D2D1::ColorF(color.r, color.g, color.b, 0.62f));
        break;
    }
    case IconKind::More:
        p.Ellipse(cx - s * 0.25f, cy, s * 0.07f, s * 0.07f, color);
        p.Ellipse(cx, cy, s * 0.07f, s * 0.07f, color);
        p.Ellipse(cx + s * 0.25f, cy, s * 0.07f, s * 0.07f, color);
        break;
    case IconKind::Folder: {
        R f = MakeR(x + s * 0.18f, y + s * 0.32f, s * 0.64f, s * 0.44f);
        p.FillRound(MakeR(f.x, f.y - s * 0.08f, s * 0.28f, s * 0.16f), s * 0.05f, color);
        p.FillRound(f, s * 0.07f, color);
        p.FillRound(MakeR(f.x + s * 0.06f, f.y + s * 0.06f, f.w - s * 0.12f, s * 0.07f), s * 0.03f, C(0xFFFFFF, 0.25f));
        break;
    }
    case IconKind::Copy: {
        R a = MakeR(x + s * 0.24f, y + s * 0.22f, s * 0.46f, s * 0.50f);
        R b = MakeR(x + s * 0.34f, y + s * 0.34f, s * 0.46f, s * 0.50f);
        p.StrokeRound(a, s * 0.06f, color, lw);
        p.StrokeRound(b, s * 0.06f, color, lw);
        break;
    }
    case IconKind::Edit: {
        RoundLine(p, x + s * 0.28f, y + s * 0.72f, x + s * 0.68f, y + s * 0.30f, color, lw * 1.6f);
        RoundLine(p, x + s * 0.62f, y + s * 0.22f, x + s * 0.76f, y + s * 0.36f, color, lw * 1.5f);
        FillTriangle(p, x + s * 0.25f, y + s * 0.75f,
            x + s * 0.35f, y + s * 0.58f,
            x + s * 0.43f, y + s * 0.77f, color);
        break;
    }
    case IconKind::Exit: {
        p.StrokeRound(MakeR(x + s * 0.20f, y + s * 0.18f, s * 0.42f, s * 0.64f), s * 0.06f, color, lw);
        RoundLine(p, x + s * 0.44f, y + s * 0.50f, x + s * 0.82f, y + s * 0.50f, color, lw * 1.2f);
        RoundLine(p, x + s * 0.70f, y + s * 0.38f, x + s * 0.82f, y + s * 0.50f, color, lw * 1.2f);
        RoundLine(p, x + s * 0.82f, y + s * 0.50f, x + s * 0.70f, y + s * 0.62f, color, lw * 1.2f);
        p.Ellipse(x + s * 0.34f, y + s * 0.50f, s * 0.035f, s * 0.035f, color);
        break;
    }
    case IconKind::Window: {
        p.StrokeRound(MakeR(x + s * 0.16f, y + s * 0.22f, s * 0.68f, s * 0.46f), s * 0.07f, color, lw);
        RoundLine(p, x + s * 0.28f, y + s * 0.34f, x + s * 0.54f, y + s * 0.34f, color, lw * 0.8f);
        RoundLine(p, cx, y + s * 0.68f, cx, y + s * 0.78f, color, lw);
        RoundLine(p, x + s * 0.35f, y + s * 0.78f, x + s * 0.65f, y + s * 0.78f, color, lw);
        break;
    }
    case IconKind::Settings: {
        IconArc(p, cx, cy, s * 0.22f, 0, 360, color, lw * 1.15f);
        for (int i = 0; i < 8; ++i) {
            float a = i * 45.0f * 3.14159265358979323846f / 180.0f;
            RoundLine(p, cx + std::cos(a) * s * 0.22f, cy + std::sin(a) * s * 0.22f,
                cx + std::cos(a) * s * 0.35f, cy + std::sin(a) * s * 0.35f, color, lw * 1.1f);
        }
        p.Ellipse(cx, cy, s * 0.055f, s * 0.055f, color);
        break;
    }
    case IconKind::Info: {
        p.StrokeRound(MakeR(x + s * 0.18f, y + s * 0.18f, s * 0.64f, s * 0.64f), s * 0.32f, color, lw);
        p.Ellipse(cx, y + s * 0.34f, s * 0.045f, s * 0.045f, color);
        RoundLine(p, cx, y + s * 0.47f, cx, y + s * 0.67f, color, lw * 1.2f);
        break;
    }
    case IconKind::Menu:
        RoundLine(p, x + s * 0.22f, y + s * 0.32f, x + s * 0.78f, y + s * 0.32f, color, lw * 1.2f);
        RoundLine(p, x + s * 0.22f, y + s * 0.50f, x + s * 0.78f, y + s * 0.50f, color, lw * 1.2f);
        RoundLine(p, x + s * 0.22f, y + s * 0.68f, x + s * 0.78f, y + s * 0.68f, color, lw * 1.2f);
        break;
    case IconKind::Remove: {
        p.StrokeRound(MakeR(x + s * 0.30f, y + s * 0.34f, s * 0.40f, s * 0.44f), s * 0.05f, color, lw);
        RoundLine(p, x + s * 0.24f, y + s * 0.28f, x + s * 0.76f, y + s * 0.28f, color, lw);
        RoundLine(p, x + s * 0.42f, y + s * 0.20f, x + s * 0.58f, y + s * 0.20f, color, lw);
        RoundLine(p, x + s * 0.44f, y + s * 0.44f, x + s * 0.44f, y + s * 0.66f, color, lw * 0.7f);
        RoundLine(p, x + s * 0.56f, y + s * 0.44f, x + s * 0.56f, y + s * 0.66f, color, lw * 0.7f);
        break;
    }
    case IconKind::Open: {
        R f = MakeR(x + s * 0.18f, y + s * 0.34f, s * 0.48f, s * 0.40f);
        p.FillRound(MakeR(f.x, f.y - s * 0.08f, s * 0.24f, s * 0.16f), s * 0.05f, color);
        p.FillRound(f, s * 0.07f, color);
        RoundLine(p, x + s * 0.58f, y + s * 0.54f, x + s * 0.82f, y + s * 0.28f, g->colors.surface, lw * 1.6f);
        RoundLine(p, x + s * 0.70f, y + s * 0.26f, x + s * 0.82f, y + s * 0.28f, g->colors.surface, lw * 1.2f);
        RoundLine(p, x + s * 0.82f, y + s * 0.28f, x + s * 0.84f, y + s * 0.42f, g->colors.surface, lw * 1.2f);
        break;
    }
    case IconKind::Home: {
        RoundLine(p, x + s * 0.20f, y + s * 0.48f, x + s * 0.50f, y + s * 0.20f, color, lw);
        RoundLine(p, x + s * 0.50f, y + s * 0.20f, x + s * 0.80f, y + s * 0.48f, color, lw);
        p.StrokeRound(MakeR(x + s * 0.28f, y + s * 0.46f, s * 0.44f, s * 0.34f), s * 0.05f, color, lw);
        break;
    }
    case IconKind::Power:
        IconArc(p, cx, cy + s * 0.02f, s * 0.29f, 130, 410, color, lw);
        RoundLine(p, cx, y + s * 0.16f, cx, cy, color, lw);
        break;
    case IconKind::Warning: {
        RoundLine(p, cx, y + s * 0.18f, x + s * 0.82f, y + s * 0.78f, color, lw * 1.1f);
        RoundLine(p, x + s * 0.82f, y + s * 0.78f, x + s * 0.18f, y + s * 0.78f, color, lw * 1.1f);
        RoundLine(p, x + s * 0.18f, y + s * 0.78f, cx, y + s * 0.18f, color, lw * 1.1f);
        RoundLine(p, cx, y + s * 0.38f, cx, y + s * 0.56f, color, lw * 1.25f);
        p.Ellipse(cx, y + s * 0.66f, s * 0.04f, s * 0.04f, color);
        break;
    }
    case IconKind::Add:
        RoundLine(p, cx - s * 0.22f, cy, cx + s * 0.22f, cy, color, lw * 1.2f);
        RoundLine(p, cx, cy - s * 0.22f, cx, cy + s * 0.22f, color, lw * 1.2f);
        break;
    default: break;
    }
}

// Lucide SVG icons (ISC). The path data below is from lucide-static 1.69.0.
// They are parsed once into Direct2D geometry and rendered with round caps/joins.
struct SvgContour {
    std::vector<D2D1_POINT_2F> points;
    bool closed = false;
};

static bool IsSvgCommand(char c) {
    return std::strchr("MmLlHhVvCcSsQqTtAaZz", c) != nullptr;
}

static void SkipSvgSep(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') ++p;
}

static bool ReadSvgNumber(const char*& p, float& value) {
    SkipSvgSep(p);
    if (!*p) return false;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return false;
    value = (float)v;
    p = end;
    return true;
}

static void FlattenCubic(const D2D1_POINT_2F& p0, const D2D1_POINT_2F& p1,
    const D2D1_POINT_2F& p2, const D2D1_POINT_2F& p3, std::vector<D2D1_POINT_2F>& out) {
    for (int i = 1; i <= 12; ++i) {
        float t = i / 12.0f, u = 1 - t;
        float x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
        float y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
        out.push_back(D2D1::Point2F(x, y));
    }
}

static void FlattenQuad(const D2D1_POINT_2F& p0, const D2D1_POINT_2F& p1,
    const D2D1_POINT_2F& p2, std::vector<D2D1_POINT_2F>& out) {
    for (int i = 1; i <= 10; ++i) {
        float t = i / 10.0f, u = 1 - t;
        out.push_back(D2D1::Point2F(u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y));
    }
}

static void FlattenSvgArc(float x1, float y1, float rx, float ry, float rotation,
    bool largeArc, bool sweep, float x2, float y2, std::vector<D2D1_POINT_2F>& out) {
    if (rx == 0 || ry == 0) { out.push_back(D2D1::Point2F(x2, y2)); return; }
    rx = std::abs(rx); ry = std::abs(ry);
    const float pi = 3.14159265358979323846f;
    float phi = rotation * pi / 180.0f;
    float cs = std::cos(phi), sn = std::sin(phi);
    float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    float xp = cs * dx + sn * dy;
    float yp = -sn * dx + cs * dy;
    float lambda = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (lambda > 1) { float k = std::sqrt(lambda); rx *= k; ry *= k; }
    float rx2 = rx * rx, ry2 = ry * ry;
    float den = rx2 * yp * yp + ry2 * xp * xp;
    float coef = std::sqrt(std::max(0.0f, (rx2 * ry2 - den) / std::max(den, 1e-6f)));
    if (largeArc == sweep) coef = -coef;
    float cxp = coef * rx * yp / ry;
    float cyp = -coef * ry * xp / rx;
    float cx = cs * cxp - sn * cyp + (x1 + x2) / 2;
    float cy = sn * cxp + cs * cyp + (y1 + y2) / 2;
    float theta1 = std::atan2((yp - cyp) / ry, (xp - cxp) / rx);
    float theta2 = std::atan2((-yp - cyp) / ry, (-xp - cxp) / rx);
    float delta = theta2 - theta1;
    if (!sweep && delta > 0) delta -= 2 * pi;
    if (sweep && delta < 0) delta += 2 * pi;
    int steps = std::max(8, (int)std::ceil(std::abs(delta) / (pi / 16)));
    for (int i = 1; i <= steps; ++i) {
        float t = theta1 + delta * i / steps;
        float px = rx * std::cos(t), py = ry * std::sin(t);
        out.push_back(D2D1::Point2F(cs * px - sn * py + cx, sn * px + cs * py + cy));
    }
}

static std::vector<SvgContour> ParseSvgPath(const std::string& d) {
    std::vector<SvgContour> contours;
    SvgContour current;
    D2D1_POINT_2F cp{}, start{}, lastControl{};
    char cmd = 0, prevCmd = 0;
    const char* p = d.c_str();
    auto flush = [&]() {
        if (!current.points.empty()) { contours.push_back(current); current = SvgContour{}; }
    };
    auto add = [&](float x, float y) {
        current.points.push_back(D2D1::Point2F(x, y));
        cp = D2D1::Point2F(x, y);
    };
    while (*p) {
        SkipSvgSep(p);
        if (!*p) break;
        if (IsSvgCommand(*p)) { cmd = *p++; }
        else if (!cmd) break;
        bool rel = std::islower((unsigned char)cmd) != 0;
        char c = (char)std::toupper((unsigned char)cmd);
        if (c == 'Z') {
            if (!current.points.empty()) { current.closed = true; flush(); }
            cp = start;
            prevCmd = 'Z';
            cmd = 0;
            continue;
        }
        float a = 0, b = 0, c1 = 0, d1 = 0, e = 0, f = 0;
        auto base = cp;
        if (c == 'M') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b)) break;
            if (rel) { a += base.x; b += base.y; }
            flush();
            current.points.push_back(D2D1::Point2F(a, b));
            cp = start = D2D1::Point2F(a, b);
            prevCmd = 'M';
            cmd = rel ? 'l' : 'L';
            continue;
        }
        if (c == 'L') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b)) break;
            add(rel ? base.x + a : a, rel ? base.y + b : b);
        } else if (c == 'H') {
            if (!ReadSvgNumber(p, a)) break;
            add(rel ? base.x + a : a, base.y);
        } else if (c == 'V') {
            if (!ReadSvgNumber(p, a)) break;
            add(base.x, rel ? base.y + a : a);
        } else if (c == 'C') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b) ||
                !ReadSvgNumber(p, c1) || !ReadSvgNumber(p, d1) ||
                !ReadSvgNumber(p, e) || !ReadSvgNumber(p, f)) break;
            D2D1_POINT_2F p1(rel ? base.x + a : a, rel ? base.y + b : b);
            D2D1_POINT_2F p2(rel ? base.x + c1 : c1, rel ? base.y + d1 : d1);
            D2D1_POINT_2F p3(rel ? base.x + e : e, rel ? base.y + f : f);
            FlattenCubic(cp, p1, p2, p3, current.points);
            lastControl = p2; cp = p3;
        } else if (c == 'S') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b) ||
                !ReadSvgNumber(p, c1) || !ReadSvgNumber(p, d1)) break;
            D2D1_POINT_2F p1 = (prevCmd == 'C' || prevCmd == 'S')
                ? D2D1::Point2F(2 * cp.x - lastControl.x, 2 * cp.y - lastControl.y) : cp;
            D2D1_POINT_2F p2(rel ? base.x + a : a, rel ? base.y + b : b);
            D2D1_POINT_2F p3(rel ? base.x + c1 : c1, rel ? base.y + d1 : d1);
            FlattenCubic(cp, p1, p2, p3, current.points);
            lastControl = p2; cp = p3;
        } else if (c == 'Q') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b) ||
                !ReadSvgNumber(p, c1) || !ReadSvgNumber(p, d1)) break;
            D2D1_POINT_2F p1(rel ? base.x + a : a, rel ? base.y + b : b);
            D2D1_POINT_2F p2(rel ? base.x + c1 : c1, rel ? base.y + d1 : d1);
            FlattenQuad(cp, p1, p2, current.points);
            lastControl = p1; cp = p2;
        } else if (c == 'T') {
            if (!ReadSvgNumber(p, a) || !ReadSvgNumber(p, b)) break;
            D2D1_POINT_2F p1 = (prevCmd == 'Q' || prevCmd == 'T')
                ? D2D1::Point2F(2 * cp.x - lastControl.x, 2 * cp.y - lastControl.y) : cp;
            D2D1_POINT_2F p2(rel ? base.x + a : a, rel ? base.y + b : b);
            FlattenQuad(cp, p1, p2, current.points);
            lastControl = p1; cp = p2;
        } else if (c == 'A') {
            float rx = 0, ry = 0, rot = 0, la = 0, sw = 0, x = 0, y = 0;
            if (!ReadSvgNumber(p, rx) || !ReadSvgNumber(p, ry) || !ReadSvgNumber(p, rot) ||
                !ReadSvgNumber(p, la) || !ReadSvgNumber(p, sw) ||
                !ReadSvgNumber(p, x) || !ReadSvgNumber(p, y)) break;
            D2D1_POINT_2F end(rel ? base.x + x : x, rel ? base.y + y : y);
            FlattenSvgArc(cp.x, cp.y, rx, ry, rot, la != 0, sw != 0, end.x, end.y, current.points);
            cp = end;
        } else {
            break;
        }
        prevCmd = c;
        if (!IsSvgCommand(*p) && !std::isdigit((unsigned char)*p) &&
            *p != '-' && *p != '+' && *p != '.') cmd = 0;
    }
    flush();
    return contours;
}

static void AddSvgRect(std::vector<SvgContour>& contours, float x, float y, float w, float h, float r) {
    if (r <= 0) {
        SvgContour c;
        c.points = { D2D1::Point2F(x, y), D2D1::Point2F(x + w, y),
            D2D1::Point2F(x + w, y + h), D2D1::Point2F(x, y + h) };
        c.closed = true;
        contours.push_back(c);
        return;
    }
    SvgContour c;
    const float pi = 3.14159265358979323846f;
    for (int corner = 0; corner < 4; ++corner) {
        float cx = (corner == 0 || corner == 3) ? x + r : x + w - r;
        float cy = (corner == 0 || corner == 1) ? y + r : y + h - r;
        float start = (corner == 0 ? 180 : corner == 1 ? 270 : corner == 2 ? 0 : 90) * pi / 180;
        for (int i = 0; i <= 4; ++i) {
            float a = start + i * (pi / 2) / 4;
            c.points.push_back(D2D1::Point2F(cx + r * std::cos(a), cy + r * std::sin(a)));
        }
    }
    c.closed = true;
    contours.push_back(c);
}

static void AddSvgCircle(std::vector<SvgContour>& contours, float cx, float cy, float r) {
    SvgContour c;
    for (int i = 0; i < 32; ++i) {
        float a = i * 2 * 3.14159265358979323846f / 32;
        c.points.push_back(D2D1::Point2F(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    c.closed = true;
    contours.push_back(c);
}

static std::string SvgAttr(const std::string& tag, const char* name) {
    std::string needle = std::string(name) + "=\"";
    size_t pos = tag.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    size_t end = tag.find('"', pos);
    return end == std::string::npos ? std::string{} : tag.substr(pos, end - pos);
}

static float SvgFloat(const std::string& tag, const char* name, float fallback = 0) {
    std::string v = SvgAttr(tag, name);
    return v.empty() ? fallback : (float)std::strtod(v.c_str(), nullptr);
}

static std::vector<SvgContour> ParseSvgBody(const std::string& body) {
    std::vector<SvgContour> contours;
    size_t pos = 0;
    while ((pos = body.find('<', pos)) != std::string::npos) {
        size_t end = body.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = body.substr(pos + 1, end - pos - 1);
        if (tag.rfind("path", 0) == 0) {
            auto c = ParseSvgPath(SvgAttr(tag, "d"));
            contours.insert(contours.end(), c.begin(), c.end());
        } else if (tag.rfind("rect", 0) == 0) {
            AddSvgRect(contours, SvgFloat(tag, "x"), SvgFloat(tag, "y"),
                SvgFloat(tag, "width"), SvgFloat(tag, "height"),
                SvgFloat(tag, "rx", SvgFloat(tag, "ry")));
        } else if (tag.rfind("circle", 0) == 0) {
            AddSvgCircle(contours, SvgFloat(tag, "cx"), SvgFloat(tag, "cy"), SvgFloat(tag, "r"));
        }
        pos = end + 1;
    }
    return contours;
}

static bool IconIsFilled(IconKind kind) {
    return kind == IconKind::Play || kind == IconKind::Stop || kind == IconKind::More;
}

static const char* IconSvgBody(IconKind kind) {
    switch (kind) {
    case IconKind::Play:
        return R"(<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>)";
    case IconKind::Stop:
        return R"(<rect width="18" height="18" x="3" y="3" rx="2"/>)";
    case IconKind::Restart:
        return R"(<path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/>)";
    case IconKind::Document:
        return R"(<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/><path d="M14 2v5a1 1 0 0 0 1 1h5"/><path d="M10 9H8"/><path d="M16 13H8"/><path d="M16 17H8"/>)";
    case IconKind::Build:
        return R"(<path d="m15 12-9.373 9.373a1 1 0 0 1-3.001-3L12 9"/><path d="m18 15 4-4"/><path d="m21.5 11.5-1.914-1.914A2 2 0 0 1 19 8.172v-.344a2 2 0 0 0-.586-1.414l-1.657-1.657A6 6 0 0 0 12.516 3H9l1.243 1.243A6 6 0 0 1 12 8.485V10l2 2h1.172a2 2 0 0 1 1.414.586L18.5 14.5"/>)";
    case IconKind::Release:
    case IconKind::Package:
        return R"(<path d="M12 15v5s3.03-.55 4-2c1.08-1.62 0-5 0-5"/><path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.7-2.13-.09-2.91a2.18 2.18 0 0 0-2.91-.09"/><path d="M9 12a22 22 0 0 1 2-3.95A12.88 12.88 0 0 1 22 2c0 2.72-.78 7.5-6 11a22.4 22.4 0 0 1-4 2z"/><path d="M9 12H4s.55-3.03 2-4c1.62-1.08 5 .05 5 .05"/>)";
    case IconKind::More:
        return R"(<circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/><circle cx="5" cy="12" r="1"/>)";
    case IconKind::Folder:
    case IconKind::Open:
        return R"(<path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/>)";
    case IconKind::Copy:
        return R"(<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>)";
    case IconKind::Edit:
        return R"(<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>)";
    case IconKind::Remove:
        return R"(<path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>)";
    case IconKind::Home:
    case IconKind::Window:
        return R"(<rect x="2" y="4" width="20" height="16" rx="2"/><path d="M10 4v4"/><path d="M2 8h20"/><path d="M6 4v4"/>)";
    case IconKind::Exit:
        return R"(<path d="m16 17 5-5-5-5"/><path d="M21 12H9"/><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/>)";
    case IconKind::Settings:
        return R"(<path d="M10 5H3"/><path d="M12 19H3"/><path d="M14 3v4"/><path d="M16 17v4"/><path d="M21 12h-9"/><path d="M21 19h-5"/><path d="M21 5h-7"/><path d="M8 10v4"/><path d="M8 12H3"/>)";
    case IconKind::Info:
        return R"(<circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>)";
    case IconKind::Menu:
        return R"(<path d="M4 5h16"/><path d="M4 12h16"/><path d="M4 19h16"/>)";
    case IconKind::Warning:
        return R"(<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3"/><path d="M12 9v4"/><path d="M12 17h.01"/>)";
    case IconKind::Add:
        return R"(<path d="M5 12h14"/><path d="M12 5v14"/>)";
    case IconKind::Power:
        return R"(<path d="M12 2v10"/><path d="M18.4 6.6a9 9 0 1 1-12.77.04"/>)";
    default: return "";
    }
}

static ID2D1StrokeStyle* g_lucideStroke = nullptr;
static std::map<IconKind, ID2D1PathGeometry*> g_lucideGeometries;

static ID2D1StrokeStyle* LucideStrokeStyle() {
    if (!g_lucideStroke && g_d2d) {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap = D2D1_CAP_STYLE_ROUND;
        props.dashCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        props.miterLimit = 2.0f;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        g_d2d->CreateStrokeStyle(props, nullptr, 0, &g_lucideStroke);
    }
    return g_lucideStroke;
}

static ID2D1PathGeometry* BuildLucideGeometry(IconKind kind) {
    auto cached = g_lucideGeometries.find(kind);
    if (cached != g_lucideGeometries.end()) return cached->second;
    auto contours = ParseSvgBody(IconSvgBody(kind));
    ID2D1PathGeometry* geo = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(g_d2d->CreatePathGeometry(&geo)) || !geo) return nullptr;
    geo->Open(&sink);
    if (sink) {
        for (auto& c : contours) {
            if (c.points.empty()) continue;
            sink->BeginFigure(c.points.front(), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < c.points.size(); ++i) sink->AddLine(c.points[i]);
            sink->EndFigure(c.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        }
        sink->Close();
        sink->Release();
    }
    g_lucideGeometries[kind] = geo;
    return geo;
}

static void DrawIcon(Painter& p, IconKind kind, R box, D2D1_COLOR_F color) {
    if (kind == IconKind::None || !p.rt || box.w <= 2 || box.h <= 2) return;
    ID2D1PathGeometry* geo = BuildLucideGeometry(kind);
    if (!geo) return;
    float s = std::min(box.w, box.h);
    float scale = s / 24.0f;
    float x = box.x + (box.w - s) / 2;
    float y = box.y + (box.h - s) / 2;
    D2D1_MATRIX_3X2_F old{};
    p.rt->GetTransform(&old);
    p.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(x, y));
    auto* brush = p.Brush(color);
    if (brush) {
        if (IconIsFilled(kind)) p.rt->FillGeometry(geo, brush);
        else p.rt->DrawGeometry(geo, brush, 2.0f, LucideStrokeStyle());
        brush->Release();
    }
    p.rt->SetTransform(old);
}


static void DrawButton(Painter& p, R r, const std::wstring& text, Act action, int index,
    bool primary, bool danger, bool disabled = false, bool iconOnly = false, bool spinner = false,
    IconKind icon = IconKind::None, bool running = false) {
    bool hot = r.has(g->mouseX, g->mouseY) && !disabled;
    D2D1_COLOR_F bg = g->colors.transparent;
    D2D1_COLOR_F fg = g->colors.sub;
    if (disabled) {
        bg = g->colors.chip; fg = g->colors.faint;
    } else if (primary) {
        bg = danger ? g->colors.redSoft : (hot ? g->colors.chipHover : g->colors.chip);
        fg = danger ? g->colors.red : g->colors.accent;
    } else {
        bg = hot ? g->colors.surface2 : g->colors.transparent;
        fg = iconOnly ? (hot ? g->colors.accentHover : g->colors.accent) : (hot ? g->colors.text : g->colors.sub);
    }
    if (bg.a > 0) p.FillRound(r, 7, bg);
    if (!disabled) {
        D2D1_COLOR_F edge = primary && !danger ? D2D1::ColorF(g->colors.accent.r, g->colors.accent.g, g->colors.accent.b, 0.32f)
            : g->colors.border;
        p.StrokeRound(r, 7, edge, 1);
    }
    if (running && !disabled) DrawRunningBorder(p, r, g->colors.accent);
    if (spinner) {
        DrawSpinner(p, r, fg);
    } else if (iconOnly) {
        DrawIcon(p, icon, Inset(r, 6), fg);
    } else if (icon != IconKind::None) {
        DrawIcon(p, icon, MakeR(r.x + 8, r.y + (r.h - 16) / 2, 16, 16),
            hot ? g->colors.accentHover : g->colors.accent);
        p.Text(text, MakeR(r.x + 28, r.y, r.w - 32, r.h), 11.2f, 500, fg);
    } else {
        p.Text(text, r, 13.0f, 600, fg, 1);
    }
    if (!disabled) g->mainSpots.push_back({ r, action, index });
}

static Hotspot* HitSpot(std::vector<Hotspot>& spots, float x, float y);

static std::wstring PrimaryTooltip(const Snapshot& s) {
    if (s.project.kind() == Kind::WindowsService && !s.serviceInstalled) return L"安装服务";
    if (s.status == Status::Starting || s.status == Status::Stopping) return L"处理中";
    if (s.status == Status::Running) return L"停止项目";
    if (s.status == Status::Failed) return L"重试启动";
    return L"启动项目";
}

static std::wstring TooltipLabel(const Hotspot& h, const std::vector<Snapshot>& snaps) {
    switch (h.action) {
    case Act::Add:
    case Act::EmptyAdd: return L"添加项目";
    case Act::StartAll: return L"全部启动";
    case Act::StopAll: return L"全部停止";
    case Act::RestartAll: return L"全部重启";
    case Act::CardPrimary:
        if (h.index >= 0 && h.index < (int)snaps.size()) return PrimaryTooltip(snaps[h.index]);
        return L"启动 / 停止";
    case Act::CardRestart: return L"重启项目";
    case Act::Menu: return L"设置与关于";
    case Act::CardBuild: return L"重新编译（Debug）";
    case Act::CardBuildRelease: return L"Release 编译";
    case Act::CardLog: return L"查看运行日志";
    case Act::CardMore: return L"更多操作";
    case Act::CardOpen: return L"在浏览器打开";
    default: return L"";
    }
}

static void DrawTooltip(Painter& p, const std::wstring& text) {
    if (text.empty() || g->mouseX < 0 || g->mouseY < 0) return;
    float fontSize = 10.2f;
    float w = p.Measure(text, fontSize, 500) + 18;
    float h = 24;
    float x = std::clamp(g->mouseX + 14, 8.0f, std::max(8.0f, p.W() - w - 8));
    float y = g->mouseY + 18;
    if (y + h > p.H() - 8) y = g->mouseY - h - 12;
    R box = MakeR(x, y, w, h);
    p.FillRound(box, 6, g->colors.surface);
    p.StrokeRound(box, 6, g->colors.border, 1);
    p.Text(text, box, fontSize, 500, g->colors.text, 1);
}

static void DrawHeaderAdd(Painter& p, R r) {
    bool hot = r.has(g->mouseX, g->mouseY);
    p.FillRound(r, 8, hot ? g->colors.accentHover : g->colors.accent);
    DrawIcon(p, IconKind::Add, MakeR(r.x + 11, r.y + 9, 16, 16), g->colors.white);
    p.Text(L"添加项目", MakeR(r.x + 32, r.y, r.w - 36, r.h), 12.0f, 600, g->colors.white);
    g->mainSpots.push_back({ r, Act::Add, -1 });
}

static void PaintMain(Painter& p) {
    if (!p.Begin(g->colors.bg)) return;
    g->mouseX = g->mainMouseX;
    g->mouseY = g->mainMouseY;
    auto snaps = g->engine.Snapshots();
    g->mainSpots.clear();
    float W = p.W(), H = p.H();

    // Header
    p.Fill(MakeR(0, 0, W, 72), g->colors.surface);
    p.FillRound(MakeR(14, 21, 3, 30), 1.5f, g->colors.accent);
    p.Text(L"DevServeManage", MakeR(26, 8, std::max(180.0f, W - 420), 32), 22, 700, g->colors.text);
    p.Text(L"本地项目统一运行台", MakeR(27, 41, 320, 20), 10.5f, 400, g->colors.faint);

    int running = 0;
    for (auto& s : snaps) if (IsActive(s.status)) ++running;
    g->runningCount = running;
    g->totalCount = (int)snaps.size();

    float statsX = std::max(250.0f, 28 + p.Measure(L"DevServeManage", 22, 700) + 30);
    p.Line(statsX - 14, 23, statsX - 14, 49, g->colors.border, 1);
    float chipX = statsX;
    std::wstring projectCount = Format(L"%d 个项目", (int)snaps.size());
    float projectW = p.Measure(projectCount, 10.0f, 500) + 22;
    R projectChip = MakeR(chipX, 25, projectW, 22);
    p.FillRound(projectChip, 6, g->colors.chip);
    p.Ellipse(projectChip.x + 9, projectChip.y + 11, 2.4f, 2.4f, g->colors.accent);
    p.Text(projectCount, MakeR(projectChip.x + 15, projectChip.y, projectChip.w - 19, projectChip.h),
        10.0f, 500, g->colors.sub);
    chipX = projectChip.x + projectChip.w + 6;
    std::wstring runningText = Format(L"%d 运行中", running);
    float runningW = p.Measure(runningText, 10.0f, 600) + 22;
    R runningChip = MakeR(chipX, 25, runningW, 22);
    p.FillRound(runningChip, 6, running > 0 ? g->colors.greenSoft : g->colors.chip);
    p.Ellipse(runningChip.x + 9, runningChip.y + 11, 2.4f, 2.4f,
        running > 0 ? g->colors.green : g->colors.faint);
    p.Text(runningText, MakeR(runningChip.x + 15, runningChip.y, runningChip.w - 19, runningChip.h),
        10.0f, 500, running > 0 ? g->colors.green : g->colors.faint);

    float bx = W - 20;
    R hamburger = MakeR(bx - 34, 19, 34, 34);
    DrawButton(p, hamburger, L"", Act::Menu, -1, false, false, false, true, false, IconKind::Menu);
    bx = hamburger.x - 8;
    R add = MakeR(bx - 108, 19, 108, 34);
    DrawHeaderAdd(p, add);
    bx = add.x - 8;
    p.Line(bx + 1, 24, bx + 1, 48, g->colors.border, 1);
    bx = add.x - 14;
    R stopAll = MakeR(bx - 80, 19, 80, 34);
    DrawButton(p, stopAll, L"全部停止", Act::StopAll, -1, false, false, false, false, false, IconKind::Stop);
    bx = stopAll.x - 6;
    R startAll = MakeR(bx - 80, 19, 80, 34);
    DrawButton(p, startAll, L"全部启动", Act::StartAll, -1, false, false, false, false, false, IconKind::Play);

    p.Line(0, 72, W, 72, g->colors.border, 1);

    float top = 88;
    if (snaps.empty()) {
        R card = MakeR(20, top, W - 40, 150);
        p.FillRound(card, 12, g->colors.surface);
        p.StrokeRound(card, 12, g->colors.border, 1);
        p.FillRound(MakeR(card.x + card.w / 2 - 22, top + 18, 44, 44), 13, g->colors.chip);
        p.Text(L"+", MakeR(card.x + card.w / 2 - 22, top + 18, 44, 44), 22, 400, g->colors.accent, 1);
        p.Text(L"还没有项目", MakeR(card.x, top + 72, card.w, 22), 14, 600, g->colors.text, 1);
        p.Text(L"点击右上角添加项目，或把项目文件夹直接拖进窗口", MakeR(card.x, top + 96, card.w, 18), 10.0f, 400, g->colors.faint, 1);
        R addBtn = MakeR(card.x + card.w / 2 - 58, top + 120, 116, 26);
        bool hot = addBtn.has(g->mouseX, g->mouseY);
        p.FillRound(addBtn, 7, hot ? g->colors.chipHover : g->colors.chip);
        p.StrokeRound(addBtn, 7, D2D1::ColorF(g->colors.accent.r, g->colors.accent.g, g->colors.accent.b, 0.4f), 1);
        p.Text(L"添加项目", addBtn, 11.0f, 600, g->colors.accent, 1);
        g->mainSpots.push_back({ addBtn, Act::EmptyAdd, -1 });
        p.End();
        return;
    }

    float cardW = W - 40, cardH = 68, gap = 5;
    float contentH = (float)snaps.size() * (cardH + gap) - gap;
    float avail = H - top - 36;
    float maxScroll = std::max(0.0f, contentH - avail);
    if (g->scroll > maxScroll) g->scroll = maxScroll;
    if (g->scroll < 0) g->scroll = 0;

    for (size_t i = 0; i < snaps.size(); ++i) {
        auto& s = snaps[i];
        float y = top + (float)i * (cardH + gap) - g->scroll;
        if (y + cardH < top - 4 || y > H + 4) continue;
        R card = MakeR(20, y, cardW, cardH);
        bool hover = card.has(g->mouseX, g->mouseY);
        p.FillRound(MakeR(card.x, card.y + 2, card.w, card.h), 10, C(0x000000, g->dark ? 0.30f : 0.06f));
        p.FillRound(card, 10, hover ? g->colors.surface2 : g->colors.surface);
        p.StrokeRound(card, 10, g->colors.border, 1);
        D2D1_COLOR_F accent = StatusColor(s);
        if (s.status == Status::Stopped) accent = g->colors.sub;
        p.FillRound(MakeR(card.x + 1, card.y + 7, 3.0f, cardH - 14), 2, accent);

        float contentRight = card.x + card.w - 12;
        float buttonsY = card.y + 18;
        R more = MakeR(contentRight - 32, buttonsY, 32, 32);
        R release = MakeR(more.x - 6 - 32, buttonsY, 32, 32);
        R debugBuild = MakeR(release.x - 6 - 32, buttonsY, 32, 32);
        R log = MakeR(debugBuild.x - 6 - 32, buttonsY, 32, 32);
        R restart = MakeR(log.x - 6 - 32, buttonsY, 32, 32);
        R primary = MakeR(restart.x - 6 - 32, buttonsY, 32, 32);
        contentRight = primary.x - 12;

        float nameX = card.x + 16;
        float nameW = std::min(p.Measure(s.project.DisplayName(), 15.0f, 600), std::max(50.0f, contentRight - nameX - 140));
        p.Text(s.project.DisplayName(), MakeR(nameX, card.y + 1, std::max(50.0f, nameW), 21), 15.0f, 600, g->colors.text);
        float badgeW = std::max(46.0f, p.Measure(KindText(s.project.kind()), 9.5f, 600) + 12);
        R kindBadge = MakeR(nameX + nameW + 7, card.y + 3, badgeW, 17);
        DrawBadge(p, kindBadge, KindText(s.project.kind()), g->colors.chip, g->colors.sub);
        if (!s.project.framework.empty()) {
            p.Text(s.project.framework, MakeR(kindBadge.x + kindBadge.w + 7, card.y + 2, std::max(24.0f, contentRight - kindBadge.x - kindBadge.w - 7), 18),
                10.0f, 400, g->colors.faint);
        }
        bool serviceNotInstalled = s.project.kind() == Kind::WindowsService && !s.serviceInstalled;
        bool failed = s.status == Status::Failed;
        std::wstring meta;
        if (s.status == Status::Running || s.status == Status::Starting || s.status == Status::Stopping) {
            if (s.pid) meta = L"PID " + std::to_wstring(s.pid);
            if (s.startedAt) {
                if (!meta.empty()) meta += L" · ";
                meta += UptimeText(s.startedAt);
            }
        } else if (s.project.kind() == Kind::WindowsService) {
            meta = s.serviceState.empty() ? L"未安装" : s.serviceState;
        } else if (failed) {
            meta = s.lastError.empty() ? L"进程已退出" : s.lastError;
        }
        if (failed || serviceNotInstalled) {
            std::wstring warning = serviceNotInstalled
                ? (L"服务未安装（" + s.project.serviceName + L"），点击左侧按钮安装。") : meta;
            p.Text(warning, MakeR(nameX, card.y + 22, std::max(50.0f, contentRight - nameX), 16), 9.8f, 400,
                serviceNotInstalled ? g->colors.amber : g->colors.red);
        } else {
            p.Text(s.project.rootPath, MakeR(nameX, card.y + 22, std::max(50.0f, contentRight - nameX), 16), 9.8f, 400, g->colors.faint);
        }

        // Status and actual port are the primary information in the row.
        std::wstring status = StatusLabel(s);
        float statusW = std::max(56.0f, p.Measure(status, 11.0f, 600) + 22);
        R statusPill = MakeR(card.x + 16, card.y + 38, statusW, 20);
        D2D1_COLOR_F statusBg = g->colors.chip;
        if (s.status == Status::Running) statusBg = g->colors.greenSoft;
        else if (s.status == Status::Failed) statusBg = g->colors.redSoft;
        else if (s.status == Status::Starting || s.status == Status::Stopping) statusBg = g->colors.amberSoft;
        p.FillRound(statusPill, 10, statusBg);
        p.Ellipse(statusPill.x + 9, statusPill.y + 10, 3.2f, 3.2f, accent);
        p.Text(status, MakeR(statusPill.x + 16, statusPill.y, statusPill.w - 18, statusPill.h), 11.0f, 700, accent);
        float chipX = statusPill.x + statusPill.w + 7;
        if (!s.ports.empty()) {
            for (int port : s.ports) {
                std::wstring label = L":" + std::to_wstring(port);
                float cw = std::max(48.0f, p.Measure(label, 11.2f, 700) + 16);
                if (chipX + cw > contentRight - 130) break;
                R chip = MakeR(chipX, card.y + 38, cw, 20);
                bool hot = chip.has(g->mouseX, g->mouseY);
                p.FillRound(chip, 5, hot ? g->colors.chipHover : g->colors.chip);
                p.StrokeRound(chip, 5, hot ? g->colors.accentHover : D2D1::ColorF(g->colors.accent.r, g->colors.accent.g, g->colors.accent.b, 0.42f), 1);
                p.Text(label, chip, 11.2f, 700, g->colors.accent, 1);
                g->mainSpots.push_back({ chip, Act::CardOpen, (int)i });
                chipX += cw + 5;
            }
        } else if (s.project.port > 0) {
            R chip = MakeR(chipX, card.y + 38, 76, 20);
            DrawBadge(p, chip, L"预期 :" + std::to_wstring(s.project.port), g->colors.chip, g->colors.faint);
            chipX += 81;
        }
        if (!meta.empty() && !failed && !serviceNotInstalled) {
            float metaX = std::max(chipX + 8, statusPill.x + statusPill.w + 10);
            p.Text(meta, MakeR(metaX, card.y + 38, std::max(30.0f, contentRight - metaX), 20), 10.3f, 400, g->colors.sub);
        }

        bool notInstalled = s.project.kind() == Kind::WindowsService && !s.serviceInstalled;
        bool busy = s.status == Status::Starting || s.status == Status::Stopping;
        WIN32_FILE_ATTRIBUTE_DATA logAttr{};
        bool hasLog = GetFileAttributesExW(g->engine.LogFileFor(s.project.id).c_str(),
            GetFileExInfoStandard, &logAttr) != FALSE
            && (logAttr.nFileSizeHigh > 0 || logAttr.nFileSizeLow > 0);
        IconKind primaryIcon = notInstalled ? IconKind::Warning : busy ? IconKind::Restart :
            (IsActive(s.status) ? IconKind::Stop : IconKind::Play);
        DrawButton(p, primary, L"", Act::CardPrimary, (int)i, true, IsActive(s.status),
            busy, true, busy, primaryIcon, busy && !notInstalled);
        DrawButton(p, restart, L"", Act::CardRestart, (int)i, false, false, notInstalled, true, false, IconKind::Restart, busy);
        DrawButton(p, log, L"", Act::CardLog, (int)i, false, false, !hasLog, true, false, IconKind::Document);
        bool hasBuildDebug = !s.project.buildCommandDebug.empty();
        bool hasBuildRelease = !s.project.buildCommandRelease.empty();
        bool building = g_building.count(s.project.id) > 0;
        DrawButton(p, debugBuild, L"", Act::CardBuild, (int)i, false, false,
            !hasBuildDebug, true, false, IconKind::Build, building);
        DrawButton(p, release, L"", Act::CardBuildRelease, (int)i, false, false,
            !hasBuildRelease, true, false, IconKind::Release, building);
        DrawButton(p, more, L"", Act::CardMore, (int)i, false, false, false, true, false, IconKind::More);

        if (hover) {
            p.StrokeRound(card, 10, D2D1::ColorF(g->colors.accent.r, g->colors.accent.g, g->colors.accent.b, 0.55f), 1);
        }
    }

    if (maxScroll > 0) {
        float trackX = W - 9, trackY = top, trackH = avail;
        p.FillRound(MakeR(trackX, trackY, 4, trackH), 2, g->colors.border);
        float thumbH = std::max(30.0f, trackH * avail / contentH);
        float thumbY = trackY + (trackH - thumbH) * (g->scroll / maxScroll);
        p.FillRound(MakeR(trackX, thumbY, 4, thumbH), 2, g->colors.faint);
    }
    p.Line(20, H - 30, W - 20, H - 30, g->colors.border, 1);
    p.Text(L"端口以进程实际监听为准", MakeR(W - 240, H - 24, 220, 18), 9.8f, 400, g->colors.faint, 2);
    if (auto* h = HitSpot(g->mainSpots, g->mouseX, g->mouseY)) {
        DrawTooltip(p, TooltipLabel(*h, snaps));
    }
    p.End();
}

static void InvalidateMain() {
    if (g && g->main) InvalidateRect(g->main, nullptr, FALSE);
}

static void PaintMainWindow(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    (void)dc;
    PaintMain(g->mainPainter);
    EndPaint(hwnd, &ps);
}

static Hotspot* HitSpot(std::vector<Hotspot>& spots, float x, float y) {
    for (auto it = spots.rbegin(); it != spots.rend(); ++it) if (it->r.has(x, y)) return &*it;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Project actions
// ---------------------------------------------------------------------------
static void StartProject(int index, HWND owner) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    auto& s = snaps[index];
    if (IsActive(s.status)) return;
    PortOwner ownerInfo;
    if (g->engine.CheckPortConflict(s.project.id, ownerInfo)) {
        if (ownerInfo.pid == 4) {
            ShowInfoDialog(owner, L"端口被占用", Format(L"端口 %d 被系统进程占用，无法自动结束。", ownerInfo.port));
            return;
        }
        std::wstring msg = Format(L"端口 %d 已被进程 PID %lu 占用。\n\n是否结束占用进程并启动「%s」？",
            ownerInfo.port, ownerInfo.pid, s.project.DisplayName().c_str());
        if (!ShowConfirmDialog(owner, L"端口被占用", msg, L"结束并启动", L"取消", true)) return;
        g->engine.KillProcessTree(ownerInfo.pid);
        Sleep(180);
    }
    g->engine.Start(s.project.id);
    InvalidateMain();
}

static void StopProject(int index) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    g->engine.Stop(snaps[index].project.id);
    InvalidateMain();
}

static void ToggleProject(int index, HWND owner) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    if (IsActive(snaps[index].status)) StopProject(index);
    else StartProject(index, owner);
}

static void OpenProjectLog(int index) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    std::wstring path = g->engine.LogFileFor(snaps[index].project.id);
    if (!FileExists(path)) {
        WriteTextFile(path, Narrow(L"还没有日志。\r\n"));
    }
    ShellExecuteW(g->main, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void CopyText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) { memcpy(p, text.c_str(), bytes); GlobalUnlock(mem); SetClipboardData(CF_UNICODETEXT, mem); }
    }
    CloseClipboard();
}

static void ConfigureProjectMenu(HMENU menu, const Snapshot& snapshot) {
    int dpi = (int)GetDpiForWindow(g->main);
    g_projectMenuFont = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_projectMenuIconFont = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    g_projectMenuBrush = CreateSolidBrush(RGB((int)(g->colors.surface.r * 255),
        (int)(g->colors.surface.g * 255), (int)(g->colors.surface.b * 255)));
    MENUINFO mi{}; mi.cbSize = sizeof(mi); mi.dwStyle = MNS_NOCHECK;
    mi.hbrBack = g_projectMenuBrush;
    SetMenuInfo(menu, &mi);

    bool hasDebug = !snapshot.project.buildCommandDebug.empty();
    bool hasRelease = !snapshot.project.buildCommandRelease.empty();
    g_projectMenuEntries = {
        { 1, L"打开项目目录", L"\uE8B7", false, true },
        { 2, L"打开日志", L"\uE8A5", false, true },
        { 3, L"复制项目路径", L"\uE8C8", false, true },
        { 4, L"编辑项目…", L"\uE70F", false, true },
        { 6, L"重新编译（Debug）", L"\uE90F", false, hasDebug },
        { 7, L"Release 编译", L"\uE7B8", false, hasRelease },
        { 8, L"打开 Debug 输出目录", L"\uE838", false, true },
        { 9, L"打开 Release 输出目录", L"\uE838", false, true },
        { 5, L"移除项目", L"\uE74D", true, true },
        { 10, L"直接运行（不安装服务）…", L"\uE768", false, true },
    };
    for (auto& entry : g_projectMenuEntries) {
        UINT flags = MF_BYCOMMAND | MF_OWNERDRAW | (entry.enabled ? 0 : MF_GRAYED);
        ModifyMenuW(menu, entry.id, flags, entry.id, (LPWSTR)&entry);
    }
}

static void ReleaseProjectMenuResources() {
    if (g_projectMenuFont) { DeleteObject(g_projectMenuFont); g_projectMenuFont = nullptr; }
    if (g_projectMenuIconFont) { DeleteObject(g_projectMenuIconFont); g_projectMenuIconFont = nullptr; }
    if (g_projectMenuBrush) { DeleteObject(g_projectMenuBrush); g_projectMenuBrush = nullptr; }
    g_projectMenuEntries.clear();
}

static void ShowProjectMenuLegacy(int index, POINT pt) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"打开项目目录");
    AppendMenuW(menu, MF_STRING, 2, L"打开日志");
    AppendMenuW(menu, MF_STRING, 3, L"复制项目路径");
    AppendMenuW(menu, MF_STRING, 4, L"编辑项目…");
    AppendMenuW(menu, MF_STRING, 8, L"打开 Debug 输出目录");
    AppendMenuW(menu, MF_STRING, 9, L"打开 Release 输出目录");
    AppendMenuW(menu, MF_STRING, 5, L"移除项目");
    if (snaps[index].project.kind() == Kind::WindowsService)
        AppendMenuW(menu, MF_STRING, 10, L"直接运行（不安装服务）…");
    ConfigureProjectMenu(menu, snaps[index]);
    SetForegroundWindow(g->main);
    int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        pt.x, pt.y, g->main, nullptr);
    DestroyMenu(menu);
    ReleaseProjectMenuResources();
    PostMessageW(g->main, WM_NULL, 0, 0);
    if (cmd == 1) ShellExecuteW(g->main, L"open", snaps[index].project.rootPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 2) OpenProjectLog(index);
    else if (cmd == 3) CopyText(g->main, snaps[index].project.rootPath);
    else if (cmd == 4) {
        Project edited = snaps[index].project;
        if (ShowEditProjectDialog(g->main, edited)) InvalidateMain();
    }
    else if (cmd == 6) StartBuild(snaps[index], false);
    else if (cmd == 7) StartBuild(snaps[index], true);
    else if (cmd == 8) OpenFolderPath(BuildOutputDir(snaps[index].project, false));
    else if (cmd == 9) OpenFolderPath(BuildOutputDir(snaps[index].project, true));
    else if (cmd == 10) {
        ShowInfoDialog(g->main, L"Windows 服务直接运行",
            L"这个项目不能像普通控制台程序一样直接运行。\n\n"
            L"它的 Program.cs 无条件调用 ServiceBase.Run(...)，没有 Environment.UserInteractive 或 --console 分支；"
            L"直接双击 exe 会被 SCM 拒绝（错误 1063）。\n\n"
            L"可以先用扩展菜单里的「安装服务并启动（管理员）…」把服务注册到 SCM，"
            L"之后就能直接启动 / 停止。\n\n"
            L"现代 .NET Worker（UseWindowsService）通常可以直接 dotnet run，但本项目不是这种结构。");
    }
    else if (cmd == 13) {
        ShellExecuteW(g->main, L"open", L"services.msc", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (cmd == 5) {
        std::wstring msg = L"确定移除项目「" + snaps[index].project.DisplayName() + L"」？\n\n不会删除磁盘上的文件。";
        if (!ShowConfirmDialog(g->main, L"移除项目", msg, L"移除", L"取消", true)) return;
        if (IsActive(snaps[index].status)) StopProject(index);
        std::wstring err;
        if (!g->engine.RemoveProject(snaps[index].project.id, &err))
            ShowInfoDialog(g->main, L"移除失败", err);
        InvalidateMain();
    }
}

static std::wstring ServiceExecutablePath(const Project& project) {
    std::wstring stem = project.projectFile.empty()
        ? project.name : std::filesystem::path(project.projectFile).stem().wstring();
    if (stem.empty()) stem = project.name;
    // Prefer the resolved Debug/Release artifact directories. This covers
    // custom OutputPath values and SDK projects whose exe lives under a
    // target-framework or publish subdirectory.
    for (bool release : { false, true }) {
        std::wstring dir = ProjectOutputDir(project, release);
        if (dir.empty()) continue;
        std::wstring path = JoinPath(dir, stem + L".exe");
        if (FileExists(path)) return path;
        path = JoinPath(dir, project.name + L".exe");
        if (FileExists(path)) return path;
    }
    for (const wchar_t* config : { L"Debug", L"Release" }) {
        std::wstring path = JoinPath(project.rootPath,
            std::wstring(L"bin\\") + config + L"\\" + stem + L".exe");
        if (FileExists(path)) return path;
    }
    for (const wchar_t* config : { L"Debug", L"Release" }) {
        std::wstring path = JoinPath(project.rootPath,
            std::wstring(L"bin\\") + config + L"\\" + project.name + L".exe");
        if (FileExists(path)) return path;
    }
    return {};
}

static std::wstring InstallUtilPath() {
    wchar_t windir[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windir, _countof(windir))) return {};
    std::wstring base = windir;
    for (const wchar_t* framework : { L"Framework64", L"Framework" }) {
        std::wstring path = JoinPath(base,
            std::wstring(L"Microsoft.NET\\") + framework + L"\\v4.0.30319\\InstallUtil.exe");
        if (FileExists(path)) return path;
    }
    return {};
}

static bool RunElevatedWait(const std::wstring& file, const std::wstring& parameters,
    const std::wstring& workingDir, DWORD& exitCode, DWORD& error) {
    exitCode = 1; error = 0;
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    sei.lpDirectory = workingDir.empty() ? nullptr : workingDir.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        error = GetLastError();
        return false;
    }
    if (!sei.hProcess) { error = ERROR_GEN_FAILURE; return false; }
    DWORD wait = WaitForSingleObject(sei.hProcess, 120000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(sei.hProcess, 1);
        WaitForSingleObject(sei.hProcess, 2000);
    }
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return wait != WAIT_FAILED;
}

static void ManageWindowsService(int index, bool uninstall) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    const Project project = snaps[index].project;
    if (project.kind() != Kind::WindowsService || project.serviceName.empty()) {
        ShowInfoDialog(g->main, L"无法管理服务", L"这个项目没有识别到 Windows 服务名。");
        return;
    }
    std::wstring displayName = project.name.empty() ? project.DisplayName() : project.name;
    std::wstring nameInfo = L"项目名称：" + project.DisplayName() + L"\n"
        L"SCM 服务名：" + project.serviceName + L"（由项目 Installer 定义）\n"
        L"显示名称：" + displayName;
    if (uninstall) {
        std::wstring msg = L"确定卸载服务「" + project.serviceName + L"」？\n\n" + nameInfo +
            L"\n\n会先尝试停止服务，再请求管理员权限执行卸载。";
        if (!ShowConfirmDialog(g->main, L"卸载 Windows 服务", msg, L"卸载", L"取消", true)) return;
    } else {
        std::wstring msg = nameInfo +
            L"\n\n将安装并启动该服务，需要管理员权限（UAC）。服务账户由项目 Installer 决定。";
        if (!ShowConfirmDialog(g->main, L"安装 Windows 服务", msg, L"安装并启动", L"取消")) return;
    }

    std::wstring exe = ServiceExecutablePath(project);
    std::wstring installUtil = InstallUtilPath();
    wchar_t sysdir[MAX_PATH]{};
    GetSystemDirectoryW(sysdir, _countof(sysdir));
    std::wstring sc = JoinPath(sysdir, L"sc.exe");
    std::wstring cmd = JoinPath(sysdir, L"cmd.exe");
    std::wstring net = JoinPath(sysdir, L"net.exe");
    if (uninstall) {
        std::wstring ignored;
        StopServiceByName(project.serviceName, ignored);
        Sleep(500);
    }
    if (exe.empty() && !uninstall) {
        ShowInfoDialog(g->main, L"找不到服务程序",
            L"没有找到 bin\\Debug 或 bin\\Release 下的服务 exe。\n\n请先编译项目，再安装服务。");
        return;
    }

    DWORD exitCode = 1, error = 0;
    bool launched = false;
    if (uninstall) {
        if (!exe.empty() && !installUtil.empty()) {
            // Stop first (net stop waits), then run the project's uninstaller.
            std::wstring params = L"/d /s /c \"\"" + net + L"\" stop \"" + project.serviceName +
                L"\" >nul 2>&1 & \"" + installUtil + L"\" /u \"" + exe + L"\"\"";
            launched = RunElevatedWait(cmd, params, DirectoryOf(exe), exitCode, error);
        } else {
            std::wstring params = L"delete \"" + project.serviceName + L"\"";
            launched = RunElevatedWait(sc, params, project.rootPath, exitCode, error);
        }
    } else if (!installUtil.empty()) {
        // InstallUtil creates the SCM entry from ProjectInstaller; set the
        // user-facing display name to the project name in the same elevation.
        std::wstring params = L"/d /s /c \"\"" + installUtil + L"\" \"" + exe +
            L"\" && \"" + sc + L"\" config \"" + project.serviceName + L"\" DisplayName= \"" +
            displayName + L"\" && \"" + sc + L"\" start \"" + project.serviceName + L"\"\"";
        launched = RunElevatedWait(cmd, params, DirectoryOf(exe), exitCode, error);
    } else {
        // Fallback when InstallUtil is unavailable: create the entry directly.
        std::wstring params = L"/d /s /c \"\"" + sc + L"\" create \"" + project.serviceName +
            L"\" binPath= \"" + exe + L"\" start= demand obj= LocalSystem DisplayName= \"" +
            displayName + L"\" && \"" + sc + L"\" start \"" + project.serviceName + L"\"\"";
        launched = RunElevatedWait(cmd, params, project.rootPath, exitCode, error);
    }
    if (!launched) {
        if (error == ERROR_CANCELLED)
            ShowInfoDialog(g->main, L"已取消", L"管理员权限请求已取消。");
        else
            ShowInfoDialog(g->main, L"无法启动管理员进程",
                Format(L"错误码：%lu", error));
        return;
    }

    bool installed = false;
    DWORD state = 0;
    std::wstring queryError;
    QueryServiceInfo(project.serviceName, installed, state, queryError);
    if (uninstall) {
        if (!installed)
            ShowInfoDialog(g->main, L"卸载完成", L"服务「" + project.serviceName + L"」已卸载。");
        else
            ShowInfoDialog(g->main, L"卸载失败",
                L"服务仍存在。\n\n" + (exitCode ? Format(L"命令返回 %lu。", exitCode) : queryError));
        g->engine.Poll();
        InvalidateMain();
        return;
    }

    if (!installed) {
        ShowInfoDialog(g->main, L"安装失败",
            L"服务没有注册成功。\n\n" + (exitCode ? Format(L"命令返回 %lu。", exitCode) : queryError));
        return;
    }

    // The setup command starts it; if that step was refused, retry once here.
    if (state != SERVICE_RUNNING && state != SERVICE_START_PENDING) {
        std::wstring startError;
        StartServiceByName(project.serviceName, startError);
        Sleep(500);
        QueryServiceInfo(project.serviceName, installed, state, queryError);
    }
    g->engine.Poll();
    InvalidateMain();
    ShowInfoDialog(g->main, L"服务已安装",
        L"服务「" + project.serviceName + L"」已安装。\n\n"
        L"项目名称 / 显示名称：" + displayName + L"\n"
        L"SCM 服务名：" + project.serviceName + L"\n"
        L"当前状态：" + ServiceStateText(state) +
        L"\n\n后续可以直接点击项目卡片上的启动/停止按钮。");
}

static void ShowProjectMenu(int index, POINT pt) {
    auto snaps = g->engine.Snapshots();
    if (index < 0 || index >= (int)snaps.size()) return;
    auto& s = snaps[index];
    WIN32_FILE_ATTRIBUTE_DATA logAttr{};
    bool hasLog = GetFileAttributesExW(g->engine.LogFileFor(s.project.id).c_str(),
        GetFileExInfoStandard, &logAttr) != FALSE
        && (logAttr.nFileSizeHigh > 0 || logAttr.nFileSizeLow > 0);
    std::vector<UiMenuItem> items = {
        { 1, L"打开项目目录", IconKind::Folder, false, true, false },
        { 2, L"打开日志", IconKind::Document, false, hasLog, false },
        { 3, L"复制项目路径", IconKind::Copy, false, true, false },
        { 0, L"", IconKind::None, false, false, true },
        { 4, L"编辑项目…", IconKind::Edit, false, true, false },
        { 8, L"打开 Debug 输出目录", IconKind::Open, false, true, false },
        { 9, L"打开 Release 输出目录", IconKind::Open, false, true, false },
        { 0, L"", IconKind::None, false, false, true },
        { 5, L"移除项目", IconKind::Remove, true, true, false },
    };
    if (s.project.kind() == Kind::WindowsService) {
        items.push_back({ 11, s.serviceInstalled ? L"卸载服务（管理员）…" : L"安装服务并启动（管理员）…",
            s.serviceInstalled ? IconKind::Remove : IconKind::Package, s.serviceInstalled, !s.project.serviceName.empty(), false });
        items.push_back({ 13, L"打开服务管理器", IconKind::Window, false, true, false });
        items.push_back({ 10, L"直接运行（不安装服务）…", IconKind::Play, false, true, false });
    }
    g->mainMouseX = -1;
    g->mainMouseY = -1;
    InvalidateMain();
    int cmd = ShowCustomMenu(g->main, pt, items);
    if (cmd == 1) ShellExecuteW(g->main, L"open", s.project.rootPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd == 2) OpenProjectLog(index);
    else if (cmd == 3) CopyText(g->main, s.project.rootPath);
    else if (cmd == 4) {
        Project edited = s.project;
        if (ShowEditProjectDialog(g->main, edited)) InvalidateMain();
    }
    else if (cmd == 8) OpenFolderPath(BuildOutputDir(s.project, false));
    else if (cmd == 9) OpenFolderPath(BuildOutputDir(s.project, true));
    else if (cmd == 10) {
        ShowInfoDialog(g->main, L"Windows 服务直接运行",
            L"这个项目当前不能像普通控制台程序一样直接运行。\n\n"
            L"它的 Program.cs 无条件调用 ServiceBase.Run(...)，没有 Environment.UserInteractive 或 --console 分支；"
            L"直接双击 exe 会被 SCM 拒绝（错误 1063）。\n\n"
            L"可以先用扩展菜单里的「安装服务并启动（管理员）…」把服务注册到 SCM，"
            L"之后就能直接启动 / 停止。\n\n"
            L"Visual Studio 的 F5 对这类 ServiceBase 项目通常也需要先安装服务，或附加到已运行的服务进程。\n\n"
            L"如果确实需要无安装运行，只能修改 Program.cs，增加控制台模式分支。");
    }
    else if (cmd == 11) {
        ManageWindowsService(index, s.serviceInstalled);
    }
    else if (cmd == 13) {
        ShellExecuteW(g->main, L"open", L"services.msc", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (cmd == 5) {
        std::wstring msg = L"确定移除项目「" + s.project.DisplayName() + L"」？\n\n不会删除磁盘上的文件。";
        if (!ShowConfirmDialog(g->main, L"移除项目", msg, L"移除", L"取消", true)) return;
        if (IsActive(s.status)) StopProject(index);
        std::wstring err;
        if (!g->engine.RemoveProject(s.project.id, &err))
            ShowInfoDialog(g->main, L"移除失败", err);
        InvalidateMain();
    }
}

static void ShowAppMenu() {
    if (!g || !g->main) return;
    RECT client{};
    GetClientRect(g->main, &client);
    POINT origin{ 0, 0 };
    ClientToScreen(g->main, &origin);
    RECT bounds{ origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };

    std::vector<UiMenuItem> items = {
        { 1, L"设置", IconKind::Settings, false, true, false },
        { 2, L"关于", IconKind::Info, false, true, false },
    };
    // Keep the app menu inside the main window, right-aligned under the
    // hamburger button instead of escaping past the window edge.
    POINT pt{ origin.x + client.right - 20 - 270, origin.y + 72 };
    int cmd = ShowCustomMenu(g->main, pt, items, &bounds);
    if (cmd == 1) ShowSettingsWindow(g->main);
    else if (cmd == 2) ShowAboutDialog(g->main);
}

static void AddProjectPath(const std::wstring& path, HWND owner) {
    if (!DirectoryExists(path)) {
        ShowInfoDialog(owner, L"添加项目", L"请选择项目文件夹，而不是文件。");
        return;
    }
    Detection d = Engine::DetectFolder(path);
    if (!d.ok) {
        ShowInfoDialog(owner, L"没有识别到项目", d.message);
        return;
    }
    std::wstring err;
    if (!g->engine.AddProject(d.project, &err)) {
        ShowInfoDialog(owner, L"添加项目失败", err);
        return;
    }
    AppLog(L"添加项目：" + d.project.name + L" · " + path);
    InvalidateMain();
}

static void OpenFolderPath(const std::wstring& path) {
    if (path.empty() || !DirectoryExists(path)) {
        ShowInfoDialog(g->main, L"输出目录不存在", L"还没有找到编译输出目录，请先编译一次。");
        return;
    }
    ShellExecuteW(g->main, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::wstring BuildOutputDir(const Project& project, bool release) {
    return ProjectOutputDir(project, release);
}

static bool LooksLikeGeneratedBuildCommand(const std::wstring& command) {
    std::wstring lower = ToLower(command);
    return lower.find(L"dotnet build") != std::wstring::npos ||
        lower.find(L"dotnet publish") != std::wstring::npos ||
        lower.find(L"/t:build") != std::wstring::npos ||
        lower.find(L"/t:rebuild") != std::wstring::npos ||
        lower.find(L"/t:publish") != std::wstring::npos ||
        lower.find(L"/t:webpublish") != std::wstring::npos;
}

static void StartBuild(const Snapshot& snapshot, bool release) {
    std::wstring command = release ? snapshot.project.buildCommandRelease : snapshot.project.buildCommandDebug;
    std::wstring generated = ProjectBuildCommand(snapshot.project, release);
    bool generatedUsed = false;
    if (!generated.empty() && (command.empty() || LooksLikeGeneratedBuildCommand(command))) {
        command = generated;
        generatedUsed = true;
    }
    if (command.empty()) {
        ShowInfoDialog(g->main, L"没有编译命令",
            L"这个项目没有可用的" + std::wstring(release ? L" Release " : L" Debug ") + L"编译命令。");
        return;
    }
    if (release && generatedUsed) {
        std::wstring clearError;
        if (!CleanProjectPublishTarget(snapshot.project, &clearError)) {
            ShowInfoDialog(g->main, L"无法清理发布目录", clearError);
            return;
        }
    }
    if (g_building.count(snapshot.project.id)) {
        ShowInfoDialog(g->main, L"正在编译", L"这个项目已经在编译中，请等待完成。");
        return;
    }
    g_building.insert(snapshot.project.id);
    InvalidateMain();

    std::wstring wd = snapshot.project.workingDirectory.empty() ? snapshot.project.rootPath : snapshot.project.workingDirectory;
    std::wstring log = g->engine.LogFileFor(snapshot.project.id);
    if (log.size() > 4 && log.substr(log.size() - 4) == L".log") log = log.substr(0, log.size() - 4);
    log += release ? L".release.build.log" : L".debug.build.log";
    std::wstring output = BuildOutputDir(snapshot.project, release);
    std::wstring name = snapshot.project.DisplayName();
    std::wstring id = snapshot.project.id;
    HWND mainWindow = g->main;

    std::thread([command, wd, log, output, name, id, release, mainWindow]() {
        std::wstring inner = L"chcp 65001>nul & cd /d " + Quote(wd) + L" & " + command +
            L" > " + Quote(log) + L" 2>&1";
        std::wstring cmdline = L"cmd.exe /d /s /c \"" + inner + L"\"";
        std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
        mutableCmd.push_back(L'\0');
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        bool ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, wd.c_str(), &si, &pi) != FALSE;
        DWORD exitCode = 1;
        if (ok) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        auto* result = new BuildJobResult{ id, name, log, output, release, ok && exitCode == 0 };
        if (mainWindow) PostMessageW(mainWindow, WM_APP_BUILD_DONE, 0, (LPARAM)result);
    }).detach();
}

static void FinishBuild(BuildJobResult* result) {
    if (!result) return;
    g_building.erase(result->id);
    if (result->success) {
        // The directory may not exist until the first build has completed.
        // Resolve it again after compilation so the dialog and Explorer open
        // the real output/publish directory (including .NET Release paths
        // read from the project file), not the pre-build fallback.
        std::wstring outputDir = result->outputDir;
        Snapshot built = g->engine.Get(result->id);
        if (!built.project.id.empty()) {
            std::wstring resolved = BuildOutputDir(built.project, result->release);
            if (!resolved.empty()) outputDir = resolved;
        }
        std::wstring message = L"「" + result->name + L"」" + (result->release ? L" Release" : L" Debug") +
            L" 编译成功。\n\n输出目录：\n" + outputDir;
        std::wstring openLabel = result->release ? L"打开 Release 目录" : L"打开输出目录";
        if (ShowConfirmDialog(g->main, L"编译完成", message, openLabel, L"关闭", false, false))
            OpenFolderPath(outputDir);
    } else {
        std::wstring msg = L"「" + result->name + L"」编译失败。\n\n是否打开编译日志？";
        if (ShowConfirmDialog(g->main, L"编译失败", msg, L"打开日志", L"关闭", true))
            ShellExecuteW(g->main, L"open", result->logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    delete result;
    InvalidateMain();
}


static void AddFolderProject(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        ShowInfoDialog(owner, L"添加项目", L"无法打开文件夹选择器。");
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    std::wstring path;
    if (dialog->Show(owner) == S_OK) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
                path = raw;
                CoTaskMemFree(raw);
            }
            item->Release();
        }
    }
    dialog->Release();
    if (!path.empty()) AddProjectPath(path, owner);
}

static void HandleAction(Act action, int index) {
    switch (action) {
    case Act::Add:
    case Act::EmptyAdd: AddFolderProject(g->main); break;
    case Act::StartAll: g->engine.StartAll(); InvalidateMain(); break;
    case Act::StopAll: g->engine.StopAll(); InvalidateMain(); break;
    case Act::RestartAll: g->engine.RestartAll(); InvalidateMain(); break;
    case Act::Menu: ShowAppMenu(); break;
    case Act::CardPrimary: {
        auto s = g->engine.Snapshots();
        if (index >= 0 && index < (int)s.size() &&
            s[index].project.kind() == Kind::WindowsService && !s[index].serviceInstalled) {
            ManageWindowsService(index, false);
        } else {
            ToggleProject(index, g->main);
        }
        break;
    }
    case Act::CardRestart: {
        auto s = g->engine.Snapshots();
        if (index >= 0 && index < (int)s.size()) g->engine.Restart(s[index].project.id);
        InvalidateMain();
        break;
    }
    case Act::CardLog: OpenProjectLog(index); break;
    case Act::CardBuild: {
        auto s = g->engine.Snapshots();
        if (index >= 0 && index < (int)s.size()) StartBuild(s[index], false);
        break;
    }
    case Act::CardBuildRelease: {
        auto s = g->engine.Snapshots();
        if (index >= 0 && index < (int)s.size()) StartBuild(s[index], true);
        break;
    }
    case Act::CardMore: {
        POINT pt{}; GetCursorPos(&pt);
        ShowProjectMenu(index, pt);
        break;
    }
    case Act::CardOpen: {
        auto s = g->engine.Snapshots();
        if (index >= 0 && index < (int)s.size() && !s[index].ports.empty()) {
            std::wstring url = L"http://localhost:" + std::to_wstring(s[index].ports[0]);
            ShellExecuteW(g->main, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Main window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g->main = hwnd;
        g->mainPainter.hwnd = hwnd;
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, TIMER_SPIN, 80, nullptr);
        ApplyDarkTitle(hwnd);
        return 0;
    case WM_SIZE:
        g->mainPainter.Discard();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        PaintMainWindow(hwnd);
        return 0;
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(ToColorRef(g->colors.bg));
        FillRect((HDC)wp, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_MOUSEMOVE: {
        float dpi = (float)GetDpiForWindow(hwnd);
        g->mainMouseX = GET_X_LPARAM(lp) * 96.0f / dpi;
        g->mainMouseY = GET_Y_LPARAM(lp) * 96.0f / dpi;
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            auto* h = HitSpot(g->mainSpots, g->mainMouseX, g->mainMouseY);
            SetCursor(LoadCursorW(nullptr, h ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    }
    case WM_MOUSELEAVE:
        g->mainMouseX = -1; g->mainMouseY = -1;
        g->hotMain = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        float dpi = (float)GetDpiForWindow(hwnd);
        float x = GET_X_LPARAM(lp) * 96.0f / dpi;
        float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
        auto* h = HitSpot(g->mainSpots, x, y);
        if (h) HandleAction(h->action, h->index);
        return 0;
    }
    case WM_RBUTTONUP: {
        float dpi = (float)GetDpiForWindow(hwnd);
        float x = GET_X_LPARAM(lp) * 96.0f / dpi;
        float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
        auto snaps = g->engine.Snapshots();
        if (x >= 20 && x <= g->mainPainter.W() - 20 && y >= 96) {
            int idx = (int)((y - 96 + g->scroll) / 73.0f);
            if (idx >= 0 && idx < (int)snaps.size()) {
                POINT pt{}; GetCursorPos(&pt);
                ShowProjectMenu(idx, pt);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g->scroll -= delta / (float)WHEEL_DELTA * 56.0f;
        if (g->scroll < 0) g->scroll = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            PollEngine();
            UpdateTrayIcon();
            if (IsWindowVisible(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == TIMER_SPIN) {
            bool animate = !g_building.empty();
            if (!animate) {
                auto snaps = g->engine.Snapshots();
                for (auto& s : snaps) {
                    if (s.status == Status::Starting || s.status == Status::Stopping) {
                        animate = true;
                        break;
                    }
                }
            }
            if (animate) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = (MEASUREITEMSTRUCT*)lp;
        if (m && m->CtlType == ODT_MENU) {
            m->itemWidth = 250;
            m->itemHeight = 32;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (!d || d->CtlType != ODT_MENU) break;
        auto* e = (ProjectMenuEntry*)d->itemData;
        if (!e) break;
        HBRUSH bg = CreateSolidBrush(RGB((int)((d->itemState & ODS_SELECTED ? g->colors.accentSoft : g->colors.surface).r * 255),
            (int)((d->itemState & ODS_SELECTED ? g->colors.accentSoft : g->colors.surface).g * 255),
            (int)((d->itemState & ODS_SELECTED ? g->colors.accentSoft : g->colors.surface).b * 255)));
        FillRect(d->hDC, &d->rcItem, bg);
        DeleteObject(bg);
        SetBkMode(d->hDC, TRANSPARENT);
        bool disabled = !e->enabled || (d->itemState & ODS_GRAYED) != 0;
        COLORREF fg = disabled ? RGB((int)(g->colors.faint.r * 255), (int)(g->colors.faint.g * 255), (int)(g->colors.faint.b * 255))
            : e->danger ? RGB((int)(g->colors.red.r * 255), (int)(g->colors.red.g * 255), (int)(g->colors.red.b * 255))
            : RGB((int)(g->colors.text.r * 255), (int)(g->colors.text.g * 255), (int)(g->colors.text.b * 255));
        COLORREF iconColor = disabled ? fg : RGB((int)(g->colors.accent.r * 255), (int)(g->colors.accent.g * 255), (int)(g->colors.accent.b * 255));
        RECT iconRect = d->rcItem; iconRect.left += 10; iconRect.right = iconRect.left + 24;
        if (g_projectMenuIconFont) SelectObject(d->hDC, g_projectMenuIconFont);
        SetTextColor(d->hDC, iconColor);
        DrawTextW(d->hDC, e->glyph.c_str(), -1, &iconRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT textRect = d->rcItem; textRect.left += 42; textRect.right -= 10;
        if (g_projectMenuFont) SelectObject(d->hDC, g_projectMenuFont);
        SetTextColor(d->hDC, fg);
        DrawTextW(d->hDC, e->label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH * 2]{};
            if (DragQueryFileW(drop, i, path, _countof(path))) {
                if (DirectoryExists(path)) AddProjectPath(path, hwnd);
                else {
                    std::wstring p = path;
                    size_t pos = p.find_last_of(L"\\/");
                    if (pos != std::wstring::npos) AddProjectPath(p.substr(0, pos), hwnd);
                }
            }
        }
        DragFinish(drop);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        AppLog(L"主窗口收进托盘");
        return 0;
    case WM_APP_SHOW:
        ShowMainWindow();
        return 0;
    case WM_APP_EXIT:
        RequestExit();
        return 0;
    case WM_APP_BUILD_DONE:
        FinishBuild((BuildJobResult*)lp);
        return 0;
    case WM_SETTINGCHANGE:
        RefreshTheme();
        return 0;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        RefreshTheme();
        return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        g->mainPainter.Discard();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 980;
        mm->ptMinTrackSize.y = 620;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ApplyDarkTitle(HWND hwnd) {
    if (!hwnd || !g) return;
    BOOL dark = g->dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    COLORREF caption = RGB((int)(g->colors.surface.r * 255), (int)(g->colors.surface.g * 255),
        (int)(g->colors.surface.b * 255));
    COLORREF text = RGB((int)(g->colors.text.r * 255), (int)(g->colors.text.g * 255),
        (int)(g->colors.text.b * 255));
    COLORREF border = RGB((int)(g->colors.border.r * 255), (int)(g->colors.border.g * 255),
        (int)(g->colors.border.b * 255));
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void RefreshTheme() {
    bool dark = EffectiveDark();
    g->dark = dark;
    g->colors = MakeColors(dark);
    ApplyDarkTitle(g->main);
    if (g->flyout) ApplyDarkTitle(g->flyout);
    InvalidateMain();
    if (g->flyout) InvalidateRect(g->flyout, nullptr, FALSE);
}

static void ShowMainWindowFirstFrame() {
    if (!g || !g->main) return;
    BOOL cloak = TRUE;
    HRESULT hr = DwmSetWindowAttribute(g->main, 13, &cloak, sizeof(cloak));
    g->mainPainter.hwnd = g->main;
    g->mainPainter.Ensure();
    ShowWindow(g->main, SW_SHOWNOACTIVATE);
    UpdateWindow(g->main);
    // Render one real frame while DWM still has the window cloaked.
    PaintMain(g->mainPainter);
    DwmFlush();
    if (SUCCEEDED(hr)) {
        cloak = FALSE;
        DwmSetWindowAttribute(g->main, 13, &cloak, sizeof(cloak));
    }
    SetForegroundWindow(g->main);
    InvalidateRect(g->main, nullptr, FALSE);
}

static void ShowMainWindow() {
    if (!g || !g->main) return;
    if (!IsWindowVisible(g->main)) {
        ShowMainWindowFirstFrame();
        return;
    }
    ShowWindow(g->main, IsIconic(g->main) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g->main);
    InvalidateRect(g->main, nullptr, FALSE);
}

static void PollEngine() {
    if (!g) return;
    g->engine.Poll();
    auto snaps = g->engine.Snapshots();
    for (auto& s : snaps) {
        if (s.status != Status::Failed || !s.portConflict) {
            g_conflictPrompted.erase(s.project.id);
            continue;
        }
        if (g_conflictPrompted.count(s.project.id)) continue;
        int port = s.conflictPort > 0 ? s.conflictPort : s.project.port;
        DWORD ownerPid = 0;
        if (port > 0 && QueryPortOwner(port, ownerPid) && ownerPid != 4) {
            g_conflictPrompted.insert(s.project.id);
            std::wstring msg = Format(L"「%s」启动失败：端口 %d 已被进程 PID %lu 占用。\n\n是否结束占用进程并重新启动？",
                s.project.DisplayName().c_str(), port, ownerPid);
            if (ShowConfirmDialog(g->main, L"端口被占用", msg, L"结束并重启", L"取消", true)) {
                g->engine.KillProcessTree(ownerPid);
                Sleep(180);
                g->engine.Start(s.project.id);
            }
        } else {
            g_conflictPrompted.insert(s.project.id);
        }
    }
}

static void RestartApplication() {
    if (!g || g->exiting) return;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, _countof(exe));
    std::wstring cmdline = L"cmd.exe /d /s /c \"ping 127.0.0.1 -n 3 >nul & start \"\" \"" +
        std::wstring(exe) + L"\"\"";
    std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    g->exiting = true;
    AppLog(L"重启程序");
    g->engine.StopAll();
    HideFlyout();
    if (g->trayAdded) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g->tray;
        nid.uID = 1;
        if (g->trayGuidMode) { nid.uFlags = NIF_GUID; nid.guidItem = kTrayGuid; }
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g->trayAdded = false;
    }
    DestroyWindow(g->main);
}

static void RequestExit() {
    if (g->exiting) return;
    auto snaps = g->engine.Snapshots();
    int running = 0;
    for (auto& s : snaps) if (IsActive(s.status)) ++running;
    if (running > 0 && g_settings.confirmExit) {
        std::wstring msg = Format(L"还有 %d 个项目在运行。\n\n退出会结束由 DevServeManage 启动的进程（Windows 服务不会被停止）。确定退出？", running);
        if (!ShowConfirmDialog(g->main, L"退出 DevServeManage", msg, L"退出", L"取消", true, true))
            return;
    }
    g->exiting = true;
    AppLog(L"退出");
    g->engine.StopAll();
    HideFlyout();
    if (g->trayAdded) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g->tray;
        nid.uID = 1;
        if (g->trayGuidMode) {
            nid.uFlags = NIF_GUID;
            nid.guidItem = kTrayGuid;
        }
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g->trayAdded = false;
    }
    DestroyWindow(g->main);
}

#include "tail.inc"
#include "dialogs.inc"
#include "menu.inc"
#include "settings.inc"
#include "about.inc"
