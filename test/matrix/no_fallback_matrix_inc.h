#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kNoFallbackMatrixCases[] = {
    {"get_hit_no_fallback", "sync", {"put k=v"}, "checkpoint_scan", "ok", "k", "v", "", "", "fallback_read_total=0", false},
    {"missing_key_scan_no_fallback", "sync", {"put k=v"}, "scan_missing", "ok", "z", "", "", "", "fallback_read_total=0", false},
};

inline constexpr int kNoFallbackMatrixCaseCount = 2;
