#include "test_helpers.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/cost.hpp"

using namespace costgovernor;

namespace {
PriceSchedule sched(std::int64_t now = 1000) {
  PriceSchedule s;
  s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = now;
  s.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), now));
  s.observations.push_back(th::price(EvidenceId(2), PriceKind::TRANSFER, MoneyMicros::from_micros(1000000), now));  // $1/GB
  return s;
}
}

CG_TEST_CASE(Adversarial_money_overflow_rejected) {
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
  e.transfer = TransferBytes(9'000'000'000'000'000'000LL);  // ~8 EB of transfer
  bool threw = false;
  try { (void)CostCalculator::project(sched(), {e}, MoneyMicros::kDefaultCurrency, 1000); }
  catch (const StatusError&) { threw = true; }
  CG_CHECK(threw);  // overflow is rejected, never wraps
}

CG_TEST_CASE(Adversarial_rounding_never_undercounts_to_zero) {
  // 1 ns of accelerator at 1000 micros/sec must cost >= 1 micro (ceil), never 0.
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(1);
  CostBreakdown bd = CostCalculator::project(sched(), {e}, MoneyMicros::kDefaultCurrency, 1000);
  CG_CHECK(bd.total == MoneyMicros::from_micros(1));
}

CG_TEST_CASE(Adversarial_negative_configured_price_rejected) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  PriceObservation o = th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(-1000), 1000);
  CG_CHECK(g->publish_price(o) == Status::INVALID_INPUT);
}

CG_TEST_CASE(Adversarial_duplicate_observation_idempotent) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  PriceObservation o = th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000);
  CG_CHECK(g->publish_price(o) == Status::OK);
  CG_CHECK(g->publish_price(o) == Status::OK);
  CG_CHECK(g->price_schedule().observations.size() == 1);
}

CG_TEST_CASE(Adversarial_stale_completion_rejects_no_mutation) {
  auto g = std::make_shared<CostGovernor>(std::make_shared<MockClock>(1000));
  g->set_policy(th::policy());
  g->set_worker_boot(WorkerId(1), WorkerBootId(1));
  CostEvidence e = th::attempt(EvidenceId(1), RequestId(1), AttemptId(1), AttemptGeneration(1), CoordinatorEpoch(1), WorkerBootId(1), EvidenceGeneration(1), 1000);
  e.accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  CG_CHECK(g->record_attempt(e) == Status::OK);
  std::size_t before = g->attempts(RequestId(1)).size();
  g->advance_epoch();
  CostEvidence stale = e; stale.id = EvidenceId(2); stale.attempt = AttemptId(2);
  CG_CHECK(g->record_attempt(stale) == Status::STALE_AUTHORITY);
  CG_CHECK(g->attempts(RequestId(1)).size() == before);  // no mutation
}
