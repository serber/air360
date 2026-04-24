#include "air360/uploads/upload_prune_policy.hpp"

namespace air360 {

PruneDecision computePruneDecision(const PerBackendCursor& cursors) {
    PruneDecision decision;
    bool have_cursor = false;

    for (const auto& cursor : cursors) {
        if (!cursor.participatesInPruneQuorum()) {
            continue;
        }

        if (!have_cursor || cursor.acknowledged_sample_id < decision.prune_up_to) {
            decision.prune_up_to = cursor.acknowledged_sample_id;
            have_cursor = true;
        }
        ++decision.quorum_size;
    }

    if (!have_cursor) {
        decision.prune_up_to = 0U;
    }

    return decision;
}

bool shouldDemoteBackendToBestEffort(
    std::uint32_t consecutive_failures,
    std::uint64_t first_failure_uptime_ms,
    std::uint64_t now_uptime_ms) {
    if (consecutive_failures < kUploadBestEffortFailureThreshold ||
        first_failure_uptime_ms == 0U ||
        now_uptime_ms < first_failure_uptime_ms) {
        return false;
    }

    return now_uptime_ms - first_failure_uptime_ms >= kUploadBestEffortFailureWindowMs;
}

}  // namespace air360
