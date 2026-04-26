#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace air360 {

constexpr std::uint32_t kUploadBestEffortFailureThreshold = 5U;
constexpr std::uint64_t kUploadBestEffortFailureWindowMs = 10ULL * 60ULL * 1000ULL;

struct BackendPruneCursor {
    std::uint32_t backend_id = 0U;
    bool enabled = false;
    bool configured = false;
    bool has_uploader = false;
    bool best_effort = false;
    std::uint64_t acknowledged_sample_id = 0U;

    bool participatesInPruneQuorum() const {
        return enabled && configured && has_uploader && !best_effort;
    }
};

using PerBackendCursor = std::vector<BackendPruneCursor>;

struct PruneDecision {
    std::uint64_t prune_up_to = 0U;
    std::size_t quorum_size = 0U;

    bool hasQuorum() const {
        return quorum_size > 0U;
    }
};

// INVARIANT 1: prune_up_to is the minimum acknowledged cursor across quorum
// backends, so a sample is pruned only after every quorum backend has accepted it.
// INVARIANT 2: disabled, unconfigured, missing-uploader, and best-effort backends
// are excluded from the quorum and cannot block pruning.
// INVARIANT 3: a backend that exceeds the consecutive-failure threshold over
// the configured time window can be demoted to best-effort and removed from
// quorum; UploadManager keeps a separate missed-sample counter for skipped work.
// INVARIANT 4: each backend acknowledgement cursor is monotonic, so a backend
// never acknowledges the same sample twice.
// INVARIANT 5: the caller must only apply decisions that advance its stored prune
// cursor; this function never invents a value outside the submitted cursors.
PruneDecision computePruneDecision(const PerBackendCursor& cursors);

bool shouldDemoteBackendToBestEffort(
    std::uint32_t consecutive_failures,
    std::uint64_t first_failure_uptime_ms,
    std::uint64_t now_uptime_ms);

}  // namespace air360
