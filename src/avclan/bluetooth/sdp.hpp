// Copyright (C) 2026 Allen Hill <allenofthehills@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <span>

#include "btstack.h" // IWYU pragma: export

namespace avclan::bluetooth {

inline void register_sdp_record(std::span<uint8_t> record,
                                [[maybe_unused]] const char *name) {
#ifndef NDEBUG
  printf("%s SDP record length: %" PRIu32 " B\n", name,
         de_get_len(record.data()));
#endif
  btstack_assert(de_get_len(record.data()) <= record.size());
  sdp_register_service(record.data());
}

} // namespace avclan::bluetooth
