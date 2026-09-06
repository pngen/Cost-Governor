#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  ex::accel_price(g, 1000);          // $0.001 per accelerator-second (policy)
  ex::transfer_price(g, 10000);      // $0.01 per GB transferred (policy)
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(100000); b.mode = BudgetMode::HARD;
  g->create_budget(b);
  ExecutionPlan a = ex::plan(PlanId(1), 2'000'000'000);       // 2s GPU, no transfer
  ExecutionPlan c = ex::plan(PlanId(2), 1'000'000'000);
  c.expected_transfer = TransferBytes(5'000'000'000);          // 5GB transfer
  std::vector<ExecutionPlan> plans{a, c};
  CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  std::cout << "decision: " << decision_kind_name(d.kind) << "\n";
  std::cout << "selected plan: " << d.selected_plan.value() << "\n";
  std::cout << "projected total: " << d.projected.total.to_string() << "\n";
  std::cout << "accelerator_time: " << (d.projected.get(CostComponent::ACCELERATOR_TIME).known ? d.projected.get(CostComponent::ACCELERATOR_TIME).value.to_string() : "UNKNOWN") << "\n";
  std::cout << "transfer: " << (d.projected.get(CostComponent::TRANSFER).known ? d.projected.get(CostComponent::TRANSFER).value.to_string() : "UNKNOWN") << "\n";
  std::cout << "state: " << cost_state_name(d.state) << "\n";
  return 0;
}
