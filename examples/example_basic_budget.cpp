#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  Budget b;
  b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(100); b.mode = BudgetMode::HARD;
  std::cout << "create_budget: " << status_name(g->create_budget(b)) << "\n";
  BudgetReservation r;
  std::cout << "reserve 40.00: " << status_name(g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(40), r)) << "\n";
  std::cout << "commit: " << status_name(g->commit(r)) << "\n";
  auto after = g->find_budget(BudgetId(1));
  std::cout << "consumed=" << after->consumed.to_string() << " reserved=" << after->reserved.to_string() << "\n";
  std::cout << "remaining=" << after->remaining().to_string() << " lifecycle=" << budget_lifecycle_name(after->lifecycle) << "\n";
  std::cout << "release after commit is idempotent (no double-charge): " << status_name(g->commit(r)) << "\n";
  return 0;
}
