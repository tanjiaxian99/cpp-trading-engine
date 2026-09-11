#include "okx/marketdata/sbe_bbo.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <type_traits>

namespace sbe {
namespace {

static_assert(std::endian::native == std::endian::little,
              "OKX SBE is little-endian; this decoder needs byte swapping on a big-endian host");

constexpr std::size_t kBlockLengthOffset = 0;
constexpr std::size_t kTemplateIdOffset = 2;
constexpr std::size_t kSchemaIdOffset = 4;
constexpr std::size_t kVersionOffset = 6;

static_assert(kVersionOffset + sizeof(std::uint16_t) == kHeaderSize,
              "Header field offsets do not add up to the messageHeader composite size");

constexpr std::size_t kInstIdCodeOffset = 0;
constexpr std::size_t kTsUsOffset = 8;
constexpr std::size_t kOutTimeOffset = 16;
constexpr std::size_t kSeqIdOffset = 24;
constexpr std::size_t kAskPxMantissaOffset = 32;
constexpr std::size_t kAskSzMantissaOffset = 40;
constexpr std::size_t kBidPxMantissaOffset = 48;
constexpr std::size_t kBidSzMantissaOffset = 56;
constexpr std::size_t kAskOrdCountOffset = 64;
constexpr std::size_t kBidOrdCountOffset = 68;
constexpr std::size_t kPxExponentOffset = 72;
constexpr std::size_t kSzExponentOffset = 73;

static_assert(kSzExponentOffset + sizeof(std::int8_t) == kBboTbtBlockLength,
              "Field offsets do not add up to the schema's blockLength");

constexpr int kMinExponent = -18;
constexpr int kMaxExponent = 18;
constexpr std::size_t kPow10Size = kMaxExponent - kMinExponent + 1;

constexpr std::array<double, kPow10Size> MakePow10Table() {
    std::array<double, kPow10Size> table{};
    for (std::size_t i = 0; i < kPow10Size; i++) {
        const int exponent = static_cast<int>(i) + kMinExponent;
        double magnitude = 1.0;
        for (int step = 0; step < (exponent < 0 ? -exponent : exponent); step++) {
            magnitude *= 10.0;
        }
        table[i] = exponent < 0 ? 1.0 / magnitude : magnitude;
    }
    return table;
}

constexpr std::array<double, kPow10Size> kPow10 = MakePow10Table();

template <typename T>
T Load(const char* source) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value;
    std::memcpy(&value, source, sizeof(T));
    return value;
}

bool ExponentInRange(std::int8_t exponent) {
    return exponent >= kMinExponent && exponent <= kMaxExponent;
}

double Scale(std::int64_t mantissa, std::int8_t exponent) {
    return static_cast<double>(mantissa) *
           kPow10[static_cast<std::size_t>(exponent - kMinExponent)];
}

}  // namespace

std::optional<MessageHeader> DecodeHeader(std::string_view message) {
    if (message.size() < kHeaderSize) {
        return std::nullopt;
    }

    const char* data = message.data();
    return MessageHeader{
        .block_length = Load<std::uint16_t>(data + kBlockLengthOffset),
        .template_id = Load<std::uint16_t>(data + kTemplateIdOffset),
        .schema_id = Load<std::uint16_t>(data + kSchemaIdOffset),
        .version = Load<std::uint16_t>(data + kVersionOffset),
    };
}

std::optional<BboTbt> DecodeBboTbt(std::string_view message) {
    const auto header = DecodeHeader(message);
    if (!header || header->schema_id != kSchemaId || header->template_id != kBboTbtTemplateId) {
        return std::nullopt;
    }

    if (header->block_length < kBboTbtBlockLength ||
        message.size() < kHeaderSize + header->block_length) {
        return std::nullopt;
    }

    const char* block = message.data() + kHeaderSize;
    const auto px_exponent = Load<std::int8_t>(block + kPxExponentOffset);
    const auto sz_exponent = Load<std::int8_t>(block + kSzExponentOffset);
    if (!ExponentInRange(px_exponent) || !ExponentInRange(sz_exponent)) {
        return std::nullopt;
    }

    return BboTbt{
        .inst_id_code = Load<std::int64_t>(block + kInstIdCodeOffset),
        .ts_us = Load<std::int64_t>(block + kTsUsOffset),
        .out_time = Load<std::int64_t>(block + kOutTimeOffset),
        .seq_id = Load<std::int64_t>(block + kSeqIdOffset),
        .ask_px = Scale(Load<std::int64_t>(block + kAskPxMantissaOffset), px_exponent),
        .ask_sz = Scale(Load<std::int64_t>(block + kAskSzMantissaOffset), sz_exponent),
        .bid_px = Scale(Load<std::int64_t>(block + kBidPxMantissaOffset), px_exponent),
        .bid_sz = Scale(Load<std::int64_t>(block + kBidSzMantissaOffset), sz_exponent),
        .ask_ord_count = Load<std::int32_t>(block + kAskOrdCountOffset),
        .bid_ord_count = Load<std::int32_t>(block + kBidOrdCountOffset),
    };
}

}  // namespace sbe
