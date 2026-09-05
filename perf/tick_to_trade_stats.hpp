#pragma once

#include "perf/latency_histogram.hpp"
#include "perf/tick_to_trade_trace.hpp"

class TickToTradeStats {
public:
    void Record(const TickToTradeTrace& trace);
    void LogSummary() const;

private:
    LatencyHistogram ws_decode_histogram_;
    LatencyHistogram book_update_histogram_;
    LatencyHistogram rate_limit_check_histogram_;
    LatencyHistogram price_format_histogram_;
    LatencyHistogram message_build_histogram_;
    LatencyHistogram send_histogram_;
    LatencyHistogram tick_to_trade_histogram_;
};
