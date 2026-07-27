#pragma once

#include <Kore/Core/Types.hpp>
#include <Kore/Memory/Module.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace Kore::Memory {

/// An IDA-style byte signature: "48 8B 05 ? ? ? ? 48 85 C0".
/// Wildcards may be written as '?' or '??'.
///
/// Patterns survive game patches far better than hardcoded offsets, which is
/// why every address in a feature should come from one of these.
class Pattern {
public:
    explicit Pattern(std::string_view signature);

    [[nodiscard]] bool Valid() const { return !m_bytes.empty(); }
    [[nodiscard]] std::size_t Length() const { return m_bytes.size(); }

    /// First match inside an arbitrary byte range. Returns 0 on failure.
    [[nodiscard]] Address Scan(std::span<const u8> range) const;

    /// First match inside a module's .text section. Returns 0 on failure.
    [[nodiscard]] Address Scan(const Module& module) const;

    /// Every match in the range. Mostly useful for diagnosing an ambiguous
    /// signature — a good signature has exactly one hit.
    [[nodiscard]] std::vector<Address> ScanAll(std::span<const u8> range) const;

private:
    std::vector<u8>   m_bytes;
    std::vector<bool> m_mask; // true = must match
};

/// Resolve a RIP-relative operand into an absolute address.
///
/// `at` points at the start of the instruction, `operandOffset` is the distance
/// from `at` to the 4-byte displacement, and `instructionLength` is the full
/// length of the instruction. For `48 8B 05 xx xx xx xx` (mov rax, [rip+d32])
/// that is operandOffset = 3, instructionLength = 7.
[[nodiscard]] Address ResolveRelative(Address at, std::size_t operandOffset, std::size_t instructionLength);

/// Follow a chain of pointers, validating readability at each step.
/// Returns 0 if any link is unmapped, which beats faulting inside the game.
[[nodiscard]] Address FollowChain(Address base, std::span<const Offset> offsets);

/// True if `address` is readable for `size` bytes in this process.
[[nodiscard]] bool IsReadable(Address address, std::size_t size = 1);

} // namespace Kore::Memory
