#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "costgovernor/budget.hpp"
#include "costgovernor/clock.hpp"
#include "costgovernor/decision.hpp"
#include "costgovernor/persistence.hpp"
#include "costgovernor/plan.hpp"
#include "costgovernor/policy.hpp"
#include "costgovernor/price.hpp"
#include "costgovernor/status.hpp"

namespace costgovernor {

// A generation-fenced budget reservation. It is *not* consumption until it is
// committed. reservation is releasable and expiry-aware.
struct BudgetReservation {
  ReservationId id;
  BudgetId budget;
  BudgetGeneration budget_generation;
  CoordinatorEpoch epoch;
  MoneyMicros amount;
  std::int64_t created_at_ms = 0;
  std::int64_t expires_at_ms = 0;
  enum class State : std::uint8_t { ACTIVE = 0, COMMITTED = 1, RELEASED = 2, EXPIRED = 3 };
  State state = State::ACTIVE;
};

// Cost Governor: the runtime authority boundary for execution economics.
//
// Single-mutex synchronization, no nested locks, and no external waits (I/O,
// network, adapters) performed while holding the state lock. RAII ownership of
// all resources. No sockets or locks exposed.
class CostGovernor {
 public:
  explicit CostGovernor(std::shared_ptr<Clock> clock = std::make_shared<SystemClock>());
  ~CostGovernor();

  CostGovernor(const CostGovernor&) = delete;
  CostGovernor& operator=(const CostGovernor&) = delete;

  // --- configuration / authority -----------------------------------------
  void set_policy(const CostPolicy& policy);
  [[nodiscard]] const CostPolicy& policy() const;
  void set_price_schedule(const PriceSchedule& schedule);
  [[nodiscard]] const PriceSchedule& price_schedule() const;
  Status create_budget(const Budget& budget);
  [[nodiscard]] CoordinatorEpoch epoch() const;
  void advance_epoch();                 // fresh epoch on coordinator restart
  void set_worker_boot(WorkerId worker, WorkerBootId boot);  // per-worker fresh boot on worker restart
  [[nodiscard]] WorkerBootId worker_boot(WorkerId worker) const;

  // --- price evidence intake (workers publish priced evidence) -----------
  Status publish_price(const PriceObservation& obs);   // boot- and epoch-fenced

  // --- realized cost evidence (per attempt) ------------------------------
  Status record_attempt(const CostEvidence& evidence);

  // --- budget accounting --------------------------------------------------
  Status reserve(const BudgetId& budget, const BudgetGeneration& gen,
                 const CoordinatorEpoch& epoch, MoneyMicros amount, BudgetReservation& out);
  Status commit(BudgetReservation& reservation);   // idempotent; never double-charges
  Status release(BudgetReservation& reservation);  // never exceeds reservation
  Status consume(const BudgetId& budget, const BudgetGeneration& gen,
                 const CoordinatorEpoch& epoch, MoneyMicros amount);

  // --- plan economics ------------------------------------------------------
  PlanEconomics evaluate_plan(const ExecutionPlan& plan);
  CostDecision evaluate(const WorkloadId& workload, const WorkloadGeneration& workload_gen,
                        const std::vector<ExecutionPlan>& plans);

  // --- pre-dispatch revalidation ------------------------------------------
  // Re-runs authority, freshness, and feasibility checks against *current*
  // state. Returns Status::OK when the current plan is still dispatchable; a
  // stale one is never AUTHORIZE. No state is mutated on rejection.
  Status validate_for_dispatch(const CostDecision& prior,
                               const std::vector<ExecutionPlan>& candidate_plans);

  // --- interventions -------------------------------------------------------
  Status propose_intervention(CostIntervention& intervention);
  Status authorize_intervention(const InterventionId& id, const InterventionGeneration& gen);
  Status acknowledge_intervention(const InterventionId& id, const InterventionGeneration& gen);
  VerificationOutcome verify_intervention(const InterventionId& id, const InterventionGeneration& gen,
                                          const std::vector<CostEvidence>& post_evidence);
  [[nodiscard]] std::vector<CostIntervention> interventions() const;

  // --- persistence ---------------------------------------------------------
  Status persist() const;
  Status load();
  [[nodiscard]] static bool state_exists(const std::string& path);

  // --- queries --------------------------------------------------------------
  [[nodiscard]] std::vector<Budget> budgets() const;
  [[nodiscard]] std::optional<Budget> find_budget(const BudgetId& id) const;
  [[nodiscard]] std::vector<CostEvidence> attempts(const RequestId& request) const;
  [[nodiscard]] std::vector<HistoryEntry> history() const;
  Status finalize_request(const RequestId& request, const WorkloadId& workload);  // appends realized cost once
  [[nodiscard]] MoneyMicros realized_request_cost(const RequestId& request) const;

  // Storage path (for persistence).
  void set_state_path(std::string path);

 private:
  PlanEconomics evaluate_plan_locked(const ExecutionPlan& plan) const;
  CostState state_from_breakdown(const CostBreakdown& bd) const;
  WorkerBootId current_boot_for(WorkerId worker) const;   // caller holds the state lock
  AuthorityContext authority_snapshot_from(const ExecutionPlan& plan,
                                           const WorkloadGeneration& workload_gen) const;
  void build_explanation(CostDecision& d, const ExecutionPlan& best, const PlanEconomics& econ,
                         const WorkloadId& workload, const WorkloadGeneration& workload_gen,
                         const std::vector<ExecutionPlan>& plans) const;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace costgovernor
