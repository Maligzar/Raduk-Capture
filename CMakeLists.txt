cmake_minimum_required(VERSION 4.2)
project(RadukCapture VERSION 1.0.5 LANGUAGES CXX)

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

    # Win32 resources — application icon + version info (Phase 44)
    src/RadukCapture.rc

    # Core
    ${DEVICE_SOURCES}
    src/core/CaptureThread.cpp

    # GUI
    src/gui/Window.cpp
    src/gui/PreviewRenderer.cpp
    src/gui/ControlPanel.cpp

    # Phase 1 header-only (no .cpp)
    # SessionLog.h, StatusBar.h, TargetProfile.h, ProfileManager.h

    # Phase 2 header-only (no .cpp)
    # SERWriter.h, WriteBuffer.h, CaptureManager.h

    # Phase 2 M7 header-only (no .cpp)
    # USBTrafficManager.h

    # Phase 3 M9 header-only (no .cpp)
    # FrameAnalyzer.h

    # Phase 3 M10 header-only (no .cpp)
    # gpu/GPUDebayer.h — D3D11 compute shader debayer

    # Phase 4 M12 header-only (no .cpp)
    # net/WebSocketServer.h — RFC 6455 WS server, Winsock2
    # net/APEXTelemetry.h  — JSON telemetry builder + command parser

    # Phase 4 M8 header-only (no .cpp)
    # core/IMount.h        — abstract mount interface (ITelescopeV3 surface + PulseGuide)

    # Phase 5 header-only (no .cpp)
    # core/CentroidTracker.h — planet centroid tracking, NexStar PulseGuide corrections

    # Phase 6 header-only (no .cpp)
    # sky/SkyObject.h      — planet ephemeris (VSOP87 simplified) + Messier catalog
    # sky/ASTAPSolver.h    — plate solving via ASTAP process
    # gui/GoToPanel.h      — GoTo tab: catalog, search, GoTo, plate solve, sync
    # core/ASCOMMount.h    — IMount via ASCOM IDispatch COM (talks to CPWI)
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
    # RC compiler needs the project root so assets/RadukCapture.ico resolves
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I \"${CMAKE_SOURCE_DIR}\"")
endif()

if(RADUK_EMULATOR)
    target_compile_definitions(RadukCapture PRIVATE RADUK_EMULATOR)
endif()

# ── Link libraries ─────────────────────────────────────────────

if(RADUK_EMULATOR)
    target_link_libraries(RadukCapture PRIVATE
        d2d1 dwrite d3d11 dxgi d3dcompiler windowscodecs shell32
        ws2_32 winhttp Shcore
    )
else()
    target_link_libraries(RadukCapture PRIVATE
        ASI_SDK EAF_SDK
        d2d1 dwrite d3d11 dxgi d3dcompiler windowscodecs shell32
        ws2_32 winhttp ole32 oleaut32 advapi32 uxtheme Shcore
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

# ── Create output directories ──────────────────────────────────

add_custom_command(TARGET RadukCapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/captures"
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/logs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/RadukCapture/profiles"
    COMMENT "Creating RadukCapture data directories"
)
