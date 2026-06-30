#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kBalancedMatrixCases[] = {
    {"balanced_put_reopen", "balanced", {"put bk=bv"}, "reopen", "ok", "bk", "bv", "", "", "stable_lsn=1", false},
};

inline constexpr int kBalancedMatrixCaseCount = 1;
