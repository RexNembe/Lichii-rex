// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

class InvalidationAccumulator {
public:
    InvalidationAccumulator() = default;
    ~InvalidationAccumulator() = default;

    void Add(GPUVAddr address, size_t size) noexcept {
        if (size == 0) {
            return;
        }
        if (accumulated_size != 0) {
            const GPUVAddr end_address = start_address + accumulated_size;
            const GPUVAddr write_end = address + size;
            // The buffer cache treats these ranges as bytes written in guest memory.
            // A gap can still contain GPU-owned data.
            if (address <= end_address && start_address <= write_end) {
                start_address = (std::min)(start_address, address);
                accumulated_size = (std::max)(end_address, write_end) - start_address;
                return;
            }
            buffer.emplace_back(start_address, accumulated_size);
        }
        start_address = address;
        accumulated_size = size;
    }

    template <typename F>
    [[nodiscard]] bool InvalidateAll(F&& f) noexcept {
        if (accumulated_size != 0) {
            for (auto [address, size] : buffer)
                f(address, size);
            f(start_address, accumulated_size);
            buffer.clear();
            start_address = 0;
            accumulated_size = 0;
            return true;
        }
        return false;
    }

private:
    std::vector<std::pair<GPUVAddr, size_t>> buffer;
    GPUVAddr start_address = 0;
    size_t accumulated_size = 0;
};

} // namespace VideoCommon
