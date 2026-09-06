#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  ex::accel_price(g, 1000);
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_micros(2000); b.mode = BudgetMode::HARD;  // 0.002 hard
  g->create_budget(b);
  ExecutionPlan a = ex::plan(PlanId(1), 3'000'000'000);  // 3s GPU -> 3000 micros > 1 unit
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  std::cout << "decision: " << decision_kind_name(d.kind) << "\n";
  if (d.kind == DecisionKind::NO_FEASIBLE_PLAN) {
    std::cout << "hard budget exceeded; no feasible plan. binding constraint: " << feasibility_reason_name(d.binding_constraint) << "\n";
  }
  return 0;
}
