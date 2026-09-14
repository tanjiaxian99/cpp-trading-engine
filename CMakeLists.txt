cmake_minimum_required(VERSION 3.20)

if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif ()

project(trading_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)
find_package(CURL REQUIRED)

set(CLION_CLANG_TIDY_HINT "/Applications/CLion.app/Contents/bin/clang/mac/aarch64/bin")
set(CLION_CLANG_FORMAT_HINT "/Applications/CLion.app/Contents/plugins/clion-radler/DotFiles/macos-arm64")

option(ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
if (ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy HINTS "${CLION_CLANG_TIDY_HINT}" REQUIRED)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND clang -print-resource-dir OUTPUT_VARIABLE APPLE_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
            "--extra-arg=-isysroot${MACOS_SDK_PATH}"
            "--extra-arg=-resource-dir=${APPLE_CLANG_RESOURCE_DIR}")
endif ()

add_executable(trading_engine main.cpp config.hpp
        net/transport.hpp net/transport.cpp net/ring_buffer.hpp
        net/websocket_handshake.hpp net/websocket_handshake.cpp
        net/websocket_frame.hpp net/websocket_frame.cpp
        net/websocket_control.hpp net/websocket_control.cpp
        net/websocket_reassembler.hpp
        rest/rest_client.hpp rest/rest_client.cpp
        okx/connectivity/auth.hpp okx/connectivity/auth.cpp okx/common/okx_constants.hpp
        okx/common/instruments.hpp okx/connectivity/okx_endpoints.hpp
        okx/common/response_utils.hpp okx/common/response_utils.cpp
        okx/connectivity/instrument.hpp okx/connectivity/instrument.cpp
        okx/orders/order_types.hpp okx/orders/order_types.cpp
        okx/orders/rest_orders.hpp okx/orders/rest_orders.cpp
        okx/orders/ws_orders.hpp okx/orders/ws_orders.cpp
        okx/orders/ws_order_round_trip.hpp okx/orders/ws_order_round_trip.cpp
        okx/connectivity/ws_response_demux.hpp okx/connectivity/ws_response_demux.cpp
        okx/connectivity/heartbeat.hpp okx/connectivity/heartbeat.cpp okx/connectivity/okx_ws_client.hpp okx/connectivity/okx_ws_client.cpp
        okx/connectivity/rate_limiter.hpp okx/connectivity/rate_limiter.cpp
        okx/marketdata/order_book.hpp okx/marketdata/order_book.cpp okx/marketdata/market_data.hpp okx/marketdata/market_data.cpp
        okx/marketdata/market_data_source.hpp okx/marketdata/market_data_source.cpp
        okx/marketdata/json_book_source.hpp okx/marketdata/json_book_source.cpp
        okx/marketdata/sbe_bbo.hpp okx/marketdata/sbe_bbo.cpp
        okx/marketdata/sbe_bbo_source.hpp okx/marketdata/sbe_bbo_source.cpp
        okx/orders/order_events.hpp okx/orders/order_events.cpp
        okx/orders/order_lifecycle.hpp okx/orders/order_lifecycle.cpp
        okx/orders/order_store.hpp okx/orders/order_store.cpp
        okx/orders/reconciliation.hpp okx/orders/reconciliation.cpp
        okx/orders/order_timeout_monitor.hpp okx/orders/order_timeout_monitor.cpp
        okx/risk/risk.hpp okx/risk/risk.cpp
        okx/risk/position.hpp okx/risk/position.cpp
        okx/risk/kill_switch.hpp okx/risk/kill_switch.cpp
        okx/strategy/strategy.hpp
        okx/strategy/naive_quoter.hpp okx/strategy/naive_quoter.cpp
        okx/risk/account_state.hpp okx/risk/account_state.cpp
        okx/engine/bootstrap.hpp okx/engine/bootstrap.cpp
        okx/engine/engine.hpp okx/engine/engine.cpp
        util/json.hpp util/json.cpp util/base64.hpp util/base64.cpp
        util/sha1.hpp util/sha1.cpp
        perf/clock.hpp
        perf/latency_histogram.hpp perf/latency_histogram.cpp
        perf/tick_to_trade_trace.hpp
        perf/tick_to_trade_stats.hpp perf/tick_to_trade_stats.cpp
        log/async_logger.hpp log/async_logger.cpp)

target_include_directories(trading_engine PRIVATE ${CMAKE_SOURCE_DIR})

target_compile_options(trading_engine PRIVATE
        -Wall -Wextra -Wpedantic
)

target_link_libraries(trading_engine PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::boost
        CURL::libcurl
)

find_program(CLANG_FORMAT_EXE NAMES clang-format HINTS "${CLION_CLANG_FORMAT_HINT}")
if (CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
            "${CMAKE_SOURCE_DIR}/*.cpp"
            "${CMAKE_SOURCE_DIR}/*.hpp"
            "${CMAKE_SOURCE_DIR}/*.h"
    )
    list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "${CMAKE_SOURCE_DIR}/build/.*")
    list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "${CMAKE_SOURCE_DIR}/cmake-build-.*")

    add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
            COMMENT "Formatting source files with clang-format"
    )
    add_custom_target(format-check
            COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
            COMMENT "Checking formatting with clang-format"
    )
endif ()
