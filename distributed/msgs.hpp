#pragma once
#include "chan.hpp"
#include "costgovernor/governor.hpp"
#include "costgovernor/plan.hpp"
#include "costgovernor/price.hpp"

namespace cgproto {

inline void put_id(Writer& w, costgovernor::CoordinatorEpoch e) { w.u64(e.value()); }
template <typename IdT> inline void put_id(Writer& w, IdT id) { w.u64(id.value()); }
template <typename IdT> inline IdT get_id(Reader& r) { return IdT(r.u64()); }

inline void encode_price(Writer& w, const costgovernor::PriceObservation& o) {
  put_id(w, o.id); w.u8(static_cast<std::uint8_t>(o.kind));
  put_id(w, o.resource_scope); put_id(w, o.device); w.i64(o.amount.total_micros());
  w.str(o.currency); w.u8(static_cast<std::uint8_t>(o.provenance)); w.u8(static_cast<std::uint8_t>(o.label));
  w.i64(o.valid_from_ms); w.i64(o.valid_to_ms); w.i64(o.observed_at_ms);
  put_id(w, o.generation); put_id(w, o.epoch); put_id(w, o.boot);
}
inline costgovernor::PriceObservation decode_price(Reader& r) {
  costgovernor::PriceObservation o;
  o.id = get_id<costgovernor::EvidenceId>(r); o.kind = static_cast<costgovernor::PriceKind>(r.u8());
  o.resource_scope = get_id<costgovernor::ResourceId>(r); o.device = get_id<costgovernor::DeviceId>(r);
  o.amount = costgovernor::MoneyMicros::from_micros(r.i64()); o.currency = r.str();
  o.provenance = static_cast<costgovernor::Provenance>(r.u8()); o.label = static_cast<costgovernor::DataLabel>(r.u8());
  o.valid_from_ms = r.i64(); o.valid_to_ms = r.i64(); o.observed_at_ms = r.i64();
  o.generation = get_id<costgovernor::EvidenceGeneration>(r); o.epoch = get_id<costgovernor::CoordinatorEpoch>(r);
  o.boot = get_id<costgovernor::WorkerBootId>(r);
  return o;
}

inline void encode_plan(Writer& w, const costgovernor::ExecutionPlan& p) {
  put_id(w, p.id); put_id(w, p.generation); put_id(w, p.workload); put_id(w, p.workload_generation);
  put_id(w, p.resource); put_id(w, p.resource_generation); put_id(w, p.device); put_id(w, p.device_generation);
  w.i64(p.expected_accelerator_time.count()); w.i64(p.expected_energy.count());
  w.i64(p.expected_transfer.count()); w.i64(p.expected_memory_hold.count());
  w.i64(p.expected_residency_hold.count()); w.i64(p.expected_storage.count());
  w.i64(p.expected_retries); w.i64(p.expected_requests); w.i64(p.expected_tokens); w.i64(p.expected_operations);
  w.u8(static_cast<std::uint8_t>(p.label)); w.u8(p.meets_hard_slo ? 1 : 0);
  w.i64(p.completion_probability_percent);
}
inline costgovernor::ExecutionPlan decode_plan(Reader& r) {
  costgovernor::ExecutionPlan p;
  p.id = get_id<costgovernor::PlanId>(r); p.generation = get_id<costgovernor::PlanGeneration>(r);
  p.workload = get_id<costgovernor::WorkloadId>(r); p.workload_generation = get_id<costgovernor::WorkloadGeneration>(r);
  p.resource = get_id<costgovernor::ResourceId>(r); p.resource_generation = get_id<costgovernor::ResourceGeneration>(r);
  p.device = get_id<costgovernor::DeviceId>(r); p.device_generation = get_id<costgovernor::DeviceGeneration>(r);
  p.expected_accelerator_time = costgovernor::AcceleratorNanoseconds(r.i64());
  p.expected_energy = costgovernor::EnergyMicroJoules(r.i64());
  p.expected_transfer = costgovernor::TransferBytes(r.i64());
  p.expected_memory_hold = costgovernor::MemoryByteNanoseconds(r.i64());
  p.expected_residency_hold = costgovernor::MemoryByteNanoseconds(r.i64());
  p.expected_storage = costgovernor::Bytes(r.i64());
  p.expected_retries = r.i64(); p.expected_requests = r.i64(); p.expected_tokens = r.i64(); p.expected_operations = r.i64();
  p.label = static_cast<costgovernor::DataLabel>(r.u8()); p.meets_hard_slo = r.u8() != 0;
  p.completion_probability_percent = r.i64();
  return p;
}

inline void encode_evidence(Writer& w, const costgovernor::CostEvidence& e) {
  put_id(w, e.id); put_id(w, e.request); put_id(w, e.workload); put_id(w, e.attempt); put_id(w, e.attempt_gen);
  put_id(w, e.boot); put_id(w, e.epoch); put_id(w, e.generation); w.i64(e.observed_at_ms);
  put_id(w, e.device); put_id(w, e.resource);
  w.u8(static_cast<std::uint8_t>(e.label)); w.u8(e.completed_successfully ? 1 : 0); w.u8(e.is_completion ? 1 : 0);
  w.i64(e.accelerator_time.count()); w.i64(e.energy.count()); w.i64(e.transfer.count());
  w.i64(e.memory_hold.count()); w.i64(e.residency_hold.count()); w.i64(e.storage_bytes.count());
  w.i64(e.requests); w.i64(e.tokens); w.i64(e.operations); w.i64(e.recovery_actions); w.i64(e.recompute_ns);
}
inline costgovernor::CostEvidence decode_evidence(Reader& r) {
  costgovernor::CostEvidence e;
  e.id = get_id<costgovernor::EvidenceId>(r); e.request = get_id<costgovernor::RequestId>(r);
  e.workload = get_id<costgovernor::WorkloadId>(r); e.attempt = get_id<costgovernor::AttemptId>(r);
  e.attempt_gen = get_id<costgovernor::AttemptGeneration>(r); e.boot = get_id<costgovernor::WorkerBootId>(r);
  e.epoch = get_id<costgovernor::CoordinatorEpoch>(r); e.generation = get_id<costgovernor::EvidenceGeneration>(r);
  e.observed_at_ms = r.i64(); e.device = get_id<costgovernor::DeviceId>(r); e.resource = get_id<costgovernor::ResourceId>(r);
  e.label = static_cast<costgovernor::DataLabel>(r.u8());
  e.completed_successfully = r.u8() != 0; e.is_completion = r.u8() != 0;
  e.accelerator_time = costgovernor::AcceleratorNanoseconds(r.i64());
  e.energy = costgovernor::EnergyMicroJoules(r.i64());
  e.transfer = costgovernor::TransferBytes(r.i64());
  e.memory_hold = costgovernor::MemoryByteNanoseconds(r.i64());
  e.residency_hold = costgovernor::MemoryByteNanoseconds(r.i64());
  e.storage_bytes = costgovernor::Bytes(r.i64());
  e.requests = r.i64(); e.tokens = r.i64(); e.operations = r.i64(); e.recovery_actions = r.i64(); e.recompute_ns = r.i64();
  return e;
}

// Decision summary sent over the wire.
struct DecisionResult {
  std::uint8_t kind = 0;
  std::uint64_t selected = 0;
  std::int64_t total_micros = 0;
  std::uint64_t rejected_count = 0;
  std::uint8_t state = 0;
  std::uint8_t status = 0;
};
inline void encode_decision(Writer& w, const costgovernor::CostDecision& d) {
  w.u8(static_cast<std::uint8_t>(d.kind)); w.u64(d.selected_plan.value());
  w.i64(d.projected.total.total_micros()); w.u64(static_cast<std::uint64_t>(d.rejected_plans.size()));
  w.u8(static_cast<std::uint8_t>(d.state)); w.u8(static_cast<std::uint8_t>(d.status));
}
inline DecisionResult decode_decision(Reader& r) {
  DecisionResult d;
  d.kind = r.u8(); d.selected = r.u64(); d.total_micros = r.i64(); d.rejected_count = r.u64();
  d.state = r.u8(); d.status = r.u8();
  return d;
}

}  // namespace cgproto
