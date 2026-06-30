#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kConcurrentMatrixCases[] = {
    {"concurrent_checkpoint", "sync", {"put cx1=cxv1"}, "checkpoint_reopen", "ok", "cx1", "cxv1", "", "", "", false},
    {"concurrent_flush_reopen", "sync", {"put cy1=cyv1"}, "flush_reopen", "ok", "cy1", "cyv1", "", "", "", false},
    {"concurrent_wal_reopen", "sync", {"put cz1=czv1"}, "reopen", "ok", "cz1", "czv1", "", "", "", false},
};

inline constexpr int kConcurrentMatrixCaseCount = 3;
