#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kFlashbackMatrixCases[] = {
    {"flashback_at_checkpoint", "sync", {"put fb_k=fb_v0"}, "flashback_checkpoint", "ok", "fb_k", "fb_v0", "", "", "", false},
    {"flashback_scan_asof", "sync", {"put fs1=fsv1", "put fs2=fsv2"}, "flashback_checkpoint", "ok", "fs1", "fsv1", "", "", "", false},
    {"flashback_no_fallback", "sync", {"put fnf1=fnfv"}, "flashback_checkpoint", "ok", "fnf1", "fnfv", "", "", "fallback_read_total=0", false},
    {"flashback_before_first_snapshot", "sync", {"put bf_k=bf_v"}, "flashback_checkpoint", "not_found", "bf_k", "", "", "6999", "", false},
    {"flashback_corrupt_wal_reopen", "sync", {"put fcw_k=fcw_v"}, "flashback_corrupt_wal_reopen", "ok", "fcw_k", "fcw_v", "", "7100", "fallback_read_total=0", false},
    {"flashback_scan_two_versions", "sync", {"put fsv1=a1", "put fsv2=b1"}, "flashback_scan_two_checkpoint", "ok", "fsv1", "a1", "", "7200", "fallback_read_total=0", false},
    {"flashback_tlog_tail", "sync", {"put ft_k=ft_v1"}, "flashback_multi_checkpoint", "ok", "", "", "", "", "tlog_snapshot_total=2", false},
};

inline constexpr int kFlashbackMatrixCaseCount = 7;
