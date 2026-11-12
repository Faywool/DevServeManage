// ---------------------------------------------------------------------------
// Tray flyout
// ---------------------------------------------------------------------------
static std::wstring ProcessPath() {
    wchar_t buf[32768]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, _countof(buf));
    return std::wstring(buf, n);
}

static bool GetTrayIconRect(RECT& out) {
    if (!g || !g->tray || !g->trayAdded) return false;
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = g->tray;
    id.uID = 1;
    if (g->trayGuidMode) id.guidItem = kTrayGuid;
    return Shell_NotifyIconGetRect(&id, &out) == S_OK;
}

static void RefreshFlySnaps() {
    auto all = g->engine.Snapshots();
    std::stable_sort(all.begin(), all.end(), [](const Snapshot& a, const Snapshot& b) {
        bool aa = IsActive(a.status), ba = IsActive(b.status);
        return aa && !ba;
    });
    g->flySnaps = std::move(all);
}

static int FlyActiveCount() {
    int n = 0;
    for (auto& s : g->flySnaps) if (IsActive(s.status)) ++n;
    return n;
}

static float FlyHeight() {
    int active = FlyActiveCount();
    int stopped = (int)g->flySnaps.size() - active;
    float h = 48;
    if (g->flySnaps.empty()) h += 76;
    else {
        if (active) h += 16 + active * 38;
        if (stopped) h += 16 + stopped * 38;
    }
    h += 40;
    return std::min(h, 680.0f);
}

static void DrawFlyButton(Painter& p, R r, const std::wstring& text, Act action, int index,
    bool primary, bool danger, bool disabled = false, bool iconOnly = false, bool spinner = false,
    IconKind icon = IconKind::None) {
    bool hot = r.has(g->mouseX, g->mouseY) && !disabled;
    D2D1_COLOR_F bg = g->colors.transparent;
    D2D1_COLOR_F fg = g->colors.sub;
    if (disabled) { bg = g->colors.chip; fg = g->colors.faint; }
    else if (primary) {
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
    if (spinner) DrawSpinner(p, r, fg);
    else if (iconOnly) DrawIcon(p, icon, Inset(r, 6), fg);
    else p.Text(text, r, primary ? 11.5f : 12.0f, primary ? 600 : 500, fg, 1);
    if (!disabled) g->flySpots.push_back({ r, action, index });
}

static void PaintFlyout(Painter& p) {
    if (!p.Begin(g->colors.surface)) return;
    g->mouseX = g->flyMouseX;
    g->mouseY = g->flyMouseY;
    g->flySpots.clear();
    float W = p.W(), H = p.H();
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.border, 1);

    int active = FlyActiveCount();
    int total = (int)g->flySnaps.size();
    p.FillRound(MakeR(14, 10, 22, 22), 6, g->colors.accent);
    p.Line(19, 14, 25, 20, g->colors.white, 2.4f);
    p.Line(25, 20, 19, 26, g->colors.white, 2.4f);
    p.Ellipse(19, 14, 1.2f, 1.2f, g->colors.white);
    p.Ellipse(25, 20, 1.2f, 1.2f, g->colors.white);
    p.Ellipse(19, 26, 1.2f, 1.2f, g->colors.white);
    p.FillRound(MakeR(26, 24, 6, 2), 1.0f, g->colors.white);
    p.Text(L"DevServeManage", MakeR(42, 7, W - 58, 21), 12.5f, 700, g->colors.text);
    p.Text(Format(L"%d 个正在运行 · 共 %d 个项目", active, total), MakeR(42, 26, W - 58, 16), 10.0f, 400, g->colors.faint);
    p.Line(0, 47, W, 47, g->colors.border, 1);

    if (g->flySnaps.empty()) {
        p.Text(L"还没有项目", MakeR(16, 64, W - 32, 20), 12, 600, g->colors.text, 1);
        p.Text(L"打开主界面，添加一个项目文件夹", MakeR(16, 88, W - 32, 16), 9.5f, 400, g->colors.faint, 1);
        DrawFlyButton(p, MakeR(16, H - 38, W - 32, 26), L"打开主界面", Act::FlyMain, -1, true, false);
        p.End();
        return;
    }

    float y = 53;
    auto drawSection = [&](const std::wstring& label) {
        p.Text(label, MakeR(16, y, W - 32, 14), 9.0f, 600, g->colors.faint);
        y += 16;
    };
    auto drawRow = [&](int index) {
        auto& s = g->flySnaps[index];
        R row = MakeR(8, y, W - 16, 38);
        bool hover = row.has(g->mouseX, g->mouseY);
        if (hover) p.FillRound(row, 7, g->colors.surface2);
        D2D1_COLOR_F accent = StatusColor(s);
        if (s.status == Status::Stopped) accent = g->colors.faint;
        p.Ellipse(row.x + 12, row.y + 11, 3.2f, 3.2f, accent);
        float right = row.x + row.w - 10;
        bool notInstalled = s.project.kind() == Kind::WindowsService && !s.serviceInstalled;
        bool busy = s.status == Status::Starting || s.status == Status::Stopping;
        IconKind primaryIcon = notInstalled ? IconKind::Warning : IsActive(s.status) ? IconKind::Stop : IconKind::Play;
        R pbtn = MakeR(right - 30, row.y + 5, 30, 30);
        right = pbtn.x - 4;
        R restart{};
        R open{};
        if (!notInstalled && (IsActive(s.status) || s.status == Status::Failed)) {
            restart = MakeR(right - 30, row.y + 5, 30, 30);
            right = restart.x - 4;
        }
        if (!s.ports.empty()) {
            open = MakeR(right - 30, row.y + 5, 30, 30);
            right = open.x - 4;
        }
        std::wstring meta = KindText(s.project.kind());
        if (!s.ports.empty()) meta += L" · :" + std::to_wstring(s.ports[0]);
        else if (s.project.port > 0) meta += L" · 预期 :" + std::to_wstring(s.project.port);
        if (s.status == Status::Running && s.startedAt) meta += L" · " + UptimeText(s.startedAt);
        if (!s.lastError.empty() && s.status == Status::Failed) meta += L" · " + s.lastError;
        p.Text(s.project.DisplayName(), MakeR(row.x + 22, row.y + 2, std::max(40.0f, right - row.x - 22), 16), 12.0f, 600, g->colors.text);
        p.Text(meta, MakeR(row.x + 22, row.y + 18, std::max(40.0f, right - row.x - 22), 15), 9.9f, 400,
            s.status == Status::Failed ? g->colors.red : g->colors.faint);
        DrawFlyButton(p, pbtn, L"", Act::FlyPrimary, index, true, IsActive(s.status), notInstalled || busy, true, busy, primaryIcon);
        if (restart.w > 0) DrawFlyButton(p, restart, L"", Act::FlyRestart, index, false, false, false, true, false, IconKind::Restart);
        if (open.w > 0) DrawFlyButton(p, open, L"", Act::FlyOpen, index, false, false, false, true, false, IconKind::Open);
        y += 38;
    };

    if (active > 0) {
        drawSection(L"正在运行 · " + std::to_wstring(active));
        for (size_t i = 0; i < g->flySnaps.size(); ++i) if (IsActive(g->flySnaps[i].status)) drawRow((int)i);
    }
    int stopped = total - active;
    if (stopped > 0) {
        drawSection(L"未运行 · " + std::to_wstring(stopped));
        for (size_t i = 0; i < g->flySnaps.size(); ++i) if (!IsActive(g->flySnaps[i].status)) drawRow((int)i);
    }

    float footerY = H - 38;
    p.Line(0, footerY, W, footerY, g->colors.border, 1);
    DrawFlyButton(p, MakeR(12, footerY + 6, 100, 26), L"打开主界面", Act::FlyMain, -1, true, false);
    DrawFlyButton(p, MakeR(W - 12 - 72, footerY + 6, 72, 26), L"全部停止", Act::FlyStopAll, -1, false, false);
    DrawFlyButton(p, MakeR(W - 12 - 72 - 6 - 72, footerY + 6, 72, 26), L"全部启动", Act::FlyStartAll, -1, false, false);
    if (auto* h = HitSpot(g->flySpots, g->mouseX, g->mouseY)) {
        std::wstring tip;
        if (h->action == Act::FlyPrimary && h->index >= 0 && h->index < (int)g->flySnaps.size()) {
            tip = PrimaryTooltip(g->flySnaps[h->index]);
        } else if (h->action == Act::FlyRestart) tip = L"重启项目";
        else if (h->action == Act::FlyOpen) tip = L"在浏览器打开";
        else if (h->action == Act::FlyMain) tip = L"打开主界面";
        else if (h->action == Act::FlyStartAll) tip = L"全部启动";
        else if (h->action == Act::FlyStopAll) tip = L"全部停止";
        DrawTooltip(p, tip);
    }
    p.End();
}

static void PositionFlyout(float w, float h) {
    RECT icon{};
    POINT cursor{};
    GetCursorPos(&cursor);
    int ix = cursor.x, iy = cursor.y;
    bool haveIcon = GetTrayIconRect(icon);
    if (haveIcon) { ix = (icon.left + icon.right) / 2; iy = icon.top; }
    HMONITOR mon = MonitorFromPoint(POINT{ ix, iy }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    int x = ix - (int)(w / 2);
    int y = iy - (int)h - 10;
    if (y < mi.rcWork.top + 8) y = (haveIcon ? icon.bottom : cursor.y) + 10;
    x = std::max((int)mi.rcWork.left + 8, std::min(x, (int)mi.rcWork.right - (int)w - 8));
    y = std::max((int)mi.rcWork.top + 8, std::min(y, (int)mi.rcWork.bottom - (int)h - 8));
    SetWindowPos(g->flyout, HWND_TOPMOST, x, y, (int)w, (int)h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void ShowFlyout() {
    if (IsCustomMenuOpen()) return;
    if (!g || !g->tray) return;
    HideFlyout();
    g->mainMouseX = -1;
    g->mainMouseY = -1;
    InvalidateMain();
    RefreshFlySnaps();
    if (!g->flyout) {
        g->flyout = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kFlyoutClass, L"", WS_POPUP, 0, 0, 430, 260, g->main, nullptr, g->instance, nullptr);
        if (!g->flyout) { AppLog(L"创建悬停浮层失败"); return; }
        g->flyPainter.hwnd = g->flyout;
        BOOL dark = g->dark;
        DwmSetWindowAttribute(g->flyout, 20, &dark, sizeof(dark));
        int corner = 2;
        DwmSetWindowAttribute(g->flyout, 33, &corner, sizeof(corner));
    }
    float h = FlyHeight();
    g->flyW = 430;
    g->flyH = h;
    PositionFlyout(g->flyW, h);
    ShowWindow(g->flyout, SW_SHOWNOACTIVATE);
    SetTimer(g->flyout, TIMER_FLY_HIDE, 200, nullptr);
    SetTimer(g->flyout, TIMER_SPIN, 80, nullptr);
    g->flyVisible = true;
    g->flyMissTicks = 0;
    g->flyShownAt = GetTickCount64();
    InvalidateRect(g->flyout, nullptr, FALSE);
}

static void HideFlyout() {
    if (!g || !g->flyout) return;
    g->flyVisible = false;
    g->flyMissTicks = 0;
    g->flyMouseX = -1;
    g->flyMouseY = -1;
    KillTimer(g->flyout, TIMER_FLY_HIDE);
    KillTimer(g->flyout, TIMER_SPIN);
    ShowWindow(g->flyout, SW_HIDE);
}

static void CheckFlyoutHover() {
    if (!g->flyVisible) return;
    POINT pt{}; GetCursorPos(&pt);
    RECT fr{}; GetWindowRect(g->flyout, &fr);
    RECT ir{};
    bool over = PtInRect(&fr, pt) != FALSE;
    if (!over && GetTrayIconRect(ir)) over = PtInRect(&ir, pt) != FALSE;
    if (over) g->flyMissTicks = 0;
    else if (++g->flyMissTicks >= 4) HideFlyout();
}

static int EngineIndexForId(const std::wstring& id) {
    auto snaps = g->engine.Snapshots();
    for (size_t i = 0; i < snaps.size(); ++i) if (snaps[i].project.id == id) return (int)i;
    return -1;
}

static void HandleFlyAction(Act action, int index) {
    if (index >= 0 && index < (int)g->flySnaps.size()) {
        auto s = g->flySnaps[index];
        int ei = EngineIndexForId(s.project.id);
        if (action == Act::FlyPrimary) {
            if (ei >= 0) {
                if (IsActive(s.status)) {
                    StopProject(ei);
                } else {
                    PortOwner ownerInfo;
                    if (g->engine.CheckPortConflict(s.project.id, ownerInfo)) {
                        // Only a real conflict needs a modal prompt; normal starts must not hide the flyout.
                        HideFlyout();
                        StartProject(ei, g->main);
                        ShowFlyout();
                    } else {
                        g->engine.Start(s.project.id);
                    }
                }
                RefreshFlySnaps();
                if (g->flyVisible) {
                    float h = FlyHeight();
                    if (std::abs(h - g->flyH) > 0.5f) {
                        g->flyH = h;
                        PositionFlyout(g->flyW, h);
                    }
                    InvalidateRect(g->flyout, nullptr, FALSE);
                }
            }
        } else if (action == Act::FlyRestart) {
            if (ei >= 0) { g->engine.Restart(s.project.id); RefreshFlySnaps(); }
        } else if (action == Act::FlyOpen) {
            if (!s.ports.empty()) {
                std::wstring url = L"http://localhost:" + std::to_wstring(s.ports[0]);
                ShellExecuteW(g->main, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                HideFlyout();
            }
        }
        InvalidateRect(g->flyout, nullptr, FALSE);
        return;
    }
    if (action == Act::FlyMain) { HideFlyout(); ShowMainWindow(); }
    else if (action == Act::FlyStartAll) { g->engine.StartAll(); RefreshFlySnaps(); InvalidateRect(g->flyout, nullptr, FALSE); }
    else if (action == Act::FlyStopAll) { g->engine.StopAll(); RefreshFlySnaps(); InvalidateRect(g->flyout, nullptr, FALSE); }
}

static LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g->flyout = hwnd;
        g->flyPainter.hwnd = hwnd;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        PaintFlyout(g->flyPainter);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        float scale = (float)GetDpiForWindow(hwnd);
        g->flyMouseX = GET_X_LPARAM(lp) * 96.0f / scale;
        g->flyMouseY = GET_Y_LPARAM(lp) * 96.0f / scale;
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        g->flyMissTicks = 0;
        return 0;
    }
    case WM_MOUSELEAVE:
        g->flyMouseX = -1;
        g->flyMouseY = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            auto* h = HitSpot(g->flySpots, g->flyMouseX, g->flyMouseY);
            SetCursor(LoadCursorW(nullptr, h ? MAKEINTRESOURCEW(32649) : MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONUP: {
        float scale = (float)GetDpiForWindow(hwnd);
        float x = GET_X_LPARAM(lp) * 96.0f / scale;
        float y = GET_Y_LPARAM(lp) * 96.0f / scale;
        auto* h = HitSpot(g->flySpots, x, y);
        if (h) HandleFlyAction(h->action, h->index);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_FLY_HIDE) CheckFlyoutHover();
        else if (wp == TIMER_SPIN) {
            for (auto& s : g->flySnaps) {
                if (s.status == Status::Starting || s.status == Status::Stopping) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
            }
        }
        return 0;
    case WM_RBUTTONUP:
        HideFlyout();
        return 0;
    case WM_DESTROY:
        g->flyout = nullptr;
        g->flyVisible = false;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------
static HICON PickTrayIcon(int& running, int& total) {
    auto snaps = g->engine.Snapshots();
    total = (int)snaps.size();
    running = 0;
    bool busy = false, failed = false;
    for (auto& s : snaps) {
        if (s.status == Status::Starting || s.status == Status::Stopping) busy = true;
        if (s.status == Status::Running) ++running;
        if (s.status == Status::Failed) failed = true;
    }
    g->runningCount = running;
    g->totalCount = total;
    if (busy) return g->iconTrayBusy;
    if (failed && running == 0) return g->iconTrayError;
    if (running > 0) return g->iconTrayRunning;
    return g->iconTrayIdle;
}

static void UpdateTrayIcon() {
    if (!g || !g->tray) return;
    int running = 0, total = 0;
    HICON icon = PickTrayIcon(running, total);
    std::wstring tip = total == 0 ? L"DevServeManage · 还没有项目" :
        Format(L"DevServeManage · %d/%d 运行中", running, total);
    if (tip.size() > 120) tip = tip.substr(0, 120);
    bool iconChanged = icon != g->currentTrayIcon;
    bool tipChanged = tip != g->lastTooltip;
    if (g->trayAdded && !iconChanged && !tipChanged) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g->tray;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_GUID;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    nid.guidItem = kTrayGuid;
    lstrcpynW(nid.szTip, tip.c_str(), _countof(nid.szTip));
    if (!g->trayAdded) {
        // The fixed GUID lets a new process replace an icon left behind by a killed instance.
        NOTIFYICONDATAW old{};
        old.cbSize = sizeof(old);
        old.hWnd = g->tray;
        old.uID = 1;
        old.uFlags = NIF_GUID;
        old.guidItem = kTrayGuid;
        Shell_NotifyIconW(NIM_DELETE, &old);

        bool added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (!added) {
            added = Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
        }
        if (!added) {
            // Very old shells may not understand NIF_GUID; fall back to hWnd/uID.
            g->trayGuidMode = false;
            nid.uFlags &= ~NIF_GUID;
            nid.guidItem = GUID{};
            added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        }
        if (added) {
            g->trayAdded = true;
            g->currentTrayIcon = icon;
            g->lastTooltip = tip;
            NOTIFYICONDATAW version{};
            version.cbSize = sizeof(version);
            version.hWnd = g->tray;
            version.uID = 1;
            version.uVersion = NOTIFYICON_VERSION_4;
            if (g->trayGuidMode) {
                version.uFlags = NIF_GUID;
                version.guidItem = kTrayGuid;
            }
            Shell_NotifyIconW(NIM_SETVERSION, &version);
            AppLog(L"托盘图标已创建");
        } else {
            AppLog(Format(L"添加托盘图标失败（%lu）", GetLastError()));
        }
    } else {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        g->currentTrayIcon = icon;
        g->lastTooltip = tip;
    }
}

static void EnsureTrayVisible() {
    if (!g || !g->tray) return;
    if (!g->trayAdded) { UpdateTrayIcon(); return; }
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = g->tray;
    id.uID = 1;
    if (g->trayGuidMode) id.guidItem = kTrayGuid;
    RECT r{};
    if (Shell_NotifyIconGetRect(&id, &r) != S_OK) {
        AppLog(L"托盘图标不在通知区域，正在重新添加");
        g->trayAdded = false;
        g->currentTrayIcon = nullptr;
        UpdateTrayIcon();
    }
}

static void ShowTrayMenu(HWND owner, POINT pt) {
    if (IsCustomMenuOpen()) return;
    HideFlyout();
    AppLog(L"托盘右键：开始");
    std::vector<UiMenuItem> items = {
        { 1, L"打开主界面", IconKind::Window, false, true, false },
        { 0, L"", IconKind::None, false, false, true },
        { 2, L"全部启动", IconKind::Play, false, true, false },
        { 3, L"全部停止", IconKind::Stop, false, true, false },
        { 4, L"全部重启", IconKind::Restart, false, true, false },
        { 0, L"", IconKind::None, false, false, true },
        { 5, L"重启程序", IconKind::Restart, false, true, false },
        { 6, L"退出 DevServeManage", IconKind::Exit, true, true, false },
    };
    int cmd = ShowCustomMenu(nullptr, pt, items);
    AppLog(L"托盘右键：菜单已关闭");
    if (cmd == 1) ShowMainWindow();
    else if (cmd == 2) { g->engine.StartAll(); UpdateTrayIcon(); InvalidateMain(); }
    else if (cmd == 3) { g->engine.StopAll(); UpdateTrayIcon(); InvalidateMain(); }
    else if (cmd == 4) { g->engine.RestartAll(); UpdateTrayIcon(); InvalidateMain(); }
    else if (cmd == 5) RestartApplication();
    else if (cmd == 6) RequestExit();
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        AppLog(L"收到 TaskbarCreated，重新注册托盘图标");
        g->trayAdded = false;
        g->currentTrayIcon = nullptr;
        UpdateTrayIcon();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        g->tray = hwnd;
        return 0;
    case WM_TRAY: {
        UINT ev = LOWORD(lp);
        if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == WM_LBUTTONDBLCLK) {
            HideFlyout();
            ShowMainWindow();
            return 0;
        }
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            KillTimer(hwnd, TIMER_HOVER);
            HideFlyout();
            POINT pt{}; GetCursorPos(&pt);
            ShowTrayMenu(hwnd, pt);
            return 0;
        }
        if (ev == WM_MOUSEMOVE || ev == NIN_POPUPOPEN) {
            if (IsCustomMenuOpen()) return 0;
            if (!g->flyVisible) SetTimer(hwnd, TIMER_HOVER, 250, nullptr);
            else g->flyMissTicks = 0;
            return 0;
        }
        if (ev == NIN_POPUPCLOSE || ev == WM_MOUSELEAVE) {
            HideFlyout();
            return 0;
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            PollEngine();
            UpdateTrayIcon();
            ++g->tick;
            if (g->tick % 5 == 0) EnsureTrayVisible();
            if (g->flyVisible) {
                RefreshFlySnaps();
                InvalidateRect(g->flyout, nullptr, FALSE);
            }
            if (g->main && IsWindowVisible(g->main)) InvalidateRect(g->main, nullptr, FALSE);
            return 0;
        }
        if (wp == TIMER_HOVER) {
            KillTimer(hwnd, TIMER_HOVER);
            if (IsCustomMenuOpen()) return 0;
            POINT pt{}; GetCursorPos(&pt);
            RECT ir{};
            if (GetTrayIconRect(ir) && PtInRect(&ir, pt)) ShowFlyout();
            return 0;
        }
        if (wp == TIMER_FLY_HIDE) {
            CheckFlyoutHover();
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if (g->trayAdded) {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = hwnd;
            nid.uID = 1;
            if (g->trayGuidMode) {
                nid.uFlags = NIF_GUID;
                nid.guidItem = kTrayGuid;
            }
            Shell_NotifyIconW(NIM_DELETE, &nid);
            g->trayAdded = false;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------
static void RegisterWindowClasses() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = g->instance;
    wc.hIcon = g->iconApp;
    wc.hIconSm = g->iconApp;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = CreateSolidBrush(ToColorRef(g->colors.bg));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW tc{};
    tc.cbSize = sizeof(tc);
    tc.lpfnWndProc = TrayWndProc;
    tc.hInstance = g->instance;
    tc.lpszClassName = kTrayClass;
    RegisterClassExW(&tc);

    WNDCLASSEXW fc{};
    fc.cbSize = sizeof(fc);
    fc.style = CS_DROPSHADOW;
    fc.lpfnWndProc = FlyoutWndProc;
    fc.hInstance = g->instance;
    fc.hIcon = g->iconApp;
    fc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32649));
    fc.lpszClassName = kFlyoutClass;
    RegisterClassExW(&fc);
}

static HICON LoadIconResource(HINSTANCE instance, int id) {
    return (HICON)LoadImageW(instance, MAKEINTRESOURCEW(id), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
}

static void CreateTrayWindow() {
    g->tray = CreateWindowExW(0, kTrayClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
        nullptr, g->instance, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    UpdateTrayIcon();
    SetTimer(g->tray, TIMER_TICK, 1000, nullptr);
}

static void CreateMainWindow(bool show) {
    g->main = CreateWindowExW(0, kMainClass, L"DevServeManage — 本地项目运行台",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, 0, 1120, 760,
        nullptr, nullptr, g->instance, nullptr);
    if (!g->main) return;
    SendMessageW(g->main, WM_SETICON, ICON_BIG, (LPARAM)g->iconApp);
    SendMessageW(g->main, WM_SETICON, ICON_SMALL, (LPARAM)g->iconApp);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = 1120, h = 760;
    int x = work.left + (work.right - work.left - w) / 2;
    int y = work.top + (work.bottom - work.top - h) / 2;
    SetWindowPos(g->main, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(g->main, 3, &disableTransitions, sizeof(disableTransitions));
    if (show) {
        ShowMainWindowFirstFrame();
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\DevServeManage.Native.SingleInstance.v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kMainClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
            PostMessageW(existing, WM_APP_SHOW, 0, 0);
        }
        CloseHandle(g_mutex);
        CoUninitialize();
        return 0;
    }

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Direct2D 初始化失败，无法启动。", L"DevServeManage", MB_OK | MB_ICONERROR);
        return 1;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwrite);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"DirectWrite 初始化失败，无法启动。", L"DevServeManage", MB_OK | MB_ICONERROR);
        return 1;
    }

    g = new AppState();
    g->instance = instance;
    LoadSettings();
    if (!FileExists(ConfigPath())) {
        std::wstring configError;
        if (!SaveSettings(nullptr, &configError)) AppLog(L"创建默认配置失败：" + configError);
    }
    g->dark = EffectiveDark();
    g->colors = MakeColors(g->dark);
    g->iconApp = LoadIconResource(instance, IDI_APP);
    g->iconTrayIdle = LoadIconResource(instance, IDI_TRAY_IDLE);
    g->iconTrayRunning = LoadIconResource(instance, IDI_TRAY_RUNNING);
    g->iconTrayBusy = LoadIconResource(instance, IDI_TRAY_BUSY);
    g->iconTrayError = LoadIconResource(instance, IDI_TRAY_ERROR);

    AppLog(L"DevServeManage Native 启动 · " + ProcessPath());

    RegisterWindowClasses();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool trayOnly = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) if (_wcsicmp(argv[i], L"--tray") == 0) trayOnly = true;
        LocalFree(argv);
    }
    CreateMainWindow(!trayOnly && !g_settings.startMinimized);
    CreateTrayWindow();
    if (!g->main) {
        MessageBoxW(nullptr, L"创建主窗口失败。", L"DevServeManage", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g->engine.Shutdown();
    if (g->trayAdded) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid); nid.hWnd = g->tray; nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    g->mainPainter.Discard();
    g->flyPainter.Discard();
    delete g; g = nullptr;
    if (g_dwrite) g_dwrite->Release();
    if (g_d2d) g_d2d->Release();
    if (g_mutex) CloseHandle(g_mutex);
    CoUninitialize();
    return 0;
}
