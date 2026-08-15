#pragma once

#include <string_view>

constexpr std::string_view kEmpty = "";

// General JSON response fields
constexpr std::string_view kCode = "code";
constexpr std::string_view kMsg = "msg";
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
constexpr std::string_view kInstIdCode = "instIdCode";

// WebSocket application-level heartbeat
constexpr std::string_view kOkxPingText = "ping";
constexpr std::string_view kOkxPongText = "pong";

// WebSocket private channel login
constexpr std::string_view kEvent = "event";
constexpr std::string_view kLoginEvent = "login";

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

// WebSocket "orders" channel push messages
constexpr std::string_view kOrdersChannel = "orders";
constexpr std::string_view kInstId = "instId";
constexpr std::string_view kSide = "side";
constexpr std::string_view kPx = "px";
constexpr std::string_view kSz = "sz";
constexpr std::string_view kAccFillSz = "accFillSz";
constexpr std::string_view kAvgPx = "avgPx";
constexpr std::string_view kState = "state";
constexpr std::string_view kStateLive = "live";
constexpr std::string_view kStatePartiallyFilled = "partially_filled";
constexpr std::string_view kStateFilled = "filled";
constexpr std::string_view kStateCanceled = "canceled";
constexpr std::string_view kStateMmpCanceled = "mmp_canceled";

// WebSocket "account" channel push messages
constexpr std::string_view kAccountChannel = "account";
constexpr std::string_view kDetails = "details";
constexpr std::string_view kCcy = "ccy";
constexpr std::string_view kCashBal = "cashBal";
constexpr std::string_view kAvailBal = "availBal";

// WebSocket trade op responses (op: "order" / "cancel-order" / "amend-order" / "batch-orders")
constexpr std::string_view kId = "id";
constexpr std::string_view kOp = "op";
constexpr std::string_view kOrderOp = "order";
constexpr std::string_view kCancelOrderOp = "cancel-order";
constexpr std::string_view kAmendOrderOp = "amend-order";
constexpr std::string_view kBatchOrdersOp = "batch-orders";