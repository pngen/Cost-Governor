#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/cost.hpp"

using namespace costgovernor;

CG_TEST_CASE(Property_same_input_same_plan) {
  auto clock = std::make_shared<MockClock>(1000);
  auto g = std::make_shared<CostGovernor>(clock);
  g->set_policy(th::policy());
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000));
  s.observations.push_back(th::price(EvidenceId(2), PriceKind::TRANSFER, MoneyMicros::from_micros(10000), 1000));
  g->set_price_schedule(s);
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100000)));
  ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  a.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000);
  ExecutionPlan b = th::plan(PlanId(2), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
  b.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  b.expected_transfer = TransferBytes(5'000'000'000);
  std::vector<ExecutionPlan> plans{a, b};
  PlanId first;
  MoneyMicros first_total;
  for (int seed = 0; seed < 200; ++seed) {
    CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    if (seed == 0) { first = d.selected_plan; first_total = d.projected.total; }
    else if (d.selected_plan != first || d.projected.total != first_total) {
      throw std::runtime_error(std::string("determinism violated at seed ") + std::to_string(seed));
    }
  }
}

CG_TEST_CASE(Property_hard_budget_never_silently_exceeded) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(5)));
  for (int i = 0; i < 50; ++i) {
    Status st = g->consume(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1));
    if (st != Status::OK) {
      // Once the budget hits its limit exactly it is EXHAUSTED (terminal); a late
      // attempt is rejected with either BUDGET_EXCEEDED or BUDGET_EXPIRED.
      CG_CHECK(st == Status::BUDGET_EXCEEDED || st == Status::BUDGET_EXPIRED);
      break;
    }
  }
  auto b = g->find_budget(BudgetId(1));
  CG_CHECK(b->consumed <= b->limit);
}

CG_TEST_CASE(Property_release_never_exceeds_reservation) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  BudgetReservation r;
  CG_CHECK(g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(30), r) == Status::OK);
  CG_CHECK(g->release(r) == Status::OK);
  auto b = g->find_budget(BudgetId(1));
  CG_CHECK(b->reserved == MoneyMicros(0));
  CG_CHECK(b->released == MoneyMicros::from_units(30));
  CG_CHECK(b->reserved >= MoneyMicros(0));
  CG_CHECK(b->consumed >= MoneyMicros(0));
  CG_CHECK(b->released >= MoneyMicros(0));
}

CG_TEST_CASE(Property_currency_never_mixes_silently) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  Budget cad = th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100));
  cad.currency = "CAD";
  CG_CHECK(g->create_budget(cad) == Status::CURRENCY_MISMATCH);
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
  PriceObservation o = th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000);
  o.currency = "CAD";
  s.observations.push_back(o);
  // CAD price cannot be ingested into a USD policy
  CG_CHECK(g->publish_price(o) == Status::CURRENCY_MISMATCH);
}

CG_TEST_CASE(Property_unknown_component_never_becomes_zero) {
  // A measured amount with no price must throw MISSING_PRICE; it is never 0.
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
  // no accel price present
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  bool threw = false;
  try { (void)CostCalculator::project(s, {e}, MoneyMicros::kDefaultCurrency, 1000); }
  catch (const StatusError& er) { threw = (er.status() == Status::MISSING_PRICE); }
  CG_CHECK(threw);
}

CG_TEST_CASE(Property_terminal_lifecycle_stays_terminal) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(10)));
  CG_CHECK(g->consume(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(10)) == Status::OK);
  auto b = g->find_budget(BudgetId(1));
  CG_CHECK(b->lifecycle == BudgetLifecycle::EXHAUSTED);
  // terminal cannot resurrect: further consume/reserve rejected
  CG_CHECK(g->consume(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1)) == Status::BUDGET_EXPIRED);
  BudgetReservation r;
  CG_CHECK(g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1), r) == Status::BUDGET_EXPIRED);
}

CG_TEST_CASE(Property_restart_requires_revalidation) {
  std::string path = "cgtest_restart.bin";
  if (StateStore::exists(path)) StateStore::remove(path);
  {
    auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
    g->set_state_path(path);
    CostPolicy p = th::policy();
    g->set_policy(p);
    PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
    s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000));
    g->set_price_schedule(s);
    g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
    CG_CHECK(g->persist() == Status::OK);
  }
  {
    auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(3000));
    g->set_state_path(path);
    CG_CHECK(g->load() == Status::OK);
    g->advance_epoch();
    g->set_worker_boot(WorkerBootId(500));
    ExecutionPlan a = th::plan(PlanId(1), PlanGeneration(1), WorkloadId(1), WorkloadGeneration(1));
    a.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);
    std::vector<ExecutionPlan> plans{a};
    // recovered dynamic evidence requires revalidation -> REVALIDATE
    CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    CG_CHECK(d.kind == DecisionKind::REVALIDATE);
    // fresh schedule clears it
    PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(9); s.created_at_ms = 3000;
    s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 3000));
    g->set_price_schedule(s);
    CostDecision d2 = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
    CG_CHECK(d2.kind == DecisionKind::AUTHORIZE);
  }
  StateStore::remove(path);
}
