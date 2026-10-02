#include "export_utils.h"
#include "test_directory.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

namespace fs = std::filesystem;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL at line " << __LINE__ << ": " << #condition << '\n'; \
        std::exit(1); \
    } \
} while (false)

static const TestDirectory test_directory("export_utils");

static void write_text(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    CHECK(output);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    CHECK(output.good());
}

static std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static ExportDocument special_document() {
    ExportDocument document;
    document.root = fs::u8path("root,\"quoted\"\n雪");
    document.complete = false;
    document.sort = {SortKey::mtime, SortOrder::asc};
    document.filter = {std::string("bad\xff", 4), ".txt"};

    EntryInfo file;
    file.type = "[FILE]";
    file.name = "ignored-display-name";
    file.full_path = document.root / fs::u8path("comma,\"quoted\"\n雪.txt");
    file.size = (static_cast<std::uintmax_t>(1) << 50);
    file.depth = 0;
    file.size_status = SizeStatus::complete;
    document.entries.push_back(file);

    EntryInfo link;
    link.type = "[LINK]";
    link.full_path = document.root / fs::u8path("broken-link");
    link.size_status = SizeStatus::unavailable;
    document.entries.push_back(link);

    document.diagnostics.push_back({file.full_path, "file_size",
        std::make_error_code(std::errc::permission_denied)});
    return document;
}

static void test_json_schema_escaping_and_large_integers() {
    const auto json = serialize_export_json(special_document());
    CHECK(json.find("\"schema_version\": 1") != std::string::npos);
    CHECK(json.find("\"complete\": false") != std::string::npos);
    CHECK(json.find("\"sort\": {\"key\": \"mtime\", \"order\": \"asc\"}") != std::string::npos);
    CHECK(json.find("\"extension\": \".txt\"") != std::string::npos);
    CHECK(json.find("comma,\\\"quoted\\\"\\n") != std::string::npos);
    CHECK(json.find("snow") == std::string::npos);
    CHECK(json.find("雪") != std::string::npos);
    CHECK(json.find("1125899906842624") != std::string::npos);
    CHECK(json.find("\"size_bytes\":null") != std::string::npos);
    CHECK(json.find("\"type\":\"link\"") != std::string::npos);
    CHECK(json.find("\xef\xbf\xbd") != std::string::npos);
    CHECK(json.find("\"diagnostics\": [\n") != std::string::npos);
}

static void test_csv_quotes_fields_and_leaves_unknown_sizes_empty() {
    const auto csv = serialize_export_csv(special_document());
    CHECK(csv.rfind("root,path,type,size_bytes,depth,size_status,scan_complete\r\n", 0) == 0);
    CHECK(csv.find("\"root,\"\"quoted\"\"\n雪\"") != std::string::npos);
    CHECK(csv.find("\"comma,\"\"quoted\"\"\n雪.txt\"") != std::string::npos);
    CHECK(csv.find(",1125899906842624,0,complete,false\r\n") != std::string::npos);
    CHECK(csv.find(",link,,0,unavailable,false\r\n") != std::string::npos);
}

static void test_unpaginated_scan_applies_shared_sort_and_filters() {
    const fs::path root = test_directory.path / "all-children";
    fs::create_directories(root / "context");
    write_text(root / "B_match.txt", "b");
    write_text(root / "A_match.TXT", "a");
    write_text(root / "ignore.txt", "i");
    write_text(root / "context" / "deep_match.txt", "deep");

    ScanOptions options;
    options.paginate = false;
    options.sort = {SortKey::name, SortOrder::asc};
    options.filter = {"match", ".txt"};
    const auto result = scan_tree_entries(root, {}, 1, options);
    CHECK(result.status == ScanStatus::complete);
    CHECK(result.matching_files == 2);
    CHECK(result.entries.size() == 3);
    CHECK(result.entries[0].name == "A_match.TXT");
    CHECK(result.entries[1].name == "B_match.txt");
    CHECK(result.entries[2].name == "context");
    CHECK(std::all_of(result.entries.begin(), result.entries.end(), [](const EntryInfo& entry) {
        return entry.depth == 0 && entry.type != "[RESTO_NEXT]" && entry.type != "[RESTO_PREV]";
    }));
}

static void test_partial_scan_serializes_status_and_exit_code() {
    const fs::path root = test_directory.path / "partial-scan";
    fs::create_directories(root);
    write_text(root / "bad.bin", "bad");
    write_text(root / "good.bin", "good");
    ScanOptions options;
    options.paginate = false;
    options.error_injector = [](const fs::path& path, const std::string& operation) {
        if (path.filename() == "bad.bin" && operation == "file_size")
            return std::make_error_code(std::errc::permission_denied);
        return std::error_code{};
    };
    const auto scan = scan_tree_entries(root, {}, 30, options);
    CHECK(scan.status == ScanStatus::partial);
    ExportDocument document;
    document.root = fs::absolute(root).lexically_normal();
    document.complete = false;
    document.entries = scan.entries;
    document.diagnostics = scan.diagnostics;
    CHECK(export_exit_code(scan.status) == 3);
    const auto json = serialize_export_json(document);
    const auto csv = serialize_export_csv(document);
    CHECK(json.find("\"complete\": false") != std::string::npos);
    CHECK(json.find("\"diagnostics\": [\n") != std::string::npos);
    CHECK(csv.find(",false\r\n") != std::string::npos);
    const auto unavailable = std::find_if(scan.entries.begin(), scan.entries.end(), [](const EntryInfo& entry) {
        return entry.name == "bad.bin";
    });
    CHECK(unavailable != scan.entries.end() && unavailable->size_status == SizeStatus::unavailable);
}

static void test_atomic_output_replaces_and_cleans_failed_temporary() {
    const fs::path root = test_directory.path / "atomic-output";
    fs::create_directories(root);
    const fs::path output = root / "report.csv";
    write_text(output, "old contents\n");
    std::string error;
    CHECK(write_export_file_atomic("new contents\n", output, error));
    CHECK(read_text(output) == "new contents\n");

    const fs::path blocked_target = root / "blocked.csv";
    fs::create_directories(blocked_target);
    write_text(blocked_target / "keep.txt", "preserve\n");
    error.clear();
    CHECK(!write_export_file_atomic("replacement\n", blocked_target, error));
    CHECK(!error.empty());
    CHECK(read_text(blocked_target / "keep.txt") == "preserve\n");
    const auto leftover = std::find_if(fs::directory_iterator(root), fs::directory_iterator{},
        [](const fs::directory_entry& entry) {
            return entry.path().filename().u8string().find(".treefiles-tmp-") != std::string::npos;
        });
    CHECK(leftover == fs::directory_iterator{});
}

int main() {
    fs::create_directories(test_directory.path);
    test_json_schema_escaping_and_large_integers();
    test_csv_quotes_fields_and_leaves_unknown_sizes_empty();
    test_unpaginated_scan_applies_shared_sort_and_filters();
    test_partial_scan_serializes_status_and_exit_code();
    test_atomic_output_replaces_and_cleans_failed_temporary();
    std::cout << "test_export_utils: PASS\n";
    return 0;
}
