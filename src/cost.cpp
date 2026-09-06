#include "costgovernor/cost.hpp"
#include <cstdint>
#include <limits>
#include "costgovernor/policy.hpp"
#include "costgovernor/status.hpp"

namespace costgovernor {

namespace {
using u64 = std::uint64_t;
constexpr u64 U64MAX = std::numeric_limits<u64>::max();
constexpr std::int64_t I64MAX = std::numeric_limits<std::int64_t>::max();

PriceKind kind_for(CostComponent c) {
  switch (c) {
    case CostComponent::ACCELERATOR_TIME: return PriceKind::ACCELERATOR_TIME;
    case CostComponent::ENERGY: return PriceKind::ENERGY;
    case CostComponent::TRANSFER: return PriceKind::TRANSFER;
    case CostComponent::MEMORY: return PriceKind::MEMORY_HOLDING;
    case CostComponent::RESIDENCY: return PriceKind::RESIDENCY_HOLDING;
    case CostComponent::STORAGE: return PriceKind::STORAGE;
    default: throw_status(Status::INVALID_INPUT, "component is not a measurable amount");
  }
}

std::int64_t denominator_for(PriceKind k) {
  switch (k) {
    case PriceKind::ACCELERATOR_TIME: return money_scale::kNsPerSecond;
    case PriceKind::ENERGY: return money_scale::kMicroJoulesPerKWh;
    case PriceKind::TRANSFER: return money_scale::kBytesPerGB;
    case PriceKind::MEMORY_HOLDING: return money_scale::kBytesPerGBs;
    case PriceKind::RESIDENCY_HOLDING: return money_scale::kBytesPerGBs;
    case PriceKind::STORAGE: return money_scale::kBytesPerGB;
    default: throw_status(Status::INVALID_INPUT, "no scalar unit for price kind");
  }
}

// ceil(a * b / d) in exact integer arithmetic for non-negative a,b,d>0.
// Rejects overflow (the value would exceed int64 micro-units).
std::int64_t ceil_mul_div(std::int64_t a, std::int64_t b, std::int64_t d) {
  if (a < 0 || b < 0 || d <= 0) throw_status(Status::INVALID_INPUT, "ceil_mul_div requires non-negative inputs");
  if (a == 0) return 0;
  u64 ua = static_cast<u64>(a);
  u64 ub = static_cast<u64>(b);
  u64 ud = static_cast<u64>(d);
  if (ub > U64MAX / ua) throw_status(Status::RESOURCE_EXHAUSTED, "cost multiplication overflow");
  u64 prod = ua * ub;
  u64 q = prod / ud;
  u64 r = prod % ud;
  if (r != 0) {
    if (q == U64MAX) throw_status(Status::RESOURCE_EXHAUSTED, "cost rounding overflow");
    ++q;
  }
  if (q > static_cast<u64>(I64MAX)) throw_status(Status::RESOURCE_EXHAUSTED, "cost exceeds representable micro-units");
  return static_cast<std::int64_t>(q);
}

bool obs_applies(const PriceObservation& p, PriceKind want, const std::string& currency,
                 std::int64_t now_ms, DeviceId dev, ResourceId res, const CostPolicy* pol) {
  if (p.kind != want) return false;
  if (!p.valid_currency(currency)) return false;
  if (now_ms < p.valid_from_ms) return false;
  if (p.valid_to_ms != 0 && now_ms >= p.valid_to_ms) return false;
  if (p.amount.negative()) return false;
  if (pol && !pol->accepts_label(p.label)) return false;
  if (dev.valid() && p.device.valid() && p.device != dev) return false;
  if (res.valid() && p.resource_scope.valid() && p.resource_scope != res) return false;
  return true;
}

const PriceObservation* find_price(const PriceSchedule& s, PriceKind want,
                                   const std::string& currency, std::int64_t now_ms,
                                   DeviceId dev, ResourceId res, const CostPolicy* pol) {
  const PriceObservation* best = nullptr;
  for (const auto& obs : s.observations) {
    if (!obs_applies(obs, want, currency, now_ms, dev, res, pol)) continue;
    if (best == nullptr || (obs.device.valid() && !best->device.valid()) ||
        (obs.resource_scope.valid() && !best->resource_scope.valid()))
      best = &obs;
  }
  return best;
}

}  // namespace

MoneyMicros CostCalculator::project_component(const PriceSchedule& schedule, CostComponent comp,
                                              std::int64_t amount, const std::string& currency,
                                              std::int64_t now_ms, DeviceId device, ResourceId resource) {
  const PriceObservation* p = find_price(schedule, kind_for(comp), currency, now_ms, device, resource, nullptr);
  if (p == nullptr)
    throw_status(Status::MISSING_PRICE, "no price for component " + std::string(cost_component_name(comp)));
  std::int64_t denom = denominator_for(p->kind);
  std::int64_t micros = ceil_mul_div(amount, p->amount.total_micros(), denom);
  return MoneyMicros::from_micros(micros);
}

CostBreakdown CostCalculator::project(const PriceSchedule& schedule,
                                       const std::vector<CostEvidence>& attempts,
                                       const std::string& currency,
                                       std::int64_t now_ms) {
  CostBreakdown bd;
  bd.currency = currency;
  std::int64_t final_success_index = -1;
  bool any_unknown = false;
  for (std::size_t i = 0; i < attempts.size(); ++i) {
    if (attempts[i].label == DataLabel::UNSUPPORTED) any_unknown = true;
    if (attempts[i].is_completion && attempts[i].completed_successfully)
      final_success_index = static_cast<std::int64_t>(i);
  }
  auto priced = [&](const CostEvidence& ev, CostComponent kind, std::int64_t amount) -> MoneyMicros {
    return CostCalculator::project_component(schedule, kind, amount, currency, now_ms, ev.device, ev.resource);
  };
  for (const auto& ev : attempts) {
    if (ev.accelerator_time.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::ACCELERATOR_TIME, ev.accelerator_time.count());
      bd.components[0].known = true; bd.components[0].value += m;
    }
    if (ev.energy.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::ENERGY, ev.energy.count());
      bd.components[1].known = true; bd.components[1].value += m;
    }
    if (ev.transfer.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::TRANSFER, ev.transfer.count());
      bd.components[2].known = true; bd.components[2].value += m;
    }
    if (ev.memory_hold.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::MEMORY, ev.memory_hold.count());
      bd.components[3].known = true; bd.components[3].value += m;
    }
    if (ev.residency_hold.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::RESIDENCY, ev.residency_hold.count());
      bd.components[4].known = true; bd.components[4].value += m;
    }
    if (ev.storage_bytes.count() != 0) {
      MoneyMicros m = priced(ev, CostComponent::STORAGE, ev.storage_bytes.count());
      bd.components[5].known = true; bd.components[5].value += m;
    }
    if (ev.recompute_ns != 0) {
      MoneyMicros m = priced(ev, CostComponent::ACCELERATOR_TIME, ev.recompute_ns);
      bd.components[8].known = true; bd.components[8].value += m;
    }
  }
  const CostEvidence* success = nullptr;
  if (final_success_index >= 0)
    success = &attempts[static_cast<std::size_t>(final_success_index)];
  for (const auto& ev : attempts) {
    if (&ev == success) continue;
    if (!(ev.is_completion && !ev.completed_successfully)) continue;
    MoneyMicros failed{0};
    if (ev.accelerator_time.count()) failed += priced(ev, CostComponent::ACCELERATOR_TIME, ev.accelerator_time.count());
    if (ev.energy.count()) failed += priced(ev, CostComponent::ENERGY, ev.energy.count());
    if (ev.transfer.count()) failed += priced(ev, CostComponent::TRANSFER, ev.transfer.count());
    if (ev.memory_hold.count()) failed += priced(ev, CostComponent::MEMORY, ev.memory_hold.count());
    if (ev.residency_hold.count()) failed += priced(ev, CostComponent::RESIDENCY, ev.residency_hold.count());
    if (ev.storage_bytes.count()) failed += priced(ev, CostComponent::STORAGE, ev.storage_bytes.count());
    bd.components[6].known = true; bd.components[6].value += failed;
  }
  // Total sums only additive quantities: measurable components (0-5),
  // recomputation (8), and other (9). RETRY (6) / RECOVERY (7) are sub-totals
  // that summarize a portion already counted in the measurable components, so
  // they are NOT added again (each unit of cost is counted exactly once).
  for (std::size_t i = 0; i < kCostComponentCount; ++i)
    if (i != 6 && i != 7 && bd.components[i].known) bd.total += bd.components[i].value;
  bd.any_unknown = any_unknown;
  return bd;
}

}  // namespace costgovernor
