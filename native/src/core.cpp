#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "core.h"
#include "json.h"

#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sddl.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

namespace dsm {

static std::mutex g_logMutex;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Trim(std::wstring s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t small[512];
    va_list ap; va_start(ap, fmt);
    int n = _vsnwprintf_s(small, _countof(small), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n >= 0) return small;
    std::wstring out((size_t)(-n) + 1, L'\0');
    va_start(ap, fmt);
    _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, fmt, ap);
    va_end(ap);
    return out;
}

std::wstring DataDirectory() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    }
    if (out.empty()) {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        out.assign(buf, n);
    }
    out = JoinPath(out, L"DevServeManage");
    std::error_code ec;
    fs::create_directories(fs::path(out), ec);
    return out;
}

std::wstring AppLogPath() { return JoinPath(DataDirectory(), L"native.log"); }

void AppLog(const std::wstring& text) {
    std::lock_guard lk(g_logMutex);
    std::error_code ec;
    fs::create_directories(fs::path(DataDirectory()), ec);
    std::wstring path = AppLogPath();
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr)
        && (((ULONGLONG)attr.nFileSizeHigh << 32) | attr.nFileSizeLow) > 2ULL * 1024 * 1024) {
        std::wstring old = path + L".old";
        DeleteFileW(old.c_str());
        MoveFileW(path.c_str(), old.c_str());
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring line = Format(L"%04d-%02d-%02d %02d:%02d:%02d.%03d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds) + text + L"\r\n";
    std::string utf8 = Narrow(line);
    DWORD written = 0;
    SetFilePointer(h, 0, nullptr, FILE_END);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ReadTextFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::string data;
    if (size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024) data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    if (!data.empty()) ReadFile(h, data.data(), (DWORD)data.size(), &read, nullptr);
    CloseHandle(h);
    data.resize(read);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);
    return data;
}

bool WriteTextFile(const std::wstring& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(DirectoryOf(path)), ec);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) != 0;
    CloseHandle(h);
    return ok && written == text.size();
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') {
        if (b.front() == L'\\' || b.front() == L'/') return a + b.substr(1);
        return a + b;
    }
    if (b.front() == L'\\' || b.front() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

std::wstring NowStamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    return Format(L"%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::wstring UptimeText(ULONGLONG startedAt) {
    if (!startedAt) return L"";
    ULONGLONG sec = (GetTickCount64() - startedAt) / 1000;
    if (sec < 60) return Format(L"%llus", sec);
    if (sec < 3600) return Format(L"%llum %llus", sec / 60, sec % 60);
    return Format(L"%lluh %llum", sec / 3600, (sec % 3600) / 60);
}

const wchar_t* KindText(Kind k) {
    switch (k) {
    case Kind::Frontend: return L"前端";
    case Kind::DotNet: return L".NET";
    case Kind::AspNetFramework: return L"ASP.NET 站点";
    case Kind::WindowsService: return L"Windows 服务";
    }
    return L"项目";
}

Kind KindFromText(const std::wstring& text) {
    if (text == L"DotNet") return Kind::DotNet;
    if (text == L"AspNetFramework") return Kind::AspNetFramework;
    if (text == L"WindowsService") return Kind::WindowsService;
    return Kind::Frontend;
}

const wchar_t* StatusText(Status s) {
    switch (s) {
    case Status::Stopped: return L"未启动";
    case Status::Starting: return L"启动中";
    case Status::Running: return L"运行中";
    case Status::Stopping: return L"停止中";
    case Status::Failed: return L"启动失败";
    }
    return L"未启动";
}

// ---------------------------------------------------------------------------
// TCP listener table. The LISTENER table is authoritative, and loopback-only
// sockets can report state=0 on some Windows builds, so no state filtering here.
// ---------------------------------------------------------------------------
std::vector<PortBindingInfo> ScanListeners() {
    std::vector<PortBindingInfo> out;
    auto read = [&](ULONG af, bool ipv6) {
        DWORD size = 0;
        DWORD rc = GetExtendedTcpTable(nullptr, &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) return;
        std::vector<BYTE> buf(size);
        rc = GetExtendedTcpTable(buf.data(), &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (rc != NO_ERROR) return;
        DWORD count = *(DWORD*)buf.data();
        BYTE* row = buf.data() + 4;
        DWORD stride = ipv6 ? 56 : 24;
        for (DWORD i = 0; i < count; ++i, row += stride) {
            DWORD rawPort = 0, pid = 0;
            if (ipv6) {
                rawPort = *(DWORD*)(row + 20);
                pid = *(DWORD*)(row + 52);
            } else {
                rawPort = *(DWORD*)(row + 8);
                pid = *(DWORD*)(row + 20);
            }
            int port = (int)((((rawPort & 0xFF) << 8) | ((rawPort >> 8) & 0xFF)));
            if (port > 0 && pid > 0) out.push_back({ port, pid, ipv6 ? L"::" : L"0.0.0.0" });
        }
    };
    read(AF_INET, false);
    read(AF_INET6, true);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.port != b.port) return a.port < b.port;
        return a.pid < b.pid;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.port == b.port && a.pid == b.pid;
    }), out.end());
    return out;
}

std::vector<DWORD> ProcessTree(DWORD root) {
    std::vector<DWORD> result;
    if (!root) return result;
    std::map<DWORD, std::vector<DWORD>> children;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return { root };
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            children[pe.th32ParentProcessID].push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::vector<DWORD> queue{ root };
    std::set<DWORD> seen;
    while (!queue.empty()) {
        DWORD p = queue.back(); queue.pop_back();
        if (!seen.insert(p).second) continue;
        result.push_back(p);
        for (DWORD c : children[p]) queue.push_back(c);
    }
    return result;
}

bool QueryPortOwner(int port, DWORD& pid) {
    for (const auto& b : ScanListeners()) {
        if (b.port == port) { pid = b.pid; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Windows Service control
// ---------------------------------------------------------------------------
std::wstring ServiceStateText(DWORD state) {
    switch (state) {
    case SERVICE_STOPPED: return L"已停止";
    case SERVICE_START_PENDING: return L"正在启动";
    case SERVICE_STOP_PENDING: return L"正在停止";
    case SERVICE_RUNNING: return L"运行中";
    case SERVICE_CONTINUE_PENDING: return L"继续中";
    case SERVICE_PAUSE_PENDING: return L"暂停中";
    case SERVICE_PAUSED: return L"已暂停";
    }
    return L"未知";
}

static bool OpenServiceHandle(const std::wstring& name, DWORD access, SC_HANDLE& scm, SC_HANDLE& svc, std::wstring& error) {
    scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { error = L"无法打开服务管理器，可能需要管理员权限"; return false; }
    svc = OpenServiceW(scm, name.c_str(), access);
    if (!svc) {
        DWORD e = GetLastError();
        error = e == ERROR_SERVICE_DOES_NOT_EXIST ? L"服务未安装" : Format(L"打开服务失败（%lu）", e);
        CloseServiceHandle(scm); scm = nullptr;
        return false;
    }
    return true;
}

bool QueryServiceInfo(const std::wstring& name, bool& installed, DWORD& state, std::wstring& error) {
    installed = false; state = 0; error.clear();
    if (name.empty()) { error = L"未配置服务名"; return false; }
    SC_HANDLE scm = nullptr, svc = nullptr;
    if (!OpenServiceHandle(name, SERVICE_QUERY_STATUS, scm, svc, error)) {
        if (error == L"服务未安装") installed = false;
        return false;
    }
    SERVICE_STATUS_PROCESS st{}; DWORD needed = 0;
    bool ok = QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&st, sizeof(st), &needed) != 0;
    if (!ok) error = Format(L"查询服务状态失败（%lu）", GetLastError());
    else { installed = true; state = st.dwCurrentState; }
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    return ok;
}

bool StartServiceByName(const std::wstring& name, std::wstring& error) {
    SC_HANDLE scm = nullptr, svc = nullptr;
    if (!OpenServiceHandle(name, SERVICE_START | SERVICE_QUERY_STATUS, scm, svc, error)) return false;
    bool ok = StartServiceW(svc, 0, nullptr) != 0;
    DWORD e = GetLastError();
    if (!ok && e == ERROR_SERVICE_ALREADY_RUNNING) ok = true;
    if (!ok) error = Format(L"启动服务失败（%lu）", e);
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    return ok;
}

bool StopServiceByName(const std::wstring& name, std::wstring& error) {
    SC_HANDLE scm = nullptr, svc = nullptr;
    if (!OpenServiceHandle(name, SERVICE_STOP | SERVICE_QUERY_STATUS, scm, svc, error)) return false;
    SERVICE_STATUS st{};
    bool ok = ControlService(svc, SERVICE_CONTROL_STOP, &st) != 0;
    if (!ok && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) error = Format(L"停止服务失败（%lu）", GetLastError());
    else ok = true;
    CloseServiceHandle(svc); CloseServiceHandle(scm);
    return ok;
}

// ---------------------------------------------------------------------------
// Detection helpers
// ---------------------------------------------------------------------------
static std::wstring GenerateId() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) return Format(L"%08x-%08x", GetTickCount(), GetCurrentProcessId());
    wchar_t buf[64]{};
    StringFromGUID2(g, buf, 64);
    std::wstring s = buf;
    if (!s.empty() && s.front() == L'{') s.erase(s.begin());
    if (!s.empty() && s.back() == L'}') s.pop_back();
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

static bool EndsWith(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() && _wcsicmp(s.c_str() + s.size() - suffix.size(), suffix.c_str()) == 0;
}

static bool HasFile(const std::wstring& dir, const std::wstring& name) {
    return FileExists(JoinPath(dir, name));
}

static std::wstring FindCsproj(const std::wstring& root) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(root), ec)) {
        if (e.is_regular_file() && e.path().extension() == L".csproj") return e.path().wstring();
    }
    // A little deeper, but never scan bin/obj/node_modules.
    for (auto it = fs::recursive_directory_iterator(fs::path(root), fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::wstring name = it->path().filename().wstring();
        if (it->is_directory()) {
            std::wstring low = ToLower(name);
            if (low == L"bin" || low == L"obj" || low == L"node_modules" || low == L".git") it.disable_recursion_pending();
            continue;
        }
        if (it->path().extension() == L".csproj") return it->path().wstring();
    }
    return {};
}

static std::wstring FindServiceName(const std::wstring& root, const std::wstring& fallback) {
    std::error_code ec;
    std::wregex re(LR"re(ServiceName\s*=\s*"([^"]+)")re", std::regex::icase);
    for (auto it = fs::recursive_directory_iterator(fs::path(root), fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory()) {
            std::wstring low = ToLower(it->path().filename().wstring());
            if (low == L"bin" || low == L"obj" || low == L"node_modules" || low == L".git") it.disable_recursion_pending();
            continue;
        }
        if (it->path().extension() != L".cs") continue;
        std::wstring text = Widen(ReadTextFile(it->path().wstring()));
        std::wsmatch m;
        if (std::regex_search(text, m, re)) return m[1].str();
    }
    return fallback;
}

static bool LooksLikeService(const std::wstring& root) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(fs::path(root), fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory()) {
            std::wstring low = ToLower(it->path().filename().wstring());
            if (low == L"bin" || low == L"obj" || low == L"node_modules" || low == L".git") it.disable_recursion_pending();
            continue;
        }
        if (it->path().extension() != L".cs") continue;
        std::wstring name = ToLower(it->path().filename().wstring());
        if (name.rfind(L"projectinstaller", 0) == 0 || name.find(L"installer") != std::wstring::npos) return true;
        std::wstring text = Widen(ReadTextFile(it->path().wstring()));
        if (text.find(L"ServiceBase") != std::wstring::npos && text.find(L"ServiceInstaller") != std::wstring::npos)
            return true;
    }
    return false;
}

static int ParsePortFromText(const std::wstring& text, int fallback) {
    std::wregex re(LR"((?:--port|-p|PORT)\s*[=: ]\s*(\d{2,5}))", std::regex::icase);
    std::wsmatch m;
    if (std::regex_search(text, m, re)) {
        int p = _wtoi(m[1].str().c_str());
        if (p > 0 && p < 65536) return p;
    }
    std::wregex url(LR"(https?://(?:localhost|127\.0\.0\.1|\[::1?\]):(\d{2,5}))", std::regex::icase);
    if (std::regex_search(text, m, url)) {
        int p = _wtoi(m[1].str().c_str());
        if (p > 0 && p < 65536) return p;
    }
    return fallback;
}

static int ParseEnvPort(const std::wstring& root, int fallback) {
    for (auto name : { L".env", L".env.local", L".env.development" }) {
        std::wstring p = JoinPath(root, name);
        if (!FileExists(p)) continue;
        std::wstring text = Widen(ReadTextFile(p));
        std::wsmatch m;
        std::wregex re(LR"((?:^|\n)\s*PORT\s*=\s*(\d{2,5}))");
        if (std::regex_search(text, m, re)) return _wtoi(m[1].str().c_str());
    }
    return fallback;
}

static int FindFreePort(int start) {
    for (int p = start; p < start + 200 && p < 65535; ++p) {
        DWORD owner = 0;
        if (!QueryPortOwner(p, owner)) return p;
    }
    return start;
}

static std::wstring FindMsBuild() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\BuildTools\\MSBuild\\Current\\Bin\\MSBuild.exe",
    };
    for (auto path : candidates) if (FileExists(path)) return path;
    return L"msbuild";
}

Detection Engine::DetectFolder(const std::wstring& folder) {
    Detection d;
    d.project.id = GenerateId();
    d.project.rootPath = folder;
    d.project.workingDirectory = folder;
    d.project.createdAt = NowStamp();
    std::wstring folderName = FileNameOf(folder);
    if (folderName.empty()) folderName = L"Project";

    std::wstring package = JoinPath(folder, L"package.json");
    std::wstring csproj = FindCsproj(folder);
    bool hasPackage = FileExists(package);
    bool hasWebConfig = HasFile(folder, L"web.config");
    std::wstring csprojText = csproj.empty() ? L"" : Widen(ReadTextFile(csproj));

    bool service = !csproj.empty() && LooksLikeService(folder);
    bool sdkWeb = csprojText.find(L"Microsoft.NET.Sdk.Web") != std::wstring::npos
        || csprojText.find(L"Microsoft.NET.Sdk.Razor") != std::wstring::npos
        || csprojText.find(L"<UsingMicrosoftNETSdkWeb>") != std::wstring::npos;

    if (!service && hasPackage) {
        std::string err;
        Json pkg = Json::Parse(ReadTextFile(package), &err);
        if (pkg.type == Json::Type::Object) {
            std::wstring name = Widen(pkg.str("name", Narrow(folderName)));
            if (name.empty()) name = folderName;
            d.project.name = name;
            d.project.kindText = L"Frontend";
            std::wstring deps;
            if (auto* x = pkg.obj("dependencies")) {
                for (auto& kv : x->object) deps += Widen(kv.first) + L" ";
            }
            if (auto* x = pkg.obj("devDependencies")) {
                for (auto& kv : x->object) deps += Widen(kv.first) + L" ";
            }
            std::wstring low = ToLower(deps);
            std::wstring framework = L"Node.js";
            if (low.find(L"next") != std::wstring::npos) framework = L"Next.js";
            else if (low.find(L"nuxt") != std::wstring::npos) framework = L"Nuxt";
            else if (low.find(L"@sveltejs/kit") != std::wstring::npos) framework = L"SvelteKit";
            else if (low.find(L"svelte") != std::wstring::npos) framework = L"Svelte";
            else if (low.find(L"vue") != std::wstring::npos) framework = L"Vue";
            else if (low.find(L"react") != std::wstring::npos) framework = L"React";
            else if (low.find(L"vite") != std::wstring::npos) framework = L"Vite";
            std::wstring scriptText;
            if (auto* scripts = pkg.obj("scripts")) {
                for (auto& kv : scripts->object) scriptText += Widen(kv.first) + L"=" + Widen(kv.second.text) + L"\n";
            }
            bool hasDev = scriptText.find(L"dev=") != std::wstring::npos;
            std::wstring pm = L"npm";
            if (FileExists(JoinPath(folder, L"pnpm-lock.yaml"))) pm = L"pnpm";
            else if (FileExists(JoinPath(folder, L"yarn.lock"))) pm = L"yarn";
            else if (FileExists(JoinPath(folder, L"bun.lockb")) || FileExists(JoinPath(folder, L"bun.lock"))) pm = L"bun";
            std::wstring run = (pm == L"yarn" || pm == L"bun") ? (hasDev ? L"dev" : L"start") : (hasDev ? L"run dev" : L"run start");
            d.project.startCommand = pm + L" " + run;
            d.project.buildCommandDebug = pm + L" run build";
            d.project.buildCommandRelease = pm + L" run build";
            d.project.framework = framework + L" · " + pm;
            d.project.toolchain = pm;
            d.project.projectFile = package;
            d.project.environment[L"BROWSER"] = L"none";
            int defaultPort = 3000;
            if (framework == L"Vite" || framework == L"Vue" || framework == L"Svelte" || framework == L"SvelteKit") defaultPort = 5173;
            int port = ParsePortFromText(scriptText, ParseEnvPort(folder, defaultPort));
            d.project.port = port;
            d.project.url = L"http://localhost:" + std::to_wstring(port);
            d.project.notes = L"启动脚本：" + run + L"；预期端口 " + std::to_wstring(port) + L"（实际端口以进程监听为准）。";
            d.ok = true;
            d.message = L"识别为 " + framework + L" 项目";
            return d;
        }
    }

    if (service) {
        d.project.kindText = L"WindowsService";
        d.project.name = folderName;
        d.project.serviceName = FindServiceName(folder, folderName);
        d.project.framework = L".NET Windows 服务";
        d.project.toolchain = L"Windows SCM";
        d.project.projectFile = csproj;
        d.project.notes = L"通过 Windows 服务控制管理器启动 / 停止；通常需要管理员权限。";
        if (!csproj.empty()) {
            std::wstring msbuild = FindMsBuild();
            d.project.buildCommandDebug = Quote(msbuild) + L" " + Quote(csproj) + L" /t:Build /p:Configuration=Debug /m /nologo /v:m";
            d.project.buildCommandRelease = Quote(msbuild) + L" " + Quote(csproj) + L" /t:Build /p:Configuration=Release /m /nologo /v:m";
        }
        d.ok = true;
        d.message = L"识别为 Windows 服务：" + d.project.serviceName;
        return d;
    }

    if (!csproj.empty() && (sdkWeb || !hasWebConfig)) {
        d.project.name = fs::path(csproj).stem().wstring();
        if (d.project.name.empty()) d.project.name = folderName;
        d.project.kindText = L"DotNet";
        d.project.projectFile = csproj;
        std::wsmatch m;
        std::wstring tfm;
        std::wregex tfmRe(LR"(<TargetFramework>([^<]+)</TargetFramework>)");
        if (std::regex_search(csprojText, m, tfmRe)) tfm = m[1].str();
        bool web = sdkWeb || csprojText.find(L"Microsoft.NET.Sdk.Web") != std::wstring::npos;
        d.project.framework = web ? (L"ASP.NET Core " + tfm) : (L".NET " + tfm);
        if (tfm.empty()) d.project.framework = web ? L"ASP.NET Core" : L".NET";
        d.project.toolchain = L"dotnet CLI";
        std::wstring launch = JoinPath(DirectoryOf(csproj), L"Properties\\launchSettings.json");
        int port = 5000;
        std::wstring profile;
        if (FileExists(launch)) {
            std::string err;
            Json ls = Json::Parse(ReadTextFile(launch), &err);
            if (auto* profiles = ls.obj("profiles")) {
                for (auto& kv : profiles->object) {
                    auto* po = &kv.second;
                    if (po->type != Json::Type::Object) continue;
                    std::wstring cn = Widen(po->str("commandName", ""));
                    if (profile.empty() && (cn == L"Project" || cn == L"Executable" || cn == L"IIS Express")) {
                        profile = Widen(kv.first);
                        std::wstring urls = Widen(po->str("applicationUrl", ""));
                        port = ParsePortFromText(urls, port);
                    }
                }
            }
        }
        d.project.port = port;
        d.project.url = L"http://localhost:" + std::to_wstring(port);
        d.project.startCommand = L"dotnet run --project " + Quote(csproj) + L" --configuration Debug";
        if (!profile.empty()) d.project.startCommand += L" --launch-profile " + Quote(profile);
        d.project.buildCommandDebug = L"dotnet build " + Quote(csproj) + L" --configuration Debug --nologo -v:m";
        d.project.buildCommandRelease = L"dotnet build " + Quote(csproj) + L" --configuration Release --nologo -v:m";
        d.project.environment[L"ASPNETCORE_ENVIRONMENT"] = L"Development";
        d.project.notes = L"预期端口 " + std::to_wstring(port) + L"；实际端口以进程监听为准。";
        d.ok = true;
        d.message = L"识别为 " + d.project.framework + L" 项目";
        return d;
    }

    if (!csproj.empty() && hasWebConfig) {
        d.project.name = fs::path(csproj).stem().wstring();
        if (d.project.name.empty()) d.project.name = folderName;
        d.project.kindText = L"AspNetFramework";
        d.project.projectFile = csproj;
        d.project.framework = L".NET Framework · ASP.NET 站点";
        d.project.toolchain = L"MSBuild + IIS Express";
        int port = FindFreePort(45000);
        std::wstring iis = L"C:\\Program Files\\IIS Express\\iisexpress.exe";
        if (!FileExists(iis)) iis = L"C:\\Program Files (x86)\\IIS Express\\iisexpress.exe";
        d.project.port = port;
        d.project.url = L"http://localhost:" + std::to_wstring(port);
        d.project.startCommand = Quote(iis) + L" /path:" + Quote(folder) + L" /port:" + std::to_wstring(port) + L" /systray:false /clr:v4.0";
        std::wstring msbuild = FindMsBuild();
        d.project.buildCommandDebug = Quote(msbuild) + L" " + Quote(csproj) + L" /t:Build /p:Configuration=Debug /m /nologo /v:m";
        d.project.buildCommandRelease = Quote(msbuild) + L" " + Quote(csproj) + L" /t:Build /p:Configuration=Release /m /nologo /v:m";
        d.project.notes = L"使用 IIS Express 运行，停止时会结束整个进程树。";
        d.ok = true;
        d.message = L"识别为 .NET Framework ASP.NET 站点";
        return d;
    }

    d.message = L"没有识别到 package.json、.csproj 或 web.config。请选择具体项目目录。";
    return d;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
Engine::Engine() { Load(); }

Engine::~Engine() { Shutdown(); }

std::wstring Engine::WorkspacePath() const { return JoinPath(DataDirectory(), L"workspace.json"); }

static std::wstring JsonStr(const Json& o, const char* key, const std::wstring& def = L"") {
    auto* v = o.find(key);
    if (!v || v->type != Json::Type::String) return def;
    return Widen(v->text);
}
static Json J(const std::wstring& s) { Json j; j.type = Json::Type::String; j.text = Narrow(s); return j; }
static Json JBool(bool b) { Json j; j.type = Json::Type::Bool; j.boolean = b; return j; }
static Json JNum(int n) { Json j; j.type = Json::Type::Number; j.number = n; return j; }

bool Engine::Load(std::wstring* error) {
    std::lock_guard lk(mutex_);
    projects_.clear();
    runtimes_.clear();
    loaded_ = false;
    std::wstring path = WorkspacePath();
    if (!FileExists(path)) { loaded_ = true; return true; }
    std::string text = ReadTextFile(path);
    std::string perr;
    Json root = Json::Parse(text, &perr);
    if (root.type != Json::Type::Object) {
        if (error) *error = L"workspace.json 解析失败：" + Widen(perr);
        AppLog(L"workspace.json 解析失败：" + Widen(perr));
        loaded_ = true;
        return false;
    }
    if (auto* arr = root.arr("projects")) {
        for (auto& p : arr->array) {
            if (p.type != Json::Type::Object) continue;
            Project x;
            x.id = JsonStr(p, "id", GenerateId());
            x.name = JsonStr(p, "name", L"Project");
            x.alias = JsonStr(p, "alias");
            x.rootPath = JsonStr(p, "rootPath");
            x.kindText = JsonStr(p, "kind", L"Frontend");
            x.framework = JsonStr(p, "framework");
            x.toolchain = JsonStr(p, "toolchain");
            x.projectFile = JsonStr(p, "projectFile");
            x.startCommand = JsonStr(p, "startCommand");
            x.workingDirectory = JsonStr(p, "workingDirectory", x.rootPath);
            x.url = JsonStr(p, "url");
            x.buildCommandDebug = JsonStr(p, "buildCommandDebug");
            x.buildCommandRelease = JsonStr(p, "buildCommandRelease");
            x.serviceName = JsonStr(p, "serviceName");
            x.notes = JsonStr(p, "notes");
            x.createdAt = JsonStr(p, "createdAt");
            x.port = p.integer("port", 0);
            x.autoStart = p.flag("autoStart", false);
            if (auto* env = p.obj("environment")) {
                for (auto& kv : env->object) {
                    if (kv.second.type == Json::Type::String) x.environment[Widen(kv.first)] = Widen(kv.second.text);
                }
            }
            if (!x.id.empty()) {
                projects_.push_back(x);
                auto rt = std::make_shared<Runtime>();
                rt->serviceInstalled = true;
                runtimes_[x.id] = rt;
            }
        }
    }
    loaded_ = true;
    return true;
}

bool Engine::Save(std::wstring* error) {
    std::lock_guard lk(mutex_);
    Json root; root.type = Json::Type::Object;
    root.object.emplace_back("version", JNum(1));
    Json arr; arr.type = Json::Type::Array;
    for (auto& p : projects_) {
        Json o; o.type = Json::Type::Object;
        o.object.emplace_back("id", J(p.id));
        o.object.emplace_back("name", J(p.name));
        o.object.emplace_back("alias", J(p.alias));
        o.object.emplace_back("rootPath", J(p.rootPath));
        o.object.emplace_back("kind", J(p.kindText));
        o.object.emplace_back("framework", J(p.framework));
        o.object.emplace_back("toolchain", J(p.toolchain));
        o.object.emplace_back("projectFile", J(p.projectFile));
        o.object.emplace_back("startCommand", J(p.startCommand));
        o.object.emplace_back("workingDirectory", J(p.workingDirectory));
        o.object.emplace_back("port", JNum(p.port));
        o.object.emplace_back("url", J(p.url));
        o.object.emplace_back("buildCommandDebug", J(p.buildCommandDebug));
        o.object.emplace_back("buildCommandRelease", J(p.buildCommandRelease));
        o.object.emplace_back("serviceName", J(p.serviceName));
        o.object.emplace_back("autoStart", JBool(p.autoStart));
        o.object.emplace_back("notes", J(p.notes));
        Json env; env.type = Json::Type::Object;
        for (auto& kv : p.environment) env.object.emplace_back(Narrow(kv.first), J(kv.second));
        o.object.emplace_back("environment", std::move(env));
        o.object.emplace_back("createdAt", J(p.createdAt));
        arr.array.push_back(std::move(o));
    }
    root.object.emplace_back("projects", std::move(arr));
    Json opts; opts.type = Json::Type::Object;
    opts.object.emplace_back("theme", J(L"System"));
    opts.object.emplace_back("showFlyoutOnHover", JBool(true));
    opts.object.emplace_back("startWithWindows", JBool(false));
    opts.object.emplace_back("closeToTray", JBool(true));
    opts.object.emplace_back("autoStartProjects", JBool(true));
    opts.object.emplace_back("refreshIntervalSeconds", JNum(2));
    root.object.emplace_back("options", std::move(opts));

    std::wstring path = WorkspacePath();
    if (FileExists(path)) CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
    std::wstring temp = path + L".tmp";
    if (!WriteTextFile(temp, root.dump(2))) {
        if (error) *error = L"写入 workspace.json 失败";
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = Format(L"替换 workspace.json 失败（%lu）", GetLastError());
        return false;
    }
    return true;
}

std::vector<Project> Engine::Projects() const {
    std::lock_guard lk(mutex_);
    return projects_;
}

const Project* Engine::FindProject(const std::wstring& id) const {
    for (auto& p : projects_) if (p.id == id) return &p;
    return nullptr;
}
Project* Engine::FindProject(const std::wstring& id) {
    for (auto& p : projects_) if (p.id == id) return &p;
    return nullptr;
}

std::shared_ptr<Engine::Runtime> Engine::RuntimeFor(const std::wstring& id) {
    auto it = runtimes_.find(id);
    if (it != runtimes_.end()) return it->second;
    auto rt = std::make_shared<Runtime>();
    runtimes_[id] = rt;
    return rt;
}

std::wstring Engine::LogFileFor(const std::wstring& id) const {
    std::lock_guard lk(mutex_);
    const Project* p = FindProject(id);
    std::wstring name = p ? p->DisplayName() : id;
    for (auto& c : name) if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
    std::wstring dir = JoinPath(DataDirectory(), L"logs");
    std::error_code ec; fs::create_directories(fs::path(dir), ec);
    return JoinPath(dir, name + L".log");
}

bool Engine::AddProject(const Project& project, std::wstring* error) {
    {
        std::lock_guard lk(mutex_);
        for (auto& p : projects_) {
            if (_wcsicmp(p.rootPath.c_str(), project.rootPath.c_str()) == 0) {
                if (error) *error = L"这个目录已经添加过了";
                return false;
            }
        }
        projects_.push_back(project);
        auto rt = std::make_shared<Runtime>();
        runtimes_[project.id] = rt;
    }
    return Save(error);
}

bool Engine::RemoveProject(const std::wstring& id, std::wstring* error) {
    {
        std::lock_guard lk(mutex_);
        auto it = std::find_if(projects_.begin(), projects_.end(), [&](const Project& p) { return p.id == id; });
        if (it == projects_.end()) return false;
        projects_.erase(it);
        runtimes_.erase(id);
    }
    return Save(error);
}

bool Engine::UpdateProject(const Project& project, std::wstring* error) {
    {
        std::lock_guard lk(mutex_);
        auto it = std::find_if(projects_.begin(), projects_.end(), [&](const Project& p) { return p.id == project.id; });
        if (it == projects_.end()) {
            if (error) *error = L"项目不存在";
            return false;
        }
        *it = project;
    }
    return Save(error);
}

Snapshot Engine::Get(const std::wstring& id) const {
    std::lock_guard lk(mutex_);
    Snapshot s;
    const Project* p = FindProject(id);
    if (!p) return s;
    s.project = *p;
    auto it = runtimes_.find(id);
    if (it != runtimes_.end()) {
        auto& rt = *it->second;
        std::lock_guard rl(rt.mutex);
        s.status = rt.status;
        s.lastError = rt.lastError;
        s.ports = rt.ports;
        s.pid = rt.pid;
        s.startedAt = rt.startedAt;
        s.exitCode = rt.exitCode;
        s.serviceInstalled = rt.serviceInstalled;
        s.serviceState = rt.serviceState;
        s.portConflict = rt.portConflict;
        s.conflictPort = rt.conflictPort;
        if (rt.status == Status::Running && s.startedAt == 0) s.startedAt = 0;
    }
    return s;
}

std::vector<Snapshot> Engine::Snapshots() const {
    std::vector<Snapshot> out;
    std::lock_guard lk(mutex_);
    for (auto& p : projects_) {
        Snapshot s; s.project = p;
        auto it = runtimes_.find(p.id);
        if (it != runtimes_.end()) {
            auto& rt = *it->second;
            std::lock_guard rl(rt.mutex);
            s.status = rt.status;
            s.lastError = rt.lastError;
            s.ports = rt.ports;
            s.pid = rt.pid;
            s.startedAt = rt.startedAt;
            s.exitCode = rt.exitCode;
            s.serviceInstalled = rt.serviceInstalled;
            s.serviceState = rt.serviceState;
            s.portConflict = rt.portConflict;
            s.conflictPort = rt.conflictPort;
        }
        out.push_back(std::move(s));
    }
    return out;
}

bool Engine::CheckPortConflict(const std::wstring& id, PortOwner& owner) const {
    owner = {};
    std::shared_ptr<Runtime> rt;
    int port = 0;
    {
        std::lock_guard lk(mutex_);
        const Project* p = FindProject(id);
        if (!p || p->port <= 0) return false;
        port = p->port;
        auto it = runtimes_.find(id);
        if (it != runtimes_.end()) rt = it->second;
    }
    DWORD pid = 0;
    if (!QueryPortOwner(port, pid)) return false;
    owner.port = port; owner.pid = pid; owner.found = true;
    if (rt) {
        std::lock_guard rl(rt->mutex);
        auto tree = ProcessTree(rt->pid);
        owner.isOurs = std::find(tree.begin(), tree.end(), pid) != tree.end();
    }
    return !owner.isOurs;
}

bool Engine::KillProcessTree(DWORD pid) {
    auto tree = ProcessTree(pid);
    std::reverse(tree.begin(), tree.end());
    bool ok = true;
    for (DWORD p : tree) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p);
        if (h) {
            if (!TerminateProcess(h, 1)) ok = false;
            CloseHandle(h);
        }
    }
    return ok;
}

void Engine::AppendLog(const std::wstring& id, const std::wstring& line) {
    // Called by worker threads; only uses the log path snapshot.
    std::wstring path;
    {
        std::lock_guard lk(mutex_);
        auto it = runtimes_.find(id);
        if (it != runtimes_.end()) path = it->second->logPath;
    }
    if (path.empty()) path = LogFileFor(id);
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring text = Format(L"%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond) + line + L"\r\n";
    std::string utf8 = Narrow(text);
    DWORD written = 0;
    SetFilePointer(h, 0, nullptr, FILE_END);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
}

static void AppendLogRaw(const std::wstring& path, const std::wstring& line) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring text = Format(L"%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond) + line + L"\r\n";
    std::string utf8 = Narrow(text);
    DWORD written = 0;
    SetFilePointer(h, 0, nullptr, FILE_END);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
}

static void ReaderThread(std::shared_ptr<Engine::Runtime> rt, HANDLE pipe) {
    std::string pending;
    char buf[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buf, sizeof(buf), &read, nullptr) || read == 0) break;
        pending.append(buf, read);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::wstring w = Widen(line);
            {
                std::lock_guard lk(rt->mutex);
                if (!rt->logPath.empty()) AppendLogRaw(rt->logPath, w);
                std::wstring low = ToLower(w);
                if (low.find(L"eaddrinuse") != std::wstring::npos || low.find(L"address already in use") != std::wstring::npos) {
                    rt->portConflict = true;
                    std::wregex conflictRe(LR"(:(\d{2,5}))");
                    for (std::wsregex_iterator it(w.begin(), w.end(), conflictRe), end; it != end; ++it) {
                        int p = _wtoi((*it)[1].str().c_str());
                        if (p >= 1024 && p <= 65535) rt->conflictPort = p;
                    }
                }
                std::wregex re(LR"((?:https?://(?:localhost|127\.0\.0\.1|0\.0\.0\.0|\[[^\]]+\]):)?(\d{2,5}))", std::regex::icase);
                if (low.find(L"http") != std::wstring::npos || low.find(L"listening") != std::wstring::npos || low.find(L"localhost") != std::wstring::npos || low.find(L"port") != std::wstring::npos) {
                    for (std::wsregex_iterator it(w.begin(), w.end(), re), end; it != end; ++it) {
                        int p = _wtoi((*it)[1].str().c_str());
                        if (p >= 1024 && p <= 65535 && std::find(rt->ports.begin(), rt->ports.end(), p) == rt->ports.end())
                            rt->ports.push_back(p);
                    }
                }
            }
        }
    }
    if (!pending.empty()) {
        std::lock_guard lk(rt->mutex);
        if (!rt->logPath.empty()) AppendLogRaw(rt->logPath, Widen(pending));
    }
    CloseHandle(pipe);
}

void Engine::StartInternal(const std::wstring& id) {
    std::shared_ptr<Runtime> rt;
    Project project;
    std::wstring logPath = LogFileFor(id);
    {
        std::lock_guard lk(mutex_);
        const Project* p = FindProject(id);
        if (!p) return;
        project = *p;
        rt = RuntimeFor(id);
    }
    std::lock_guard rl(rt->mutex);
    if (rt->status == Status::Running || rt->status == Status::Starting || rt->status == Status::Stopping) return;
    rt->lastError.clear();
    rt->exitCode = 0;
    rt->ports.clear();
    rt->portConflict = false;
    rt->conflictPort = 0;
    rt->userStopped = false;
    rt->restartPending = false;
    rt->startedAt = GetTickCount64();
    rt->logPath = logPath;

    if (project.kind() == Kind::WindowsService) {
        std::wstring err;
        if (StartServiceByName(project.serviceName, err)) {
            rt->status = Status::Starting;
            rt->serviceState = L"正在启动";
            rt->startedAt = GetTickCount64();
            AppendLogRaw(rt->logPath, L"启动服务：" + project.serviceName);
        } else {
            rt->status = Status::Failed;
            rt->lastError = err;
            rt->startedAt = 0;
            AppendLogRaw(rt->logPath, L"启动服务失败：" + err);
        }
        return;
    }

    if (project.startCommand.empty()) {
        rt->status = Status::Failed;
        rt->lastError = L"没有启动命令，请删除后重新添加项目";
        rt->startedAt = 0;
        return;
    }

    AppendLogRaw(rt->logPath, L"===== 启动 " + project.name + L" =====");
    AppendLogRaw(rt->logPath, L"命令：" + project.startCommand);

    std::wstring wd = project.workingDirectory.empty() ? project.rootPath : project.workingDirectory;
    std::wstring inner = L"chcp 65001>nul & cd /d " + Quote(wd) + L" & ";
    for (auto& kv : project.environment) {
        if (kv.first == L"BROWSER") continue;
        inner += L"set \"" + kv.first + L"=" + kv.second + L"\" & ";
    }
    inner += project.startCommand;
    std::wstring cmdline = L"cmd.exe /d /s /c \"" + inner + L"\"";

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        if (outRead) CloseHandle(outRead); if (outWrite) CloseHandle(outWrite);
        if (errRead) CloseHandle(errRead); if (errWrite) CloseHandle(errWrite);
        rt->status = Status::Failed;
        rt->lastError = L"创建进程管道失败";
        rt->startedAt = 0;
        return;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    si.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back(L'\0');
    BOOL created = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
        wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    CloseHandle(outWrite); CloseHandle(errWrite);
    if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);
    if (!created) {
        DWORD e = GetLastError();
        CloseHandle(outRead); CloseHandle(errRead);
        rt->status = Status::Failed;
        rt->lastError = Format(L"启动进程失败（%lu）", e);
        rt->startedAt = 0;
        AppendLogRaw(rt->logPath, L"启动进程失败：" + Format(L"%lu", e));
        return;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    rt->process = pi.hProcess;
    rt->job = job;
    rt->pid = pi.dwProcessId;
    rt->status = Status::Starting;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    std::thread(ReaderThread, rt, outRead).detach();
    std::thread(ReaderThread, rt, errRead).detach();
}

void Engine::Start(const std::wstring& id) {
    StartInternal(id);
    AppLog(L"启动项目：" + id);
}

void Engine::StopInternal(const std::wstring& id) {
    std::shared_ptr<Runtime> rt;
    Project project;
    {
        std::lock_guard lk(mutex_);
        const Project* p = FindProject(id);
        if (!p) return;
        project = *p;
        auto it = runtimes_.find(id);
        if (it == runtimes_.end()) return;
        rt = it->second;
    }
    std::lock_guard rl(rt->mutex);
    if (rt->status == Status::Stopped || rt->status == Status::Failed) {
        rt->status = Status::Stopped;
        rt->startedAt = 0;
        return;
    }
    rt->userStopped = true;
    rt->status = Status::Stopping;
    if (project.kind() == Kind::WindowsService) {
        std::wstring err;
        if (!StopServiceByName(project.serviceName, err)) {
            rt->lastError = err;
            AppendLogRaw(rt->logPath, L"停止服务失败：" + err);
        } else {
            AppendLogRaw(rt->logPath, L"正在停止服务：" + project.serviceName);
        }
        return;
    }
    AppendLogRaw(rt->logPath, L"停止项目");
    if (rt->job) {
        TerminateJobObject(rt->job, 1);
    } else if (rt->process) {
        TerminateProcess(rt->process, 1);
    }
    if (!rt->process) {
        rt->status = Status::Stopped;
        rt->startedAt = 0;
    }
}

void Engine::Stop(const std::wstring& id) { StopInternal(id); }

void Engine::Restart(const std::wstring& id) {
    std::shared_ptr<Runtime> rt;
    {
        std::lock_guard lk(mutex_);
        auto it = runtimes_.find(id);
        if (it == runtimes_.end()) return;
        rt = it->second;
    }
    Status status = Status::Stopped;
    {
        std::lock_guard rl(rt->mutex);
        status = rt->status;
        if (status != Status::Stopped && status != Status::Failed)
            rt->restartPending = true;
    }
    if (status == Status::Stopped || status == Status::Failed)
        StartInternal(id);
    else
        StopInternal(id);
}

void Engine::StartAll() {
    std::vector<std::wstring> ids;
    {
        std::lock_guard lk(mutex_);
        for (auto& p : projects_) ids.push_back(p.id);
    }
    for (auto& id : ids) StartInternal(id);
}

void Engine::StopAll() {
    std::vector<std::wstring> ids;
    {
        std::lock_guard lk(mutex_);
        for (auto& p : projects_) ids.push_back(p.id);
    }
    for (auto& id : ids) StopInternal(id);
}

void Engine::RestartAll() {
    std::vector<std::wstring> ids;
    {
        std::lock_guard lk(mutex_);
        for (auto& p : projects_) ids.push_back(p.id);
    }
    for (auto& id : ids) {
        Snapshot s;
        {
            std::lock_guard lk(mutex_);
            auto it = runtimes_.find(id);
            if (it != runtimes_.end()) {
                std::lock_guard rl(it->second->mutex);
                s.status = it->second->status;
            }
        }
        if (s.status == Status::Running || s.status == Status::Starting || s.status == Status::Failed)
            Restart(id);
    }
}

static void ClearLogFile(const std::wstring& path);

void Engine::RefreshService(Runtime& rt, const Project& project) {
    if (project.serviceName.empty()) {
        rt.serviceInstalled = false;
        rt.serviceState = L"未配置服务名";
        return;
    }
    bool installed = false; DWORD state = 0; std::wstring error;
    QueryServiceInfo(project.serviceName, installed, state, error);
    rt.serviceInstalled = installed;
    rt.serviceState = installed ? ServiceStateText(state) : L"未安装";
    if (!installed) {
        if (rt.status != Status::Failed) {
            rt.status = Status::Failed;
            rt.lastError = L"服务未安装：" + project.serviceName;
        }
        return;
    }
    if (state == SERVICE_RUNNING) {
        if (rt.status != Status::Running) rt.startedAt = rt.startedAt ? rt.startedAt : GetTickCount64();
        rt.status = Status::Running;
    } else if (state == SERVICE_START_PENDING || state == SERVICE_CONTINUE_PENDING) {
        rt.status = Status::Starting;
    } else if (state == SERVICE_STOP_PENDING || state == SERVICE_PAUSE_PENDING) {
        rt.status = Status::Stopping;
    } else if (state == SERVICE_STOPPED) {
        if (rt.status == Status::Stopping || rt.userStopped) rt.status = Status::Stopped;
        else if (rt.status == Status::Running || rt.status == Status::Starting) rt.status = Status::Stopped;
        rt.startedAt = 0;
        if (rt.userStopped) {
            ClearLogFile(rt.logPath);
            rt.userStopped = false;
        }
    }
}

void Engine::RefreshPorts(Runtime& rt, const Project& project, ULONGLONG now) {
    if (!rt.pid) return;
    auto tree = ProcessTree(rt.pid);
    std::set<DWORD> pids(tree.begin(), tree.end());
    std::vector<int> ports;
    for (auto& b : cachedListeners_) {
        if (pids.count(b.pid)) {
            if (std::find(ports.begin(), ports.end(), b.port) == ports.end()) ports.push_back(b.port);
        }
    }
    if (project.port > 0 && std::find(ports.begin(), ports.end(), project.port) == ports.end()) {
        for (auto& b : cachedListeners_) if (b.port == project.port && pids.count(b.pid)) ports.push_back(b.port);
    }
    std::sort(ports.begin(), ports.end());
    rt.ports = ports;
    if (!ports.empty() && rt.status == Status::Starting) rt.status = Status::Running;
    if (rt.status == Status::Starting && now - rt.startedAt > 3500) rt.status = Status::Running;
}

static void ClearLogFile(const std::wstring& path) {
    if (path.empty()) return;
    DeleteFileW(path.c_str());
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void Engine::PollRuntime(const std::wstring& id, const Project& project, std::vector<std::wstring>& restart) {
    std::shared_ptr<Runtime> rt;
    {
        std::lock_guard lk(mutex_);
        auto it = runtimes_.find(id);
        if (it == runtimes_.end()) return;
        rt = it->second;
    }
    std::lock_guard rl(rt->mutex);
    if (project.kind() == Kind::WindowsService) {
        RefreshService(*rt, project);
        return;
    }
    if (rt->process) {
        if (WaitForSingleObject(rt->process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(rt->process, &code);
            rt->exitCode = (int)code;
            CloseHandle(rt->process);
            rt->process = nullptr;
            if (rt->job) { CloseHandle(rt->job); rt->job = nullptr; }
            rt->pid = 0;
            rt->ports.clear();
            bool clearLog = rt->userStopped;
            if (rt->status != Status::Stopping) {
                rt->status = (code == 0 && !rt->portConflict) ? Status::Stopped : Status::Failed;
                if (rt->portConflict) rt->lastError = L"端口被占用，启动已退出";
                else if (code != 0) rt->lastError = Format(L"进程退出码 %lu", code);
            } else {
                rt->status = Status::Stopped;
            }
            if (rt->userStopped) rt->status = Status::Stopped;
            rt->startedAt = 0;
            AppendLogRaw(rt->logPath, Format(L"进程已退出，退出码 %lu", code));
            if (clearLog) {
                ClearLogFile(rt->logPath);
                rt->userStopped = false;
            }
            if (rt->restartPending) {
                rt->restartPending = false;
                restart.push_back(id);
            }
            return;
        }
        RefreshPorts(*rt, project, GetTickCount64());
    }
}

void Engine::Poll() {
    std::vector<std::wstring> restart;
    std::vector<std::pair<std::wstring, Project>> items;
    {
        std::lock_guard lk(mutex_);
        ULONGLONG now = GetTickCount64();
        if (now - lastScan_ > 1800) {
            cachedListeners_ = ScanListeners();
            lastScan_ = now;
        }
        for (auto& p : projects_) items.emplace_back(p.id, p);
    }
    for (auto& item : items) PollRuntime(item.first, item.second, restart);
    for (auto& id : restart) StartInternal(id);
}

void Engine::Shutdown() {
    std::vector<std::wstring> ids;
    {
        std::lock_guard lk(mutex_);
        for (auto& p : projects_) ids.push_back(p.id);
    }
    for (auto& id : ids) StopInternal(id);
    for (int i = 0; i < 40; ++i) {
        bool active = false;
        {
            std::lock_guard lk(mutex_);
            for (auto& kv : runtimes_) {
                std::lock_guard rl(kv.second->mutex);
                if (kv.second->process) { active = true; break; }
            }
        }
        if (!active) break;
        Sleep(50);
    }
    std::lock_guard lk(mutex_);
    for (auto& kv : runtimes_) {
        std::lock_guard rl(kv.second->mutex);
        if (kv.second->process) { TerminateProcess(kv.second->process, 1); CloseHandle(kv.second->process); kv.second->process = nullptr; }
        if (kv.second->job) { CloseHandle(kv.second->job); kv.second->job = nullptr; }
    }
}

} // namespace dsm
