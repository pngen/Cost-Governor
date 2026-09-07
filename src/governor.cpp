#include "costgovernor/governor.hpp"
#include <algorithm>
#include <optional>
#include "costgovernor/cost.hpp"

namespace costgovernor {

namespace {
PriceKind kind_for_check(CostComponent c) {
  switch (c) {
    case CostComponent::ACCELERATOR_TIME: return PriceKind::ACCELERATOR_TIME;
    case CostComponent::ENERGY: return PriceKind::ENERGY;
    case CostComponent::TRANSFER: return PriceKind::TRANSFER;
    case CostComponent::MEMORY: return PriceKind::MEMORY_HOLDING;
    case CostComponent::RESIDENCY: return PriceKind::RESIDENCY_HOLDING;
    case CostComponent::STORAGE: return PriceKind::STORAGE;
    default: return PriceKind::CUSTOM_TYPED_COMPONENT;
  }
}
struct PriceCheck { bool present = false; bool stale = false; const PriceObservation* obs = nullptr; };
bool in_interval(const PriceObservation& o, std::int64_t now_ms) {
  if (now_ms < o.valid_from_ms) return false;
  if (o.valid_to_ms != 0 && now_ms >= o.valid_to_ms) return false;
  return true;
}
PriceCheck check_price(const PriceSchedule& s, PriceKind want, const std::string& currency,
                       std::int64_t now_ms, const CostPolicy& pol,
                       DeviceId dev = DeviceId(), ResourceId res = ResourceId()) {
  PriceCheck out;
  bool schedule_stale = pol.price_schedule_max_age_ms > 0 &&
                        (now_ms - s.created_at_ms) > pol.price_schedule_max_age_ms;
  const PriceObservation* best = nullptr;
  for (const auto& obs : s.observations) {
    if (obs.kind != want) continue;
    if (!obs.valid_currency(currency)) continue;
    if (!in_interval(obs, now_ms)) continue;
    if (!pol.accepts_label(obs.label)) continue;
    if (obs.amount.negative()) continue;
    if (dev.valid() && obs.device.valid() && obs.device != dev) continue;
    if (res.valid() && obs.resource_scope.valid() && obs.resource_scope != res) continue;
    if (best == nullptr || (obs.device.valid() && !best->device.valid()) ||
        (obs.resource_scope.valid() && !best->resource_scope.valid()))
      best = &obs;
  }
  if (best == nullptr) return out;
  out.present = true;
  out.obs = best;
  bool obs_stale = pol.max_evidence_age_ms > 0 &&
                   ((now_ms - best->observed_at_ms) > pol.max_evidence_age_ms);
  out.stale = schedule_stale || obs_stale;
  return out;
}
bool amount_present(const ExecutionPlan& p, CostComponent c) {
  switch (c) {
    case CostComponent::ACCELERATOR_TIME: return p.expected_accelerator_time.count() != 0;
    case CostComponent::ENERGY: return p.expected_energy.count() != 0;
    case CostComponent::TRANSFER: return p.expected_transfer.count() != 0;
    case CostComponent::MEMORY: return p.expected_memory_hold.count() != 0;
    case CostComponent::RESIDENCY: return p.expected_residency_hold.count() != 0;
    case CostComponent::STORAGE: return p.expected_storage.count() != 0;
    default: return false;
  }
}
CostEvidence plan_to_evidence(const ExecutionPlan& p) {
  CostEvidence ev;
  ev.accelerator_time = p.expected_accelerator_time;
  ev.energy = p.expected_energy;
  ev.transfer = p.expected_transfer;
  ev.memory_hold = p.expected_memory_hold;
  ev.residency_hold = p.expected_residency_hold;
  ev.storage_bytes = p.expected_storage;
  ev.requests = p.expected_requests;
  ev.tokens = p.expected_tokens;
  ev.operations = p.expected_operations;
  ev.label = p.label;
  ev.recompute_ns = p.recovery_recompute_ns;
  ev.device = p.device;
  ev.resource = p.resource;
  return ev;
}
}  // namespace

struct CostGovernor::Impl {
  std::shared_ptr<Clock> clock;
  mutable std::mutex mtx;
  CostPolicy policy;
  PriceSchedule schedule;
  CoordinatorEpoch epoch;
  std::map<WorkerId, WorkerBootId> worker_boots;  // per-worker current incarnation
  bool revalidation_needed = false;
  std::unordered_map<BudgetId, Budget> budgets;
  std::unordered_map<RequestId, std::map<AttemptId, CostEvidence>> attempts;
  std::unordered_map<BudgetId, std::vector<BudgetReservation>> reservations;
  std::vector<HistoryEntry> history;
  std::unordered_map<InterventionId, CostIntervention> interventions;
  std::uint64_t next_reservation = 1;
  std::uint64_t next_intervention = 1;
  std::unique_ptr<StateStore> store;
  Impl(std::shared_ptr<Clock> c) : clock(std::move(c)) {
    policy.id = CostPolicyId(1);
    policy.generation = CostPolicyGeneration(1);
    policy.epoch = CoordinatorEpoch(1);
    epoch = CoordinatorEpoch(1);
  }
};

CostGovernor::CostGovernor(std::shared_ptr<Clock> clock)
    : impl_(std::make_unique<Impl>(std::move(clock))) {}
CostGovernor::~CostGovernor() = default;
void CostGovernor::set_state_path(std::string path) {
  impl_->store = std::make_unique<StateStore>(std::move(path));
}

void CostGovernor::set_policy(const CostPolicy& p) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  impl_->policy = p;
  impl_->epoch = p.epoch;
  impl_->policy.epoch = p.epoch;
  impl_->revalidation_needed = true;
}
const CostPolicy& CostGovernor::policy() const { return impl_->policy; }

void CostGovernor::set_price_schedule(const PriceSchedule& s) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  impl_->schedule = s;
  impl_->revalidation_needed = false;
}
const PriceSchedule& CostGovernor::price_schedule() const { return impl_->schedule; }

Status CostGovernor::create_budget(const Budget& budget) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  if (budget.limit.negative()) return Status::INVALID_INPUT;
  if (budget.currency != impl_->policy.currency) return Status::CURRENCY_MISMATCH;
  if (budget.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (budget.policy_generation != impl_->policy.generation) return Status::STALE_AUTHORITY;
  if (impl_->budgets.count(budget.id)) return Status::INVALID_INPUT;
  Budget b = budget;
  b.lifecycle = BudgetLifecycle::ACTIVE;
  impl_->budgets.emplace(b.id, std::move(b));
  return Status::OK;
}

CoordinatorEpoch CostGovernor::epoch() const { return impl_->epoch; }
void CostGovernor::advance_epoch() {
  std::lock_guard<std::mutex> g(impl_->mtx);
  impl_->epoch = CoordinatorEpoch(impl_->epoch.value() + 1);
  impl_->revalidation_needed = true;
}
void CostGovernor::set_worker_boot(WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  impl_->worker_boots[worker] = boot;
}
WorkerBootId CostGovernor::worker_boot(WorkerId worker) const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->worker_boots.find(worker);
  return it == impl_->worker_boots.end() ? WorkerBootId() : it->second;
}
WorkerBootId CostGovernor::current_boot_for(WorkerId worker) const {
  auto it = impl_->worker_boots.find(worker);
  return it == impl_->worker_boots.end() ? WorkerBootId() : it->second;
}

Status CostGovernor::publish_price(const PriceObservation& obs) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  if (obs.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (obs.boot.valid() && obs.boot != current_boot_for(obs.worker)) return Status::STALE_AUTHORITY;
  if (!obs.valid_currency(impl_->policy.currency)) return Status::CURRENCY_MISMATCH;
  if (obs.amount.negative()) return Status::INVALID_INPUT;
  for (auto& o : impl_->schedule.observations) {
    if (o.id == obs.id) { o = obs; impl_->schedule.generation = PriceScheduleGeneration(impl_->schedule.generation.value() + 1); impl_->revalidation_needed = false; return Status::OK; }
  }
  impl_->schedule.observations.push_back(obs);
  impl_->schedule.generation = PriceScheduleGeneration(impl_->schedule.generation.value() + 1);
  impl_->revalidation_needed = false;
  return Status::OK;
}

Status CostGovernor::record_attempt(const CostEvidence& evidence) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  if (evidence.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (evidence.boot != current_boot_for(evidence.worker)) return Status::STALE_AUTHORITY;
  if (evidence.label == DataLabel::UNSUPPORTED) return Status::INVALID_INPUT;
  if (evidence.accelerator_time.count() < 0 || evidence.energy.count() < 0 ||
      evidence.transfer.count() < 0 || evidence.memory_hold.count() < 0 ||
      evidence.residency_hold.count() < 0 || evidence.storage_bytes.count() < 0 ||
      evidence.recompute_ns < 0 || evidence.tokens < 0 || evidence.operations < 0)
    return Status::INVALID_INPUT;
  auto& req = impl_->attempts[evidence.request];
  auto it = req.find(evidence.attempt);
  if (it != req.end()) {
    if (it->second.attempt_gen == evidence.attempt_gen) return Status::INVALID_INPUT;  // duplicate charge
    it->second = evidence;
    return Status::OK;
  }
  req.emplace(evidence.attempt, evidence);
  return Status::OK;
}

Status CostGovernor::reserve(const BudgetId& budget, const BudgetGeneration& gen,
                             const CoordinatorEpoch& epoch, MoneyMicros amount,
                             BudgetReservation& out) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->budgets.find(budget);
  if (it == impl_->budgets.end()) return Status::INVALID_INPUT;
  Budget& b = it->second;
  if (b.generation != gen || b.epoch != epoch) return Status::STALE_AUTHORITY;
  if (b.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (!b.is_live()) return Status::BUDGET_EXPIRED;
  if (b.expires_at_ms != 0 && impl_->clock->now_ms() > b.expires_at_ms) { b.lifecycle = BudgetLifecycle::EXPIRED; return Status::BUDGET_EXPIRED; }
  if (amount.negative()) return Status::INVALID_INPUT;
  MoneyMicros reserved_after = b.reserved + amount;
  if (reserved_after > b.limit) return Status::BUDGET_EXCEEDED;
  b.reserved = reserved_after;
  b.lifecycle = BudgetLifecycle::RESERVED;
  BudgetReservation r;
  r.id = ReservationId(impl_->next_reservation++);
  r.budget = budget; r.budget_generation = gen; r.epoch = epoch; r.amount = amount;
  r.created_at_ms = impl_->clock->now_ms(); r.expires_at_ms = b.expires_at_ms;
  impl_->reservations[budget].push_back(r);
  out = r;
  return Status::OK;
}

Status CostGovernor::commit(BudgetReservation& reservation) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->budgets.find(reservation.budget);
  if (it == impl_->budgets.end()) return Status::INVALID_INPUT;
  Budget& b = it->second;
  if (b.generation != reservation.budget_generation) return Status::STALE_AUTHORITY;
  if (reservation.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (b.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (reservation.state != BudgetReservation::State::ACTIVE) return Status::OK;  // idempotent
  if (!b.is_live()) return Status::BUDGET_EXPIRED;
  if (reservation.expires_at_ms != 0 && impl_->clock->now_ms() > reservation.expires_at_ms) {
    reservation.state = BudgetReservation::State::EXPIRED;
    b.reserved -= reservation.amount; b.released += reservation.amount;
    return Status::BUDGET_EXPIRED;
  }
  MoneyMicros consumed_after = b.consumed + reservation.amount;
  if (b.mode == BudgetMode::HARD && consumed_after > b.limit) return Status::BUDGET_EXCEEDED;
  b.reserved -= reservation.amount;
  b.consumed = consumed_after;
  reservation.state = BudgetReservation::State::COMMITTED;
  b.lifecycle = (b.consumed < b.limit) ? BudgetLifecycle::PARTIALLY_CONSUMED : BudgetLifecycle::EXHAUSTED;
  return Status::OK;
}

Status CostGovernor::release(BudgetReservation& reservation) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->budgets.find(reservation.budget);
  if (it == impl_->budgets.end()) return Status::INVALID_INPUT;
  Budget& b = it->second;
  if (b.generation != reservation.budget_generation) return Status::STALE_AUTHORITY;
  if (reservation.epoch != impl_->epoch) return Status::STALE_AUTHORITY;
  if (reservation.state != BudgetReservation::State::ACTIVE) return Status::OK;
  b.reserved -= reservation.amount;
  b.released += reservation.amount;
  reservation.state = BudgetReservation::State::RELEASED;
  bool any_active = false;
  for (const auto& r : impl_->reservations[reservation.budget]) if (r.state == BudgetReservation::State::ACTIVE) any_active = true;
  b.lifecycle = any_active ? BudgetLifecycle::RESERVED
             : (b.consumed.is_zero() ? BudgetLifecycle::ACTIVE : BudgetLifecycle::PARTIALLY_CONSUMED);
  return Status::OK;
}

Status CostGovernor::consume(const BudgetId& budget, const BudgetGeneration& gen,
                             const CoordinatorEpoch& epoch, MoneyMicros amount) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->budgets.find(budget);
  if (it == impl_->budgets.end()) return Status::INVALID_INPUT;
  Budget& b = it->second;
  if (b.generation != gen || b.epoch != epoch) return Status::STALE_AUTHORITY;
  if (!b.is_live()) return Status::BUDGET_EXPIRED;
  if (amount.negative()) return Status::INVALID_INPUT;
  if (b.expires_at_ms != 0 && impl_->clock->now_ms() > b.expires_at_ms) { b.lifecycle = BudgetLifecycle::EXPIRED; return Status::BUDGET_EXPIRED; }
  MoneyMicros consumed_after = b.consumed + amount;
  if (b.mode == BudgetMode::HARD && consumed_after > b.limit) return Status::BUDGET_EXCEEDED;
  b.consumed = consumed_after;
  b.lifecycle = (b.consumed < b.limit) ? BudgetLifecycle::PARTIALLY_CONSUMED : BudgetLifecycle::EXHAUSTED;
  return Status::OK;
}

PlanEconomics CostGovernor::evaluate_plan(const ExecutionPlan& plan) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  return evaluate_plan_locked(plan);
}
PlanEconomics CostGovernor::evaluate_plan_locked(const ExecutionPlan& plan) const {
  PlanEconomics e;
  const Impl& ig = *impl_;
  if (plan.label == DataLabel::UNSUPPORTED) { e.feasible = Feasibility::UNKNOWN; e.reason = FeasibilityReason::UNKNOWN_COST_COMPONENT; return e; }
  if (!plan.meets_hard_slo) { e.feasible = Feasibility::INFEASIBLE; e.reason = FeasibilityReason::SLO_CONFLICT; return e; }
  if (ig.revalidation_needed) { e.feasible = Feasibility::REVALIDATION_REQUIRED; e.reason = FeasibilityReason::STALE_PRICE; return e; }
  std::int64_t now = ig.clock->now_ms();
  const std::string& cur = ig.policy.currency;
  const CostComponent comps[6] = {CostComponent::ACCELERATOR_TIME, CostComponent::ENERGY, CostComponent::TRANSFER,
                                  CostComponent::MEMORY, CostComponent::RESIDENCY, CostComponent::STORAGE};
  for (CostComponent c : comps) {
    if (!amount_present(plan, c)) continue;
    PriceCheck pc = check_price(ig.schedule, kind_for_check(c), cur, now, ig.policy, plan.device, plan.resource);
    if (!pc.present) { e.feasible = Feasibility::INSUFFICIENT_EVIDENCE; e.reason = FeasibilityReason::MISSING_PRICE; return e; }
    if (pc.stale) { e.feasible = Feasibility::REVALIDATION_REQUIRED; e.reason = FeasibilityReason::STALE_PRICE; return e; }
  }
  std::vector<CostEvidence> attempts{plan_to_evidence(plan)};
  CostBreakdown bd;
  try { bd = CostCalculator::project(ig.schedule, attempts, cur, now); }
  catch (const StatusError& se) {
    if (se.status() == Status::MISSING_PRICE) { e.feasible = Feasibility::INSUFFICIENT_EVIDENCE; e.reason = FeasibilityReason::MISSING_PRICE; return e; }
    throw;
  }
  if (bd.any_unknown) { e.feasible = Feasibility::UNKNOWN; e.reason = FeasibilityReason::UNKNOWN_COST_COMPONENT; return e; }
  e.projected = bd;
  e.projected_per_request = plan.expected_requests > 0 ? bd.total.checked_div(plan.expected_requests, MoneyMicros::Rounding::kCeil) : MoneyMicros(0);
  if (plan.expected_tokens > 0) { e.projected_per_token = bd.total.checked_div(plan.expected_tokens, MoneyMicros::Rounding::kCeil); e.per_token_valid = true; }
  for (const auto& [bid, b] : ig.budgets) {
    (void)bid;
    if (b.mode == BudgetMode::HARD && bd.total > b.remaining()) {
      e.feasible = Feasibility::INFEASIBLE;
      e.reason = b.objective == BudgetObjective::MAX_ENERGY_COST ? FeasibilityReason::ENERGY_TOO_EXPENSIVE
               : b.objective == BudgetObjective::MAX_TRANSFER_COST ? FeasibilityReason::TRANSFER_TOO_EXPENSIVE
               : FeasibilityReason::HARD_BUDGET_EXCEEDED;
      return e;
    }
  }
  e.feasible = Feasibility::FEASIBLE;
  return e;
}

CostState CostGovernor::state_from_breakdown(const CostBreakdown& bd) const {
  const Impl& ig = *impl_;
  CostState st = CostState::WITHIN_BUDGET;
  for (const auto& [bid, b] : ig.budgets) {
    (void)bid;
    if (b.mode != BudgetMode::HARD || b.limit.is_zero()) continue;
    if (bd.total > b.remaining()) return CostState::OVER_HARD_BUDGET;
    MoneyMicros near = b.limit.checked_mul(ig.policy.near_budget_percent).checked_div(100, MoneyMicros::Rounding::kCeil);
    MoneyMicros atrisk = b.limit.checked_mul(ig.policy.at_risk_percent).checked_div(100, MoneyMicros::Rounding::kCeil);
    if (bd.total >= atrisk) st = CostState::AT_RISK;
    else if (bd.total >= near) st = CostState::NEAR_BUDGET;
  }
  return st;
}

AuthorityContext CostGovernor::authority_snapshot_from(const ExecutionPlan& plan,
                                                       const WorkloadGeneration& workload_gen) const {
  const Impl& ig = *impl_;
  AuthorityContext ctx;
  ctx.epoch = ig.epoch;
  ctx.policy_generation = ig.policy.generation;
  ctx.price_generation = ig.schedule.generation;
  ctx.plan_generation = plan.generation;
  ctx.workload_generation = workload_gen;
  ctx.worker = plan.worker;
  ctx.boot = current_boot_for(plan.worker);
  ctx.budget_generation = BudgetGeneration(0);
  return ctx;
}

void CostGovernor::build_explanation(CostDecision& d, const ExecutionPlan& best, const PlanEconomics& econ,
                                     const WorkloadId& workload, const WorkloadGeneration& workload_gen,
                                     const std::vector<ExecutionPlan>& plans) const {
  const Impl& ig = *impl_;
  CostExplanation& ex = d.explanation;
  ex.objective = "COST_GOVERNED_PLAN";
  ex.currency = ig.policy.currency;
  ex.projected = econ.projected;
  ex.label = best.label;
  ex.selected = best.id;
  ex.authority = authority_snapshot_from(best, workload_gen);
  (void)workload;
  for (const auto& p : plans) {
    if (p.id == best.id) continue;
    ex.candidate_set.push_back(p.id);
    ex.rejected.push_back(p.id);
  }
  ex.candidate_set.push_back(best.id);
  // what-would-change: deterministic alternatives
  WhatWouldChange wwc;
  WhatWouldChange::Alt bud;
  bud.name = "BUDGET_INCREASES";
  bud.applicable = false;
  for (const auto& [bid, b] : ig.budgets) {
    (void)bid;
    if (b.mode == BudgetMode::HARD) {
      bud.applicable = econ.projected.total > b.remaining();
      bud.feasible = bud.applicable ? Feasibility::FEASIBLE : Feasibility::FEASIBLE;
      bud.projected_cost = econ.projected.total;
      bud.delta = b.remaining() - econ.projected.total;
      break;
    }
  }
  wwc.alternatives.push_back(bud);
  WhatWouldChange::Alt drop;
  drop.name = "ENERGY_PRICE_DROPS";
  drop.applicable = econ.projected.get(CostComponent::ENERGY).known;
  drop.feasible = Feasibility::FEASIBLE;
  drop.projected_cost = econ.projected.total;
  ex.what_would_change = wwc;
}

CostDecision CostGovernor::evaluate(const WorkloadId& workload, const WorkloadGeneration& workload_gen,
                                    const std::vector<ExecutionPlan>& plans) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  Impl& ig = *impl_;
  CostDecision d;
  if (ig.revalidation_needed) { d.kind = DecisionKind::REVALIDATE; d.status = Status::STALE_EVIDENCE; d.detail = "recovered dynamic evidence requires revalidation"; return d; }
  if (plans.empty()) { d.kind = DecisionKind::NO_FEASIBLE_PLAN; d.status = Status::PLAN_INFEASIBLE; return d; }
  struct Candidate { const ExecutionPlan* plan; PlanEconomics econ; };
  std::vector<Candidate> candidates;
  FeasibilityReason worst = FeasibilityReason::NONE;
  for (const auto& plan : plans) {
    Candidate c{&plan, evaluate_plan_locked(plan)};
    if (c.econ.feasible != Feasibility::FEASIBLE && c.econ.reason != FeasibilityReason::NONE) worst = c.econ.reason;
    candidates.push_back(std::move(c));
  }
  std::vector<Candidate> feasible;
  for (auto& c : candidates) if (c.econ.feasible == Feasibility::FEASIBLE) feasible.push_back(std::move(c));
  auto better = [](const Candidate& a, const Candidate& b) -> bool {
    if (a.econ.projected.total != b.econ.projected.total) return a.econ.projected.total < b.econ.projected.total;
    if (a.econ.projected_per_request != b.econ.projected_per_request) return a.econ.projected_per_request < b.econ.projected_per_request;
    if (a.econ.projected_per_token != b.econ.projected_per_token) return a.econ.projected_per_token < b.econ.projected_per_token;
    MoneyMicros ae = a.econ.projected.get(CostComponent::ENERGY).known ? a.econ.projected.get(CostComponent::ENERGY).value : MoneyMicros(0);
    MoneyMicros be = b.econ.projected.get(CostComponent::ENERGY).known ? b.econ.projected.get(CostComponent::ENERGY).value : MoneyMicros(0);
    if (ae != be) return ae < be;
    MoneyMicros at = a.econ.projected.get(CostComponent::TRANSFER).known ? a.econ.projected.get(CostComponent::TRANSFER).value : MoneyMicros(0);
    MoneyMicros bt = b.econ.projected.get(CostComponent::TRANSFER).known ? b.econ.projected.get(CostComponent::TRANSFER).value : MoneyMicros(0);
    if (at != bt) return at < bt;
    MoneyMicros aa = a.econ.projected.get(CostComponent::ACCELERATOR_TIME).known ? a.econ.projected.get(CostComponent::ACCELERATOR_TIME).value : MoneyMicros(0);
    MoneyMicros ba = b.econ.projected.get(CostComponent::ACCELERATOR_TIME).known ? b.econ.projected.get(CostComponent::ACCELERATOR_TIME).value : MoneyMicros(0);
    if (aa != ba) return aa < ba;
    if (a.plan->completion_probability_percent != b.plan->completion_probability_percent)
      return a.plan->completion_probability_percent > b.plan->completion_probability_percent;
    return a.plan->id.value() < b.plan->id.value();
  };
  if (feasible.empty()) {
    d.kind = DecisionKind::NO_FEASIBLE_PLAN; d.status = Status::PLAN_INFEASIBLE;
    d.binding_constraint = worst;
    d.detail = "no feasible candidate under current economics";
    return d;
  }
  std::stable_sort(feasible.begin(), feasible.end(), better);
  const Candidate& best = feasible.front();
  d.kind = DecisionKind::AUTHORIZE;
  d.selected_plan = best.plan->id;
  d.plan_generation = best.plan->generation;
  d.projected = best.econ.projected;
  d.status = Status::OK;
  d.binding_constraint = FeasibilityReason::NONE;
  for (auto& c : candidates) if (c.plan->id != best.plan->id) d.rejected_plans.push_back(c.plan->id);
  MoneyMicros remaining{0};
  for (const auto& [bid, b] : ig.budgets) { (void)bid; if (b.mode == BudgetMode::HARD) { remaining = b.remaining(); break; } }
  d.remaining_budget = remaining;
  d.state = state_from_breakdown(best.econ.projected);
  d.authority = authority_snapshot_from(*best.plan, workload_gen);
  build_explanation(d, *best.plan, best.econ, workload, workload_gen, plans);
  return d;
}

Status CostGovernor::validate_for_dispatch(const CostDecision& prior, const std::vector<ExecutionPlan>& candidate_plans) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  if (prior.kind != DecisionKind::AUTHORIZE) return Status::OK;
  Impl& ig = *impl_;
  if (prior.authority.epoch != ig.epoch) return Status::STALE_AUTHORITY;
  if (prior.authority.policy_generation != ig.policy.generation) return Status::STALE_AUTHORITY;
  if (prior.authority.price_generation != ig.schedule.generation) return Status::STALE_AUTHORITY;
  if (prior.authority.boot != current_boot_for(prior.authority.worker)) return Status::STALE_AUTHORITY;
  if (ig.revalidation_needed) return Status::STALE_EVIDENCE;
  for (const auto& plan : candidate_plans) {
    if (plan.id == prior.selected_plan) {
      if (!plan.meets_hard_slo) return Status::PLAN_INFEASIBLE;
      PlanEconomics e = evaluate_plan_locked(plan);
      if (e.feasible != Feasibility::FEASIBLE) return Status::PLAN_INFEASIBLE;
      for (const auto& [bid, b] : ig.budgets) { (void)bid; if (b.mode == BudgetMode::HARD && e.projected.total > b.remaining()) return Status::BUDGET_EXCEEDED; }
      return Status::OK;
    }
  }
  return Status::PLAN_SUPERSEDED;
}

Status CostGovernor::propose_intervention(CostIntervention& intervention) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  intervention.id = InterventionId(impl_->next_intervention++);
  intervention.generation = InterventionGeneration(1);
  intervention.lifecycle = InterventionLifecycle::PROPOSED;
  intervention.created_at_ms = impl_->clock->now_ms();
  impl_->interventions.emplace(intervention.id, intervention);
  return Status::OK;
}
Status CostGovernor::authorize_intervention(const InterventionId& id, const InterventionGeneration& gen) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->interventions.find(id);
  if (it == impl_->interventions.end()) return Status::INVALID_INPUT;
  if (it->second.generation != gen) return Status::STALE_AUTHORITY;
  if (it->second.lifecycle != InterventionLifecycle::PROPOSED) return Status::INTERVENTION_FAILED;
  it->second.lifecycle = InterventionLifecycle::AUTHORIZED;
  return Status::OK;
}
Status CostGovernor::acknowledge_intervention(const InterventionId& id, const InterventionGeneration& gen) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->interventions.find(id);
  if (it == impl_->interventions.end()) return Status::INVALID_INPUT;
  if (it->second.generation != gen) return Status::STALE_AUTHORITY;
  if (it->second.lifecycle != InterventionLifecycle::AUTHORIZED && it->second.lifecycle != InterventionLifecycle::DISPATCHED)
    return Status::INTERVENTION_FAILED;
  it->second.lifecycle = InterventionLifecycle::ACKNOWLEDGED;
  it->second.acknowledged_at_ms = impl_->clock->now_ms();
  return Status::OK;
}
VerificationOutcome CostGovernor::verify_intervention(const InterventionId& id, const InterventionGeneration& gen,
                                                      const std::vector<CostEvidence>& post_evidence) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->interventions.find(id);
  if (it == impl_->interventions.end() || it->second.generation != gen) return VerificationOutcome::OUTCOME_UNKNOWN;
  if (post_evidence.empty()) return VerificationOutcome::INSUFFICIENT_POST_ACTION_EVIDENCE;
  if (it->second.lifecycle != InterventionLifecycle::ACKNOWLEDGED && it->second.lifecycle != InterventionLifecycle::DISPATCHED)
    return VerificationOutcome::OUTCOME_UNKNOWN;
  it->second.lifecycle = InterventionLifecycle::EFFECTIVE;
  it->second.verified_at_ms = impl_->clock->now_ms();
  return VerificationOutcome::EFFECTIVE;
}
std::vector<CostIntervention> CostGovernor::interventions() const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  std::vector<CostIntervention> out;
  out.reserve(impl_->interventions.size());
  for (auto& [k, v] : impl_->interventions) { (void)k; out.push_back(v); }
  return out;
}

Status CostGovernor::persist() const {
  DurableSnapshot snap;
  {
    std::lock_guard<std::mutex> g(impl_->mtx);
    snap.epoch = impl_->epoch;
    snap.policy = impl_->policy;
    snap.price_schedule = impl_->schedule;
    for (auto& [k, b] : impl_->budgets) { (void)k; snap.budgets.push_back(b); }
    std::sort(snap.budgets.begin(), snap.budgets.end(), [](const Budget& a, const Budget& b) { return a.id < b.id; });
    snap.history = impl_->history;
    for (auto& [k, i] : impl_->interventions) { (void)k; InterventionEntry e; e.id = i.id; e.kind = i.kind; e.lifecycle = i.lifecycle; e.generation = i.generation; e.created_at_ms = i.created_at_ms; snap.interventions.push_back(e); }
  }
  if (!impl_->store) return Status::INTERVENTION_FAILED;
  return impl_->store->save(snap);
}
Status CostGovernor::load() {
  if (!impl_->store) return Status::INTERVENTION_FAILED;
  DurableSnapshot snap = impl_->store->load();
  std::lock_guard<std::mutex> g(impl_->mtx);
  impl_->epoch = snap.epoch;
  impl_->policy = snap.policy;
  impl_->schedule = snap.price_schedule;
  impl_->budgets.clear();
  for (auto& b : snap.budgets) { Budget bb = b; bb.reserved = MoneyMicros(0); if (bb.consumed.is_zero() && bb.released.is_zero() && bb.lifecycle == BudgetLifecycle::RESERVED) bb.lifecycle = BudgetLifecycle::ACTIVE; impl_->budgets.emplace(bb.id, std::move(bb)); }
  impl_->history = snap.history;
  impl_->interventions.clear();
  for (auto& e : snap.interventions) { CostIntervention ci; ci.id = e.id; ci.kind = e.kind; ci.lifecycle = e.lifecycle; ci.generation = e.generation; ci.created_at_ms = e.created_at_ms; impl_->interventions.emplace(ci.id, ci); }
  impl_->revalidation_needed = true;
  return Status::OK;
}
bool CostGovernor::state_exists(const std::string& path) { return StateStore::exists(path); }

std::vector<Budget> CostGovernor::budgets() const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  std::vector<Budget> out;
  for (auto& [k, b] : impl_->budgets) { (void)k; out.push_back(b); }
  return out;
}
std::optional<Budget> CostGovernor::find_budget(const BudgetId& id) const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->budgets.find(id);
  if (it == impl_->budgets.end()) return std::nullopt;
  return it->second;
}
std::vector<CostEvidence> CostGovernor::attempts(const RequestId& request) const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->attempts.find(request);
  if (it == impl_->attempts.end()) return {};
  std::vector<CostEvidence> out;
  out.reserve(it->second.size());
  for (auto& [k, v] : it->second) { (void)k; out.push_back(v); }
  return out;
}
std::vector<HistoryEntry> CostGovernor::history() const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  return impl_->history;
}
MoneyMicros CostGovernor::realized_request_cost(const RequestId& request) const {
  std::lock_guard<std::mutex> g(impl_->mtx);
  auto it = impl_->attempts.find(request);
  if (it == impl_->attempts.end()) return MoneyMicros(0);
  std::vector<CostEvidence> evs;
  for (auto& [k, v] : it->second) { (void)k; evs.push_back(v); }
  CostBreakdown bd = CostCalculator::project(impl_->schedule, evs, impl_->policy.currency, impl_->clock->now_ms());
  return bd.total;
}
Status CostGovernor::finalize_request(const RequestId& request, const WorkloadId& workload) {
  std::lock_guard<std::mutex> g(impl_->mtx);
  for (const auto& h : impl_->history) if (h.request == request) return Status::OK;
  auto it = impl_->attempts.find(request);
  if (it == impl_->attempts.end()) return Status::INVALID_INPUT;
  std::vector<CostEvidence> evs;
  for (auto& [k, v] : it->second) { (void)k; evs.push_back(v); }
  CostBreakdown bd = CostCalculator::project(impl_->schedule, evs, impl_->policy.currency, impl_->clock->now_ms());
  HistoryEntry h;
  h.request = request; h.workload = workload; h.completed_at_ms = impl_->clock->now_ms();
  h.realized_cost = bd.total; h.attempts = static_cast<std::int64_t>(evs.size()); h.tokens = 0;
  impl_->history.push_back(std::move(h));
  return Status::OK;
}

}  // namespace costgovernor
