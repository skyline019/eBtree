#pragma once

#include "matrix_case.h"

inline const EbMatrixCase kShardMatrixCases[] = {
    {"four_shard_reopen", "sync", {"put sk0=sv0", "put sk1=sv1"}, "multishard_reopen", "ok", "sk0", "sv0", "", "", "", false},
    {"route_stable_key", "sync", {"put route_key=rv"}, "multishard_reopen", "ok", "route_key", "rv", "", "", "", false},
    {"sixteen_shard_reopen", "sync", {"put s16k=s16v"}, "multishard16_reopen", "ok", "s16k", "s16v", "", "", "", false},
    {"two_fifty_six_shard_reopen", "sync", {"put s256k=s256v"}, "multishard256_reopen", "ok", "s256k", "s256v", "", "", "", false},
    {"route_key_stable", "sync", {"put r256k=r256v"}, "multishard256_reopen", "ok", "r256k", "r256v", "", "", "", false},
};

inline constexpr int kShardMatrixCaseCount = 5;
