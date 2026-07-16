#pragma once

#include <bit>
#include <cstdint>
#include <limits>

// https://en.wikipedia.org/wiki/Fast_inverse_square_root
static constexpr float fastInvSqrt(float x) noexcept
{
    static_assert(std::numeric_limits<float>::is_iec559);
    float const y = std::bit_cast<float>(
            0x5F1FFFF9 - (std::bit_cast<std::uint32_t>(x) >> 1));
    return y * (0.703952253f * (2.38924456f - (x * y * y)));
}

static constexpr float fastSqrt(float x)
{
    return fastInvSqrt(x) * x;
}
