// ---------------------------------------------------------------------------
// Custom modal dialogs (D2D): no system MessageBox for product UI.
// ---------------------------------------------------------------------------
static const wchar_t* kMessageClass = L"DevServeManageMessageDialog";
static const wchar_t* kEditProjectClass = L"DevServeManageEditProjectDialog";

static void CenterWindowOnOwner(HWND hwnd, HWND owner, int w, int h) {
    RECT work{};
    if (owner && IsWindowVisible(owner)) {
        GetWindowRect(owner, &work);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
}

static void DrawDialogButton(Painter& p, R r, const std::wstring& label, D2D1_COLOR_F bg,
    D2D1_COLOR_F fg, bool hovered) {
    if (hovered) bg = g->colors.accentHover;
    p.FillRound(r, 7, bg);
    p.Text(label, r, 11.5f, 600, fg, 1);
}

struct MessageDialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring title;
    std::wstring message;
    std::wstring primary;
    std::wstring secondary;
    bool danger = false;
    bool result = false;
    Painter painter;
    int hot = 0; // 1=primary, 2=secondary, 3=close
    R primaryRect{};
    R secondaryRect{};
    R closeRect{};
};
static MessageDialogState* g_msgDialog = nullptr;

static void PaintMessageDialog(MessageDialogState& s) {
    Painter& p = s.painter;
    if (!p.Begin(g->colors.bg)) return;
    float W = p.W(), H = p.H();
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.border, 1);

    p.Text(s.title, MakeR(28, 22, W - 100, 30), 18.0f, 700, g->colors.text);
    s.closeRect = MakeR(W - 46, 20, 28, 28);
    bool closeHot = s.closeRect.has(g->mouseX, g->mouseY);
    if (closeHot) p.FillRound(s.closeRect, 6, g->colors.surface2);
    p.Text(L"×", s.closeRect, 18.0f, 400, closeHot ? g->colors.text : g->colors.faint, 1);
    p.Line(0, 64, W, 64, g->colors.border, 1);

    p.TextWrapped(s.message, MakeR(28, 82, W - 56, H - 158), 11.5f, 400, g->colors.sub);

    float btnY = H - 54;
    float primaryW = std::max(100.0f, p.Measure(s.primary, 11.5f, 600) + 30.0f);
    float secondaryW = s.secondary.empty() ? 0.0f
        : std::max(88.0f, p.Measure(s.secondary, 11.5f, 500) + 28.0f);
    float bh = 32;
    s.primaryRect = MakeR(W - 28 - primaryW, btnY, primaryW, bh);
    bool ph = s.primaryRect.has(g->mouseX, g->mouseY);
    DrawDialogButton(p, s.primaryRect, s.primary, s.danger ? g->colors.red : g->colors.accent,
        s.danger ? g->colors.redSoft : g->colors.white, ph);
    if (!s.secondary.empty()) {
        s.secondaryRect = MakeR(s.primaryRect.x - 8 - secondaryW, btnY, secondaryW, bh);
        bool sh = s.secondaryRect.has(g->mouseX, g->mouseY);
        p.FillRound(s.secondaryRect, 7, sh ? g->colors.surface2 : g->colors.transparent);
        p.StrokeRound(s.secondaryRect, 7, g->colors.border, 1);
        p.Text(s.secondary, s.secondaryRect, 11.5f, 500, sh ? g->colors.text : g->colors.sub, 1);
    } else {
        s.secondaryRect = {};
    }
    p.End();
}

static LRESULT CALLBACK MessageDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MessageDialogState* s = g_msgDialog;
    switch (msg) {
    case WM_CREATE:
        if (s) s->hwnd = hwnd;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        if (s) PaintMessageDialog(*s);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            g->mouseX = GET_X_LPARAM(lp) * 96.0f / dpi;
            g->mouseY = GET_Y_LPARAM(lp) * 96.0f / dpi;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            if (s->primaryRect.has(x, y)) { s->result = true; DestroyWindow(hwnd); }
            else if (s->secondaryRect.has(x, y) || s->closeRect.has(x, y)) { s->result = false; DestroyWindow(hwnd); }
        }
        return 0;
    case WM_KEYDOWN:
        if (s) {
            if (wp == VK_ESCAPE) { s->result = false; DestroyWindow(hwnd); }
            else if (wp == VK_RETURN) { s->result = true; DestroyWindow(hwnd); }
        }
        return 0;
    case WM_CLOSE:
        if (s) s->result = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureMessageClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = MessageDialogProc;
    wc.hInstance = g->instance;
    wc.hIcon = g->iconApp;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = kMessageClass;
    RegisterClassExW(&wc);
    registered = true;
}

static bool ShowMessageDialogImpl(HWND owner, const std::wstring& title, const std::wstring& message,
    const std::wstring& primary, const std::wstring& secondary, bool danger, bool topmost) {
    if (!g || !g->main) return false;
    EnsureMessageClass();
    MessageDialogState state;
    state.owner = owner;
    state.title = title;
    state.message = message;
    state.primary = primary;
    state.secondary = secondary;
    state.danger = danger;
    g_msgDialog = &state;

    int w = 560, h = 270;
    if (message.size() > 90) h = 310;
    if (message.size() > 180) h = 350;
    state.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | (topmost ? WS_EX_TOPMOST : 0), kMessageClass, L"", WS_POPUP,
        0, 0, w, h, owner, nullptr, g->instance, nullptr);
    if (!state.hwnd) { g_msgDialog = nullptr; return false; }
    state.painter.hwnd = state.hwnd;
    BOOL dark = g->dark;
    DwmSetWindowAttribute(state.hwnd, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(state.hwnd, 33, &corner, sizeof(corner));
    CenterWindowOnOwner(state.hwnd, owner, w, h);
    if (topmost)
        SetWindowPos(state.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    EnableWindow(owner, FALSE);
    ShowWindow(state.hwnd, SW_SHOW);
    if (topmost)
        SetWindowPos(state.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(state.hwnd);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (IsWindow(state.hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    g_msgDialog = nullptr;
    return state.result;
}

static void ShowInfoDialog(HWND owner, const std::wstring& title, const std::wstring& message) {
    ShowMessageDialogImpl(owner, title, message, L"确定", L"", false, false);
}

static bool ShowConfirmDialog(HWND owner, const std::wstring& title, const std::wstring& message,
    const std::wstring& primary, const std::wstring& secondary, bool danger, bool topmost) {
    return ShowMessageDialogImpl(owner, title, message, primary, secondary, danger, topmost);
}

// ---------------------------------------------------------------------------
// Project editor: custom D2D window with native EDIT controls for input.
// ---------------------------------------------------------------------------
struct EditProjectState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    Project original;
    Project edited;
    HWND edits[5]{};
    HFONT font = nullptr;
    HBRUSH editBrush = nullptr;
    Painter painter;
    bool saved = false;
    int hot = -1;
    R saveRect{};
    R cancelRect{};
    R closeRect{};
};
static EditProjectState* g_editDialog = nullptr;

enum EditField { EditAlias = 0, EditName, EditCommand, EditWorkDir, EditPort, EditCount };

static std::wstring GetEditText(HWND edit) {
    if (!edit) return {};
    int len = GetWindowTextLengthW(edit);
    std::wstring s((size_t)std::max(0, len) + 1, L'\0');
    GetWindowTextW(edit, s.data(), len + 1);
    s.resize((size_t)len);
    return s;
}

static void SetEditText(HWND edit, const std::wstring& text) {
    if (edit) SetWindowTextW(edit, text.c_str());
}

static void LayoutEditDialog(EditProjectState& s) {
    if (!s.hwnd) return;
    float dpi = (float)GetDpiForWindow(s.hwnd);
    float scale = dpi / 96.0f;
    int editX = (int)std::lround(180 * scale);
    int editW = (int)std::lround(440 * scale);
    int editH = (int)std::lround(30 * scale);
    int ys[5] = { 80, 130, 180, 230, 280 };
    for (int i = 0; i < EditCount; ++i) {
        if (s.edits[i]) {
            SetWindowPos(s.edits[i], nullptr, editX, (int)std::lround(ys[i] * scale),
                editW, editH, SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(s.edits[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM((int)std::lround(7 * scale), (int)std::lround(7 * scale)));
        }
    }
}

static bool SaveEditDialog(EditProjectState& s) {
    std::wstring alias = Trim(GetEditText(s.edits[EditAlias]));
    std::wstring name = Trim(GetEditText(s.edits[EditName]));
    std::wstring command = Trim(GetEditText(s.edits[EditCommand]));
    std::wstring workDir = Trim(GetEditText(s.edits[EditWorkDir]));
    std::wstring portText = Trim(GetEditText(s.edits[EditPort]));
    if (name.empty()) name = s.original.name;
    if (command.empty() && s.original.kind() != Kind::WindowsService) {
        ShowInfoDialog(s.hwnd, L"无法保存", L"启动命令不能为空。");
        return false;
    }
    int port = _wtoi(portText.c_str());
    if (port < 0 || port > 65535) {
        ShowInfoDialog(s.hwnd, L"端口不正确", L"预期端口必须是 0–65535 之间的数字。0 表示不指定。");
        return false;
    }
    s.edited.alias = alias;
    s.edited.name = name;
    s.edited.startCommand = command;
    s.edited.workingDirectory = workDir.empty() ? s.original.rootPath : workDir;
    s.edited.port = port;
    if (port > 0) s.edited.url = L"http://localhost:" + std::to_wstring(port);
    else if (s.edited.url.rfind(L"http://localhost:", 0) == 0) s.edited.url.clear();
    std::wstring error;
    if (!g->engine.UpdateProject(s.edited, &error)) {
        ShowInfoDialog(s.hwnd, L"保存失败", error);
        return false;
    }
    s.saved = true;
    DestroyWindow(s.hwnd);
    return true;
}

static void PaintEditDialog(EditProjectState& s) {
    Painter& p = s.painter;
    if (!p.Begin(g->colors.bg)) return;
    float W = p.W(), H = p.H();
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.border, 1);
    p.Text(L"编辑项目", MakeR(28, 20, 300, 30), 18.0f, 700, g->colors.text);
    p.Text(L"保存后下次启动生效；当前正在运行的进程不会自动重启。", MakeR(28, 50, W - 80, 18), 10.5f, 400, g->colors.faint);
    s.closeRect = MakeR(W - 46, 18, 28, 28);
    bool closeHot = s.closeRect.has(g->mouseX, g->mouseY);
    if (closeHot) p.FillRound(s.closeRect, 6, g->colors.surface2);
    p.Text(L"×", s.closeRect, 18.0f, 400, closeHot ? g->colors.text : g->colors.faint, 1);
    p.Line(0, 68, W, 68, g->colors.border, 1);

    const wchar_t* labels[5] = { L"别名（显示名）", L"项目名称", L"启动命令", L"工作目录", L"预期端口" };
    int y[5] = { 88, 138, 188, 238, 288 };
    for (int i = 0; i < 5; ++i) {
        p.Text(labels[i], MakeR(28, y[i], 142, 18), 10.5f, 500, g->colors.sub);
    }
    p.Text(L"别名留空时显示项目名称", MakeR(28, 306, 430, 16), 9.8f, 400, g->colors.faint);

    float by = H - 54;
    s.saveRect = MakeR(W - 28 - 100, by, 100, 32);
    s.cancelRect = MakeR(s.saveRect.x - 8 - 100, by, 100, 32);
    bool sh = s.saveRect.has(g->mouseX, g->mouseY);
    bool ch = s.cancelRect.has(g->mouseX, g->mouseY);
    DrawDialogButton(p, s.saveRect, L"保存", g->colors.accent, g->colors.white, sh);
    p.FillRound(s.cancelRect, 7, ch ? g->colors.surface2 : g->colors.transparent);
    p.StrokeRound(s.cancelRect, 7, g->colors.border, 1);
    p.Text(L"取消", s.cancelRect, 11.5f, 500, ch ? g->colors.text : g->colors.sub, 1);
    p.End();
}

static LRESULT CALLBACK EditProjectProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditProjectState* s = g_editDialog;
    switch (msg) {
    case WM_CREATE:
        if (s) {
            s->hwnd = hwnd;
            s->font = CreateFontW(-MulDiv(11, (int)GetDpiForWindow(hwnd), 72), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            s->editBrush = CreateSolidBrush(RGB((int)(g->colors.surface2.r * 255),
                (int)(g->colors.surface2.g * 255), (int)(g->colors.surface2.b * 255)));
            const std::wstring portText = s->edited.port ? std::to_wstring(s->edited.port) : L"";
            const wchar_t* values[5] = {
                s->edited.alias.c_str(), s->edited.name.c_str(), s->edited.startCommand.c_str(),
                s->edited.workingDirectory.c_str(), portText.c_str()
            };
            for (int i = 0; i < EditCount; ++i) {
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
                if (i == EditPort) style |= ES_NUMBER;
                s->edits[i] = CreateWindowExW(0, L"EDIT", values[i], style,
                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(100 + i), g->instance, nullptr);
                SendMessageW(s->edits[i], WM_SETFONT, (WPARAM)s->font, TRUE);
                SendMessageW(s->edits[i], EM_SETLIMITTEXT, 4096, 0);
                SetWindowSubclass(s->edits[i], [](HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) -> LRESULT {
                    if (m == WM_KEYDOWN && w == VK_ESCAPE) {
                        PostMessageW(GetParent(h), WM_CLOSE, 0, 0);
                        return 0;
                    }
                    if (m == WM_KEYDOWN && w == VK_RETURN) {
                        PostMessageW(GetParent(h), WM_APP_SAVE_EDIT, 0, 0);
                        return 0;
                    }
                    return DefSubclassProc(h, m, w, l);
                }, 1, 0);
            }
            LayoutEditDialog(*s);
            SetFocus(s->edits[EditAlias]);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        if (s) PaintEditDialog(*s);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (s) LayoutEditDialog(*s);
        return 0;
    case WM_MOUSEMOVE:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            g->mouseX = GET_X_LPARAM(lp) * 96.0f / dpi;
            g->mouseY = GET_Y_LPARAM(lp) * 96.0f / dpi;
            int hot = s->saveRect.has(g->mouseX, g->mouseY) ? 1 :
                s->cancelRect.has(g->mouseX, g->mouseY) ? 2 :
                s->closeRect.has(g->mouseX, g->mouseY) ? 3 : -1;
            if (hot != s->hot) {
                s->hot = hot;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (s && s->hot != -1) {
            s->hot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        if (s && s->editBrush) {
            SetTextColor((HDC)wp, RGB((int)(g->colors.text.r * 255), (int)(g->colors.text.g * 255), (int)(g->colors.text.b * 255)));
            SetBkColor((HDC)wp, RGB((int)(g->colors.surface2.r * 255), (int)(g->colors.surface2.g * 255), (int)(g->colors.surface2.b * 255)));
            return (LRESULT)s->editBrush;
        }
        break;
    case WM_LBUTTONUP:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            if (s->saveRect.has(x, y)) SaveEditDialog(*s);
            else if (s->cancelRect.has(x, y) || s->closeRect.has(x, y)) DestroyWindow(hwnd);
        }
        return 0;
    case WM_APP_SAVE_EDIT:
        if (s) SaveEditDialog(*s);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (s) {
            if (s->font) DeleteObject(s->font);
            if (s->editBrush) DeleteObject(s->editBrush);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureEditProjectClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = EditProjectProc;
    wc.hInstance = g->instance;
    wc.hIcon = g->iconApp;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = kEditProjectClass;
    RegisterClassExW(&wc);
    registered = true;
}

static bool ShowEditProjectDialog(HWND owner, Project& project) {
    if (!g || !g->main) return false;
    EnsureEditProjectClass();
    EditProjectState state;
    state.owner = owner;
    state.original = project;
    state.edited = project;
    g_editDialog = &state;
    int w = 660, h = 450;
    state.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kEditProjectClass, L"", WS_POPUP | WS_CLIPCHILDREN,
        0, 0, w, h, owner, nullptr, g->instance, nullptr);
    if (!state.hwnd) { g_editDialog = nullptr; return false; }
    state.painter.hwnd = state.hwnd;
    BOOL dark = g->dark;
    DwmSetWindowAttribute(state.hwnd, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(state.hwnd, 33, &corner, sizeof(corner));
    CenterWindowOnOwner(state.hwnd, owner, w, h);
    EnableWindow(owner, FALSE);
    ShowWindow(state.hwnd, SW_SHOW);
    SetForegroundWindow(state.hwnd);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (IsWindow(state.hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    g_editDialog = nullptr;
    if (state.saved) project = state.edited;
    return state.saved;
}
