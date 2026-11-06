// Custom Direct2D context menu, shared by the project list and the tray icon.
#pragma once

struct CustomMenuState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::vector<UiMenuItem> items;
    Painter painter;
    int hot = -1;
    bool leftWasDown = false;
    int result = 0;
    bool closing = false;
};

static const wchar_t* kCustomMenuClass = L"DevServeManageCustomMenu";
static CustomMenuState* g_customMenu = nullptr;
static HHOOK g_menuMouseHook = nullptr;

static void MenuItemRects(const CustomMenuState& state, std::vector<std::pair<R, int>>& out);
static void CloseCustomMenu(CustomMenuState& state, int id);
static void ChooseCustomMenu(CustomMenuState& state, int id);

static int HitMenuItem(CustomMenuState& state, float x, float y) {
    std::vector<std::pair<R, int>> rects;
    MenuItemRects(state, rects);
    for (auto& [r, idx] : rects) if (r.has(x, y)) return idx;
    return -1;
}

// Foreground rules can prevent a tray popup from owning the mouse capture.
// A low-level mouse hook keeps hover/click/outside-dismiss working even when
// Explorer still owns the foreground window.
static LRESULT CALLBACK MenuMouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !g_customMenu || !g_customMenu->hwnd || g_customMenu->closing)
        return CallNextHookEx(g_menuMouseHook, code, wp, lp);
    auto& state = *g_customMenu;
    auto* ms = (MSLLHOOKSTRUCT*)lp;
    POINT screen = ms->pt;
    POINT client = screen;
    ScreenToClient(state.hwnd, &client);
    float dpi = (float)GetDpiForWindow(state.hwnd);
    float x = client.x * 96.0f / dpi;
    float y = client.y * 96.0f / dpi;

    if (wp == WM_MOUSEMOVE) {
        int hot = HitMenuItem(state, x, y);
        if (hot != state.hot) {
            state.hot = hot;
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursorW(nullptr, hot >= 0 ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
        return CallNextHookEx(g_menuMouseHook, code, wp, lp);
    }
    if (wp == WM_LBUTTONDOWN) {
        int item = HitMenuItem(state, x, y);
        if (item >= 0 && item < (int)state.items.size() && state.items[item].enabled)
            ChooseCustomMenu(state, state.items[item].id);
        else
            CloseCustomMenu(state, 0);
        return 1;
    }
    if (wp == WM_LBUTTONUP) {
        return 1;
    }
    if (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP) {
        CloseCustomMenu(state, 0);
        return 1;
    }
    return CallNextHookEx(g_menuMouseHook, code, wp, lp);
}

static int MenuHeight(const std::vector<UiMenuItem>& items) {
    int h = 12;
    for (auto& item : items) h += item.separator ? 9 : 34;
    return h;
}

static void MenuItemRects(const CustomMenuState& state, std::vector<std::pair<R, int>>& out) {
    out.clear();
    float y = 6;
    for (size_t i = 0; i < state.items.size(); ++i) {
        auto& item = state.items[i];
        if (item.separator) { y += 9; continue; }
        out.push_back({ MakeR(6, y, 258, 34), (int)i });
        y += 34;
    }
}

static void PaintCustomMenu(CustomMenuState& state) {
    Painter& p = state.painter;
    if (!p.Begin(g->colors.bg)) return;
    float W = p.W(), H = p.H();
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 10, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 10, g->colors.border, 1);

    float y = 6;
    for (size_t i = 0; i < state.items.size(); ++i) {
        auto& item = state.items[i];
        if (item.separator) {
            p.Line(14, y + 4, W - 14, y + 4, g->colors.border, 1);
            y += 9;
            continue;
        }
        R row = MakeR(6, y, W - 12, 34);
        bool hot = (int)i == state.hot && item.enabled;
        if (hot) {
            p.FillRound(row, 7, g->colors.accentSoft);
            p.FillRound(MakeR(row.x + 2, row.y + 8, 3, 18), 1.5f, g->colors.accent);
        }
        D2D1_COLOR_F fg = !item.enabled ? g->colors.faint : item.danger ? g->colors.red : g->colors.text;
        D2D1_COLOR_F iconColor = !item.enabled ? g->colors.faint : item.danger ? g->colors.red : (hot ? g->colors.accentHover : g->colors.accent);
        DrawIcon(p, item.icon, MakeR(row.x + 10, row.y + 9, 16, 16), iconColor);
        p.Text(item.label, MakeR(row.x + 42, row.y, row.w - 50, 34), 12.5f, 500, fg);
        y += 34;
    }
    p.End();
}

static void CloseCustomMenu(CustomMenuState& state, int id) {
    if (state.closing) return;
    state.result = id;
    state.closing = true;
    DestroyWindow(state.hwnd);
}

static void ChooseCustomMenu(CustomMenuState& state, int id) {
    CloseCustomMenu(state, id);
}

static LRESULT CALLBACK CustomMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CustomMenuState* s = g_customMenu;
    switch (msg) {
    case WM_CREATE:
        if (s) s->hwnd = hwnd;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        if (s) PaintCustomMenu(*s);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && s) {
            POINT pt{}; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            float dpi = (float)GetDpiForWindow(hwnd);
            int item = HitMenuItem(*s, pt.x * 96.0f / dpi, pt.y * 96.0f / dpi);
            SetCursor(LoadCursorW(nullptr, item >= 0 ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (s && !s->closing) {
            POINT pt{}; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = pt.x * 96.0f / dpi, y = pt.y * 96.0f / dpi;
            int hot = HitMenuItem(*s, x, y);
            if (hot != s->hot) { s->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
            SetCursor(LoadCursorW(nullptr, hot >= 0 ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
            bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (!down && s->leftWasDown) {
                s->leftWasDown = false;
                if (hot >= 0 && hot < (int)s->items.size() && s->items[hot].enabled)
                    ChooseCustomMenu(*s, s->items[hot].id);
                else
                    CloseCustomMenu(*s, 0);
                return 0;
            }
            s->leftWasDown = down;
            if (GetCapture() != hwnd) CloseCustomMenu(*s, 0);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            std::vector<std::pair<R, int>> rects;
            MenuItemRects(*s, rects);
            int hot = -1;
            for (auto& [r, idx] : rects) if (r.has(x, y)) { hot = idx; break; }
            if (hot != s->hot) { s->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            RECT rc{}; GetClientRect(hwnd, &rc);
            R client = MakeR(0, 0, (float)rc.right, (float)rc.bottom);
            if (!client.has(x, y)) {
                CloseCustomMenu(*s, 0);
                return 0;
            }
            int item = HitMenuItem(*s, x, y);
            if (item >= 0 && item < (int)s->items.size() && s->items[item].enabled)
                ChooseCustomMenu(*s, s->items[item].id);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s) {
            float dpi = (float)GetDpiForWindow(hwnd);
            float x = GET_X_LPARAM(lp) * 96.0f / dpi;
            float y = GET_Y_LPARAM(lp) * 96.0f / dpi;
            std::vector<std::pair<R, int>> rects;
            MenuItemRects(*s, rects);
            for (auto& [r, idx] : rects) {
                if (r.has(x, y)) {
                    if (s->items[idx].enabled) ChooseCustomMenu(*s, s->items[idx].id);
                    return 0;
                }
            }
            s->result = 0;
            s->closing = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_RBUTTONUP:
        if (s) CloseCustomMenu(*s, 0);
        return 0;
    case WM_KEYDOWN:
        if (s) {
            if (wp == VK_ESCAPE) { CloseCustomMenu(*s, 0); return 0; }
            if (wp == VK_RETURN) {
                if (s->hot >= 0 && s->hot < (int)s->items.size() && s->items[s->hot].enabled)
                    ChooseCustomMenu(*s, s->items[s->hot].id);
                return 0;
            }
            if (wp == VK_DOWN || wp == VK_UP) {
                int step = wp == VK_DOWN ? 1 : -1;
                int idx = s->hot;
                for (int n = 0; n < (int)s->items.size(); ++n) {
                    idx = (idx + step + (int)s->items.size()) % (int)s->items.size();
                    if (!s->items[idx].separator && s->items[idx].enabled) { s->hot = idx; break; }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (s && !s->closing) CloseCustomMenu(*s, 0);
        return 0;
    case WM_CLOSE:
        if (s) CloseCustomMenu(*s, 0);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        ReleaseCapture();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterCustomMenuClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = CustomMenuProc;
    wc.hInstance = g->instance;
    wc.hIcon = g->iconApp;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = kCustomMenuClass;
    RegisterClassExW(&wc);
    registered = true;
}

static bool IsCustomMenuOpen() {
    return g_customMenu && g_customMenu->hwnd && !g_customMenu->closing;
}

static int ShowCustomMenu(HWND owner, POINT pt, const std::vector<UiMenuItem>& items,
    const RECT* clip) {
    if (!g || items.empty()) return 0;
    if (IsCustomMenuOpen()) {
        SetForegroundWindow(g_customMenu->hwnd);
        return 0;
    }
    RegisterCustomMenuClass();
    CustomMenuState state;
    state.owner = owner;
    state.items = items;
    g_customMenu = &state;

    int width = 270;
    int height = MenuHeight(items);
    int x = pt.x, y = pt.y;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    if (x + width > mi.rcWork.right - 8) x = mi.rcWork.right - width - 8;
    if (y + height > mi.rcWork.bottom - 8) y = pt.y - height;
    x = std::max((int)mi.rcWork.left + 8, x);
    y = std::max((int)mi.rcWork.top + 8, y);
    // Always keep the complete menu inside the work area, above the taskbar.
    if (x + width > mi.rcWork.right - 8) x = mi.rcWork.right - width - 8;
    if (y + height > mi.rcWork.bottom - 8) y = mi.rcWork.bottom - height - 8;
    if (x < mi.rcWork.left + 8) x = mi.rcWork.left + 8;
    if (y < mi.rcWork.top + 8) y = mi.rcWork.top + 8;
    if (clip) {
        int left = clip->left + 8, right = clip->right - 8;
        int top = clip->top + 8, bottom = clip->bottom - 8;
        if (x + width > right) x = right - width;
        if (y + height > bottom) y = bottom - height;
        if (x < left) x = left;
        if (y < top) y = top;
    }

    state.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kCustomMenuClass, L"",
        WS_POPUP, x, y, width, height, owner, nullptr, g->instance, nullptr);
    if (!state.hwnd) { g_customMenu = nullptr; return 0; }
    state.painter.hwnd = state.hwnd;
    BOOL dark = g->dark;
    DwmSetWindowAttribute(state.hwnd, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(state.hwnd, 33, &corner, sizeof(corner));

    ShowWindow(state.hwnd, SW_SHOW);
    SetWindowPos(state.hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    BOOL setFg = SetForegroundWindow(state.hwnd);
    (void)setFg;
    SetFocus(state.hwnd);
    SetCapture(state.hwnd);
    SetTimer(state.hwnd, 1, 30, nullptr);
    g_menuMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MenuMouseHook, GetModuleHandleW(nullptr), 0);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (IsWindow(state.hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_menuMouseHook) {
        UnhookWindowsHookEx(g_menuMouseHook);
        g_menuMouseHook = nullptr;
    }
    g_customMenu = nullptr;
    return state.result;
}
