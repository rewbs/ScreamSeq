#pragma once
#include <cstddef>

namespace ScreamSeq::Api {
// Newline-delimited request/reply bounds shared by the adapter and transport.
// A completed write must detect an undeliverable result before replay retention.
inline constexpr std::size_t maxProtocolRequestBytes = 32 * 1024 * 1024;
inline constexpr std::size_t maxProtocolResponseBytes = 32 * 1024 * 1024;
}
