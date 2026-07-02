#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kWalIndexMatrixCases[] = {
    {"wal_index_lazy_get", "sync", {"put wi1=wiv1"}, "fast_open_reopen", "ok", "wi1", "wiv1", "", "", "wal_full_scan_total=0", false, false},
};

inline constexpr int kWalIndexMatrixCaseCount = 1;
