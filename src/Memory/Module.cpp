#include <Kore/Memory/Module.hpp>
#include <Kore/Core/Logger.hpp>

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#pragma comment(lib, "Psapi.lib")

namespace Kore::Memory {
namespace {

std::string ToLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::wstring Widen(std::string_view text) {
    if (text.empty())
        return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

std::string Narrow(std::wstring_view text) {
    if (text.empty())
        return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), needed, nullptr, nullptr);
    return out;
}

const IMAGE_NT_HEADERS* NtHeaders(HMODULE handle) {
    if (!handle)
        return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(handle);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const u8*>(handle) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    return nt;
}

} // namespace

Module::Module(HMODULE handle) : m_handle(handle) {
    if (!m_handle)
        return;

    if (const auto* nt = NtHeaders(m_handle))
        m_size = nt->OptionalHeader.SizeOfImage;

    wchar_t path[MAX_PATH]{};
    if (::GetModuleFileNameW(m_handle, path, MAX_PATH)) {
        std::wstring_view view(path);
        if (const auto slash = view.find_last_of(L"\\/"); slash != std::wstring_view::npos)
            view.remove_prefix(slash + 1);
        m_name = Narrow(view);
    }
}

Module Module::Main() {
    return Module(::GetModuleHandleW(nullptr));
}

std::optional<Module> Module::Find(std::string_view name) {
    // GetModuleHandleW does the case-insensitive match for us in the common
    // case; the manual sweep is the fallback for extension-less queries.
    if (const HMODULE direct = ::GetModuleHandleW(Widen(name).c_str()))
        return Module(direct);

    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &needed))
        return std::nullopt;

    const std::string wanted = ToLower(name);
    const std::size_t count = needed / sizeof(HMODULE);
    for (std::size_t i = 0; i < count; ++i) {
        Module candidate(modules[i]);
        if (ToLower(candidate.Name()) == wanted)
            return candidate;
    }
    return std::nullopt;
}

std::optional<Module> Module::WaitFor(std::string_view name, std::uint32_t timeoutMs) {
    const auto deadline = ::GetTickCount64() + timeoutMs;
    for (;;) {
        if (auto found = Find(name))
            return found;
        if (::GetTickCount64() >= deadline) {
            KORE_WARN("Timed out waiting for module '{}'", name);
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::span<const u8> Module::Bytes() const {
    if (!Valid())
        return {};
    return { reinterpret_cast<const u8*>(m_handle), m_size };
}

std::optional<std::span<const u8>> Module::Section(std::string_view name) const {
    const auto* nt = NtHeaders(m_handle);
    if (!nt)
        return std::nullopt;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        // Section names are 8 bytes and only NUL-terminated when shorter.
        char raw[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        std::memcpy(raw, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        if (name == raw) {
            const auto* start = reinterpret_cast<const u8*>(m_handle) + section->VirtualAddress;
            const auto size = section->Misc.VirtualSize ? section->Misc.VirtualSize
                                                        : section->SizeOfRawData;
            return std::span<const u8>(start, size);
        }
    }
    return std::nullopt;
}

std::span<const u8> Module::Text() const {
    if (auto text = Section(".text"))
        return *text;
    return Bytes();
}

void* Module::ExportRaw(const char* name) const {
    return reinterpret_cast<void*>(::GetProcAddress(m_handle, name));
}

} // namespace Kore::Memory
