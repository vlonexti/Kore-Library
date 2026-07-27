#include <Kore/Core/Logger.hpp>

#include "Core/Paths.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace Kore {
namespace {

struct LoggerState {
    std::mutex    mutex;
    std::ofstream file;
    FILE*         console     = nullptr;
    bool          ownsConsole = false;
    bool          active      = false;
    LogLevel      minLevel    = LogLevel::Trace;
};

LoggerState& State() {
    static LoggerState state;
    return state;
}

const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

WORD LevelColour(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return FOREGROUND_INTENSITY;
        case LogLevel::Info:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LogLevel::Warn:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LogLevel::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
    }
    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto local = std::chrono::current_zone()->to_local(now);
    return std::format("{:%H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(local));
}

} // namespace

std::filesystem::path KoreDataDirectory() {
    PWSTR raw = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        base = raw;
        ::CoTaskMemFree(raw);
    } else {
        base = std::filesystem::temp_directory_path();
    }
    base /= L"KoreLibrary";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

void Logger::Init(std::string_view name, bool allocConsole) {
    auto& state = State();
    std::scoped_lock lock(state.mutex);
    if (state.active)
        return;

    if (allocConsole) {
        // If the game already has a console we attach to it rather than
        // stacking a second, orphaned window on top.
        if (::GetConsoleWindow() == nullptr && ::AllocConsole()) {
            state.ownsConsole = true;
            const std::string title = std::string(name) + " — KoreLibrary";
            ::SetConsoleTitleA(title.c_str());
        }
        freopen_s(&state.console, "CONOUT$", "w", stdout);
    }

    const auto path = KoreDataDirectory() / (std::string(name) + ".log");
    state.file.open(path, std::ios::out | std::ios::trunc);

    state.active = true;

    Write(LogLevel::Info, std::format("KoreLibrary attached — logging to {}", path.string()));
}

void Logger::Shutdown() {
    auto& state = State();
    std::scoped_lock lock(state.mutex);
    if (!state.active)
        return;

    state.active = false;
    if (state.file.is_open())
        state.file.close();

    if (state.console) {
        std::fclose(state.console);
        state.console = nullptr;
    }
    if (state.ownsConsole) {
        ::FreeConsole();
        state.ownsConsole = false;
    }
}

void Logger::SetMinLevel(LogLevel level) {
    State().minLevel = level;
}

void Logger::Write(LogLevel level, std::string_view message) {
    auto& state = State();
    if (!state.active || level < state.minLevel)
        return;

    std::scoped_lock lock(state.mutex);
    const std::string line = std::format("[{}] [{}] {}", Timestamp(), LevelTag(level), message);

    if (state.console) {
        const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        const bool colour = ::GetConsoleScreenBufferInfo(out, &info) != FALSE;
        if (colour)
            ::SetConsoleTextAttribute(out, LevelColour(level));
        std::fputs(line.c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        if (colour)
            ::SetConsoleTextAttribute(out, info.wAttributes);
    }

    if (state.file.is_open()) {
        state.file << line << '\n';
        state.file.flush();
    }

    ::OutputDebugStringA((line + "\n").c_str());
}

} // namespace Kore
