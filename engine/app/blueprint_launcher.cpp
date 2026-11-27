#include "app/blueprint_launcher.h"
#include "base/log.h"

#include <filesystem>
#include <system_error>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
#include <vector>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace
{
    namespace fs = std::filesystem;
#if !defined(_WIN32)
    pid_t opener = 0;
#endif

    fs::path studioPath()
    {
        fs::path executable;
        std::error_code error;
#if defined(_WIN32)
        wchar_t buffer[32768];
        const DWORD size = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
        if (size == 0 || size >= sizeof(buffer) / sizeof(buffer[0])) return {};
        executable = fs::path(buffer);
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (!size) return {};
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
        executable = fs::canonical(buffer.data(), error);
#else
        executable = fs::read_symlink("/proc/self/exe", error);
#endif
        if (error || executable.empty()) return {};
#if defined(__APPLE__)
        return executable.parent_path().parent_path() / "Resources" / "NexgenBlueprintStudio.html";
#else
        return executable.parent_path() / "NexgenBlueprintStudio.html";
#endif
    }

    bool valid(const fs::path& file)
    {
        std::error_code error;
        return !file.empty() && fs::is_regular_file(file, error) && !error &&
            fs::file_size(file, error) > 0 && !error;
    }
}

bool blueprintStudioAvailable()
{
    return valid(studioPath());
}

void pollBlueprintLauncher()
{
#if !defined(_WIN32)
    if (!opener) return;
    int status = 0;
    const pid_t result = waitpid(opener, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return;
    opener = 0;
    if (result < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        NX_ERROR("Blueprint Studio: the desktop file opener failed.");
#endif
}

bool openBlueprintStudio()
{
    const fs::path file = studioPath();
    if (!valid(file))
    {
        NX_ERROR("Blueprint Studio: NexgenBlueprintStudio.html is missing from the installation.");
        return false;
    }
#if defined(_WIN32)
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    {
        NX_ERROR("Blueprint Studio: Windows could not open the local HTML file.");
        return false;
    }
#else
    pollBlueprintLauncher();
    if (opener) return true;
#if defined(__APPLE__)
    char program[] = "/usr/bin/open";
#else
    char program[] = "xdg-open";
#endif
    // Separate argv entries: no shell interpretation of paths, including spaces and metacharacters.
    auto name = file.string();
    char* args[] = { program, name.data(), nullptr };
    if (posix_spawnp(&opener, program, nullptr, nullptr, args, environ) != 0)
    {
        opener = 0;
        NX_ERROR("Blueprint Studio: no desktop file opener is available.");
        return false;
    }
#endif
    return true;
}
