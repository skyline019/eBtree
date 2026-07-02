#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kPagedMatrixCases[] = {
    {"paged_checkpoint_reopen", "sync", {"put pg1=pgv1", "put pg2=pgv2"}, "checkpoint_reopen", "ok", "pg2", "pgv2", "", "", "", false, false},
    {"paged_flush_reopen", "sync", {"put pf1=pfv1", "put pf2=pfv2"}, "flush_reopen", "ok", "pf2", "pfv2", "", "", "", false, false},
    {"paged_multi_key", "sync", {"put pm1=pmv1", "put pm2=pmv2", "put pm3=pmv3"}, "checkpoint_reopen", "ok", "pm3", "pmv3", "", "", "", false, false},
};

inline constexpr int kPagedMatrixCaseCount = 3;
