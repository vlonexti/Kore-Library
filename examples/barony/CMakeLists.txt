# The payload: a DLL you inject into the game.
#
# Barony is an SDL2 + OpenGL title, so this builds against the GL backend. For a
# D3D11 game nothing here changes — Overlay::Initialize() picks the backend at
# runtime.

add_library(BaronyMenu SHARED Main.cpp)

target_link_libraries(BaronyMenu PRIVATE Kore::Library)

# The static CRT is configured project-wide in the top-level CMakeLists.
set_target_properties(BaronyMenu PROPERTIES
    OUTPUT_NAME "BaronyMenu"
    PREFIX "")

if(MSVC)
    target_compile_options(BaronyMenu PRIVATE /W4)
endif()
