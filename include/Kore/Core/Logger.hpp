#pragma once

#include <format>
#include <string>
#include <string_view>

namespace Kore {

enum class LogLevel { Trace, Info, Warn, Error };

/// Process-wide log sink. Writes to an allocated console (optional) and to
/// %LOCALAPPDATA%\KoreLibrary\<name>.log. Safe to call before Init(), in which
/// case messages are dropped rather than crashing a half-attached process.
class Logger {
public:
    static void Init(std::string_view name, bool allocConsole = true);
    static void Shutdown();

    static void SetMinLevel(LogLevel level);
    static void Write(LogLevel level, std::string_view message);

    template <typename... Args>
    static void Log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        Write(level, std::format(fmt, std::forward<Args>(args)...));
    }
};

} // namespace Kore

#define KORE_TRACE(...) ::Kore::Logger::Log(::Kore::LogLevel::Trace, __VA_ARGS__)
#define KORE_INFO(...)  ::Kore::Logger::Log(::Kore::LogLevel::Info,  __VA_ARGS__)
#define KORE_WARN(...)  ::Kore::Logger::Log(::Kore::LogLevel::Warn,  __VA_ARGS__)
#define KORE_ERROR(...) ::Kore::Logger::Log(::Kore::LogLevel::Error, __VA_ARGS__)
