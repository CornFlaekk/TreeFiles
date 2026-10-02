#include "localization.h"
#include "file_utils.h"
#include "test_directory.h"
#include <cstdio>
#include <fstream>
#include <string>

static int failed = 0;
static void check(bool condition, const char* name) {
    std::printf("  %s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failed;
}

int main() {
    check(std::string(language_code()) == "en", "English is the default language");
    for (const char* language : {"en", "es"}) {
        check(set_language(language), "supported language can be selected");
        bool complete = true;
        for (int key = 0; key < static_cast<int>(Text::Count); ++key)
            complete = complete && text(static_cast<Text>(key)) && *text(static_cast<Text>(key));
        check(complete, "all interface translations exist");
    }
    check(!set_language("fr"), "unsupported language is rejected");
    check(std::string(language_code()) == "es", "invalid language preserves current choice");
    check(std::string(text(Text::DeletePrompt)) == "Borrar", "Spanish delete prompt");
    const TestDirectory fixture("localization");
    std::filesystem::create_directories(fixture.path);
    for (int i = 0; i < 3; ++i) std::ofstream(fixture.path / ("file_" + std::to_string(i))) << "test";
    std::vector<EntryInfo> entries;
    build_tree_entries(fixture.path, {}, entries, 0, 2);
    check(entries.back().name.find("Siguiente (1/2)") != std::string::npos, "Spanish page navigation");
    set_language("en");
    entries.clear();
    build_tree_entries(fixture.path, {}, entries, 0, 2);
    check(entries.back().name.find("Next (1/2)") != std::string::npos, "English page navigation");
    check(std::string(text(Text::Usage)).find("--lang en|es") != std::string::npos, "help documents language option");
    check(std::string(text(Text::Usage)).find("--save-settings") != std::string::npos, "help documents explicit settings persistence");
    return failed ? 1 : 0;
}
