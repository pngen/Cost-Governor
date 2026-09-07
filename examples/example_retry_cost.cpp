#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  ex::accel_price(g, 1000);
  auto ev = [&](EvidenceId id, AttemptId att, std::int64_t ns, bool success) {
    CostEvidence e;
    e.id = id; e.request = RequestId(1); e.attempt = att; e.attempt_gen = AttemptGeneration(1);
    e.epoch = CoordinatorEpoch(1); e.worker = WorkerId(1); e.boot = WorkerBootId(1); e.generation = EvidenceGeneration(1);
    e.observed_at_ms = 1000; e.label = DataLabel::POLICY;
    e.accelerator_time = AcceleratorNanoseconds(ns);
    e.is_completion = true; e.completed_successfully = success;
    return e;
  };
  std::cout << "attempt 1 (fail, 2s): " << status_name(g->record_attempt(ev(EvidenceId(10), AttemptId(1), 2'000'000'000, false))) << "\n";
  std::cout << "attempt 2 (fail, 1s): " << status_name(g->record_attempt(ev(EvidenceId(11), AttemptId(2), 1'000'000'000, false))) << "\n";
  std::cout << "attempt 3 (ok, 3s):  " << status_name(g->record_attempt(ev(EvidenceId(12), AttemptId(3), 3'000'000'000, true))) << "\n";
  auto attempts = g->attempts(RequestId(1));
  CostBreakdown bd = CostCalculator::project(g->price_schedule(), attempts, MoneyMicros::kDefaultCurrency, g->policy().max_evidence_age_ms ? 1000 : 1000);
  std::cout << "realized request cost: " << bd.total.to_string() << " (includes all attempts)\n";
  std::cout << "accelerator_time component: " << bd.get(CostComponent::ACCELERATOR_TIME).value.to_string() << "\n";
  std::cout << "retry component (failed attempts only): " << bd.get(CostComponent::RETRY).value.to_string() << "\n";
  // duplicate completion does not double-charge
  std::cout << "duplicate attempt 3 rejected: " << status_name(g->record_attempt(ev(EvidenceId(12), AttemptId(3), 3'000'000'000, true))) << "\n";
  std::cout << "realized cost unchanged: " << g->realized_request_cost(RequestId(1)).to_string() << "\n";
  return 0;
}
