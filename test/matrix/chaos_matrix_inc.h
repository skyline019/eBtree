#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kChaosMatrixCases[] = {
    {"chaos_checkpoint_survives", "sync", {"put ch1=cv1", "put ch2=cv2"}, "checkpoint_reopen", "ok", "ch2", "cv2", "", "", "", false, false},
    {"chaos_wal_reopen", "sync", {"put cw1=cwv1"}, "reopen", "ok", "cw1", "cwv1", "", "", "", false, false},
    {"chaos_flush_reopen", "sync", {"put cf1=cfv1"}, "flush_reopen", "ok", "cf1", "cfv1", "", "", "", false, false},
};

inline constexpr int kChaosMatrixCaseCount = 3;
