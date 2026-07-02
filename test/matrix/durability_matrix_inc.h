#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kDurabilityMatrixCases[] = {
    {"sync_put_survives_reopen", "sync", {"put k1=v1"}, "checkpoint_reopen", "ok", "k1", "v1", "", "", "", false, false},
    {"group_put_survives_reopen", "group", {"put g1=gv"}, "checkpoint_reopen", "ok", "g1", "gv", "", "", "", false, false},
    {"sync_put_after_checkpoint", "sync", {"put ck1=ckv"}, "checkpoint_reopen", "ok", "ck1", "ckv", "", "", "", false, false},
    {"missing_key_not_found", "sync", {}, "get missing", "fail", "missing", "", "not found", "", "", false, false},
    {"bad_epoch_rejected", "sync", {"put x=1", "corrupt superblock"}, "reopen", "fail", "", "", "CorruptSuperBlock", "", "", false, false},
};

inline constexpr int kDurabilityMatrixCaseCount = 5;
