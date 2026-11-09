// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"

namespace VideoCommon::Nx2OotReadbackTrace {

// Set EDEN_NX2_OOT_READBACK_TRACE to an existing directory. Creating readback.now there
// writes a snapshot of recent reads and downloads without submitting new GPU work.
enum class Event : u8 {
    GuestRead,
    ReusedArea,
    Preemptive,
    FlushRequested,
    FlushFinished,
    GpuMarked,
    Download,
    Excluded,
};

bool Enabled();
bool Watches(DAddr addr, u64 size);
void Record(Event event, DAddr addr, u64 size, std::span<const u8> bytes = {}, u64 detail = 0);
void Poll();

} // namespace VideoCommon::Nx2OotReadbackTrace
