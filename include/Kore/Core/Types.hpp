#pragma once

#include <cstddef>
#include <cstdint>
#include <Windows.h>

namespace Kore {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

/// A process address. Deliberately not void* so arithmetic is well-defined.
using Address = std::uintptr_t;

#if defined(_WIN64)
inline constexpr bool kIs64Bit = true;
#else
inline constexpr bool kIs64Bit = false;
#endif

/// Byte offset applied to a base address. Signed so backwards walks are natural.
using Offset = std::ptrdiff_t;

} // namespace Kore
