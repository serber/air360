#include "air360/uploads/upload_prune_policy.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace {

struct SampleState {
    std::uint64_t id = 0U;
    std::vector<bool> acknowledged_by_backend;
};

struct BackendModel {
    std::uint32_t id = 0U;
    bool enabled = true;
    bool configured = true;
    bool has_uploader = true;
    bool best_effort = false;
    std::uint64_t acknowledged_sample_id = 0U;
    std::uint32_t failures = 0U;
    std::uint64_t first_failure_uptime_ms = 0U;
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void requireEqual(
    std::uint64_t actual,
    std::uint64_t expected,
    const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        std::exit(1);
    }
}

air360::PerBackendCursor cursorsFrom(const std::vector<BackendModel>& backends) {
    air360::PerBackendCursor cursors;
    cursors.reserve(backends.size());
    for (const auto& backend : backends) {
        cursors.push_back(air360::BackendPruneCursor{
            backend.id,
            backend.enabled,
            backend.configured,
            backend.has_uploader,
            backend.best_effort,
            backend.acknowledged_sample_id,
        });
    }
    return cursors;
}

void acknowledgeTo(
    BackendModel& backend,
    std::size_t backend_index,
    std::uint64_t sample_id,
    std::vector<SampleState>& samples) {
    require(sample_id >= backend.acknowledged_sample_id, "ack cursor is monotonic");
    const std::uint64_t previous = backend.acknowledged_sample_id;
    backend.acknowledged_sample_id = sample_id;

    for (auto& sample : samples) {
        if (sample.id <= previous || sample.id > sample_id) {
            continue;
        }
        require(
            !sample.acknowledged_by_backend[backend_index],
            "sample is not acknowledged twice by one backend");
        sample.acknowledged_by_backend[backend_index] = true;
    }
}

std::uint64_t applyPrune(
    const std::vector<BackendModel>& backends,
    const std::vector<SampleState>& samples,
    std::uint64_t previous_prune_cursor) {
    const air360::PruneDecision decision = air360::computePruneDecision(cursorsFrom(backends));
    const std::uint64_t next_prune_cursor =
        std::max(previous_prune_cursor, decision.prune_up_to);
    require(next_prune_cursor >= previous_prune_cursor, "prune cursor is monotonic");

    if (decision.hasQuorum() && decision.prune_up_to > previous_prune_cursor) {
        for (const auto& sample : samples) {
            if (sample.id <= previous_prune_cursor || sample.id > decision.prune_up_to) {
                continue;
            }
            for (std::size_t index = 0U; index < backends.size(); ++index) {
                const auto& backend = backends[index];
                if (!backend.enabled || !backend.configured ||
                    !backend.has_uploader || backend.best_effort) {
                    continue;
                }
                require(
                    backend.acknowledged_sample_id >= sample.id,
                    "sample is pruned only after every quorum backend acknowledged it");
                require(
                    sample.acknowledged_by_backend[index],
                    "pruned sample has an explicit quorum acknowledgement");
            }
        }
    }

    return next_prune_cursor;
}

void testSingleBackendHappyPath() {
    const air360::PruneDecision decision = air360::computePruneDecision({
        air360::BackendPruneCursor{1U, true, true, true, false, 17U},
    });

    require(decision.hasQuorum(), "single enabled backend forms quorum");
    requireEqual(decision.prune_up_to, 17U, "single backend cursor is prune cursor");
}

void testTwoBackendsOnePermanentlyFailing() {
    require(
        !air360::shouldDemoteBackendToBestEffort(
            air360::kUploadBestEffortFailureThreshold - 1U,
            1000U,
            1000U + air360::kUploadBestEffortFailureWindowMs),
        "backend is not demoted before failure threshold");
    require(
        !air360::shouldDemoteBackendToBestEffort(
            air360::kUploadBestEffortFailureThreshold,
            1000U,
            1000U + air360::kUploadBestEffortFailureWindowMs - 1U),
        "backend is not demoted before failure window elapses");
    require(
        air360::shouldDemoteBackendToBestEffort(
            air360::kUploadBestEffortFailureThreshold,
            1000U,
            1000U + air360::kUploadBestEffortFailureWindowMs),
        "backend is demoted after threshold and time window");

    const air360::PruneDecision decision = air360::computePruneDecision({
        air360::BackendPruneCursor{1U, true, true, true, false, 40U},
        air360::BackendPruneCursor{2U, true, true, true, true, 0U},
    });
    requireEqual(decision.quorum_size, 1U, "best-effort backend is removed from quorum");
    requireEqual(decision.prune_up_to, 40U, "failing best-effort backend does not block pruning");
}

void testDisabledBackendAndRuntimeAddRemove() {
    air360::PruneDecision decision = air360::computePruneDecision({
        air360::BackendPruneCursor{1U, true, true, true, false, 12U},
        air360::BackendPruneCursor{2U, false, true, true, false, 0U},
    });
    requireEqual(decision.prune_up_to, 12U, "disabled backend does not block pruning");

    decision = air360::computePruneDecision({
        air360::BackendPruneCursor{1U, true, true, true, false, 12U},
        air360::BackendPruneCursor{2U, true, true, true, false, 4U},
    });
    requireEqual(decision.prune_up_to, 4U, "newly enabled backend joins quorum at its cursor");

    decision = air360::computePruneDecision({
        air360::BackendPruneCursor{1U, true, true, true, false, 20U},
        air360::BackendPruneCursor{2U, false, true, true, false, 4U},
    });
    requireEqual(decision.prune_up_to, 20U, "removed backend leaves quorum");
}

void testQueueFullBacklogFlushModel() {
    constexpr std::size_t kCapacity = 256U;
    std::vector<SampleState> queue;
    queue.reserve(kCapacity);
    std::uint32_t dropped = 0U;

    for (std::uint64_t id = 1U; id <= 300U; ++id) {
        if (queue.size() == kCapacity) {
            queue.erase(queue.begin());
            ++dropped;
        }
        queue.push_back(SampleState{id, std::vector<bool>{false}});
    }

    requireEqual(dropped, 44U, "queue model drops oldest samples beyond capacity");
    BackendModel backend{1U};
    acknowledgeTo(backend, 0U, queue.back().id, queue);
    const std::uint64_t prune_cursor = applyPrune({backend}, queue, 0U);
    requireEqual(prune_cursor, 300U, "backlog flush can prune through latest retained sample");
}

void testRandomizedFlappingBackendProperties() {
    std::vector<BackendModel> backends{
        BackendModel{1U},
        BackendModel{2U},
    };
    std::vector<SampleState> samples;
    samples.reserve(256U);
    std::uint64_t next_sample_id = 1U;
    std::uint64_t applied_prune_cursor = 0U;
    std::uint64_t now_ms = 1000U;
    std::mt19937 rng(0xA1360U);

    for (std::size_t step = 0U; step < 2000U; ++step) {
        const int op = static_cast<int>(rng() % 6U);
        now_ms += 30000U;

        if (op == 0 || samples.empty()) {
            if (samples.size() == 256U) {
                samples.erase(samples.begin());
            }
            samples.push_back(SampleState{
                next_sample_id++,
                std::vector<bool>(backends.size(), false),
            });
        } else if (op == 1) {
            const std::size_t index = static_cast<std::size_t>(rng() % backends.size());
            auto& backend = backends[index];
            if (backend.enabled && !backend.best_effort && !samples.empty()) {
                const std::uint64_t min_id = backend.acknowledged_sample_id;
                const std::uint64_t max_id = samples.back().id;
                if (max_id > min_id) {
                    const std::uint64_t span = max_id - min_id;
                    acknowledgeTo(backend, index, min_id + 1U + (rng() % span), samples);
                    backend.failures = 0U;
                    backend.first_failure_uptime_ms = 0U;
                }
            }
        } else if (op == 2) {
            auto& backend = backends[1U];
            ++backend.failures;
            if (backend.first_failure_uptime_ms == 0U) {
                backend.first_failure_uptime_ms = now_ms;
            }
            if (air360::shouldDemoteBackendToBestEffort(
                    backend.failures,
                    backend.first_failure_uptime_ms,
                    now_ms)) {
                backend.best_effort = true;
            }
        } else if (op == 3) {
            backends[1U].enabled = false;
        } else if (op == 4) {
            backends[1U].enabled = true;
        } else {
            auto& backend = backends[1U];
            backend.best_effort = false;
            backend.failures = 0U;
            backend.first_failure_uptime_ms = 0U;
        }

        applied_prune_cursor = applyPrune(backends, samples, applied_prune_cursor);
    }
}

}  // namespace

int main() {
    testSingleBackendHappyPath();
    testTwoBackendsOnePermanentlyFailing();
    testDisabledBackendAndRuntimeAddRemove();
    testQueueFullBacklogFlushModel();
    testRandomizedFlappingBackendProperties();
    std::cout << "upload_prune_policy tests passed\n";
    return 0;
}
