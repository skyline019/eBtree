#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kRecoveryMatrixCases[] = {
    {"wal_replay_without_checkpoint", "sync", {"put w1=wv"}, "reopen", "ok", "w1", "wv", "", "", "", false, false},
    {"multi_key_wal_recovery", "sync", {"put a=1", "put b=2"}, "reopen", "ok", "b", "2", "", "", "", false, false},
    {"flush_then_reopen", "sync", {"put f1=fv"}, "flush_reopen", "ok", "f1", "fv", "", "", "", false, false},
    {"wal_corrupt_tlog_fallback", "sync", {"put tf1=tv"}, "checkpoint_corrupt_wal_reopen", "ok", "tf1", "tv", "", "", "", false, false},
    {"fast_open_reopen", "sync", {"put fo_k=fo_v"}, "fast_open_reopen", "ok", "fo_k", "fo_v", "", "", "", false, false},
    {"lazy_root_corrupt", "sync", {"put lr1=lv1"}, "lazy_root_reopen", "ok", "lr1", "lv1", "", "", "", false, false},
    {"full_replay_reopen", "sync", {"put fr_k=fr_v"}, "full_replay_reopen", "ok", "fr_k", "fr_v", "", "", "", false, false},
    {"balanced_fast_open_reopen", "balanced", {"put bf_k=bf_v"}, "fast_open_reopen", "ok", "bf_k", "bf_v", "", "", "", false, false},
};

inline constexpr int kRecoveryMatrixCaseCount = 8;
