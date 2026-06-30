#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kPipelineMatrixCases[] = {
    {"group_put_survives_group_commit_reopen", "group", {"put gc1=gcv"}, "group_commit_reopen", "ok", "gc1", "gcv", "", "", "group_commit_total=1", false},
    {"async_put_needs_flush_for_stable", "async", {"put async1=av"}, "async_stable_check", "ok", "async1", "av", "", "", "flusher_flush_total=1", false},
    {"flush_order_wal_before_datafile", "sync", {"put fo1=fov"}, "flush_reopen", "ok", "fo1", "fov", "", "", "wal_append_total=1", false},
    {"auto_batch_group_commit", "group", {"put b1=v1", "put b2=v2"}, "auto_batch_group_commit", "ok", "", "", "", "", "group_commit_total=1", false},
    {"scan_before_flush", "sync", {"put sk=sv"}, "scan_before_flush", "ok", "sk", "sv", "", "", "", false},
    {"async_reopen_lost", "async", {"put lost_k=lost_v"}, "async_reopen_lost", "fail", "lost_k", "", "", "", "", false},
    {"memtable_3stage_flush", "sync", {"put mt_k=mt_v"}, "memtable_3stage_reopen", "ok", "mt_k", "mt_v", "", "", "", false},
};

inline constexpr int kPipelineMatrixCaseCount = 7;
