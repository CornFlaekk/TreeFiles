#pragma once
#include <filesystem>
#include <string>
#include <vector>

class ConsoleEncoding {
public:
    ConsoleEncoding();
    ~ConsoleEncoding();
    ConsoleEncoding(const ConsoleEncoding&) = delete;
    ConsoleEncoding& operator=(const ConsoleEncoding&) = delete;
private:
    unsigned int input_code_page = 0;
    unsigned int output_code_page = 0;
};

std::vector<std::string> command_line_arguments(int argc, char* argv[]);
std::filesystem::path home_directory();
std::filesystem::path configuration_file();
bool replace_file(const std::filesystem::path& source, const std::filesystem::path& target, std::string& error);
bool open_path(const std::filesystem::path& path, std::string& error);
