#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kOndiskMatrixCases[] = {
    {"ondisk_checkpoint_reopen", "sync", {"put od1=odv1", "put od2=odv2"}, "checkpoint_reopen", "ok", "od2", "odv2", "", "", "", false, false},
    {"ondisk_open_pages_touched", "sync", {"put op1=opv1", "put op2=opv2"}, "checkpoint_reopen", "ok", "op1", "opv1", "", "", "pages_touched=1", false, false},
    {"ondisk_compress_pages_reopen", "sync", {"put cp1=cpv1", "put cp2=cpv2"}, "checkpoint_reopen", "ok", "cp2", "cpv2", "", "", "", true, false},
};

inline constexpr int kOndiskMatrixCaseCount = 3;
