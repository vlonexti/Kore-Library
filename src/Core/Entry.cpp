#include <Kore/Core/Entry.hpp>
#include <Kore/Core/Logger.hpp>
#include <Kore/Core/Config.hpp>
#include <Kore/Memory/Module.hpp>
#include <Kore/Hooks/HookManager.hpp>
#include <Kore/Render/Overlay.hpp>
#include <Kore/Menu/Feature.hpp>
#include <Kore/Menu/Menu.hpp>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace Kore {
namespace {

std::atomic<bool> g_unloadRequested{false};

} // namespace

void RequestUnload() {
    g_unloadRequested.store(true, std::memory_order_release);
}

void Run(HMODULE self, StartupOptions options) {
    Logger::Init(options.name, options.console);
    KORE_INFO("KoreLibrary {} starting in PID {}", options.name, ::GetCurrentProcessId());

    Config::Get().Init(options.name);

    if (!options.waitForModule.empty()) {
        KORE_INFO("Waiting for module '{}'...", options.waitForModule);
        if (!Memory::Module::WaitFor(options.waitForModule, options.waitTimeoutMs)) {
            KORE_ERROR("Required module never appeared - aborting");
            Logger::Shutdown();
            ::FreeLibraryAndExitThread(self, 1);
        }
    }

    auto& overlay = Render::Overlay::Get();
    overlay.SetMenuKey(options.menuKey);

    if (!overlay.Initialize(options.backend)) {
        KORE_ERROR("Overlay initialisation failed - aborting");
        Hooks::Shutdown();
        Logger::Shutdown();
        ::FreeLibraryAndExitThread(self, 1);
    }

    Menu::Get().SetTitle(options.name);

    if (options.onAttach)
        options.onAttach();

    FeatureManager::Get().AttachAll();

    // Only now install the draw callback. Registering features mutates the
    // manager's vector from this thread; if the render thread were already
    // iterating it, a reallocation would leave it walking freed memory.
    overlay.SetDrawCallback([] { Menu::Get().Draw(); });

    // The menu starts open so it's obvious the library loaded.
    overlay.SetMenuOpen(true);
    KORE_INFO("Ready. Menu key: {:#x}, unload key: {:#x}", options.menuKey, options.unloadKey);

    while (!g_unloadRequested.load(std::memory_order_acquire)) {
        if (::GetAsyncKeyState(options.unloadKey) & 1) {
            KORE_INFO("Unload key pressed");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // ---- teardown, in the reverse order of setup -------------------------

    KORE_INFO("Unloading");

    // Features first: they own memory patches that must be reverted while the
    // game is still running normally.
    FeatureManager::Get().DisableAll();

    if (options.onDetach)
        options.onDetach();

    Config::Get().Save();

    // Retires the render and input hooks and waits for the render thread to
    // leave them before releasing ImGui.
    overlay.Shutdown();

    Hooks::Shutdown();

    // Give any thread that was mid-call in a trampoline a moment to unwind
    // before the DLL's code pages disappear.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    KORE_INFO("Goodbye");
    Logger::Shutdown();

    ::FreeLibraryAndExitThread(self, 0);
}

} // namespace Kore
