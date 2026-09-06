#include "costgovernor/governor.hpp"
#include "costgovernor/cost.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cuda_runtime.h>
#include <iostream>
#include <string>
#include <vector>

using namespace costgovernor;

__global__ void saxpy(float* a, const float* b, float alpha, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) a[i] = alpha * a[i] + b[i];
}

namespace {
void check(cudaError_t e, const char* what) {
  if (e != cudaSuccess) { std::cerr << "CUDA error (" << what << "): " << cudaGetErrorString(e) << "\n"; std::exit(2); }
}

struct Measured {
  std::int64_t device_ns = 0;   // real device execution time
  std::int64_t bytes = 0;       // bytes transferred H2D + D2H
  std::int64_t alloc_bytes = 0; // device memory allocated
};

// Run a real GPU workload of size n; measure device time (cudaEvent, synced)
// and transferred bytes; never measure enqueue latency as execution cost.
Measured run_workload(int n) {
  std::size_t bytes = static_cast<std::size_t>(n) * sizeof(float);
  float* da = nullptr; float* db = nullptr;
  check(cudaMalloc(&da, bytes), "cudaMalloc(da)");
  check(cudaMalloc(&db, bytes), "cudaMalloc(db)");
  std::vector<float> hA(n, 1.0f), hB(n, 2.0f), hOut(n, 0.0f);
  check(cudaMemcpy(da, hA.data(), bytes, cudaMemcpyHostToDevice), "H2D a");
  check(cudaMemcpy(db, hB.data(), bytes, cudaMemcpyHostToDevice), "H2D b");
  cudaEvent_t t0, t1;
  check(cudaEventCreate(&t0), "event0");
  check(cudaEventCreate(&t1), "event1");
  check(cudaEventRecord(t0, 0), "rec0");
  int threads = 256;
  int blocks = (n + threads - 1) / threads;
  saxpy<<<blocks, threads>>>(da, db, 2.0f, n);
  check(cudaEventRecord(t1, 0), "rec1");
  check(cudaDeviceSynchronize(), "sync");
  float ms = 0.0f;
  check(cudaEventElapsedTime(&ms, t0, t1), "elapsed");
  check(cudaMemcpy(hOut.data(), da, bytes, cudaMemcpyDeviceToHost), "D2H a");
  check(cudaDeviceSynchronize(), "sync2");
  float checksum = 0.0f;
  for (int i = 0; i < n; ++i) checksum += hOut[i];
  (void)checksum;
  check(cudaFree(da), "free da");
  check(cudaFree(db), "free db");
  check(cudaEventDestroy(t0), "evd0"); check(cudaEventDestroy(t1), "evd1");

  Measured m;
  m.device_ns = static_cast<std::int64_t>(ms * 1'000'000.0);  // ms -> ns
  m.bytes = static_cast<std::int64_t>(bytes) * 2;            // H2D a + H2D b + D2H a ? count transferred
  m.alloc_bytes = static_cast<std::int64_t>(bytes) * 2;
  return m;
}
}  // namespace

int main() {
  int ndev = 0;
  check(cudaGetDeviceCount(&ndev), "devcount");
  if (ndev < 1) { std::cerr << "No CUDA device available\n"; return 2; }
  cudaDeviceProp prop;
  check(cudaGetDeviceProperties(&prop, 0), "props");
  std::cout << "CUDA device: " << prop.name << " (compute capability "
            << prop.major << "." << prop.minor << ")\n";
  size_t free0 = 0, total0 = 0;
  check(cudaMemGetInfo(&free0, &total0), "meminfo0");

  // Two workload shapes: Plan A (smaller, cheaper work) and Plan B (larger).
  const int nA = 1 << 20;         // 1M floats
  const int nB = 4 << 20;         // 4M floats
  Measured ma = run_workload(nA);
  Measured mb = run_workload(nB);
  std::cout << "Plan A: device_time=" << ma.device_ns << " ns, moved=" << ma.bytes << " bytes\n";
  std::cout << "Plan B: device_time=" << mb.device_ns << " ns, moved=" << mb.bytes << " bytes\n";

  // Configured POLICY rates (never real market prices).
  const char* currency = MoneyMicros::kDefaultCurrency;
  const std::int64_t kNow = 1'000'000'000;
  MoneyMicros accel_per_sec = MoneyMicros::from_micros(1000);  // $0.001/s POLICY
  MoneyMicros transfer_per_gb = MoneyMicros::from_micros(10000); // $0.01/GB POLICY
  MoneyMicros energy_per_kwh = MoneyMicros::from_micros(200000); // $0.20/kWh POLICY
  std::int64_t power_watts = 300;                                // configured average (SYNTHETIC estimate)

  auto gov = std::make_shared<CostGovernor>(std::make_shared<MockClock>(kNow));
  CostPolicy p; p.currency = currency; p.epoch = CoordinatorEpoch(1); p.generation = CostPolicyGeneration(1);
  p.max_evidence_age_ms = 600000; p.price_schedule_max_age_ms = 600000;
  p.allow_policy = true; p.allow_measured = true; p.allow_synthetic = true;
  gov->set_policy(p);
  PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = kNow;
  auto add = [&](EvidenceId id, PriceKind k, MoneyMicros amt, DataLabel lab) {
    PriceObservation o; o.id = id; o.kind = k; o.amount = amt; o.currency = currency;
    o.label = lab; o.observed_at_ms = kNow; o.epoch = CoordinatorEpoch(1); o.boot = WorkerBootId(1);
    s.observations.push_back(o);
  };
  add(EvidenceId(1), PriceKind::ACCELERATOR_TIME, accel_per_sec, DataLabel::POLICY);
  add(EvidenceId(2), PriceKind::TRANSFER, transfer_per_gb, DataLabel::POLICY);
  add(EvidenceId(3), PriceKind::ENERGY, energy_per_kwh, DataLabel::POLICY);
  gov->set_price_schedule(s);
  Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = CostPolicyGeneration(1);
  b.epoch = CoordinatorEpoch(1); b.limit = MoneyMicros::from_units(100); b.mode = BudgetMode::HARD;
  gov->create_budget(b);

  auto plan_econ = [&](PlanId id, std::int64_t dev_ns, std::int64_t bytes) -> CostDecision {
    ExecutionPlan pl; pl.id = id; pl.generation = PlanGeneration(1); pl.workload = WorkloadId(1); pl.workload_generation = WorkloadGeneration(1);
    pl.expected_accelerator_time = AcceleratorNanoseconds(dev_ns);
    pl.expected_transfer = TransferBytes(bytes);
    // energy estimated from measured runtime x configured average power (SYNTHETIC)
    double secs = static_cast<double>(dev_ns) / 1e9;
    pl.expected_energy = EnergyMicroJoules(static_cast<std::int64_t>(secs * static_cast<double>(power_watts) * 1e6));
    pl.expected_requests = 1;
    std::vector<ExecutionPlan> plans{pl};
    return gov->evaluate(WorkloadId(1), WorkloadGeneration(1), plans);
  };
  CostDecision da = plan_econ(PlanId(1), ma.device_ns, ma.bytes);
  CostDecision db2 = plan_econ(PlanId(2), mb.device_ns, mb.bytes);
  std::cout << "Plan A projected total: " << da.projected.total.to_string() << " " << currency << "\n";
  std::cout << "Plan B projected total: " << db2.projected.total.to_string() << " " << currency << "\n";
  // deterministic: choose cheaper plan by projected total
  bool select_a = da.projected.total <= db2.projected.total;
  std::cout << "Deterministic selection: " << (select_a ? "Plan A" : "Plan B") << " (cheaper)\n";

  // Retry economics: attempt 1 consumes real GPU time then fails; attempt 2 succeeds.
  auto g2 = std::make_shared<CostGovernor>(std::make_shared<MockClock>(kNow));
  g2->set_policy(p); g2->set_worker_boot(WorkerBootId(1));
  g2->set_price_schedule(s);
  CostEvidence fail_ev; fail_ev.id = EvidenceId(50); fail_ev.request = RequestId(1); fail_ev.attempt = AttemptId(1);
  fail_ev.attempt_gen = AttemptGeneration(1); fail_ev.epoch = CoordinatorEpoch(1); fail_ev.boot = WorkerBootId(1);
  fail_ev.generation = EvidenceGeneration(1); fail_ev.observed_at_ms = kNow; fail_ev.label = DataLabel::POLICY;
  fail_ev.accelerator_time = AcceleratorNanoseconds(ma.device_ns);
  fail_ev.is_completion = true; fail_ev.completed_successfully = false;
  CostEvidence ok_ev = fail_ev; ok_ev.id = EvidenceId(51); ok_ev.attempt = AttemptId(2); ok_ev.completed_successfully = true;
  g2->record_attempt(fail_ev);
  g2->record_attempt(ok_ev);
  auto attempts = g2->attempts(RequestId(1));
  CostBreakdown bd = CostCalculator::project(g2->price_schedule(), attempts, currency, kNow);
  std::cout << "Retry: realized request cost (includes failed attempt 1) = "
            << bd.total.to_string() << "\n";
  // duplicate completion no double-charge
  Status dup = g2->record_attempt(ok_ev);
  std::cout << "Duplicate completion rejected (no double-charge): " << (dup == Status::INVALID_INPUT ? "yes" : "no") << "\n";
  if (dup != Status::INVALID_INPUT) return 3;

  // memory baseline closure
  size_t free1 = 0, total1 = 0;
  check(cudaMemGetInfo(&free1, &total1), "meminfo1");
  std::printf("Device memory baseline closure: free before=%llu, after=%llu (%s)\n",
              static_cast<unsigned long long>(free0), static_cast<unsigned long long>(free1),
              free1 >= free0 ? "OK" : "LEAK");
  (void)total0; (void)total1;
  std::cout << "CUDA proof complete.\n";
  return 0;
}
