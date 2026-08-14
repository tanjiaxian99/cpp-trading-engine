#pragma once

#include <string_view>

constexpr std::string_view kEmpty = "";

// General JSON response fields
constexpr std::string_view kCode = "code";
constexpr std::string_view kData = "data";
constexpr std::string_view kOrdId = "ordId";
constexpr std::string_view kClOrdId = "clOrdId";
constexpr std::string_view kSCode = "sCode";
constexpr std::string_view kSMsg = "sMsg";

// The value `code`/`sCode` hold on success.
constexpr std::string_view kSuccessCode = "0";

// /public/time
constexpr std::string_view kTs = "ts";

// /public/instruments
constexpr std::string_view kTickSz = "tickSz";
constexpr std::string_view kLotSz = "lotSz";
constexpr std::string_view kMinSz = "minSz";

// Order JSON fields
constexpr std::string_view kCash = "cash";
constexpr std::string_view kBuy = "buy";
constexpr std::string_view kSell = "sell";
constexpr std::string_view kLimit = "limit";
constexpr std::string_view kMarket = "market";
constexpr std::string_view kBaseCcy = "base_ccy";

// WebSocket application-level heartbeat
constexpr std::string_view kOkxPingText = "ping";
constexpr std::string_view kOkxPongText = "pong";

// WebSocket public channel push messages
constexpr std::string_view kChannel = "channel";
constexpr std::string_view kBooksChannel = "books";
constexpr std::string_view kTradesChannel = "trades";
constexpr std::string_view kSeqId = "seqId";
constexpr std::string_view kPrevSeqId = "prevSeqId";
constexpr int kSnapshotSeqId = -1;
constexpr std::string_view kAsks = "asks";
constexpr std::string_view kBids = "bids";
constexpr int kBooksLevelPriceIdx = 0;
constexpr int kBooksLevelSizeIdx = 1;