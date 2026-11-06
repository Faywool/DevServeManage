#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

namespace dsm {

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);
std::wstring Trim(std::wstring s);
std::wstring ToLower(std::wstring s);
std::wstring Format(const wchar_t* fmt, ...);
std::wstring DataDirectory();
std::wstring AppLogPath();
void AppLog(const std::wstring& text);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
std::string ReadTextFile(const std::wstring& path);
bool WriteTextFile(const std::wstring& path, const std::string& text);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);
std::wstring Quote(const std::wstring& s);
std::wstring NowStamp();
std::wstring UptimeText(ULONGLONG startedAt);

enum class Status { Stopped, Starting, Running, Stopping, Failed };
enum class Kind { Frontend, DotNet, AspNetFramework, WindowsService };

const wchar_t* KindText(Kind k);
Kind KindFromText(const std::wstring& text);
const wchar_t* StatusText(Status s);

struct Project {
    std::wstring id;
    std::wstring name;
    std::wstring alias;
    std::wstring rootPath;
    std::wstring kindText = L"Frontend";
    std::wstring framework;
    std::wstring toolchain;
    std::wstring projectFile;
    std::wstring startCommand;
    std::wstring workingDirectory;
    std::wstring url;
    std::wstring buildCommandDebug;
    std::wstring buildCommandRelease;
    std::wstring serviceName;
    std::wstring notes;
    std::wstring createdAt;
    int port = 0;
    bool autoStart = false;
    std::map<std::wstring, std::wstring> environment;

    std::wstring DisplayName() const { return alias.empty() ? name : alias; }
    Kind kind() const { return KindFromText(kindText); }
};

struct Snapshot {
    Project project;
    Status status = Status::Stopped;
    bool serviceInstalled = true;
    std::wstring serviceState;
    std::wstring lastError;
    std::vector<int> ports;
    DWORD pid = 0;
    ULONGLONG startedAt = 0;
    int exitCode = 0;
    bool portConflict = false;
    int conflictPort = 0;
    int runningCount = 0;
    int totalCount = 0;
};

struct Detection {
    bool ok = false;
    Project project;
    std::wstring message;
};

struct PortOwner {
    int port = 0;
    DWORD pid = 0;
    bool found = false;
    bool isOurs = false;
};

struct PortBindingInfo {
    int port = 0;
    DWORD pid = 0;
    std::wstring address;
};

class Engine {
public:
    Engine();
    ~Engine();

    bool Load(std::wstring* error = nullptr);
    bool Save(std::wstring* error = nullptr);

    std::vector<Project> Projects() const;
    Snapshot Get(const std::wstring& id) const;
    std::vector<Snapshot> Snapshots() const;

    bool CheckPortConflict(const std::wstring& id, PortOwner& owner) const;
    bool KillProcessTree(DWORD pid);

    void Start(const std::wstring& id);
    void Stop(const std::wstring& id);
    void Restart(const std::wstring& id);
    void StartAll();
    void StopAll();
    void RestartAll();
    void Poll();
    void Shutdown();

    std::wstring LogFileFor(const std::wstring& id) const;
    bool RemoveProject(const std::wstring& id, std::wstring* error = nullptr);
    bool AddProject(const Project& project, std::wstring* error = nullptr);
    bool UpdateProject(const Project& project, std::wstring* error = nullptr);

    static Detection DetectFolder(const std::wstring& folder);

    struct Runtime {
        mutable std::mutex mutex;
        Status status = Status::Stopped;
        HANDLE process = nullptr;
        HANDLE job = nullptr;
        DWORD pid = 0;
        ULONGLONG startedAt = 0;
        int exitCode = 0;
        std::wstring lastError;
        std::wstring serviceState;
        bool serviceInstalled = true;
        bool userStopped = false;
        bool portConflict = false;
        int conflictPort = 0;
        bool restartPending = false;
        std::vector<int> ports;
        std::wstring logPath;
    };

private:
    void StartInternal(const std::wstring& id);
    void StopInternal(const std::wstring& id);
    void PollRuntime(const std::wstring& id, const Project& project, std::vector<std::wstring>& restart);
    void RefreshService(Runtime& rt, const Project& project);
    void RefreshPorts(Runtime& rt, const Project& project, ULONGLONG now);
    void AppendLog(const std::wstring& id, const std::wstring& line);
    std::shared_ptr<Runtime> RuntimeFor(const std::wstring& id);
    const Project* FindProject(const std::wstring& id) const;
    Project* FindProject(const std::wstring& id);
    std::wstring WorkspacePath() const;

    mutable std::mutex mutex_;
    std::vector<Project> projects_;
    std::map<std::wstring, std::shared_ptr<Runtime>> runtimes_;
    ULONGLONG lastScan_ = 0;
    std::vector<PortBindingInfo> cachedListeners_;
    bool loaded_ = false;
};

std::vector<PortBindingInfo> ScanListeners();
std::vector<DWORD> ProcessTree(DWORD root);
bool QueryPortOwner(int port, DWORD& pid);
std::wstring ServiceStateText(DWORD state);
bool QueryServiceInfo(const std::wstring& name, bool& installed, DWORD& state, std::wstring& error);
bool StartServiceByName(const std::wstring& name, std::wstring& error);
bool StopServiceByName(const std::wstring& name, std::wstring& error);

} // namespace dsm
