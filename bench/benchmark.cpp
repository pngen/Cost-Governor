#include "costgovernor/governor.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace costgovernor;
using SteadyClock = std::chrono::steady_clock;

std::shared_ptr<CostGovernor> make_gov() {
  auto g = std::make_shared<CostGovernor>(std::make_shared<SystemClock>());
  CostPolicy p; p.currency = MoneyMicros::kDefaultCurrency;
  p.max_evidence_age_ms = 300000; p.price_schedule_max_age_ms = 300000;
  p.allow_policy = true; p.allow_measured = true; p.allow_synthetic = true;
  g->set_policy(p); g->set_worker_boot(WorkerBootId(1));
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = 1;
  for (EvidenceId i(1); i.value() <= 10; i = EvidenceId(i.value() + 1)) {
    PriceObservation o; o.id = i; o.kind = PriceKind::ACCELERATOR_TIME; o.amount = MoneyMicros::from_micros(1000);
    o.currency = MoneyMicros::kDefaultCurrency; o.label = DataLabel::POLICY; o.observed_at_ms = 1;
    o.epoch = CoordinatorEpoch(1); o.boot = WorkerBootId(1); s.observations.push_back(o);
  }
  g->set_price_schedule(s);
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(1'000'000'000); b.mode = BudgetMode::HARD;
  g->create_budget(b);
  return g;
}
ExecutionPlan make_plan(std::int64_t id) {
  ExecutionPlan p; p.id = PlanId(id); p.generation = PlanGeneration(1); p.workload = WorkloadId(1); p.workload_generation = WorkloadGeneration(1);
  p.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000 * (1 + (id % 5)));
  p.expected_requests = 1; p.expected_tokens = 500;
  return p;
}
double report(const char* name, long count, long long ns) {
  double secs = static_cast<double>(ns) / 1e9;
  double ops = secs > 0 ? static_cast<double>(count) / secs : 0.0;
  std::cout << name << " x " << count << ": " << ops << " ops/sec (" << secs << " s)\n";
  return ops;
}

int main() {
  const long scales[] = {100, 1000, 10000, 100000, 1000000};
  for (long scale : scales) {
    auto g = make_gov();
    // cost evidence ingest
    auto t0 = SteadyClock::now();
    for (long i = 0; i < scale; ++i) {
      CostEvidence e; e.id = EvidenceId(1000 + i); e.request = RequestId(i + 1); e.attempt = AttemptId(1);
      e.attempt_gen = AttemptGeneration(1); e.epoch = CoordinatorEpoch(1); e.boot = WorkerBootId(1);
      e.generation = EvidenceGeneration(1); e.observed_at_ms = 1;
      e.accelerator_time = AcceleratorNanoseconds(1'000'000);
      g->record_attempt(e);
    }
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0).count();
    report("evidence_ingest", scale, ns);
    if (scale > 100000) break;  // keep runtime bounded for huge scales
  }
  for (long scale : {100, 1000, 10000, 100000}) {
    auto g = make_gov();
    std::vector<ExecutionPlan> plans;
    for (long i = 1; i <= 4; ++i) plans.push_back(make_plan(i));
    auto t0 = SteadyClock::now();
    for (long i = 0; i < scale; ++i) {
      CostDecision d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
      (void)d;
    }
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0).count();
    report("plan_eval", scale, ns);
  }
  for (long scale : {100, 1000, 10000, 100000}) {
    auto g = make_gov();
    auto t0 = SteadyClock::now();
    for (long i = 0; i < scale; ++i) {
      BudgetReservation r;
      if (g->reserve(BudgetId(1), BudgetGeneration(1), CoordinatorEpoch(1), MoneyMicros::from_micros(1), r) == Status::OK)
        g->release(r);
    }
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0).count();
    report("budget_reserve_release", scale, ns);
  }
  // persistence
  {
    auto g = make_gov();
    g->set_state_path("cg_bench.bin");
    auto t0 = SteadyClock::now();
    for (int i = 0; i < 100; ++i) g->persist();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0).count();
    report("persist", 100, ns);
  }
  // concurrent evaluation
  {
    auto g = make_gov();
    std::vector<ExecutionPlan> plans; for (long i = 1; i <= 4; ++i) plans.push_back(make_plan(i));
    auto t0 = SteadyClock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) ts.emplace_back([&]{ for (int i = 0; i < 10000; ++i) { auto d = g->evaluate(WorkloadId(1), WorkloadGeneration(1), plans); (void)d; } });
    for (auto& th : ts) th.join();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0).count();
    report("concurrent_eval", 8 * 10000, ns);
  }
  return 0;
}
