#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include <atomic>
#include <thread>
#include <vector>

using namespace costgovernor;

CG_TEST_CASE(Concurrency_concurrent_evidence_ingestion) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->set_worker_boot(WorkerId(1), WorkerBootId(1));
  const int kThreads = 8, kPer = 500;
  std::vector<std::thread> ts;
  std::atomic<int> ok{0}, bad{0};
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&, t] {
      for (int i = 0; i < kPer; ++i) {
        CostEvidence e = th::attempt(EvidenceId(t * kPer + i + 1), RequestId(t), AttemptId(i + 1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
        e.accelerator_time = AcceleratorNanoseconds(1'000'000);
        Status st = g->record_attempt(e);
        if (st == Status::OK) ++ok; else ++bad;
      }
    });
  }
  for (auto& th : ts) th.join();
  CG_CHECK(bad == 0);
  CG_CHECK(ok == kThreads * kPer);
}

CG_TEST_CASE(Concurrency_concurrent_reservation_never_over_reserves) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  const int kThreads = 16;
  std::vector<std::thread> ts;
  std::atomic<int> accepted{0};
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 100; ++i) {
        BudgetReservation r;
        // reserve 0.5 units = 500000 micros each round; 1600 attempts needs 800 units
        if (g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_micros(500000), r) == Status::OK) ++accepted;
      }
    });
  }
  for (auto& th : ts) th.join();
  auto b = g->find_budget(BudgetId(1));
  CG_CHECK(b->reserved <= b->limit);   // never over-reserves past limit
  CG_CHECK(b->reserved == MoneyMicros::from_micros(accepted.load() * 500000));
}

CG_TEST_CASE(Concurrency_concurrent_plan_evaluation_deterministic) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1000;
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
  std::atomic<int> mismatch{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 200; ++i) {
        CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
        if (d.selected_plan != PlanId(1)) ++mismatch;
      }
    });
  }
  for (auto& th : ts) th.join();
  CG_CHECK(mismatch == 0);
}

CG_TEST_CASE(Concurrency_concurrent_persist_with_mutation) {
  std::string path = "cgtest_conc.bin";
  if (StateStore::exists(path)) StateStore::remove(path);
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_state_path(path);
  g->set_policy(th::policy());
  g->create_budget(th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(100)));
  std::atomic<bool> stop{false};
  std::atomic<int> persist_ok{0};
  std::thread writer([&] {
    while (!stop) {
      Status st = g->persist();
      if (st == Status::OK) ++persist_ok;
    }
  });
  std::vector<std::thread> mutators;
  for (int t = 0; t < 4; ++t) {
    mutators.emplace_back([&, t] {
      for (int i = 0; i < 200; ++i) {
        BudgetReservation r;
        if (g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_units(1), r) == Status::OK) {
          if (i % 2 == 0) g->commit(r); else g->release(r);
        }
      }
    });
  }
  for (auto& m : mutators) m.join();
  stop = true;
  writer.join();
  CG_CHECK(persist_ok > 0);
  // the persisted state is loadable
  CostGovernor g2(std::make_shared<MockClock>(2000));
  g2.set_state_path(path);
  CG_CHECK(g2.load() == Status::OK);
  CG_CHECK(g2.find_budget(BudgetId(1)).has_value());
  StateStore::remove(path);
}
