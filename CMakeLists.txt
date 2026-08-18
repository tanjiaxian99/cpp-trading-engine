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

# Homebrew's llvm isn't required — CLion ships its own clang-tidy/clang-format,
# used here as a fallback so the CLI build works without an extra install.
set(CLION_CLANG_TIDY_HINT "/Applications/CLion.app/Contents/bin/clang/mac/aarch64/bin")
set(CLION_CLANG_FORMAT_HINT "/Applications/CLion.app/Contents/plugins/clion-radler/DotFiles/macos-arm64")

# Must be set before add_executable(): CXX_CLANG_TIDY is a target property
# seeded from this variable at target-creation time, not read afterward.
option(ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
if (ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy HINTS "${CLION_CLANG_TIDY_HINT}" REQUIRED)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND clang -print-resource-dir OUTPUT_VARIABLE APPLE_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    # CLion's bundled clang-tidy ships without its own resource-dir (no stdarg.h etc.),
    # and doesn't infer the SDK sysroot the way /usr/bin/c++ does — borrow both from
    # Apple clang, which is what actually compiles this project.
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
        okx/auth.hpp okx/auth.cpp okx/okx_constants.hpp
        okx/response_utils.hpp okx/response_utils.cpp
        okx/instrument.hpp okx/instrument.cpp
        okx/order_types.hpp okx/order_types.cpp
        okx/rest_orders.hpp okx/rest_orders.cpp
        okx/ws_orders.hpp okx/ws_orders.cpp
        okx/ws_response_demux.hpp okx/ws_response_demux.cpp
        okx/heartbeat.hpp okx/heartbeat.cpp okx/ws_client.hpp okx/ws_client.cpp
        okx/order_book.hpp okx/order_book.cpp okx/market_data.hpp okx/market_data.cpp
        okx/order_events.hpp okx/order_events.cpp
        okx/order_lifecycle.hpp okx/order_lifecycle.cpp
        okx/order_store.hpp okx/order_store.cpp
        okx/reconciliation.hpp okx/reconciliation.cpp
        okx/account_state.hpp okx/account_state.cpp
        util/json.hpp util/json.cpp util/base64.hpp util/base64.cpp
        util/sha1.hpp util/sha1.cpp)

# Lets every file use root-relative includes (e.g. "okx/auth.hpp" from
# main.cpp, or even from within okx/rest_orders.cpp itself), regardless of
# which directory the including file lives in.
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
