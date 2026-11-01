# P5.5. The floor is 3.21, found by building, not guessed. Tested
# 2026-10-25 with MSVC 14.50 (VS 2026), both configurations:
#
#   3.16.9  builds, but WRONG: maps C++20 to /std:c++latest, which on
#           this compiler is a C++23 preview -- a different language
#           from the one every other build compiles. Below the floor.
#   3.21.0  builds, -std:c++20. The floor.
#   3.22.6  builds, -std:c++20.
#   4.1.2   builds (the CMake bundled with VS 2026, which the old 4.2
#           floor refused), with Ninja and the VS 18 2026 generator.
#   4.3.0   builds.
#
# "...4.2" is the policy ceiling: CMake 4.2 and newer behave exactly as
# they did when 4.2 was the floor. Checked: under 4.3 the generated
# build.ninja is byte-identical either way, in both configurations.
#
# Kitware's own CMake learned the "Visual Studio 18 2026" generator in
# 4.2, so 3.21 to 4.1 need -G Ninja (shipped with Visual Studio) unless
# they are VS's bundled build. build_all.bat uses the VS generator.
cmake_minimum_required(VERSION 3.21...4.2)

# ── Version — src/Version.h is the single source of truth ─────
#
# The title bar shows RADUK_VERSION_A together with main.cpp's compile
# timestamp, so a stale exe (wrong build dir, or run before a rebuild
# finished) is obvious at a glance. That only works if the number is
# current, so it is written by hand in exactly one place and everything
# else is derived from it here: this project's version, and the Win32
# version resource in src/RadukCapture.rc.in.
#
# Why it is derived rather than copied: it used to live in four
# hand-maintained places and they drifted to 1.1.9 (Version.h), 1.0.5
# (here) and 1.0.8.0 (twice, in the two .rc files) while the help file
# documented v1.2.3 as shipped. The authoritative runtime number was the
# stalest of them, which is precisely the failure the title-bar check
# exists to prevent.
#
# To bump the version, edit src/Version.h and nothing else. Editing it
# re-runs this configure step (CMAKE_CONFIGURE_DEPENDS below), so the
# resource follows on the next build without a manual re-configure.

set(RADUK_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h")
file(READ "${RADUK_VERSION_HEADER}" RADUK_VERSION_H_TEXT)

string(REGEX MATCH
    "#define[ \t]+RADUK_VERSION_A[ \t]+\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\""
    RADUK_VERSION_A_MATCH "${RADUK_VERSION_H_TEXT}")
if(NOT RADUK_VERSION_A_MATCH)
    message(FATAL_ERROR
        "Could not parse RADUK_VERSION_A from ${RADUK_VERSION_HEADER}.\n"
        "It must read:  #define RADUK_VERSION_A   \"MAJOR.MINOR.PATCH\"")
endif()
set(RADUK_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

# The wide and narrow macros are two literals that must agree. Nothing in
# the compiler checks that, and a bump that updates one and not the other
# would put a different version in the title bar than in the resource.
string(REGEX MATCH
    "#define[ \t]+RADUK_VERSION_W[ \t]+L\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
    RADUK_VERSION_W_MATCH "${RADUK_VERSION_H_TEXT}")
if(NOT RADUK_VERSION_W_MATCH)
    message(FATAL_ERROR
        "Could not parse RADUK_VERSION_W from ${RADUK_VERSION_HEADER}.\n"
        "It must read:  #define RADUK_VERSION_W  L\"MAJOR.MINOR.PATCH\"")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL RADUK_VERSION)
    message(FATAL_ERROR
        "Version mismatch in ${RADUK_VERSION_HEADER}:\n"
        "  RADUK_VERSION_W = ${CMAKE_MATCH_1}\n"
        "  RADUK_VERSION_A = ${RADUK_VERSION}\n"
        "Both macros must carry the same version.")
endif()

project(RadukCapture VERSION ${RADUK_VERSION} LANGUAGES CXX)

message(STATUS "RadukCapture version ${RADUK_VERSION} (from src/Version.h)")

# Editing Version.h must re-run configure, or the generated resource keeps
# the previous version until someone re-configures by hand. file(READ)
# creates no such dependency on its own.
set_property(DIRECTORY APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${RADUK_VERSION_HEADER}")

# ── Win32 version resource, generated ─────────────────────────
set(RADUK_RC_IN        "${CMAKE_CURRENT_SOURCE_DIR}/src/RadukCapture.rc.in")
set(RADUK_RC_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/RadukCapture.rc")
set(RADUK_ICON_PATH    "${CMAKE_CURRENT_SOURCE_DIR}/assets/RadukCapture.ico")
set(RADUK_VERSION_COMMA
    "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(RADUK_VERSION_DOT
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
configure_file("${RADUK_RC_IN}" "${RADUK_RC_GENERATED}" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Emulator mode ──────────────────────────────────────────────
option(RADUK_EMULATOR "Build in emulator mode — no hardware or SDK DLLs required" OFF)

if(RADUK_EMULATOR)
    message(STATUS "*** RADUK_EMULATOR ON — emulator build ***")
else()
    message(STATUS "Hardware build — requires ASI SDK, EAF SDK, camera, EAF, mount")
endif()

# ── SDK libraries (hardware mode only) ────────────────────────

if(NOT RADUK_EMULATOR)
    set(ASI_SDK_DIR "${CMAKE_SOURCE_DIR}/external/ASI_SDK")
    add_library(ASI_SDK SHARED IMPORTED)
    set_target_properties(ASI_SDK PROPERTIES
        IMPORTED_LOCATION "${ASI_SDK_DIR}/lib/x64/ASICamera2.dll"
        IMPORTED_IMPLIB   "${ASI_SDK_DIR}/lib/x64/ASICamera2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${ASI_SDK_DIR}/include"
    )

    set(EAF_SDK_DIR "${CMAKE_SOURCE_DIR}/external/EAF_SDK")
    add_library(EAF_SDK SHARED IMPORTED)
    set_target_properties(EAF_SDK PROPERTIES
        IMPORTED_LOCATION "${EAF_SDK_DIR}/lib/x64/EAF_focuser.dll"
        IMPORTED_IMPLIB   "${EAF_SDK_DIR}/lib/x64/EAF_focuser.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${EAF_SDK_DIR}/include"
    )
endif()

# ── Source files ───────────────────────────────────────────────

if(RADUK_EMULATOR)
    set(DEVICE_SOURCES)
else()
    set(DEVICE_SOURCES
        src/core/ASICamera.cpp
        src/core/EAFFocuser.cpp
        src/core/NexStarMount.cpp
    )
endif()

add_executable(RadukCapture WIN32
    src/main.cpp

    # Win32 resources — application icon + version info (Phase 44).
    # Generated from src/RadukCapture.rc.in with the version from
    # src/Version.h; see the version block at the top of this file.
    ${RADUK_RC_GENERATED}

    # Core
    ${DEVICE_SOURCES}
    src/core/CaptureThread.cpp
    src/core/CaptureManager.cpp
    src/core/SERWriter.cpp
    src/core/FITSWriter.cpp
    src/core/WriteBuffer.cpp
    src/core/FrameAnalyzer.cpp
    src/core/ADCAlignment.cpp
    src/core/CentroidTracker.cpp
    src/core/USBTrafficManager.cpp

    # GUI
    src/gui/Window.cpp
    src/gui/PreviewRenderer.cpp
    src/gui/ControlPanel.cpp

    # Net
    src/net/WebSocketServer.cpp
    src/net/APEXTelemetry.cpp
    src/net/PreviewStreamer.cpp
)

# ── Include directories ────────────────────────────────────────

target_include_directories(RadukCapture PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

if(NOT RADUK_EMULATOR)
    target_include_directories(RadukCapture PRIVATE
        ${ASI_SDK_DIR}/include
        ${EAF_SDK_DIR}/include
    )
endif()

# ── Preprocessor defines ──────────────────────────────────────

# Force MSVC to read source files as UTF-8 — prevents Â× and Â· corruption
# of Unicode characters in string literals when system codepage is not UTF-8.
if(MSVC)
    target_compile_options(RadukCapture PRIVATE /utf-8)
    # The generated resource names the icon by absolute path, so this is no
    # longer required for it. Kept so a hand-written .rc added later can
    # still use a project-relative path.
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I \"${CMAKE_SOURCE_DIR}\"")
endif()

if(RADUK_EMULATOR)
    target_compile_definitions(RadukCapture PRIVATE RADUK_EMULATOR)
endif()

# ── Link libraries ─────────────────────────────────────────────

if(RADUK_EMULATOR)
    target_link_libraries(RadukCapture PRIVATE
        d2d1 dwrite d3d11 dxgi d3dcompiler windowscodecs shell32
        ws2_32 winhttp windowsapp Shcore dwmapi
    )
else()
    target_link_libraries(RadukCapture PRIVATE
        ASI_SDK EAF_SDK
        d2d1 dwrite d3d11 dxgi d3dcompiler windowscodecs shell32
        ws2_32 winhttp windowsapp ole32 oleaut32 advapi32 uxtheme Shcore dwmapi
    )
endif()

# ── DLL copy (hardware mode only) ─────────────────────────────

if(NOT RADUK_EMULATOR)
    add_custom_command(TARGET RadukCapture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASI_SDK_DIR}/lib/x64/ASICamera2.dll"
            "$<TARGET_FILE_DIR:RadukCapture>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EAF_SDK_DIR}/lib/x64/EAF_focuser.dll"
            "$<TARGET_FILE_DIR:RadukCapture>"
    )
endif()

# ── Help file (both builds) ────────────────────────────────────
# AIContextBuilder loads RadukCapture-Help.md as the AI assistant's whole
# app reference, looking next to the exe first and only then in the current
# directory. Without this copy, launching the exe from anywhere other than
# the project root left the assistant with
# "(Help file not found — limited app reference available)" — it answered
# anyway, just without knowing anything about the application.

# A POST_BUILD step on the exe only fires when the exe RELINKS, so
# editing the help alone left the deployed copies stale - and those
# are the ones the assistant actually reads. A custom target with ALL
# runs on every build regardless of whether any source changed.
add_custom_target(copy_help ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RadukCapture-Help.md"
        "$<TARGET_FILE_DIR:RadukCapture>"
    DEPENDS "${CMAKE_SOURCE_DIR}/RadukCapture-Help.md"
    COMMENT "Copying help file (AI assistant app reference) next to the exe"
)
add_dependencies(copy_help RadukCapture)

# ── sqlite3.dll (both builds) ──────────────────────────────────
#
# Phase 92a. ObjectKnowledgeDB loads SQLite at runtime with
# LoadLibrary and returns gracefully when the DLL is absent, which is
# correct behaviour and is also why this went unnoticed for months:
# the 2.4 MB object_knowledge.db built in March has never once been
# readable by the running app. The object info panel fell back to
# "basic ephemeris only" every time it opened and the AI assistant's
# knowledge lookups returned empty strings, silently, in both cases.
#
# BOTH builds, unlike the camera and focuser SDKs above. The emulator
# build is where bench work happens and the knowledge base has nothing
# to do with whether a camera is attached.
#
# SQLite is public domain, so shipping the binary alongside the SDK
# DLLs carries no licence obligation.
add_custom_target(copy_sqlite ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/external/sqlite3/lib/x64/sqlite3.dll"
        "$<TARGET_FILE_DIR:RadukCapture>"
    DEPENDS "${CMAKE_SOURCE_DIR}/external/sqlite3/lib/x64/sqlite3.dll"
    COMMENT "Copying sqlite3.dll (object knowledge database) next to the exe"
)
add_dependencies(copy_sqlite RadukCapture)

# ── Create output directories ──────────────────────────────────

add_custom_command(TARGET RadukCapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/captures"
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/logs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/profiles"
    COMMENT "Creating RadukCapture data directories"
)
