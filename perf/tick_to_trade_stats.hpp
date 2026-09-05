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
    LatencyHistogram strategy_build_histogram_;
    LatencyHistogram send_histogram_;
    LatencyHistogram tick_to_trade_histogram_;
};
