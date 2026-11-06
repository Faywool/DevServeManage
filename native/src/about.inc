// A standalone, richly laid out About window.
#pragma once

struct AboutState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    Painter painter;
    R closeRect{};
    R configRect{};
    R githubRect{};
    R mailRect{};
    R closeButton{};
    int hot = -1;
    bool closing = false;
    float mouseX = -1, mouseY = -1;
};

static const wchar_t* kAboutClass = L"DevServeManageAboutWindow";
static AboutState* g_aboutState = nullptr;

static void PaintAbout(AboutState& s) {
    Painter& p = s.painter;
    if (!p.Begin(g->colors.bg)) return;
    float W = p.W(), H = p.H();
    p.FillRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.surface);
    p.StrokeRound(MakeR(0.5f, 0.5f, W - 1, H - 1), 12, g->colors.border, 1);

    // App icon: accent rounded square, white chevron and underscore.
    R icon = MakeR(32, 28, 72, 72);
    p.FillRound(icon, 18, g->colors.accent);
    p.Line(icon.x + 16, icon.y + 16, icon.x + 42, icon.y + 36, g->colors.white, 7.0f);
    p.Line(icon.x + 42, icon.y + 36, icon.x + 16, icon.y + 56, g->colors.white, 7.0f);
    p.Ellipse(icon.x + 16, icon.y + 16, 3.5f, 3.5f, g->colors.white);
    p.Ellipse(icon.x + 42, icon.y + 36, 3.5f, 3.5f, g->colors.white);
    p.Ellipse(icon.x + 16, icon.y + 56, 3.5f, 3.5f, g->colors.white);
    p.FillRound(MakeR(icon.x + 44, icon.y + 55, 18, 5), 2.5f, g->colors.white);

    p.Text(L"DevServeManage", MakeR(128, 28, 380, 34), 24, 700, g->colors.text);
    p.Text(L"本地项目统一运行台", MakeR(130, 64, 300, 20), 11.5f, 400, g->colors.faint);
    R version = MakeR(128, 88, 72, 22);
    p.FillRound(version, 6, g->colors.chip);
    p.Text(L"v1.0.0", version, 10.5f, 600, g->colors.accent, 1);

    s.closeRect = MakeR(W - 48, 18, 30, 30);
    bool closeHot = s.closeRect.has(s.mouseX, s.mouseY);
    p.FillRound(s.closeRect, 7, closeHot ? g->colors.chipHover : g->colors.transparent);
    RoundLine(p, s.closeRect.x + 9, s.closeRect.y + 9, s.closeRect.x + 21, s.closeRect.y + 21,
        closeHot ? g->colors.text : g->colors.sub, 1.4f);
    RoundLine(p, s.closeRect.x + 21, s.closeRect.y + 9, s.closeRect.x + 9, s.closeRect.y + 21,
        closeHot ? g->colors.text : g->colors.sub, 1.4f);

    p.Line(28, 124, W - 28, 124, g->colors.border, 1);
    p.Text(L"一个原生 Windows 本地项目管理台。",
        MakeR(32, 144, W - 64, 22), 11.5f, 500, g->colors.text);
    p.Text(L"启动 / 停止本地前端、.NET、IIS Express 与 Windows 服务项目，集中查看端口、PID、日志和构建结果。",
        MakeR(32, 170, W - 64, 36), 10.2f, 400, g->colors.sub);

    float chipX = 32;
    const wchar_t* techs[] = { L"C++20", L"Win32", L"Direct2D", L"DirectWrite", L"Lucide SVG" };
    for (auto tech : techs) {
        float cw = p.Measure(tech, 9.5f, 600) + 18;
        R chip = MakeR(chipX, 224, cw, 22);
        p.FillRound(chip, 6, g->colors.chip);
        p.Text(tech, chip, 9.5f, 600, g->colors.sub, 1);
        chipX += cw + 7;
    }

    p.Line(28, 276, W - 28, 276, g->colors.border, 1);
    p.Text(L"许可与来源", MakeR(32, 294, 180, 20), 11.5f, 700, g->colors.text);
    p.Text(L"MIT License · Copyright © 2026 DevServeManage contributors",
        MakeR(32, 318, W - 64, 20), 10.0f, 400, g->colors.sub);
    p.Text(L"图标：Lucide（ISC；部分源于 Feather，MIT）。详见 THIRD_PARTY_NOTICES.md。",
        MakeR(32, 340, W - 64, 36), 9.5f, 400, g->colors.faint);

    p.Line(28, 366, W - 28, 366, g->colors.border, 1);
    p.Text(L"GitHub", MakeR(32, 382, 72, 20), 10.0f, 500, g->colors.faint);
    p.Text(L"https://github.com/Faywool/DevServeManage",
        MakeR(110, 382, W - 310, 20), 10.2f, 500, g->colors.accent);
    s.githubRect = MakeR(W - 184, 376, 152, 30);
    bool githubHot = s.githubRect.has(s.mouseX, s.mouseY);
    p.FillRound(s.githubRect, 7, githubHot ? g->colors.chipHover : g->colors.chip);
    p.StrokeRound(s.githubRect, 7, g->colors.border, 1);
    p.Text(L"打开 GitHub", s.githubRect, 10.5f, 500, githubHot ? g->colors.text : g->colors.sub, 1);

    p.Text(L"邮箱", MakeR(32, 418, 72, 20), 10.0f, 500, g->colors.faint);
    p.Text(L"Faywool@Live.cn", MakeR(110, 418, W - 310, 20), 10.5f, 500, g->colors.text);
    s.mailRect = MakeR(W - 184, 412, 152, 30);
    bool mailHot = s.mailRect.has(s.mouseX, s.mouseY);
    p.FillRound(s.mailRect, 7, mailHot ? g->colors.chipHover : g->colors.chip);
    p.StrokeRound(s.mailRect, 7, g->colors.border, 1);
    p.Text(L"发送邮件", s.mailRect, 10.5f, 500, mailHot ? g->colors.text : g->colors.sub, 1);

    p.Line(28, 454, W - 28, 454, g->colors.border, 1);
    p.Text(L"配置目录", MakeR(32, 470, 100, 20), 10.0f, 500, g->colors.faint);
    p.Text(L"%APPDATA%\\DevServeManage", MakeR(110, 470, 300, 20), 10.5f, 400, g->colors.text);
    s.configRect = MakeR(W - 184, 466, 152, 30);
    bool configHot = s.configRect.has(s.mouseX, s.mouseY);
    p.FillRound(s.configRect, 7, configHot ? g->colors.chipHover : g->colors.chip);
    p.StrokeRound(s.configRect, 7, g->colors.border, 1);
    p.Text(L"打开配置目录", s.configRect, 10.5f, 500, configHot ? g->colors.text : g->colors.sub, 1);

    s.closeButton = MakeR(W - 116, H - 54, 84, 30);
    bool closeButtonHot = s.closeButton.has(s.mouseX, s.mouseY);
    p.FillRound(s.closeButton, 7, closeButtonHot ? g->colors.chipHover : g->colors.chip);
    p.StrokeRound(s.closeButton, 7, g->colors.border, 1);
    p.Text(L"关闭", s.closeButton, 11.0f, 600, g->colors.text, 1);
    p.End();
}

static LRESULT CALLBACK AboutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AboutState* s = g_aboutState;
    switch (msg) {
    case WM_CREATE:
        if (s) s->hwnd = hwnd;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
        if (s) {
            g->mouseX = s->mouseX;
            g->mouseY = s->mouseY;
            PaintAbout(*s);
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
            int hot = s->closeButton.has(s->mouseX, s->mouseY) ? 1 :
                s->configRect.has(s->mouseX, s->mouseY) ? 2 :
                s->githubRect.has(s->mouseX, s->mouseY) ? 4 :
                s->mailRect.has(s->mouseX, s->mouseY) ? 5 :
                s->closeRect.has(s->mouseX, s->mouseY) ? 3 : -1;
            if (hot != s->hot) { s->hot = hot; InvalidateRect(hwnd, nullptr, FALSE); }
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (s && s->hot != -1) { s->hot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
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
            if (s->closeButton.has(x, y) || s->closeRect.has(x, y)) DestroyWindow(hwnd);
            else if (s->configRect.has(x, y))
                ShellExecuteW(hwnd, L"open", DataDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else if (s->githubRect.has(x, y))
                ShellExecuteW(hwnd, L"open", L"https://github.com/Faywool/DevServeManage", nullptr, nullptr, SW_SHOWNORMAL);
            else if (s->mailRect.has(x, y))
                ShellExecuteW(hwnd, L"open", L"mailto:Faywool@Live.cn", nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowAboutWindow(HWND owner) {
    if (!g || g_aboutState) return;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = AboutProc;
        wc.hInstance = g->instance;
        wc.hIcon = g->iconApp;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kAboutClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    AboutState state;
    state.owner = owner;
    g_aboutState = &state;
    int width = 640, height = 580;
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (work.right - work.left - width) / 2;
    int y = work.top + (work.bottom - work.top - height) / 2;
    state.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kAboutClass, L"关于 DevServeManage",
        WS_POPUP, x, y, width, height, owner, nullptr, g->instance, nullptr);
    if (!state.hwnd) { g_aboutState = nullptr; return; }
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
    g_aboutState = nullptr;
}
