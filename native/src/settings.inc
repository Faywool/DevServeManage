// Settings window: a standalone Direct2D surface, with live theme preview.
#pragma once

struct SettingsHit { R r; int action = 0; };

struct SettingsDialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    Painter painter;
    AppSettings draft;
    AppSettings original;
    std::vector<SettingsHit> hits;
    int hot = -1;
    bool saved = false;
    bool closing = false;
    bool hasImportedWorkspace = false;
    std::string importedWorkspace;
    float mouseX = -1, mouseY = -1;
};

static const wchar_t* kSettingsClass = L"DevServeManageSettingsWindow";
static SettingsDialogState* g_settingsDialog = nullptr;

enum SettingsAction {
    SNone = 0, SClose = 1, SThemeSystem = 10, SThemeLight = 11, SThemeDark = 12,
    SFollowAccent = 20, SAccentBase = 100, SStartTray = 30, SConfirmExit = 31,
    SAnimations = 32, SReset = 40, SExport = 41, SImport = 42,
    SCustomAccent = 200, SCancel = 50, SSave = 51
};

struct AccentPreset { unsigned int rgb; const wchar_t* name; };
static const AccentPreset kAccentPresets[] = {
    { 0x2F8F83, L"青" }, { 0x3B82F6, L"蓝" }, { 0x8B5CF6, L"紫" }, { 0xD97706, L"琥珀" },
    { 0xE11D48, L"玫红" }, { 0x64748B, L"石墨" }, { 0x0EA5E9, L"天蓝" }, { 0x22C55E, L"绿" },
};

static void SettingsApplyDraft(SettingsDialogState& state) {
    g_settings = state.draft;
    RefreshTheme();
    ApplyDarkTitle(state.hwnd);
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);
}

static void AddSettingsHit(SettingsDialogState& state, R r, int action) {
    state.hits.push_back({ r, action });
}

static void DrawSettingsButton(Painter& p, SettingsDialogState& state, R r,
    const std::wstring& label, int action, bool selected = false, bool primary = false) {
    bool hot = r.has(state.mouseX, state.mouseY);
    D2D1_COLOR_F bg = selected ? g->colors.accentSoft : (hot ? g->colors.chipHover : g->colors.chip);
    D2D1_COLOR_F fg = selected || primary ? g->colors.accent : (hot ? g->colors.text : g->colors.sub);
    p.FillRound(r, 7, bg);
    p.StrokeRound(r, 7, selected ? g->colors.accent : g->colors.border, selected ? 1.4f : 1);
    p.Text(label, r, 11.5f, selected || primary ? 600 : 500, fg, 1);
    AddSettingsHit(state, r, action);
}

static void DrawSettingsToggle(Painter& p, SettingsDialogState& state, float x, float y,
    bool value, int action) {
    R track = MakeR(x, y, 44, 24);
    bool hot = track.has(state.mouseX, state.mouseY);
    p.FillRound(track, 12, value ? g->colors.accent : (hot ? g->colors.chipHover : g->colors.chip));
    p.Ellipse(value ? track.x + 31 : track.x + 13, track.y + 12, 8, 8, g->colors.surface);
    AddSettingsHit(state, track, action);
}

static void PaintSettings(SettingsDialogState& state) {
    Painter& p = state.painter;
    if (!p.Begin(g->colors.bg)) return;
    float W = p.W(), H = p.H();
    // The outer card fills the popup.
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.border, 1);

    p.Text(L"设置", MakeR(28, 18, 200, 28), 20, 700, g->colors.text);
    p.Text(L"外观、Primary 颜色、行为与配置", MakeR(28, 45, 420, 18), 10.5f, 400, g->colors.faint);
    // close button
    R close = MakeR(W - 52, 18, 28, 28);
    bool closeHot = close.has(state.mouseX, state.mouseY);
    p.FillRound(close, 7, closeHot ? g->colors.chipHover : g->colors.transparent);
    RoundLine(p, close.x + 9, close.y + 9, close.x + 19, close.y + 19,
        closeHot ? g->colors.text : g->colors.sub, 1.4f);
    RoundLine(p, close.x + 19, close.y + 9, close.x + 9, close.y + 19,
        closeHot ? g->colors.text : g->colors.sub, 1.4f);
    AddSettingsHit(state, close, SClose);
    p.Line(24, 72, W - 24, 72, g->colors.border, 1);

    p.Text(L"外观", MakeR(28, 88, 160, 20), 12, 700, g->colors.text);
    p.Text(L"主题模式", MakeR(28, 118, 150, 20), 11.5f, 500, g->colors.text);
    R themeSystem = MakeR(200, 112, 100, 32);
    R themeLight = MakeR(306, 112, 100, 32);
    R themeDark = MakeR(412, 112, 100, 32);
    DrawSettingsButton(p, state, themeSystem, L"跟随系统", SThemeSystem, state.draft.themeMode == 0);
    DrawSettingsButton(p, state, themeLight, L"浅色", SThemeLight, state.draft.themeMode == 1);
    DrawSettingsButton(p, state, themeDark, L"深色", SThemeDark, state.draft.themeMode == 2);

    p.Text(L"Primary 颜色", MakeR(28, 162, 150, 20), 11.5f, 500, g->colors.text);
    R follow = MakeR(200, 156, 100, 30);
    DrawSettingsButton(p, state, follow, L"跟随系统", SFollowAccent, state.draft.followSystemAccent);
    float swatchX = 310;
    for (int i = 0; i < (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0])); ++i) {
        R sw = MakeR(swatchX + i * 38, 156, 30, 30);
        bool selected = !state.draft.followSystemAccent && state.draft.primaryColor == kAccentPresets[i].rgb;
        p.FillRound(sw, 8, C(kAccentPresets[i].rgb));
        if (selected) p.StrokeRound(MakeR(sw.x - 3, sw.y - 3, sw.w + 6, sw.h + 6), 10, g->colors.accent, 2);
        else p.StrokeRound(sw, 8, g->colors.border, 1);
        AddSettingsHit(state, sw, SAccentBase + i);
    }
    DrawSettingsButton(p, state, MakeR(620, 156, 84, 30), L"自定义…", SCustomAccent);
    p.Text(state.draft.followSystemAccent ? L"当前：跟随系统" : L"当前：" + ColorToHex(state.draft.primaryColor),
        MakeR(310, 190, 300, 18), 10.0f, 400, g->colors.faint);

    p.Line(24, 220, W - 24, 220, g->colors.border, 1);
    p.Text(L"行为", MakeR(28, 236, 160, 20), 12, 700, g->colors.text);
    auto settingRow = [&](float y, const wchar_t* title, const wchar_t* desc, bool value, int action) {
        p.Text(title, MakeR(28, y, 250, 20), 11.5f, 500, g->colors.text);
        p.Text(desc, MakeR(28, y + 19, 480, 16), 9.8f, 400, g->colors.faint);
        DrawSettingsToggle(p, state, W - 80, y - 1, value, action);
    };
    settingRow(262, L"启动时只驻留托盘", L"双击启动时不显示主界面；仍可从托盘打开", state.draft.startMinimized, SStartTray);
    settingRow(306, L"退出时确认", L"还有项目运行时，退出前弹出确认", state.draft.confirmExit, SConfirmExit);
    settingRow(350, L"界面动画", L"启动中的旋转加载动画", state.draft.animations, SAnimations);

    p.Line(24, 400, W - 24, 400, g->colors.border, 1);
    p.Text(L"配置", MakeR(28, 414, 160, 20), 12, 700, g->colors.text);
    DrawSettingsButton(p, state, MakeR(28, 442, 120, 32), L"恢复默认设置", SReset);
    DrawSettingsButton(p, state, MakeR(156, 442, 116, 32), L"导出配置…", SExport);
    DrawSettingsButton(p, state, MakeR(280, 442, 116, 32), L"导入配置…", SImport);
    p.Text(L"导出为单文件配置（设置 + 项目）", MakeR(410, 448, 280, 20), 9.8f, 400, g->colors.faint);

    p.Line(24, 492, W - 24, 492, g->colors.border, 1);
    p.Text(L"DevServeManage Native · 1.1.0", MakeR(28, 508, 260, 22), 10.0f, 400, g->colors.faint);
    R cancel = MakeR(W - 204, 502, 84, 30);
    R save = MakeR(W - 112, 502, 84, 30);
    DrawSettingsButton(p, state, cancel, L"取消", SCancel);
    DrawSettingsButton(p, state, save, L"保存", SSave, true, true);

    p.End();
}

static void SettingsClose(SettingsDialogState& state, bool save) {
    if (state.closing) return;
    state.closing = true;
    if (save) {
        g_settings = state.draft;
        std::wstring error;
        bool ok = SaveSettings(state.hasImportedWorkspace ? &state.importedWorkspace : nullptr, &error);
        if (!ok) {
            state.closing = false;
            ShowInfoDialog(state.hwnd, L"保存失败", error + L"\n\n配置文件：" + ConfigPath());
            return;
        }
        RefreshTheme();
        state.saved = true;
    } else {
        g_settings = state.original;
        RefreshTheme();
    }
    DestroyWindow(state.hwnd);
}

static bool PickJsonFile(HWND owner, bool save, std::wstring& outPath) {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
        nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;
    COMDLG_FILTERSPEC filter{ L"DevServeManage 配置 (*.json)", L"*.json" };
    dialog->SetFileTypes(1, &filter);
    if (save) {
        dialog->SetDefaultExtension(L"json");
        dialog->SetFileName(L"DevServeManage-config.json");
    }
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                outPath = path;
                CoTaskMemFree(path);
                item->Release();
                dialog->Release();
                return true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return false;
}

static AppSettings SettingsFromJson(const dsm::Json& json, const AppSettings& fallback) {
    AppSettings s = fallback;
    if (json.type != dsm::Json::Type::Object) return s;
    s.themeMode = std::clamp(json.integer("themeMode", s.themeMode), 0, 2);
    s.followSystemAccent = json.flag("followSystemAccent", s.followSystemAccent);
    s.primaryColor = ParseColorHex(json.str("primaryColor", Narrow(ColorToHex(s.primaryColor))), s.primaryColor);
    s.startMinimized = json.flag("startMinimized", s.startMinimized);
    s.confirmExit = json.flag("confirmExit", s.confirmExit);
    s.animations = json.flag("animations", s.animations);
    return s;
}

static void ExportSettingsConfig(HWND owner, const AppSettings& settings) {
    std::wstring path;
    if (!PickJsonFile(owner, true, path)) return;
    dsm::Json root = LoadConfigRoot();
    dsm::Json out;
    out.type = dsm::Json::Type::Object;
    dsm::Json version; version.type = dsm::Json::Type::Number; version.number = 1;
    out.object.emplace_back("version", std::move(version));
    out.object.emplace_back("settings", SettingsNode(settings));
    out.object.emplace_back("workspace", WorkspaceNodeFromConfig(root));
    if (!WriteTextFile(path, out.dump(2))) {
        ShowInfoDialog(owner, L"导出失败", L"无法写入配置文件。");
        return;
    }
    ShowInfoDialog(owner, L"导出成功", L"设置和项目工作区已导出到：\n" + path);
}

static void ImportSettingsConfig(SettingsDialogState& state) {
    std::wstring path;
    if (!PickJsonFile(state.hwnd, false, path)) return;
    std::string text = ReadTextFile(path);
    std::string error;
    dsm::Json root = dsm::Json::Parse(text, &error);
    if (root.type != dsm::Json::Type::Object) {
        ShowInfoDialog(state.hwnd, L"导入失败", L"配置文件不是有效的 JSON。");
        return;
    }
    const dsm::Json* settingsNode = root.obj("settings");
    if (!settingsNode) {
        ShowInfoDialog(state.hwnd, L"导入失败", L"配置中缺少 settings 字段。");
        return;
    }
    const dsm::Json* workspace = root.obj("workspace");
    if (!workspace) {
        ShowInfoDialog(state.hwnd, L"导入失败", L"配置中缺少 workspace 字段。");
        return;
    }

    state.draft = SettingsFromJson(*settingsNode, state.draft);
    state.hasImportedWorkspace = true;
    state.importedWorkspace = workspace->dump(2);
    SettingsApplyDraft(state);
    ShowInfoDialog(state.hwnd, L"导入成功", L"设置已载入；点击「保存」后写入配置文件。");
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsDialogState* s = g_settingsDialog;
    switch (msg) {
    case WM_CREATE:
        if (s) s->hwnd = hwnd;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        if (s) {
            g->mouseX = s->mouseX;
            g->mouseY = s->mouseY;
            PaintSettings(*s);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            s->mouseX = GET_X_LPARAM(lp) * 96.0f / dpi;
            s->mouseY = GET_Y_LPARAM(lp) * 96.0f / dpi;
            int hot = -1;
            for (auto& h : s->hits) if (h.r.has(s->mouseX, s->mouseY)) { hot = h.action; break; }
            if (hot != s->hot) { s->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            for (auto& h : s->hits) {
                if (!h.r.has(x, y)) continue;
                int a = h.action;
                if (a == SClose || a == SCancel) { SettingsClose(*s, false); return 0; }
                if (a == SSave) { SettingsClose(*s, true); return 0; }
                if (a == SCustomAccent) {
                    static COLORREF customColors[16]{};
                    CHOOSECOLORW cc{};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = customColors;
                    cc.rgbResult = RGB((s->draft.primaryColor >> 16) & 0xFF,
                        (s->draft.primaryColor >> 8) & 0xFF, s->draft.primaryColor & 0xFF);
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        s->draft.followSystemAccent = false;
                        s->draft.primaryColor = (GetRValue(cc.rgbResult) << 16) |
                            (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
                        SettingsApplyDraft(*s);
                    }
                    return 0;
                }
                if (a >= SAccentBase && a < SAccentBase + (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]))) {
                    s->draft.followSystemAccent = false;
                    s->draft.primaryColor = kAccentPresets[a - SAccentBase].rgb;
                    SettingsApplyDraft(*s);
                    return 0;
                }
                switch (a) {
                case SFollowAccent: s->draft.followSystemAccent = true; SettingsApplyDraft(*s); break;
                case SThemeSystem: s->draft.themeMode = 0; SettingsApplyDraft(*s); break;
                case SThemeLight: s->draft.themeMode = 1; SettingsApplyDraft(*s); break;
                case SThemeDark: s->draft.themeMode = 2; SettingsApplyDraft(*s); break;
                case SStartTray: s->draft.startMinimized = !s->draft.startMinimized; InvalidateRect(hwnd, nullptr, FALSE); break;
                case SConfirmExit: s->draft.confirmExit = !s->draft.confirmExit; InvalidateRect(hwnd, nullptr, FALSE); break;
                case SAnimations: s->draft.animations = !s->draft.animations; InvalidateRect(hwnd, nullptr, FALSE); break;
                case SReset: s->draft = AppSettings{}; SettingsApplyDraft(*s); break;
                case SExport: ExportSettingsConfig(hwnd, s->draft); break;
                case SImport: ImportSettingsConfig(*s); break;
                }
                return 0;
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { if (s) SettingsClose(*s, false); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'S')) { if (s) SettingsClose(*s, true); return 0; }
        break;
    case WM_CLOSE:
        if (s) SettingsClose(*s, false);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_ACTIVATE:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterSettingsClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = g->instance;
    wc.hIcon = g->iconApp;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);
    registered = true;
}

static void ShowSettingsWindow(HWND owner) {
    if (!g || g_settingsDialog) return;
    RegisterSettingsClass();
    SettingsDialogState state;
    state.owner = owner;
    state.draft = g_settings;
    state.original = g_settings;
    g_settingsDialog = &state;

    int width = 720, height = 560;
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (work.right - work.left - width) / 2;
    int y = work.top + (work.bottom - work.top - height) / 2;
    state.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSettingsClass, L"设置",
        WS_POPUP, x, y, width, height, owner, nullptr, g->instance, nullptr);
    if (!state.hwnd) { g_settingsDialog = nullptr; return; }
    state.painter.hwnd = state.hwnd;
    BOOL dark = g->dark;
    DwmSetWindowAttribute(state.hwnd, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(state.hwnd, 33, &corner, sizeof(corner));
    EnableWindow(owner, FALSE);
    ShowWindow(state.hwnd, SW_SHOW);
    SetForegroundWindow(state.hwnd);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (IsWindow(state.hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (!state.saved) {
        g_settings = state.original;
        RefreshTheme();
    }
    g_settingsDialog = nullptr;
}

static void ShowAboutDialog(HWND owner) {
    ShowAboutWindow(owner);
}
