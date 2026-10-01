#include "platform_utils.h"
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
#endif

ConsoleEncoding::ConsoleEncoding() {
#ifdef _WIN32
    input_code_page = GetConsoleCP();
    output_code_page = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

ConsoleEncoding::~ConsoleEncoding() {
#ifdef _WIN32
    if (input_code_page) SetConsoleCP(input_code_page);
    if (output_code_page) SetConsoleOutputCP(output_code_page);
#endif
}

std::vector<std::string> command_line_arguments(int argc, char* argv[]) {
    std::vector<std::string> arguments;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    auto wide_arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide_arguments) throw std::runtime_error("Cannot read command-line arguments.");
    for (int i = 1; i < count; ++i)
        arguments.push_back(std::filesystem::path(wide_arguments[i]).u8string());
    LocalFree(wide_arguments);
#else
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
#endif
    return arguments;
}

std::filesystem::path home_directory() {
#ifdef _WIN32
    const wchar_t* home = _wgetenv(L"USERPROFILE");
    return home ? std::filesystem::path(home) : std::filesystem::path();
#else
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) : std::filesystem::path();
#endif
}

bool open_path(const std::filesystem::path& path, std::string& error) {
    auto absolute_path = std::filesystem::absolute(path);
#ifdef _WIN32
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = absolute_path.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        error = "Cannot open this path (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
#else
    auto filename = absolute_path.string();
    char opener[] = "xdg-open";
    char* arguments[] = {opener, filename.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    int result = posix_spawnp(&child, opener, &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        error = "Cannot run xdg-open: " + std::string(std::strerror(result));
        return false;
    }
    std::thread([child]() {
        while (waitpid(child, nullptr, 0) == -1 && errno == EINTR) {}
    }).detach();
#endif
    return true;
}
