#pragma once
#include <chrono>
#include <filesystem>
#include <string>

struct TestDirectory {
    std::filesystem::path path;

    explicit TestDirectory(const std::string& component) {
        auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("treefiles_test_" + component + "_" + std::to_string(id));
    }

    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
