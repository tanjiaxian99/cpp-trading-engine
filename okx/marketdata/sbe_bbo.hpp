#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sbe {

inline constexpr std::size_t kHeaderSize = 8;
inline constexpr std::uint16_t kSchemaId = 1;

inline constexpr std::uint16_t kBboTbtTemplateId = 1000;
inline constexpr std::uint16_t kBboTbtBlockLength = 74;

struct MessageHeader {
    std::uint16_t block_length;
    std::uint16_t template_id;
    std::uint16_t schema_id;
    std::uint16_t version;
};

struct BboTbt {
    std::int64_t inst_id_code;
    std::int64_t ts_us;
    std::int64_t out_time;
    std::int64_t seq_id;
    double ask_px;
    double ask_sz;
    double bid_px;
    double bid_sz;
    std::int32_t ask_ord_count;
    std::int32_t bid_ord_count;
};

[[nodiscard]] std::optional<MessageHeader> DecodeHeader(std::string_view message);
[[nodiscard]] std::optional<BboTbt> DecodeBboTbt(std::string_view message);

}  // namespace sbe
