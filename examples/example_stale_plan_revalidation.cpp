#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  ex::accel_price(g, 1000);
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(100); b.mode = BudgetMode::HARD;
  g->create_budget(b);
  ExecutionPlan a = ex::plan(PlanId(1), 1'000'000'000);
  std::vector<ExecutionPlan> plans{a};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  std::cout << "authorized under price generation: " << d.authority.price_generation.value() << "\n";
  std::cout << "pre-dispatch check (unchanged): " << status_name(g->validate_for_dispatch(d, plans)) << "\n";
  // price schedule generation advances before dispatch
  PriceSchedule s = g->price_schedule();
  s.generation = PriceScheduleGeneration(s.generation.value() + 1);
  g->set_price_schedule(s);
  std::cout << "pre-dispatch check (price changed): " << status_name(g->validate_for_dispatch(d, plans)) << "\n";
  return 0;
}
