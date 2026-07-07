#pragma once

#include <cstdint>

namespace HFT::protocol
{
    /**
     * @brief Flag defining the derivative contract type.
     */
    enum class OptionType : std::uint8_t
    {
        Call = 0,
        Put = 1
    };

    /**
     * @brief Zero-allocation, cache-aligned, footprint-optimized market tick packet
     */
    struct alignas(64) MarketPacket
    {
        // 8-Byte Fields (48 Bytes Total)
        std::uint64_t timestamp_ns;
        double u_price;
        double strike;
        double tte;
        double bid_price;
        double ask_price;

        // 4-Byte Fields (12 Bytes Total)
        std::uint32_t bid_size;
        std::uint32_t ask_size;
        std::uint32_t asset_id;

        // 1-Byte & Padding Fields (4 Bytes Total)
        OptionType option_type;
        std::uint8_t padding[3];

        // Internal member guards are legal because primitive type sizes are complete
        static_assert(sizeof(timestamp_ns) == 8, "Timestamp alignment failure");
        static_assert(sizeof(OptionType) == 1, "OptionType underlying type violation");
    };

    // --- STRUCTURAL FIREWALLS ---
    static_assert(sizeof(MarketPacket) == 64, "MarketPacket size deviates from 64-byte cache line alignment");

} // namespace HFT::protocol