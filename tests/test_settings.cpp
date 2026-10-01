#include "settings.h"
#include "platform_utils.h"
#include "test_directory.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;
static int failed = 0;
static void check(bool condition, const char* name) {
    std::printf("  %s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failed;
}

int main() {
    const TestDirectory fixture("settings");
    const auto config = fixture.path / fs::u8path("colores \u00f1") / "config.ini";
    std::string error;
    auto colors = load_color_settings(config, error);
    check(colors.foreground == 0 && colors.background == 3 && error.empty(), "missing config uses defaults");
    check(!fs::exists(config), "reading defaults does not create a config");
    check(save_color_settings(config, {6, 4}, error), "save into a new Unicode directory");
    colors = load_color_settings(config, error);
    check(colors.foreground == 6 && colors.background == 4, "colors survive a reload");
    check(save_color_settings(config, {7, 1}, error), "replace an existing config");
    colors = load_color_settings(config, error);
    check(colors.foreground == 7 && colors.background == 1, "replacement survives a reload");
    check(!save_color_settings(config, {-1, 3}, error), "invalid colors cannot overwrite config");
    colors = load_color_settings(config, error);
    check(colors.foreground == 7 && colors.background == 1, "rejected save preserves existing colors");

    {
        std::ofstream file(config);
        file << "# settings\nforeground = green\r\nbackground=invalid\nunknown=red\nbroken line\n";
    }
    colors = load_color_settings(config, error);
    check(colors.foreground == 2 && colors.background == 3, "invalid fields fall back independently");
    {
        std::ofstream file(config);
        file << "\xef\xbb\xbf" "foreground=blue\nbackground=white\n";
    }
    colors = load_color_settings(config, error);
    check(colors.foreground == 4 && colors.background == 7, "UTF-8 BOM from a Windows editor is supported");
    const auto directory_target = fixture.path / "directory";
    fs::create_directories(directory_target);
    std::ofstream(directory_target / "keep.txt") << "keep";
    check(!save_color_settings(directory_target, {0, 3}, error), "failed replacement reports an error");
    check(!error.empty() && fs::exists(directory_target / "keep.txt"), "failed replacement preserves the target");
    colors = load_color_settings(directory_target, error);
    check(!error.empty() && colors.foreground == 0 && colors.background == 3, "unreadable config reports error and uses defaults");
    bool temporary_left = false;
    for (const auto& entry : fs::recursive_directory_iterator(fixture.path))
        if (entry.path().filename().u8string().find(".tmp.") != std::string::npos) temporary_left = true;
    check(!temporary_left, "save failures leave no temporary files");

#ifdef _WIN32
    const wchar_t* previous = _wgetenv(L"TREEFILES_CONFIG");
    const std::optional<std::wstring> old = previous ? std::optional<std::wstring>(previous) : std::nullopt;
    _wputenv_s(L"TREEFILES_CONFIG", config.c_str());
    check(configuration_file() == config, "Windows config override supports Unicode");
    _wputenv_s(L"TREEFILES_CONFIG", old ? old->c_str() : L"");
#else
    const char* previous = std::getenv("TREEFILES_CONFIG");
    const std::optional<std::string> old = previous ? std::optional<std::string>(previous) : std::nullopt;
    setenv("TREEFILES_CONFIG", config.c_str(), 1);
    check(configuration_file() == config, "Linux config override supports Unicode");
    if (old) setenv("TREEFILES_CONFIG", old->c_str(), 1);
    else unsetenv("TREEFILES_CONFIG");
#endif
    return failed ? 1 : 0;
}
