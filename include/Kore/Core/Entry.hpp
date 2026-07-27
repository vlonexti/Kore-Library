#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Render/IRenderBackend.hpp>

#include <functional>
#include <string>

namespace Kore {

/// How the library should come up inside the host process.
struct StartupOptions {
    /// Names the log file, the config file, and the menu window title.
    std::string name = "KoreLibrary";

    /// Allocate a console for log output. Turn this off once you're past the
    /// bring-up stage — a stray console window is the loudest thing a mod does.
    bool console = true;

    /// Force a graphics backend instead of auto-detecting.
    Render::Backend backend = Render::Backend::None;

    /// Opens and closes the menu.
    int menuKey = VK_INSERT;

    /// Unloads the library and frees the DLL.
    int unloadKey = VK_END;

    /// Wait for this module before doing anything. Set it when the game loads
    /// its renderer or game DLL after your loader has already injected.
    std::string waitForModule;
    std::uint32_t waitTimeoutMs = 30000;

    /// Register your features here. Runs after the overlay is live.
    std::function<void()> onAttach;

    /// Last chance to undo anything the framework doesn't know about.
    std::function<void()> onDetach;
};

/// The library's main thread body. Sets up logging, hooks, the overlay and the
/// menu, then blocks until the unload key is pressed.
///
/// You normally reach this through KORE_ENTRY rather than calling it directly.
void Run(HMODULE self, StartupOptions options);

/// Request an orderly shutdown from anywhere (a menu button, say).
void RequestUnload();

} // namespace Kore

/// Declares DllMain and spins up Kore::Run on its own thread.
///
///     KORE_ENTRY(opts) {
///         opts.name = "BaronyMenu";
///         opts.onAttach = [] { ... };
///     }
///
/// The body configures `opts` — a Kore::StartupOptions& — and returns nothing.
#define KORE_ENTRY(optsName)                                                             \
    static void KoreConfigureStartup(::Kore::StartupOptions& optsName);                  \
    namespace {                                                                          \
    struct KoreEntryState { HMODULE self = nullptr; };                                    \
    inline KoreEntryState g_koreEntry;                                                    \
    inline DWORD WINAPI KoreThreadMain(LPVOID) {                                          \
        ::Kore::StartupOptions options;                                                   \
        KoreConfigureStartup(options);                                                    \
        ::Kore::Run(g_koreEntry.self, std::move(options));                                \
        return 0;                                                                         \
    }                                                                                     \
    }                                                                                     \
    BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {                         \
        if (reason == DLL_PROCESS_ATTACH) {                                               \
            ::DisableThreadLibraryCalls(module);                                          \
            g_koreEntry.self = module;                                                    \
            if (HANDLE t = ::CreateThread(nullptr, 0, KoreThreadMain, nullptr, 0, nullptr))\
                ::CloseHandle(t);                                                         \
        }                                                                                 \
        return TRUE;                                                                      \
    }                                                                                     \
    static void KoreConfigureStartup(::Kore::StartupOptions& optsName)
