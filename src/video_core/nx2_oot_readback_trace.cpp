// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "common/logging.h"
#include "video_core/nx2_oot_readback_trace.h"

namespace VideoCommon::Nx2OotReadbackTrace {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// Second pages of the two alternating OoT depth buffers seen in the local captures.
constexpr std::array<DAddr, 2> PAGES{0x12eb95000, 0x12eb9a000};
constexpr u64 PAGE_SIZE = 0x1000;
constexpr size_t RECORD_LIMIT = 16384;
constexpr size_t PAYLOAD_LIMIT = 8 * 1024 * 1024;

struct TraceRecord {
    Event event;
    Clock::time_point time;
    DAddr address;
    u64 size;
    u64 detail;
    std::vector<u8> bytes;
};

struct TraceState {
    std::mutex mutex;
    std::deque<TraceRecord> records;
    size_t payload_size{};
    std::atomic<u64> dropped{};
    u64 evicted{};
};

TraceState& State() {
    static TraceState state;
    return state;
}

const std::filesystem::path& Directory() {
    static const auto directory = [] {
        const char* value = std::getenv("EDEN_NX2_OOT_READBACK_TRACE");
        return value ? std::filesystem::path{value} : std::filesystem::path{};
    }();
    return directory;
}

bool Intersects(DAddr address, u64 size, DAddr page) {
    return size != 0 && (address <= page ? page - address < size : address - page < PAGE_SIZE);
}

std::string_view Name(Event event) {
    constexpr std::array names{"guest_read", "reused_area", "preemptive", "flush_requested",
                               "flush_finished", "gpu_marked", "download", "excluded"};
    return names[static_cast<size_t>(event)];
}

std::string Hex(std::span<const u8> bytes) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const u8 byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0xf]);
    }
    return result;
}

} // namespace

bool Enabled() {
    return !Directory().empty();
}

bool Watches(DAddr addr, u64 size) {
    return Enabled() && std::ranges::any_of(PAGES, [&](DAddr page) {
        return Intersects(addr, size, page);
    });
}

void Record(Event event, DAddr addr, u64 size, std::span<const u8> bytes, u64 detail) {
    if (!Watches(addr, size)) {
        return;
    }
    auto& state = State();
    // Diagnostic events must not block the CPU or GPU on each other.
    std::unique_lock lock{state.mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
        state.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (const DAddr page : PAGES) {
        if (!Intersects(addr, size, page)) {
            continue;
        }
        const DAddr start = (std::max)(addr, page);
        const u64 offset = start - addr;
        const u64 length = (std::min)(size - offset, page + PAGE_SIZE - start);
        TraceRecord record{event, Clock::now(), start, length, detail, {}};
        if (offset < bytes.size()) {
            const auto payload = bytes.subspan(
                static_cast<size_t>(offset),
                static_cast<size_t>((std::min<u64>)(length, bytes.size() - offset)));
            record.bytes.assign(payload.begin(), payload.end());
        }
        while (state.records.size() >= RECORD_LIMIT ||
               state.payload_size + record.bytes.size() > PAYLOAD_LIMIT) {
            state.payload_size -= state.records.front().bytes.size();
            state.records.pop_front();
            ++state.evicted;
        }
        state.payload_size += record.bytes.size();
        state.records.push_back(std::move(record));
    }
}

void Poll() {
    if (!Enabled()) {
        return;
    }
    static std::atomic<Clock::time_point> next_poll{};
    const auto now = Clock::now();
    auto next = next_poll.load(std::memory_order_relaxed);
    if (now < next || !next_poll.compare_exchange_strong(next, now + 250ms,
                                                         std::memory_order_relaxed)) {
        return;
    }
    const auto trigger = Directory() / "readback.now";
    std::error_code error;
    if (!std::filesystem::is_regular_file(trigger, error)) {
        return;
    }
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const auto path = Directory() / fmt::format("readback-{}.jsonl", timestamp);
    std::ofstream output{path};
    if (!output) {
        LOG_WARNING(HW_GPU, "OoT readback trace could not open {}", path.string());
        return;
    }
    auto& state = State();
    std::deque<TraceRecord> records;
    u64 evicted;
    u64 dropped;
    {
        std::scoped_lock lock{state.mutex};
        records.swap(state.records);
        state.payload_size = 0;
        evicted = state.evicted;
        state.evicted = 0;
        dropped = state.dropped.exchange(0, std::memory_order_relaxed);
    }
    output << fmt::format(R"({{"kind":"summary","records":{},"evicted":{},"dropped":{}}})",
                          records.size(), evicted, dropped)
           << '\n';
    for (const auto& record : records) {
        const auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              record.time.time_since_epoch())
                              .count();
        output << fmt::format(
                      R"({{"kind":"{}","time_ns":{},"addr":"{:#x}","size":{},"detail":"{:#x}","bytes":"{}"}})",
                      Name(record.event), time, record.address, record.size, record.detail,
                      Hex(record.bytes))
               << '\n';
    }
    output.close();
    if (!output) {
        LOG_WARNING(HW_GPU, "OoT readback trace could not write {}", path.string());
        return;
    }
    std::filesystem::remove(trigger, error);
    if (error) {
        LOG_WARNING(HW_GPU, "OoT readback trace could not remove the trigger: {}", error.message());
    }
    LOG_INFO(HW_GPU, "OoT readback trace saved {} events to {}", records.size(), path.string());
}

} // namespace VideoCommon::Nx2OotReadbackTrace
