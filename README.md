# trading-engine

A low-latency spot trading engine in C++20, targeting OKX V5.

Maintains a live order book from an incremental WebSocket feed, runs a strategy against it,
and manages the full order lifecycle — placement, fills, cancellation, and reconciliation
across reconnects — with pre-trade risk limits and a kill switch.

The design goal is a system that actually trades, with measured latency rather than
claimed latency.

## Status

**Early development. Not yet functional.**

| Milestone | Scope | State |
|---|---|---|
| A | Signed REST order entry — place, fill, cancel | Not started |
| B | Async engine: WebSocket feed, order lifecycle, risk, strategy | Not started |
| C | Order book implementations and benchmarks | Not started |
| D | Optional upgrades | — |

## Design

The engine leans on libraries for solved, generic infrastructure and implements everything
specific to exchange connectivity and market data itself.

| Layer | Implementation |
|---|---|
| Readiness notification, timers | **Boost.Asio** — reactor only |
| TLS transport | **`asio::ssl::stream`** over OpenSSL |
| REST (cold path) | **libcurl** — startup, reconnect, cancel-all |
| HMAC-SHA256, base64 | **OpenSSL** |
| **WebSocket framing (RFC 6455)** | In-tree — handshake, frame codec, masking, continuation |
| **Receive buffering, in-place parsing** | In-tree — decode off the read buffer, no DOM |
| **Order books, sequencing, gap recovery** | In-tree — pre-allocated, zero hot-path allocation |
| **Order lifecycle, reconciliation, risk** | In-tree |
| **Slabs, ring buffers, SPSC queue, measurement** | In-tree |

Two deliberate choices worth stating:

**Asio is used strictly as a reactor** — `asio::ip::tcp::socket` plus `asio::ssl::stream`,
with framing layered on top. Boost.Beast is intentionally *not* used: it would own the
WebSocket codec, and message framing is where the latency-relevant decisions live.

**Receive buffers are owned by the engine**, not by Asio, even though Asio could manage them.
Buffer ownership is what makes it possible to parse messages in place off the read buffer
rather than copying them into intermediate objects.

## Architecture

```
  OKX public WS ─┐
                 ├─ Asio reactor ─ TLS ─ RFC 6455 codec ─ parser ─ order book
  OKX private WS ┘                                          │
                                                            │ SPSC ring
                                                            ▼
                                                        strategy
                                                            │
                                            risk checks ────┤
                                                            ▼
                                       order manager ─ WS / REST ─ OKX
```

## Requirements

Developed on macOS (Apple Silicon). Asio's reactor is portable across `kqueue`/`epoll`, so
nothing in the build is Linux-specific.

- macOS 14+, arm64, Xcode Command Line Tools (Apple clang 17+ / C++20)
- CMake 3.20+, Ninja
- OpenSSL 3, Boost (header-only Asio is enough)
- System `libcurl` (ships with the macOS SDK)

Absolute latency numbers measured here reflect a laptop without core isolation or hugepages,
so they are directionally correct but noisier than a tuned Linux box. Relative comparisons
between implementations — the point of the Phase C benchmarks — hold regardless.

## Build

```sh
cmake --preset release      # or: debug, asan
cmake --build --preset release
ctest --preset release
```

## Configuration

Credentials are read from the environment. OKX issues three, not two:

```sh
export OKX_API_KEY=...
export OKX_API_SECRET=...
export OKX_PASSPHRASE=...
```

## Safety

> **⚠ OKX demo trading shares the live hostname.** Simulated trading is selected by an
> `x-simulated-trading: 1` header, not by a separate endpoint. Omitting it sends a real order
> to the real exchange.

The request builder asserts the header is present and refuses to send without it. Beyond
that, run with an unfunded live account and use a demo-only API key, which the live endpoint
rejects. Credentials are never read from files inside the repository.

## Benchmarks

Not yet measured. Results will be reported as p50 / p99 / p99.9 / max with the measurement
boundary stated — never as averages.

| Metric | Boundary | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|
| Tick-to-trade | socket read return → order bytes at `write()` | — | — | — | — |
| Book update | frame decoded → book consistent | — | — | — | — |

---

# Implementation plan

## Conventions

Rules the implementation holds itself to:

1. **No protocol library.** No Beast, no websocketpp, no exchange SDK.
2. **Demo trading only** until the kill switch works — see *Safety*.
3. **No allocation on the hot path.** Fixed-capacity buffers and slabs, sized up front.
4. **No number is recorded until it has been measured**, with its boundary written next to it.
5. **Percentiles, never averages.**

Hour estimates below are planning figures, not commitments.

---

# Milestone A — First real order

**~15–21 hours.** End state: a limit order placed on OKX demo trading, filled,
and cancelled. Synchronous and ugly is fine.

### A0 · Environment — 3–4 h

- [x] Toolchain: Xcode Command Line Tools (clang, included), `brew install cmake ninja boost` — **0.5 h**
- [x] `openssl@3` via Homebrew; point CMake at it explicitly since macOS does not put it
      on the default include/lib path (`-DOPENSSL_ROOT_DIR`) — **0.5 h**
- [x] Repo skeleton + CMake presets: `debug`, `asan+ubsan`, `release` (`-O2 -march=native`) — **1.5 h**
- [x] OKX account → Assets → Start Demo Trading → Personal Center → Demo Trading API →
      create a V5 demo key. Record key / secret / **passphrase**; wire up env loading — **1 h**
- [x] *(optional)* GitHub Actions: build + test on push — **1 h**

### A1 · Transport (Asio reactor) — 2–3 h

Asio owns readiness and TLS; everything above the byte stream is in-tree.

- [x] `io_context`, resolver, `asio::ssl::stream<tcp::socket>`, cert verification, SNI — **1 h**
- [x] `TCP_NODELAY`; connect + TLS handshake to `wspap.okx.com:8443` — **0.5 h**
- [x] Smoke test: TLS connect, read bytes — **0.5 h**

> Fixed-capacity RX/TX ring buffers (originally scoped here) moved to B1 — there's nothing to
> accumulate into a ring until the WebSocket codec exists to read out of one. A1 only ever did a
> single one-shot blocking read into a throwaway stack buffer, which doesn't exercise ring
> semantics (write cursor, wraparound, multi-call accumulation) at all.

> Buffers stay engine-owned even though Asio could manage them — that ownership is what makes
> in-place parsing possible later. Asio's responsibility ends at "bytes arrived."

### A2 · REST client via libcurl — 1–2 h

- [x] Thin `RestClient` wrapper: `curl_easy` handle, method, path, headers, body, timeout — **1 h**
- [x] Reuse one handle per thread so connections stay pooled across calls — **0.5 h**

> Keep this behind a narrow interface (`Response get(path)` / `Response post(path, body)`).
> Moving the REST path onto the in-tree transport later then touches only this file.

### A3 · Auth and signing — 4–5 h

OKX signs differently from most exchanges: the signature covers the **body**, the timestamp
is **ISO 8601**, and the digest is **base64**, not hex.

```
prehash = OK-ACCESS-TIMESTAMP + METHOD + requestPath + body
        = "2026-08-05T09:08:57.715Z" + "GET" + "/api/v5/account/balance?ccy=BTC" + ""
sign    = base64( HMAC_SHA256(prehash, secret) )
```

- [x] ISO 8601 UTC timestamp with millisecond precision, `...Z` suffix — **0.5 h**
- [x] HMAC-SHA256 + base64 encoding via OpenSSL `EVP` — **1 h**
- [x] Prehash assembly — method uppercased, query string included in `requestPath`,
      body byte-identical to what is actually sent — **1 h**
- [x] Headers via `curl_slist`: `OK-ACCESS-KEY`, `OK-ACCESS-SIGN`, `OK-ACCESS-TIMESTAMP`,
      `OK-ACCESS-PASSPHRASE`, plus the `x-simulated-trading: 1` guard — **0.5 h**
- [x] Signed `GET /api/v5/account/balance`, parse it — **1.5 h**
- [x] Clock drift check against `GET /api/v5/public/time` — **0.5 h**

> Response parsing here is intentionally minimal — a one-off `ts`-field extraction for the
> clock-drift check, not general JSON parsing. The real targeted field scanner and two-level
> response envelope handling belong to A4; building them early here would be reaching ahead of
> scope.

### A4 · Order entry — 5–7 h

- [x] Targeted JSON field scanner — locate key, extract number/string in place, no DOM — **2 h**
- [x] Instrument specs from `GET /api/v5/public/instruments?instType=SPOT`:
      `tickSz`, `lotSz`, `minSz` — **1.5 h**
- [x] `POST /api/v5/trade/order` — `instId`, `tdMode: "cash"`, `side`, `ordType`, `px`,
      `sz`, `clOrdId` — **2 h**
- [x] `POST /api/v5/trade/cancel-order`, `GET /api/v5/trade/orders-pending` — **1 h**
- [x] Two-level response envelope handling — **1 h**

> **Two non-obvious failure modes.**
>
> *Response envelope:* HTTP 200 with a top-level `code: "0"` does **not** mean the order was
> accepted. Each entry in `data[]` carries its own `sCode`/`sMsg`. Both levels must be
> checked, or rejected orders are silently treated as live.
>
> *Market-buy sizing:* for spot, `sz` is in the base currency — except market buys, where it
> depends on `tgtCcy`. Set it explicitly; never rely on the default.

### Definition of done

- Places a limit order that rests, then cancels it.
- Rejects a malformed order locally (bad `tickSz` / below `minSz`) before sending it.
- Refuses to send at all if `x-simulated-trading` is absent.
- Runs clean under ASan and UBSan.

---

# Milestone B — Live order lifecycle

**~43–62 hours.** End state: an async engine holding two WebSocket connections
(`wspap.okx.com:8443` — public and private), placing orders over the authenticated private
connection, running a naive quoting strategy that survives disconnects, with risk limits and a
kill switch.

### B1 · WebSocket codec — 8–12 h

Written from RFC 6455.

- [x] Fixed-capacity RX/TX ring buffers behind Asio's read/write, engine-owned so parsing can
      happen in place off them — **1 h**
- [x] Handshake: random `Sec-WebSocket-Key`, base64, verify `Sec-WebSocket-Accept` — **2 h**
- [x] Frame decoder: FIN/opcode, 7 / 16 / 64-bit payload lengths — **3.5 h**
- [x] Frame encoder with mandatory client-side masking — **1.5 h**
- [ ] Control frames: ping/pong, close handshake — **1.5 h**
- [ ] OKX application-level heartbeat: send the literal text `ping`, expect `pong`.
      The connection drops after 30 s of silence, so run a <30 s timer — **0.5 h**
- [ ] Continuation-frame reassembly into a fixed-capacity per-connection buffer, sized for the
      largest expected message; a message that overflows it closes the connection rather than
      falling back to the heap — **1.5 h**
- [ ] Drive the codec from Asio completion handlers; reconnect with exponential backoff
      + jitter via `asio::steady_timer` — **1.5 h**

> OKX's heartbeat is a *text frame containing the word* `ping`, distinct from the RFC 6455
> ping opcode. Both are required — the protocol-level control frames and the application-level
> string. Implementing only one results in a disconnect every 30 seconds.

### B2 · Market data — 5–8 h

- [ ] Subscribe to the `books` and `trades` channels on the public endpoint — **1 h**
- [ ] Snapshot + incremental application, sequenced by `seqId` / `prevSeqId`
      (`prevSeqId` is `-1` on the initial snapshot) — **2.5 h**
- [ ] Pre-allocated L2 book: fixed-size sorted arrays, zero allocation on update — **2.5 h**
- [ ] Gap detection (`prevSeqId` ≠ last `seqId`) → resubscribe for a fresh snapshot — **1 h**
- [ ] *(if still present)* CRC32 `checksum` validation against the local book — **1 h**

> The snapshot arrives on the socket, so no separate REST snapshot fetch is needed and there
> is no deltas-buffered-while-fetching window to handle. `books` pushes every 100 ms;
> `books-l2-tbt` / `books50-l2-tbt` push every 10 ms but are VIP-gated on live accounts —
> confirm what the demo key can subscribe to before designing around them.
>
> OKX has been **deprecating the `checksum` field** in favour of `seqId`/`prevSeqId`.
> Sequencing is the primary path; checksum is a bonus where the channel still carries it.
> Verify against the current docs before writing the CRC32 code.

### B3 · Private channel — 5–7 h

Order and account updates arrive over an authenticated WebSocket session rather than a
polled or keepalive-maintained REST token.

- [ ] WS login: `op: "login"` with `apiKey` / `passphrase` / `timestamp` / `sign`.
      Timestamp here is **Unix epoch seconds as a string** — *not* the ISO 8601 format used
      for REST. Prehash is `timestamp + "GET" + "/users/self/verify"`. Expires after 30 s — **1.5 h**
- [ ] Subscribe to `orders`, `account`, `positions` — **1 h**
- [ ] `orders` channel → normalized ack / partial-fill / fill / reject events — **2 h**
- [ ] `account` channel → balance and position tracking — **1 h**
- [ ] On reconnect: re-login, resubscribe, reconcile against
      `GET /api/v5/trade/orders-pending` — **1.5 h**

> Two different timestamp formats and two different prehash strings for REST vs WebSocket
> auth is the single most common OKX integration bug. Write both signers side by side in one
> file with a comment explaining the difference.

### B4 · Order entry over the private WebSocket — 6–8 h

Orders go out on the session authenticated in B3. REST stays for cold-path work only — startup,
reconciliation, cancel-all — which is what the Design table already claims it is for.

- [ ] `op: "order"` request framing, `id` field generated and correlated to `clOrdId` — **2 h**
- [ ] `op: "cancel-order"`, `op: "amend-order"`; `batch-orders` for multi-leg requotes — **1.5 h**
- [ ] Response demux: match the ack's `id` back to the originating order, route to the
      state machine — **1.5 h**
- [ ] TX path: encode into the engine-owned TX ring, mask, one `write()` — **1 h**
- [ ] REST fallback when the private socket is down, behind the same risk checks — **1 h**

> This is what makes the tick-to-trade boundary honest. Over libcurl the send is a blocking
> `curl_easy_perform` sitting at the end of the execution loop, so a sub-millisecond number
> measured up to `write()` would be quietly excluding the slowest part of the path. Reusing the
> authenticated session removes the TLS handshake, HTTP framing and per-order signing entirely.
>
> The two-level envelope check from A4 applies identically here: a WS ack carries per-item
> `sCode`/`sMsg`, and both levels must be checked before treating an order as live.

### B5 · Order lifecycle — 6–8 h

- [ ] State machine: `PendingNew → New → PartiallyFilled → Filled | Canceled | Rejected`,
      plus `PendingCancel` / `PendingReplace` — **2.5 h**
- [ ] `clOrdId` generation: monotonic, unique across restarts, alphanumeric ≤32 chars — **1 h**
- [ ] Order store: pre-allocated slab + id→slot index, no per-order allocation — **1.5 h**
- [ ] Reconciliation: diff local state vs exchange, resolve divergence — **2 h**
- [ ] Timeouts for unacknowledged orders — **1 h**

### B6 · Risk — 3–4 h

- [ ] Pre-trade checks: max order size, max notional, price collar, max open orders — **1.5 h**
- [ ] Position and PnL tracking — **1 h**
- [ ] Kill switch: cancel-all + halt, triggered manually and automatically — **1.5 h**

### B7 · Strategy — 5–8 h

- [ ] Strategy interface: `on_book_update`, `on_fill`, `on_reject`, `on_timer` — **1 h**
- [ ] Naive quoter: post bid/ask at ±k bps, requote when mid moves past a threshold — **3 h**
- [ ] Cancel/replace logic, per-endpoint token-bucket rate limiter (OKX limits are
      per-endpoint requests-per-2s, not a global weight budget) — **2 h**
- [ ] Run it live for several hours; fix everything that breaks — **2 h**

### B8 · Observability — 3–4 h

- [ ] TSC timestamping + log-bucketed latency histogram — **2 h**
- [ ] Async logger: SPSC ring → writer thread, zero I/O on the hot path — **2 h**
- [ ] Tick-to-trade harness: wire arrival → order bytes handed to `write()` — **1 h**

### B9 · Documentation — 2–3 h

- [ ] Architecture diagram, measured latency table, run instructions — **2.5 h**

### Definition of done

- Runs unattended for 4+ hours, quoting and trading, surviving at least one forced disconnect
  (kill the socket by hand and watch it re-login, resubscribe and reconcile).
- Orders are placed, amended and cancelled over the private WebSocket; REST is used only for
  startup, reconciliation and cancel-all.
- Local order state matches the exchange after every reconnect.
- Kill switch cancels everything and halts within one second.
- README contains a tick-to-trade table with p50/p99/p99.9/max and a stated measurement boundary.

---

# Phase C — Order book implementations *(later)*

**~35–45 hours.** Sketch only; expand when Milestone B is done.

The point is a **bench harness** that replays one identical message stream through several
book implementations and reports latency percentiles for each.

| Impl | Structure | Purpose |
|---|---|---|
| 1 | `std::map<Price, Qty>` + `unordered_map<OrderId, Ref>` | Correctness oracle, baseline |
| 2 | Sorted `std::vector<Level>` | Cache locality; usually beats the map several-fold |
| 3 | Price-indexed ladder + occupancy bitset | O(1) update, O(1) top-of-book |
| 4 | Hybrid: hot ladder window + cold map | Handles wide price ranges |
| 5 | Order-by-order: id hash → intrusive FIFO list per level | Queue position |

Query specializations to support and measure:

- Top of book — O(1)
- Top **k** levels — bitset scan from the touch
- **kth** level specifically — Fenwick descent, O(log n), no walk
- Best VWAP for a target notional — Fenwick over `(qty, notional)`, O(log n)

Also implement update batching as a swappable policy — eager / batched / conflated /
adaptive (batch only while a backlog exists) — and measure throughput against staleness.

Data source: Nasdaq ITCH 5.0 sample files. Binary, big-endian, 48-bit timestamps,
order-by-order. Deterministic and large, so the benchmarks are reproducible.

---

# Phase D — Optional upgrades

- Upgrade to `books-l2-tbt` / `books50-l2-tbt` (10 ms vs 100 ms) where permitted — **3 h**
- Second venue behind the same gateway abstraction — **20 h**
- Raw `kqueue` (macOS) or `epoll`/`io_uring` (Linux) transport behind the same `Transport`
  interface, to quantify Asio's overhead — **6 h**
- Move the REST path off libcurl onto the same transport. Only `RestClient` changes.
  Low value, do it last — **6 h**

---

# Effort summary

| Phase | Hours | At 10 h/week |
|---|---|---|
| A — first real order | 15–21 | 2 weeks |
| B — live lifecycle | 43–62 | 4–6 weeks |
| **A + B (first trading build)** | **58–83** | **6–8 weeks** |
| C — order book work | 35–45 | 4 weeks |
| D — upgrades | optional | — |

The top of that range assumes every task independently hits its worst case, which does not
happen in practice. Plan around **~68 hours** for A + B and treat 83 as the tail.

---

# License

MIT.
