cmake_minimum_required(VERSION 3.20)
project(TreeFiles LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Threads REQUIRED)

if(WIN32)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(pdcurses
        URL https://codeload.github.com/wmcbrine/PDCurses/zip/refs/tags/3.9
        URL_HASH SHA256=5c2ccab1d8fbf7e2c82eac954444a6760eb949d47d6dc57e09f339d34a50ba79
    )
    FetchContent_MakeAvailable(pdcurses)
    # PDCurses 3.9 has Makefiles but no CMake project. Build its wincon port.
    file(GLOB pdcurses_sources CONFIGURE_DEPENDS
        "${pdcurses_SOURCE_DIR}/pdcurses/*.c"
        "${pdcurses_SOURCE_DIR}/wincon/*.c"
    )
    add_library(treefiles_curses STATIC ${pdcurses_sources})
    target_include_directories(treefiles_curses SYSTEM PUBLIC "${pdcurses_SOURCE_DIR}")
    target_compile_definitions(treefiles_curses PUBLIC PDC_WIDE PDC_FORCE_UTF8)
    target_link_libraries(treefiles_curses PUBLIC user32 advapi32)
else()
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
    add_library(treefiles_curses INTERFACE)
    target_include_directories(treefiles_curses SYSTEM INTERFACE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(treefiles_curses INTERFACE ${CURSES_LIBRARIES})
endif()

add_library(treefiles_core STATIC src/file_utils.cpp src/localization.cpp src/settings.cpp src/platform_utils.cpp)
target_include_directories(treefiles_core PUBLIC include)
target_link_libraries(treefiles_core PUBLIC Threads::Threads)

add_executable(treefiles src/main.cpp src/ui_utils.cpp)
target_link_libraries(treefiles PRIVATE treefiles_core treefiles_curses)
if(WIN32)
    target_compile_definitions(treefiles_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(treefiles_core PUBLIC shell32)
    if(MINGW)
        target_link_options(treefiles PRIVATE -static)
    endif()
endif()

function(treefiles_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()
treefiles_warnings(treefiles_core)
treefiles_warnings(treefiles)

include(CTest)
if(BUILD_TESTING)
    foreach(component human_readable_size build_tree pagination settings localization)
        add_executable(test_${component} tests/test_${component}.cpp)
        target_link_libraries(test_${component} PRIVATE treefiles_core)
        treefiles_warnings(test_${component})
        add_test(NAME ${component} COMMAND test_${component})
    endforeach()
    if(WIN32)
        find_program(TREEFILES_POWERSHELL NAMES pwsh powershell REQUIRED)
        add_test(NAME integration
            COMMAND "${TREEFILES_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_scenarios.ps1"
                -Binary $<TARGET_FILE:treefiles>
        )
    else()
        find_program(TREEFILES_BASH NAMES bash REQUIRED)
        add_test(NAME integration
            COMMAND ${CMAKE_COMMAND} -E env "TREEFILES_BINARY=$<TARGET_FILE:treefiles>"
                "${TREEFILES_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_scenarios.sh"
        )
    endif()
endif()

install(TARGETS treefiles RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
install(DIRECTORY media/ DESTINATION media)
install(DIRECTORY licenses/ DESTINATION licenses)
