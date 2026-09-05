#include "perf/tick_to_trade_stats.hpp"

#include "log/async_logger.hpp"

void TickToTradeStats::Record(const TickToTradeTrace& trace) {
    ws_decode_histogram_.RecordTicks(trace.message_decoded_ticks - trace.wire_arrival_ticks);
    book_update_histogram_.RecordTicks(trace.book_consistent_ticks - trace.message_decoded_ticks);
    strategy_build_histogram_.RecordTicks(trace.message_built_ticks - trace.book_consistent_ticks);
    send_histogram_.RecordTicks(trace.send_ticks - trace.message_built_ticks);
    tick_to_trade_histogram_.RecordTicks(trace.send_ticks - trace.wire_arrival_ticks);
}

void TickToTradeStats::LogSummary() const {
    auto log_stage = [](std::string_view name, const LatencyHistogram& hist) {
        Log.Info("{}: count={} p50={} ns p99={} ns p99.9={} ns max={} ns", name, hist.Count(),
                 hist.Percentile(0.5), hist.Percentile(0.99), hist.Percentile(0.999), hist.Max());
    };
    log_stage("WS decode", ws_decode_histogram_);
    log_stage("Book update", book_update_histogram_);
    log_stage("Strategy + order build", strategy_build_histogram_);
    log_stage("Send (encode+write)", send_histogram_);
    log_stage("Tick-to-trade (total)", tick_to_trade_histogram_);
}
