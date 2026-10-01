#pragma once
#include <string>

enum class Text {
    Previous, Next, Page, Navigation, Actions, System,
    MoveBinding, PageBinding, ExpandBinding, OpenBinding, DeleteBinding, ColorBinding, QuitBinding,
    Scan, Yes, No, BackgroundColor, ForegroundColor,
    Black, Red, Green, Yellow, Blue, Magenta, Cyan, White,
    Loading, DeletePrompt, Error, ColorsUnsupported, Usage, PageSizeError, LanguageError,
    UnknownOption, UsageHint, UnreadableDirectory, ConfigLoadWarning, ConfigSaveWarning,
    OpenPathError, OpenCommandError, Bytes, InvalidColorEvent,
    WarningsBinding, ScanWarnings, ScanDiagnostics, PressAnyKey, SizePartial, SizeUnavailable,
    RefreshBinding, EnterDirectoryBinding, ParentDirectoryBinding, ChangeRootBinding,
    PathPrompt, PathPromptHint, NavigationError,
    Count
};

bool set_language(const std::string& code);
const char* language_code();
const char* text(Text key);
