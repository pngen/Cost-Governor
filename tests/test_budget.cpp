#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/budget.hpp"

using namespace costgovernor;

CG_TEST_CASE(Budget_create_reserve_commit) {
  auto clock = std::make_shared<MockClock>(1000);
  CostGovernor g(clock);
  CostPolicy p = th::policy();
  g.set_policy(p);
  Status st = g.create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  CG_CHECK(st == Status::OK);
  BudgetReservation r;
  st = g.reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(40), r);
  CG_CHECK(st == Status::OK);
  CG_CHECK(r.amount == MoneyMicros::from_units(40));
  auto b = g.find_budget(BudgetId(1));
  CG_CHECK(b.has_value());
  CG_CHECK(b->reserved == MoneyMicros::from_units(40));
  CG_CHECK(b->consumed.is_zero());
  // commit
  st = g.commit(r);
  CG_CHECK(st == Status::OK);
  b = g.find_budget(BudgetId(1));
  CG_CHECK(b->reserved.is_zero());
  CG_CHECK(b->consumed == MoneyMicros::from_units(40));
  // commit twice: idempotent (no double charge)
  st = g.commit(r);
  CG_CHECK(st == Status::OK);
  b = g.find_budget(BudgetId(1));
  CG_CHECK(b->consumed == MoneyMicros::from_units(40));
}

CG_TEST_CASE(Budget_hard_budget_exceeded_rejected) {
  auto clock = std::make_shared<MockClock>(1000);
  CostGovernor g(clock);
  g.set_policy(th::policy());
  g.create_budget(th::budget(BudgetId(2), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(10)));
  // reserving more than the limit is rejected
  BudgetReservation r;
  Status st = g.reserve(BudgetId(2), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(20), r);
  CG_CHECK(st == Status::BUDGET_EXCEEDED);
  // consuming more than the limit is rejected
  st = g.consume(BudgetId(2), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(15));
  CG_CHECK(st == Status::BUDGET_EXCEEDED);
}

CG_TEST_CASE(Budget_release_never_exceeds_reservation) {
  auto clock = std::make_shared<MockClock>(1000);
  CostGovernor g(clock);
  g.set_policy(th::policy());
  g.create_budget(th::budget(BudgetId(3), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  BudgetReservation r;
  CG_CHECK(g.reserve(BudgetId(3), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(30), r) == Status::OK);
  CG_CHECK(g.release(r) == Status::OK);
  auto b = g.find_budget(BudgetId(3));
  CG_CHECK(b->reserved == MoneyMicros(0));
  CG_CHECK(b->released == MoneyMicros::from_units(30));
  // release again is idempotent; release never exceeds reservation
  CG_CHECK(g.release(r) == Status::OK);
  b = g.find_budget(BudgetId(3));
  CG_CHECK(b->released == MoneyMicros::from_units(30));
  CG_CHECK(b->consumed.is_zero());
}

CG_TEST_CASE(Budget_soft_budget_allows_violation) {
  auto clock = std::make_shared<MockClock>(1000);
  CostGovernor g(clock);
  g.set_policy(th::policy());
  g.create_budget(th::budget(BudgetId(4), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(5), BudgetMode::SOFT));
  Status st = g.consume(BudgetId(4), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(50));
  CG_CHECK(st == Status::OK);  // soft limit exceeded but exposed as violation
  auto b = g.find_budget(BudgetId(4));
  CG_CHECK(b->consumed == MoneyMicros::from_units(50));
  CG_CHECK(b->over_hard() || b->consumed > b->limit);
}

CG_TEST_CASE(Budget_stale_epoch_rejected) {
  auto clock = std::make_shared<MockClock>(1000);
  CostGovernor g(clock);
  g.set_policy(th::policy());
  g.create_budget(th::budget(BudgetId(5), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  g.advance_epoch();  // epoch now 2, old reservation epoch 1 becomes stale
  BudgetReservation r;
  // reserve under old epoch
  CG_CHECK(g.reserve(BudgetId(5), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(10), r) == Status::STALE_AUTHORITY);
}
