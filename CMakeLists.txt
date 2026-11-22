cmake_minimum_required(VERSION 3.24)
project(GeekDeskCpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Third-party C JSON library (compiled separately cleanly without project warnings)
add_library(geekdesk_yyjson STATIC
    third_party/yyjson/yyjson.c
)
set_target_properties(geekdesk_yyjson PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_include_directories(geekdesk_yyjson SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
)
if(MSVC)
    target_compile_options(geekdesk_yyjson PRIVATE /W0)
else()
    target_compile_options(geekdesk_yyjson PRIVATE -w)
endif()

# Core domain & storage library
add_library(geekdesk_core STATIC
    src/core/utf8.cpp
    src/core/models.cpp
    src/core/catalog_mutator.cpp
    src/core/pinyin.cpp
    src/core/hotkey_parser.cpp
    src/core/placement_math.cpp
    src/core/catalog_sorter.cpp
    src/core/folder_sync.cpp
    src/core/search_engine.cpp
    src/platform/folder_watcher.cpp
    src/storage/json_codec.cpp
    src/storage/atomic_writer.cpp
    src/storage/backup_manager.cpp
    src/storage/logger.cpp
    src/ui/theme_palette.cpp
    src/ui/icon_crop.cpp
)

target_include_directories(geekdesk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

target_compile_definitions(geekdesk_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(geekdesk_core PRIVATE /W4 /WX /utf-8 /EHsc)
else()
    target_compile_options(geekdesk_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_link_libraries(geekdesk_core PUBLIC
    geekdesk_yyjson
    user32
    shell32
    ole32
    uuid
    dwmapi
    shlwapi
    advapi32
)

# Main executable (Win32 System Shell + Mode A Direct2D Software MainWindow)
add_executable(GeekDeskCpp WIN32
    src/main.cpp
    src/platform/dpi_helper.cpp
    src/platform/backdrop.cpp
    src/platform/hotkey_manager.cpp
    src/platform/mouse_hook.cpp
    src/platform/fullscreen_detector.cpp
    src/platform/autostart.cpp
    src/platform/tray_icon.cpp
    src/platform/shortcut_resolver.cpp
    src/platform/shell_launcher.cpp
    src/platform/single_instance.cpp
    src/platform/file_dialogs.cpp
    src/ui/icon_cache.cpp
    src/ui/d2d_renderer.cpp
    src/ui/drag_drop.cpp
    src/ui/context_menus.cpp
    src/ui/item_dialogs.cpp
    src/ui/settings_window.cpp
    src/ui/main_window.cpp
    resources/app.rc
)
target_link_libraries(GeekDeskCpp PRIVATE
    geekdesk_core
    d2d1
    dwrite
    windowscodecs
    imm32
    wtsapi32
    shcore
    uxtheme
    gdi32
    comctl32
)
if(MSVC)
    target_compile_options(GeekDeskCpp PRIVATE /W4 /WX /utf-8 /EHsc)
elseif(MINGW)
    target_compile_options(GeekDeskCpp PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # Suppress MinGW's bundled default-manifest.o so our custom app.manifest
    # (PerMonitorV2, UTF-8 ActiveCodePage, Common Controls v6, longPathAware) links cleanly
    # without `.rsrc merge failure: multiple non-default manifests`.
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -dumpspecs
        OUTPUT_VARIABLE _mingw_specs
        RESULT_VARIABLE _mingw_specs_rc
    )
    set(_custom_spec_opt "")
    if(_mingw_specs_rc EQUAL 0 AND _mingw_specs MATCHES "\\*endfile:\r?\n([^\r\n]+)")
        set(_endfile_line "${CMAKE_MATCH_1}")
        string(REPLACE "%{!shared:%:if-exists(default-manifest.o%s)}" "" _endfile_line "${_endfile_line}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/custom_manifest.spec" "*endfile:\n${_endfile_line}\n")
        set(_custom_spec_opt "-specs=${CMAKE_CURRENT_BINARY_DIR}/custom_manifest.spec")
    endif()
    target_link_options(GeekDeskCpp PRIVATE
        -municode
        ${_custom_spec_opt}
        $<$<CONFIG:Release>:-s>
        -static
        -static-libgcc
        -static-libstdc++
    )
else()
    target_compile_options(GeekDeskCpp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Unit tests
enable_testing()
add_executable(geekdesk_tests
    tests/core_tests.cpp
)
target_include_directories(geekdesk_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest
)
target_link_libraries(geekdesk_tests PRIVATE geekdesk_core)
if(MSVC)
    target_compile_options(geekdesk_tests PRIVATE /W4 /WX /utf-8 /EHsc)
elseif(MINGW)
    target_compile_options(geekdesk_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(geekdesk_tests PRIVATE
        $<$<CONFIG:Release>:-s>
        -static
        -static-libgcc
        -static-libstdc++
    )
else()
    target_compile_options(geekdesk_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME core_tests COMMAND geekdesk_tests)
