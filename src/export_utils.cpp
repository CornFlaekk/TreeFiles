#include "export_utils.h"
#include "platform_utils.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace fs = std::filesystem;

namespace {
static size_t valid_utf8_sequence(const std::string& value, size_t offset) {
    const auto byte = [&](size_t index) {
        return static_cast<unsigned char>(value[index]);
    };
    const unsigned char lead = byte(offset);
    if (lead <= 0x7f) return 1;
    const size_t remaining = value.size() - offset;
    if (lead >= 0xc2 && lead <= 0xdf && remaining >= 2 &&
        (byte(offset + 1) & 0xc0) == 0x80) return 2;
    if (lead >= 0xe0 && lead <= 0xef && remaining >= 3) {
        const unsigned char second = byte(offset + 1);
        const unsigned char third = byte(offset + 2);
        if ((third & 0xc0) != 0x80) return 0;
        if (lead == 0xe0) return second >= 0xa0 && second <= 0xbf ? 3 : 0;
        if (lead == 0xed) return second >= 0x80 && second <= 0x9f ? 3 : 0;
        return (second & 0xc0) == 0x80 ? 3 : 0;
    }
    if (lead >= 0xf0 && lead <= 0xf4 && remaining >= 4) {
        const unsigned char second = byte(offset + 1);
        if ((byte(offset + 2) & 0xc0) != 0x80 || (byte(offset + 3) & 0xc0) != 0x80)
            return 0;
        if (lead == 0xf0) return second >= 0x90 && second <= 0xbf ? 4 : 0;
        if (lead == 0xf4) return second >= 0x80 && second <= 0x8f ? 4 : 0;
        return (second & 0xc0) == 0x80 ? 4 : 0;
    }
    return 0;
}

static std::string sanitized_utf8(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (size_t offset = 0; offset < value.size();) {
        const size_t sequence = valid_utf8_sequence(value, offset);
        if (sequence == 0) {
            output += "\xef\xbf\xbd";
            ++offset;
        } else {
            output.append(value, offset, sequence);
            offset += sequence;
        }
    }
    return output;
}

static std::string json_string(const std::string& value) {
    const auto clean = sanitized_utf8(value);
    std::string output;
    output.reserve(clean.size() + 2);
    output.push_back('"');
    for (const unsigned char character : clean) {
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    output += "\\u00";
                    output.push_back(hex[(character >> 4) & 0x0f]);
                    output.push_back(hex[character & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
        }
    }
    output.push_back('"');
    return output;
}

static std::string csv_field(const std::string& value) {
    const auto clean = sanitized_utf8(value);
    if (clean.find_first_of(",\"\r\n") == std::string::npos) return clean;
    std::string output;
    output.reserve(clean.size() + 2);
    output.push_back('"');
    for (const char character : clean) {
        if (character == '"') output.push_back('"');
        output.push_back(character);
    }
    output.push_back('"');
    return output;
}

static const char* entry_type(const EntryInfo& entry) {
    if (entry.type == "[FILE]") return "file";
    if (entry.type == "[DIR] ") return "directory";
    if (entry.type == "[LINK]") return "link";
    return "unknown";
}

static const char* size_status(SizeStatus status) {
    switch (status) {
        case SizeStatus::complete: return "complete";
        case SizeStatus::partial: return "partial";
        case SizeStatus::unavailable: return "unavailable";
    }
    return "unavailable";
}

static std::string relative_path(const ExportDocument& document, const EntryInfo& entry) {
    return entry.full_path.lexically_relative(document.root).generic_u8string();
}

static std::string json_diagnostic(const ScanIssue& issue) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"path\":" << json_string(issue.path.u8string())
           << ",\"operation\":" << json_string(issue.operation)
           << ",\"error_code\":" << issue.error.value()
           << ",\"message\":" << json_string(issue.error.message()) << '}';
    return output.str();
}

static std::string make_temporary_name() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream suffix;
    suffix.imbue(std::locale::classic());
    suffix << ".treefiles-tmp-" << std::hex << ticks << '-'
           << sequence.fetch_add(1, std::memory_order_relaxed);
    return suffix.str();
}
} // namespace

std::string serialize_export_json(const ExportDocument& document) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\n  \"schema_version\": 1,\n  \"root\": "
           << json_string(document.root.u8string())
           << ",\n  \"complete\": " << (document.complete ? "true" : "false")
           << ",\n  \"sort\": {\"key\": " << json_string(sort_key_name(document.sort.key))
           << ", \"order\": " << json_string(sort_order_name(document.sort.order))
           << "},\n  \"filter\": {\"text\": " << json_string(document.filter.text)
           << ", \"extension\": " << json_string(document.filter.extension)
           << "},\n  \"entries\": [";
    if (!document.entries.empty()) output << '\n';
    for (size_t index = 0; index < document.entries.size(); ++index) {
        const auto& entry = document.entries[index];
        output << "    {\"path\":" << json_string(relative_path(document, entry))
               << ",\"type\":" << json_string(entry_type(entry)) << ",\"size_bytes\":";
        if (entry.size_status == SizeStatus::unavailable) output << "null";
        else output << entry.size;
        output << ",\"depth\":" << entry.depth
               << ",\"size_status\":" << json_string(size_status(entry.size_status)) << '}';
        output << (index + 1 == document.entries.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"diagnostics\": [";
    if (!document.diagnostics.empty()) output << '\n';
    for (size_t index = 0; index < document.diagnostics.size(); ++index) {
        output << "    " << json_diagnostic(document.diagnostics[index]);
        output << (index + 1 == document.diagnostics.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string serialize_export_csv(const ExportDocument& document) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "root,path,type,size_bytes,depth,size_status,scan_complete\r\n";
    const auto root = document.root.generic_u8string();
    for (const auto& entry : document.entries) {
        output << csv_field(root) << ','
               << csv_field(relative_path(document, entry)) << ','
               << csv_field(entry_type(entry)) << ',';
        if (entry.size_status != SizeStatus::unavailable) output << entry.size;
        output << ',' << entry.depth << ',' << csv_field(size_status(entry.size_status))
               << ',' << (document.complete ? "true" : "false") << "\r\n";
    }
    return output.str();
}

int export_exit_code(ScanStatus status) {
    switch (status) {
        case ScanStatus::complete: return 0;
        case ScanStatus::partial: return 3;
        case ScanStatus::failed: return 1;
    }
    return 1;
}

bool write_export_file_atomic(const std::string& contents, const fs::path& target,
                              std::string& error) {
    if (target.empty() || target.filename().empty()) {
        error = "Output path must name a file.";
        return false;
    }
    const fs::path parent = target.has_parent_path() ? target.parent_path() : fs::path(".");
    std::error_code filesystem_error;
    if (!fs::is_directory(parent, filesystem_error) || filesystem_error) {
        error = filesystem_error ? filesystem_error.message()
            : std::make_error_code(std::errc::not_a_directory).message();
        return false;
    }

    fs::path temporary;
    bool created = false;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = parent / fs::u8path(make_temporary_name());
        filesystem_error.clear();
        if (!fs::exists(temporary, filesystem_error) && !filesystem_error) {
            created = true;
            break;
        }
    }
    if (!created) {
        error = "Could not allocate a temporary output file.";
        return false;
    }
    if (contents.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "Export is too large to write.";
        return false;
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output) {
            error = "Could not open the temporary output file.";
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) {
            error = "Could not write the temporary output file.";
            output.close();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
        output.close();
        if (output.fail()) {
            error = "Could not close the temporary output file.";
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }

    if (!replace_file(temporary, target, error)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}
