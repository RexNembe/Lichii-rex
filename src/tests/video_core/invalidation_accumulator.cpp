// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/invalidation_accumulator.h"

namespace {

std::vector<std::pair<GPUVAddr, size_t>> Ranges(VideoCommon::InvalidationAccumulator& accumulator) {
    std::vector<std::pair<GPUVAddr, size_t>> ranges;
    (void)accumulator.InvalidateAll(
        [&ranges](GPUVAddr address, size_t size) { ranges.emplace_back(address, size); });
    return ranges;
}

} // namespace

TEST_CASE("A cached word invalidates only the written bytes", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x1054, 4);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x1054);
    CHECK(ranges[0].second == 4);
}

TEST_CASE("Words written from mid-block on are all invalidated", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    for (GPUVAddr address = 0x2054; address < 0x2084; address += 4) {
        accumulator.Add(address, 4);
    }
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x2054);
    CHECK(ranges[0].second == 0x30);
}

TEST_CASE("A cached write spanning blocks keeps its original range", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x3018, 0x30);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x3018);
    CHECK(ranges[0].second == 0x30);
}

TEST_CASE("Cached writes preserve unwritten gaps", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x4020, 8);
    accumulator.Add(0x401c, 8);
    accumulator.Add(0x4030, 4);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0].first == 0x401c);
    CHECK(ranges[0].second == 0xc);
    CHECK(ranges[1].first == 0x4030);
    CHECK(ranges[1].second == 4);
}
