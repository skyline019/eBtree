#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kNoFallbackMatrixCases[] = {
    {"get_hit_no_fallback", "sync", {"put k=v"}, "checkpoint_scan", "ok", "k", "v", "", "", "unexpected_path_total=0", false, false},
    {"missing_key_scan_no_fallback", "sync", {"put k=v"}, "scan_missing", "ok", "z", "", "", "", "unexpected_path_total=0", false, false},
    {"memtable_get_no_fallback", "sync", {"put mk=mv"}, "get_only", "ok", "mk", "mv", "", "", "unexpected_path_total=0", false, false},
    {"checkpoint_reopen_no_fallback", "sync", {"put crk=crv"}, "checkpoint_reopen", "ok", "crk", "crv", "", "", "unexpected_path_total=0", false, false},
    {"fast_open_reopen_no_fallback", "balanced", {"put fok=fov"}, "fast_open_reopen", "ok", "fok", "fov", "", "", "unexpected_path_total=0", false, false},
    {"lazy_root_reopen_no_fallback", "sync", {"put lr1=lv1"}, "lazy_root_reopen", "ok", "lr1", "lv1", "", "", "unexpected_path_total=0", false, false},
    {"flashback_no_fallback", "sync", {"put fnf1=fnfv"}, "flashback_checkpoint", "ok", "fnf1", "fnfv", "", "", "unexpected_path_total=0", false, false},
    {"lazy_range_scan_no_fallback", "balanced", {"put lrk0=v0", "put lrk1=v1", "put lrk2=v2"}, "lazy_range_scan", "ok", "lrk0", "v0", "", "", "unexpected_path_total=0", false, false},
    {"flush_reopen_no_fallback", "sync", {"put frk=frv"}, "flush_reopen", "ok", "frk", "frv", "", "", "unexpected_path_total=0", false, false},
    {"group_commit_reopen_no_fallback", "group", {"put gck=gcv"}, "group_commit_reopen", "ok", "gck", "gcv", "", "", "unexpected_path_total=0", false, false},
    {"corrupt_wal_reopen_no_fallback", "sync", {"put cwk=cwv"}, "checkpoint_corrupt_wal_reopen", "ok", "cwk", "cwv", "", "", "unexpected_path_total=0", false, false},
    {"memtable_3stage_no_fallback", "sync", {"put ms1=msv1"}, "memtable_3stage_reopen", "ok", "ms1", "msv1", "", "", "unexpected_path_total=0", false, false},
    {"full_replay_no_fallback", "sync", {"put frp=frpv"}, "full_replay_reopen", "ok", "frp", "frpv", "", "", "unexpected_path_total=0", false, false},
    {"async_stable_no_fallback", "async", {"put ask=asv"}, "async_stable_check", "ok", "ask", "asv", "", "", "unexpected_path_total=0", false, false},
    {"balanced_puts_no_fallback", "balanced", {"put bp0=v"}, "balanced_concurrent_puts", "ok", "", "", "", "", "unexpected_path_total=0", false, false},
    {"get_missing_no_fallback", "sync", {"put gmk=gmv"}, "get missing", "not_found", "missing_key", "", "", "", "unexpected_path_total=0", false, false},
    {"snapshot_vcs_get", "sync", {"put sk=sv1"}, "snapshot_vcs_get", "ok", "sk", "sv1", "", "", "unexpected_path_total=0", false, false},
    {"snapshot_vcs_scan", "sync", {"put sk0=sv0", "put sk1=sv1"}, "snapshot_vcs_scan", "ok", "sk0", "sv0", "", "", "unexpected_path_total=0", false, false},
    {"compress_checkpoint_reopen", "balanced", {"put_large ck=z"}, "checkpoint_reopen", "ok", "ck", "LARGE", "", "", "unexpected_path_total=0", false, true},
    {"compress_lazy_scan", "balanced", {"put_large lrk0=z", "put_large lrk1=z", "put_large lrk2=z"}, "lazy_range_scan", "ok", "lrk0", "z", "", "", "unexpected_path_total=0", false, true},
    {"compress_fast_open", "balanced", {"put_large fok=z"}, "fast_open_reopen", "ok", "fok", "LARGE", "", "", "unexpected_path_total=0", false, true},
    {"compress_flush_reopen", "balanced", {"put_large frk=z"}, "flush_reopen", "ok", "frk", "LARGE", "", "", "unexpected_path_total=0", false, true},
    {"compress_get_only", "balanced", {"put mk=mv"}, "get_only", "ok", "mk", "mv", "", "", "unexpected_path_total=0", false, true},
};

inline constexpr int kNoFallbackMatrixCaseCount = 23;
