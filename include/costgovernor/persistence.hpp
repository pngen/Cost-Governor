#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "costgovernor/budget.hpp"
#include "costgovernor/decision.hpp"
#include "costgovernor/policy.hpp"
#include "costgovernor/price.hpp"
#include "costgovernor/status.hpp"

namespace costgovernor {

// Historic realized-cost ledger entry (not current authority).
struct HistoryEntry {
  RequestId request;
  WorkloadId workload;
  std::int64_t completed_at_ms = 0;
  MoneyMicros realized_cost{0};
  std::int64_t attempts = 0;
  std::int64_t tokens = 0;
};

// Historic intervention ledger entry.
struct InterventionEntry {
  InterventionId id;
  InterventionKind kind = InterventionKind::NO_ACTION;
  InterventionLifecycle lifecycle = InterventionLifecycle::PROPOSED;
  InterventionGeneration generation;
  std::int64_t created_at_ms = 0;
};

// Durable snapshot. Dynamic evidence is never persisted as "current"; a
// recovered snapshot requires revalidation by construction.
struct DurableSnapshot {
  CoordinatorEpoch epoch;
  CostPolicy policy;
  PriceSchedule price_schedule;
  std::vector<Budget> budgets;
  std::vector<HistoryEntry> history;
  std::vector<InterventionEntry> interventions;
};

// Deterministic binary codec. Layout: MAGIC(8) VERSION(4) PAYLOAD_LEN(8)
// CRC32(payload). Unknown version, truncated payload, trailing garbage, and
// checksum mismatch all reject. All decoders are bounds-checked.
//
// Format versions:
//   v1 (Cost Governor 1.0.0): no WorkerId on a persisted PriceObservation.
//   v2 (Cost Governor 1.0.1): PriceObservation carries WorkerId before WorkerBootId.
struct SnapshotCodec {
  static constexpr std::uint32_t kVersion = 2;
  static constexpr const char* kMagic = "CGSNAP01";
  [[nodiscard]] static std::vector<std::uint8_t> encode(const DurableSnapshot& s);
  [[nodiscard]] static DurableSnapshot decode(const std::vector<std::uint8_t>& bytes);
  [[nodiscard]] static std::uint32_t crc32(const std::uint8_t* data, std::size_t n) noexcept;
};

// Atomic file-backed store: writes to a temp file then renames; rejects corrupt,
// truncated, trailing-garbage, or unknown-version content on load.
class StateStore {
 public:
  explicit StateStore(std::string path);
  Status save(const DurableSnapshot& s) const;
  // Loads; throws StatusError(PERSISTENCE_CORRUPT) on any integrity failure and
  // StatusError(PERSISTENCE_UNSUPPORTED_VERSION) on a v1 (Cost Governor 1.0.0) snapshot.
  DurableSnapshot load() const;
  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  static bool exists(const std::string& path);
  static bool remove(const std::string& path);
 private:
  std::string path_;
};

}  // namespace costgovernor
