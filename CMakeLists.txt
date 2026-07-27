cmake_minimum_required(VERSION 3.21)
project(KoreLibrary VERSION 0.1.0 LANGUAGES CXX C)

if(NOT WIN32)
    message(FATAL_ERROR "KoreLibrary targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT everywhere, including the fetched dependencies. An injected DLL
# lands in whatever process the user picks, and that process may not have the
# VC++ redistributable — and mixing CRTs across our targets would fail to link
# anyway. Set before any target is defined so it applies to all of them.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(GNUInstallDirs)

option(KORE_BACKEND_OPENGL "Build the OpenGL overlay backend" ON)
option(KORE_BACKEND_D3D11  "Build the D3D11 overlay backend"  ON)
option(KORE_BACKEND_D3D9   "Build the D3D9 overlay backend"   ON)
option(KORE_GL_LEGACY      "Use the fixed-function GL2 ImGui backend instead of GL3" OFF)
option(KORE_BUILD_EXAMPLES "Build the example payload DLLs" ON)

# Package managers supply ImGui and MinHook themselves. The vcpkg port turns
# this on; a plain source build leaves it off and fetches them, so cloning and
# configuring just works with no manual dependency setup.
option(KORE_USE_EXTERNAL_DEPS "Find imgui/minhook via find_package instead of fetching them" OFF)

# ---------------------------------------------------------------- dependencies

if(KORE_USE_EXTERNAL_DEPS)
    find_package(imgui CONFIG REQUIRED)
    find_package(minhook CONFIG REQUIRED)

    # Normalise the target names so the rest of the file doesn't care where the
    # dependencies came from.
    add_library(kore_imgui INTERFACE)
    target_link_libraries(kore_imgui INTERFACE imgui::imgui)

    add_library(kore_minhook INTERFACE)
    if(TARGET minhook::minhook)
        target_link_libraries(kore_minhook INTERFACE minhook::minhook)
    else()
        target_link_libraries(kore_minhook INTERFACE minhook)
    endif()
else()
    include(FetchContent)

    # MinHook ships no CMakeLists at this tag, so fetch the sources only and
    # build them here. Five C files and a length disassembler — cheaper than
    # depending on whatever build system upstream happens to have.
    FetchContent_Declare(minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG        v1.3.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(minhook)

    add_library(kore_minhook STATIC
        ${minhook_SOURCE_DIR}/src/buffer.c
        ${minhook_SOURCE_DIR}/src/hook.c
        ${minhook_SOURCE_DIR}/src/trampoline.c
        ${minhook_SOURCE_DIR}/src/hde/hde32.c
        ${minhook_SOURCE_DIR}/src/hde/hde64.c)

    # Build-interface only: MinHook.h is used by our .cpp files, never by a
    # public Kore header, so consumers of the installed package don't need it.
    target_include_directories(kore_minhook PUBLIC
        $<BUILD_INTERFACE:${minhook_SOURCE_DIR}/include>)
    target_include_directories(kore_minhook PRIVATE ${minhook_SOURCE_DIR}/src)

    if(MSVC)
        # Upstream C predates several warnings we enable for our own code.
        target_compile_options(kore_minhook PRIVATE /W3)
    endif()

    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)

    # ImGui ships no CMakeLists, so compile it ourselves.
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)

    if(KORE_BACKEND_OPENGL)
        if(KORE_GL_LEGACY)
            list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp)
        else()
            list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
        endif()
    endif()
    if(KORE_BACKEND_D3D11)
        list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    endif()
    if(KORE_BACKEND_D3D9)
        list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
    endif()

    add_library(kore_imgui STATIC ${IMGUI_SOURCES})
    target_include_directories(kore_imgui PUBLIC
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
        # Kore.hpp includes imgui.h, so the installed package must carry the
        # ImGui headers alongside our own.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(kore_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

    file(GLOB KORE_VENDORED_IMGUI_HEADERS
        "${imgui_SOURCE_DIR}/*.h"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_*.h")
    set(KORE_INSTALL_VENDORED_IMGUI ON)
endif()

# ---------------------------------------------------------------------- KoreLibrary

set(KORE_SOURCES
    src/Core/Logger.cpp
    src/Core/Config.cpp
    src/Core/Entry.cpp
    src/Math/Matrix.cpp
    src/Memory/Module.cpp
    src/Memory/Pattern.cpp
    src/Memory/Protect.cpp
    src/Memory/Value.cpp
    src/Hooks/HookManager.cpp
    src/Hooks/VmtHook.cpp
    src/Hooks/WndProcHook.cpp
    src/Render/Overlay.cpp
    src/Render/Draw.cpp
    src/Menu/Feature.cpp
    src/Menu/Menu.cpp)

if(KORE_BACKEND_OPENGL)
    list(APPEND KORE_SOURCES src/Render/OpenGLBackend.cpp)
endif()
if(KORE_BACKEND_D3D11)
    list(APPEND KORE_SOURCES src/Render/D3D11Backend.cpp)
endif()
if(KORE_BACKEND_D3D9)
    list(APPEND KORE_SOURCES src/Render/D3D9Backend.cpp)
endif()

add_library(KoreLibrary STATIC ${KORE_SOURCES})
add_library(Kore::Library ALIAS KoreLibrary)

target_include_directories(KoreLibrary
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(KoreLibrary PUBLIC kore_imgui kore_minhook)

if(KORE_BACKEND_OPENGL)
    target_compile_definitions(KoreLibrary PUBLIC KORE_BACKEND_OPENGL=1)
    target_link_libraries(KoreLibrary PUBLIC opengl32)
    if(KORE_GL_LEGACY)
        target_compile_definitions(KoreLibrary PUBLIC KORE_GL_LEGACY=1)
    endif()
endif()
if(KORE_BACKEND_D3D11)
    target_compile_definitions(KoreLibrary PUBLIC KORE_BACKEND_D3D11=1)
    target_link_libraries(KoreLibrary PUBLIC d3d11 dxgi delayimp)

    # Delay-load the D3D DLLs so a payload built with both backends still loads
    # into an OpenGL-only game. Without this the loader resolves d3d11.dll and
    # d3dcompiler_47.dll at DLL_PROCESS_ATTACH and injection fails outright if
    # either is missing. INTERFACE because the flags belong to the final DLL.
    if(MSVC)
        target_link_options(KoreLibrary INTERFACE
            "/DELAYLOAD:d3d11.dll"
            "/DELAYLOAD:d3dcompiler_47.dll")
    endif()
endif()

if(KORE_BACKEND_D3D9)
    target_compile_definitions(KoreLibrary PUBLIC KORE_BACKEND_D3D9=1)
    target_link_libraries(KoreLibrary PUBLIC d3d9 delayimp)
    if(MSVC)
        target_link_options(KoreLibrary INTERFACE "/DELAYLOAD:d3d9.dll")
    endif()
endif()

target_compile_definitions(KoreLibrary PUBLIC
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

if(MSVC)
    target_compile_options(KoreLibrary PRIVATE /W4 /permissive- /MP)
    # Our sources are UTF-8. Without this MSVC decodes them in the system
    # codepage, which mangles any non-ASCII character in a string literal.
    # PUBLIC so payloads compiled against us get the same treatment.
    target_compile_options(KoreLibrary PUBLIC /utf-8)
endif()

if(KORE_BUILD_EXAMPLES)
    add_subdirectory(examples/barony)
endif()

# ------------------------------------------------------------------- install

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The dependency shims are INTERFACE/STATIC targets in KoreLibrary's link
# interface, so they have to be part of the same export set or the generated
# config file references targets that don't exist.
set(KORE_EXPORT_TARGETS KoreLibrary kore_imgui kore_minhook)

install(TARGETS ${KORE_EXPORT_TARGETS}
    EXPORT KoreLibraryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/Kore DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Flattened into the include root, which is the layout package managers use and
# which src/Render/ImGuiBackends.hpp already probes for.
if(KORE_INSTALL_VENDORED_IMGUI)
    install(FILES ${KORE_VENDORED_IMGUI_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(EXPORT KoreLibraryTargets
    FILE      KoreLibraryTargets.cmake
    NAMESPACE Kore::
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/KoreLibrary)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KoreLibraryConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/KoreLibraryConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/KoreLibraryConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/KoreLibrary)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KoreLibraryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KoreLibraryConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/KoreLibrary)
