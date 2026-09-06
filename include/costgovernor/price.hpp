#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "costgovernor/identity.hpp"
#include "costgovernor/money.hpp"

namespace costgovernor {

// Which economic dimension a price applies to. The unit of the price is
// implied by the kind (e.g. ACCELERATOR_TIME is per accelerator second).
enum class PriceKind : std::uint8_t {
  ACCELERATOR_TIME = 0,
  ENERGY = 1,
  TRANSFER = 2,
  MEMORY_HOLDING = 3,
  STORAGE = 4,
  RESIDENCY_HOLDING = 5,
  REQUEST = 6,
  TOKEN = 7,
  OPERATION = 8,
  CUSTOM_TYPED_COMPONENT = 9,
};
constexpr std::string_view price_kind_name(PriceKind k) noexcept {
  switch (k) {
    case PriceKind::ACCELERATOR_TIME: return "ACCELERATOR_TIME";
    case PriceKind::ENERGY: return "ENERGY";
    case PriceKind::TRANSFER: return "TRANSFER";
    case PriceKind::MEMORY_HOLDING: return "MEMORY_HOLDING";
    case PriceKind::STORAGE: return "STORAGE";
    case PriceKind::RESIDENCY_HOLDING: return "RESIDENCY_HOLDING";
    case PriceKind::REQUEST: return "REQUEST";
    case PriceKind::TOKEN: return "TOKEN";
    case PriceKind::OPERATION: return "OPERATION";
    case PriceKind::CUSTOM_TYPED_COMPONENT: return "CUSTOM_TYPED_COMPONENT";
  }
  return "UNKNOWN";
}

// Where the price came from. CONFIGURED_POLICY and SYNTHETIC are never real
// market quotes; MEASURED/REPORTED/DERIVED come from evidence.
enum class Provenance : std::uint8_t {
  CONFIGURED_POLICY = 0,
  MEASURED = 1,
  REPORTED = 2,
  DERIVED = 3,
  SYNTHETIC = 4,
  UNKNOWN = 5,
};
constexpr std::string_view provenance_name(Provenance p) noexcept {
  switch (p) {
    case Provenance::CONFIGURED_POLICY: return "CONFIGURED_POLICY";
    case Provenance::MEASURED: return "MEASURED";
    case Provenance::REPORTED: return "REPORTED";
    case Provenance::DERIVED: return "DERIVED";
    case Provenance::SYNTHETIC: return "SYNTHETIC";
    case Provenance::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// REAL (measured on-device/real TCP/real process), SYNTHETIC (fabricated
// scenario), POLICY (configured constant, never a market quote), UNSUPPORTED.
enum class DataLabel : std::uint8_t {
  REAL = 0,
  SYNTHETIC = 1,
  POLICY = 2,
  UNSUPPORTED = 3,
};
constexpr std::string_view data_label_name(DataLabel l) noexcept {
  switch (l) {
    case DataLabel::REAL: return "REAL";
    case DataLabel::SYNTHETIC: return "SYNTHETIC";
    case DataLabel::POLICY: return "POLICY";
    case DataLabel::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// A single price observation: how much one unit of "kind" costs, in micro-units
// of "currency", with provenance, generation, freshness (timestamp), and the
// resource scope it applies to. "amount" is the price per the kind's implied
// unit. Negative prices are semantically illegal and rejected on construction.
struct PriceObservation {
  EvidenceId id;
  PriceKind kind = PriceKind::ACCELERATOR_TIME;
  ResourceId resource_scope;   // ResourceId() = applies to any resource
  DeviceId device;             // DeviceId() = applies to any device
  MoneyMicros amount;          // price per the kind's unit
  std::string currency = MoneyMicros::kDefaultCurrency;
  Provenance provenance = Provenance::CONFIGURED_POLICY;
  DataLabel label = DataLabel::POLICY;
  std::int64_t valid_from_ms = 0;    // inclusive
  std::int64_t valid_to_ms = 0;      // exclusive; 0 = never expires
  std::int64_t observed_at_ms = 0;   // freshness timestamp
  EvidenceGeneration generation;
  CoordinatorEpoch epoch;
  WorkerBootId boot;

  [[nodiscard]] bool valid_currency(const std::string& policy_currency) const noexcept {
    return currency == policy_currency;
  }
};

// An ordered, generation-bound collection of price observations. The schedule
// identity/generation fence price authority.
struct PriceSchedule {
  PriceScheduleId id;
  PriceScheduleGeneration generation;
  std::vector<PriceObservation> observations;
  std::int64_t created_at_ms = 0;
  [[nodiscard]] std::size_t size() const noexcept { return observations.size(); }
};

}  // namespace costgovernor
