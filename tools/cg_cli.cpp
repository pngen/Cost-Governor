#include "costgovernor/governor.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace costgovernor;

namespace {
std::shared_ptr<CostGovernor> make_gov() {
  const std::int64_t kNow = 1'000'000'000;
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(kNow));
  CostPolicy p; p.currency = MoneyMicros::kDefaultCurrency; p.epoch = CoordinatorEpoch(1); p.generation = CostPolicyGeneration(1);
  p.max_evidence_age_ms = 30000; p.price_schedule_max_age_ms = 30000;
  p.allow_policy = true; p.allow_measured = true; p.allow_synthetic = true;
  g->set_policy(p);
  g->set_worker_boot(WorkerBootId(1));
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = kNow;
  auto add = [&](EvidenceId id, PriceKind k, MoneyMicros amt) {
    PriceObservation o; o.id = id; o.kind = k; o.amount = amt; o.currency = MoneyMicros::kDefaultCurrency;
    o.label = DataLabel::POLICY; o.provenance = Provenance::CONFIGURED_POLICY; o.observed_at_ms = kNow;
    o.epoch = CoordinatorEpoch(1); o.boot = WorkerBootId(1); s.observations.push_back(o);
  };
  add(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000));
  add(EvidenceId(2), PriceKind::TRANSFER, MoneyMicros::from_micros(10000));
  g->set_price_schedule(s);
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(100000); b.mode = BudgetMode::HARD;
  g->create_budget(b);
  return g;
}
std::vector<ExecutionPlan> two_plans() {
  ExecutionPlan a; a.id = PlanId(1); a.generation = PlanGeneration(1); a.workload = WorkloadId(1); a.workload_generation = WorkloadGeneration(1);
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000); a.expected_requests = 1;
  ExecutionPlan c; c.id = PlanId(2); c.generation = PlanGeneration(1); c.workload = WorkloadId(1); c.workload_generation = WorkloadGeneration(1);
  c.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000); c.expected_transfer = TransferBytes(5'000'000'000); c.expected_requests = 1;
  return {a, c};
}
void print_breakdown(const CostBreakdown& bd) {
  const char* names[] = {"accelerator_time","energy","transfer","memory","residency","storage","retry","recovery","recomputation","other"};
  for (std::size_t i = 0; i < kCostComponentCount; ++i)
    std::cout << "  " << names[i] << ": " << (bd.components[i].known ? bd.components[i].value.to_string() : "UNKNOWN") << " " << bd.currency << "\n";
  std::cout << "  total: " << bd.total.to_string() << " " << bd.currency << "\n";
}
}

int main(int argc, char** argv) {
  std::string cmd = argc > 1 ? argv[1] : "evaluate";
  if (cmd == "show-policy") {
    auto g = make_gov();
    const CostPolicy& p = g->policy();
    std::cout << "currency: " << p.currency << "\n";
    std::cout << "ranking: " << ranking_mode_name(p.ranking) << "\n";
    std::cout << "max_evidence_age_ms: " << p.max_evidence_age_ms << "\n";
    std::cout << "near_budget_percent: " << p.near_budget_percent << " at_risk_percent: " << p.at_risk_percent << "\n";
    return 0;
  }
  if (cmd == "evaluate" || cmd == "compare-plans") {
    auto g = make_gov();
    auto plans = two_plans();
    CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    std::cout << "decision: " << decision_kind_name(d.kind) << "\n";
    std::cout << "selected plan: " << d.selected_plan.value() << "\n";
    std::cout << "state: " << cost_state_name(d.state) << "\n";
    print_breakdown(d.projected);
    return 0;
  }
  if (cmd == "show-budget") {
    auto g = make_gov();
    BudgetReservation r;
    Status st = g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(30), r);
    std::cout << "reserve: " << status_name(st) << "\n";
    auto b = g->find_budget(BudgetId(1));
    std::cout << "limit: " << b->limit.to_string() << " reserved: " << b->reserved.to_string() << " consumed: " << b->consumed.to_string() << "\n";
    return 0;
  }
  if (cmd == "show-breakdown") {
    auto g = make_gov();
    CostEvidence e; e.id = EvidenceId(1); e.request = RequestId(1); e.epoch = CoordinatorEpoch(1); e.boot = WorkerBootId(1);
    e.accelerator_time = AcceleratorNanoseconds(2'000'000'000); e.transfer = TransferBytes(1'000'000'000);
    CostBreakdown bd = CostCalculator::project(g->price_schedule(), {e}, g->policy().currency, 1'000'000'000);
    print_breakdown(bd);
    return 0;
  }
  if (cmd == "show-what-would-change") {
    auto g = make_gov();
    auto plans = two_plans();
    CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    for (const auto& alt : d.explanation.what_would_change.alternatives)
      std::cout << alt.name << " applicable=" << alt.applicable << " projected=" << alt.projected_cost.to_string() << "\n";
    return 0;
  }
  if (cmd == "inspect-history") {
    auto g = make_gov();
    std::cout << "history entries: " << g->history().size() << "\n";
    return 0;
  }
  if (cmd == "validate-state") {
    auto g = make_gov();
    g->set_state_path("cg_state.bin");
    Status save = g->persist();
    std::cout << "persist: " << status_name(save) << "\n";
    CostGovernor g2(std::make_shared<SystemClock>());
    g2.set_state_path("cg_state.bin");
    Status load = g2.load();
    std::cout << "load: " << status_name(load) << "\n";
    std::cout << "loadable budgets: " << g2.budgets().size() << "\n";
    return 0;
  }
  if (cmd == "replay-decision") {
    auto g = make_gov();
    auto plans = two_plans();
    CostDecision a = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    // deterministic replay: fresh identical scenario must yield identical decision
    auto g2 = make_gov();
    CostDecision b = g2->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    std::cout << "replay identical: " << (a.selected_plan == b.selected_plan && a.projected.total == b.projected.total ? "YES" : "NO") << "\n";
    return 0;
  }
  std::cerr << "unknown command: " << cmd << "\n";
  std::cerr << "usage: cg [evaluate|compare-plans|show-budget|show-breakdown|show-policy|show-what-would-change|inspect-history|validate-state|replay-decision]\n";
  return 2;
}
