#pragma once

#include "file_utils.h"
#include <filesystem>
#include <string>
#include <vector>

struct ExportDocument {
    std::filesystem::path root;
    bool complete = true;
    SortOptions sort;
    FilterOptions filter;
    std::vector<EntryInfo> entries;
    std::vector<ScanIssue> diagnostics;
};

std::string serialize_export_json(const ExportDocument& document);
std::string serialize_export_csv(const ExportDocument& document);
int export_exit_code(ScanStatus status);
bool write_export_file_atomic(const std::string& contents,
                              const std::filesystem::path& target,
                              std::string& error);
