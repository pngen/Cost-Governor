#include "costgovernor/persistence.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <limits>
#include <string>

namespace costgovernor {
namespace {

class Writer {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void str(const std::string& s) {
    if (s.size() > 0xFFFFFFFFull) throw_status(Status::RESOURCE_EXHAUSTED, "string too large in snapshot");
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void raw(const void* p, std::size_t n) {
    const auto* c = static_cast<const std::uint8_t*>(p);
    buf_.insert(buf_.end(), c, c + n);
  }
  [[nodiscard]] const std::vector<std::uint8_t>& data() const { return buf_; }
 private:
  std::vector<std::uint8_t> buf_;
};

class Reader {
 public:
  Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
  std::uint8_t u8() { need(1); return p_[pos_++]; }
  std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p_[pos_++]) << (8 * i); return v; }
  std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p_[pos_++]) << (8 * i); return v; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  std::string str() {
    std::uint32_t len = u32();
    if (len > 16 * 1024 * 1024) throw_status(Status::PERSISTENCE_CORRUPT, "string length exceeds bound");
    need(len);
    std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
    pos_ += len;
    return s;
  }
  [[nodiscard]] std::size_t remaining() const { return n_ - pos_; }
  [[nodiscard]] std::size_t pos() const { return pos_; }
  void expect_end() { if (pos_ != n_) throw_status(Status::PERSISTENCE_CORRUPT, "trailing garbage in snapshot"); }
 private:
  void need(std::size_t k) { if (k > n_ - pos_) throw_status(Status::PERSISTENCE_CORRUPT, "truncated snapshot"); }
  const std::uint8_t* p_; std::size_t n_; std::size_t pos_ = 0;
};

template <typename IdT> void write_id(Writer& w, IdT id) { w.u64(id.value()); }
template <typename IdT> IdT read_id(Reader& r) { return IdT(r.u64()); }

void write_budget(Writer& w, const Budget& b) {
  write_id(w, b.id); write_id(w, b.generation); write_id(w, b.policy_generation); write_id(w, b.epoch);
  w.str(b.currency); w.u8(static_cast<std::uint8_t>(b.mode)); w.u8(static_cast<std::uint8_t>(b.objective));
  w.i64(b.limit.total_micros()); w.u8(static_cast<std::uint8_t>(b.lifecycle));
  w.i64(b.reserved.total_micros()); w.i64(b.consumed.total_micros()); w.i64(b.released.total_micros());
  w.i64(b.created_at_ms); w.i64(b.expires_at_ms); w.u8(static_cast<std::uint8_t>(b.label));
}
Budget read_budget(Reader& r) {
  Budget b;
  b.id = read_id<BudgetId>(r); b.generation = read_id<BudgetGeneration>(r);
  b.policy_generation = read_id<CostPolicyGeneration>(r); b.epoch = read_id<CoordinatorEpoch>(r);
  b.currency = r.str(); b.mode = static_cast<BudgetMode>(r.u8()); b.objective = static_cast<BudgetObjective>(r.u8());
  b.limit = MoneyMicros::from_micros(r.i64()); b.lifecycle = static_cast<BudgetLifecycle>(r.u8());
  b.reserved = MoneyMicros::from_micros(r.i64()); b.consumed = MoneyMicros::from_micros(r.i64());
  b.released = MoneyMicros::from_micros(r.i64()); b.created_at_ms = r.i64(); b.expires_at_ms = r.i64();
  b.label = static_cast<DataLabel>(r.u8());
  return b;
}

void write_obs(Writer& w, const PriceObservation& o) {
  write_id(w, o.id); w.u8(static_cast<std::uint8_t>(o.kind));
  write_id(w, o.resource_scope); write_id(w, o.device); w.i64(o.amount.total_micros());
  w.str(o.currency); w.u8(static_cast<std::uint8_t>(o.provenance)); w.u8(static_cast<std::uint8_t>(o.label));
  w.i64(o.valid_from_ms); w.i64(o.valid_to_ms); w.i64(o.observed_at_ms);
  write_id(w, o.generation); write_id(w, o.epoch); write_id(w, o.boot);
}
PriceObservation read_obs(Reader& r) {
  PriceObservation o;
  o.id = read_id<EvidenceId>(r); o.kind = static_cast<PriceKind>(r.u8());
  o.resource_scope = read_id<ResourceId>(r); o.device = read_id<DeviceId>(r); o.amount = MoneyMicros::from_micros(r.i64());
  o.currency = r.str(); o.provenance = static_cast<Provenance>(r.u8()); o.label = static_cast<DataLabel>(r.u8());
  o.valid_from_ms = r.i64(); o.valid_to_ms = r.i64(); o.observed_at_ms = r.i64();
  o.generation = read_id<EvidenceGeneration>(r); o.epoch = read_id<CoordinatorEpoch>(r); o.boot = read_id<WorkerBootId>(r);
  return o;
}

void write_policy(Writer& w, const CostPolicy& p) {
  write_id(w, p.id); write_id(w, p.generation); write_id(w, p.epoch); w.str(p.currency);
  w.u8(static_cast<std::uint8_t>(p.ranking)); w.i64(p.min_evidence_freshness_ms); w.i64(p.max_evidence_age_ms);
  w.i64(p.price_schedule_max_age_ms); w.i64(p.max_projection_uncertainty); w.i64(p.hysteresis_ms); w.i64(p.cooldown_ms);
  w.i64(p.near_budget_percent); w.i64(p.at_risk_percent); w.i64(p.retry_budget.total_micros()); w.i64(p.recovery_budget.total_micros());
  w.u8(p.recovery_cost_known ? 1 : 0); w.u8(p.allow_synthetic ? 1 : 0); w.u8(p.allow_policy ? 1 : 0); w.u8(p.allow_measured ? 1 : 0);
}
CostPolicy read_policy(Reader& r) {
  CostPolicy p;
  p.id = read_id<CostPolicyId>(r); p.generation = read_id<CostPolicyGeneration>(r); p.epoch = read_id<CoordinatorEpoch>(r);
  p.currency = r.str(); p.ranking = static_cast<RankingMode>(r.u8());
  p.min_evidence_freshness_ms = r.i64(); p.max_evidence_age_ms = r.i64(); p.price_schedule_max_age_ms = r.i64();
  p.max_projection_uncertainty = r.i64(); p.hysteresis_ms = r.i64(); p.cooldown_ms = r.i64();
  p.near_budget_percent = r.i64(); p.at_risk_percent = r.i64(); p.retry_budget = MoneyMicros::from_micros(r.i64());
  p.recovery_budget = MoneyMicros::from_micros(r.i64());
  p.recovery_cost_known = r.u8() != 0; p.allow_synthetic = r.u8() != 0; p.allow_policy = r.u8() != 0; p.allow_measured = r.u8() != 0;
  return p;
}

void write_history(Writer& w, const HistoryEntry& h) {
  write_id(w, h.request); write_id(w, h.workload); w.i64(h.completed_at_ms); w.i64(h.realized_cost.total_micros());
  w.i64(h.attempts); w.i64(h.tokens);
}
HistoryEntry read_history(Reader& r) {
  HistoryEntry h; h.request = read_id<RequestId>(r); h.workload = read_id<WorkloadId>(r);
  h.completed_at_ms = r.i64(); h.realized_cost = MoneyMicros::from_micros(r.i64()); h.attempts = r.i64(); h.tokens = r.i64();
  return h;
}
void write_interv(Writer& w, const InterventionEntry& i) {
  write_id(w, i.id); w.u8(static_cast<std::uint8_t>(i.kind)); w.u8(static_cast<std::uint8_t>(i.lifecycle));
  write_id(w, i.generation); w.i64(i.created_at_ms);
}
InterventionEntry read_interv(Reader& r) {
  InterventionEntry i; i.id = read_id<InterventionId>(r); i.kind = static_cast<InterventionKind>(r.u8());
  i.lifecycle = static_cast<InterventionLifecycle>(r.u8()); i.generation = read_id<InterventionGeneration>(r); i.created_at_ms = r.i64();
  return i;
}

}  // namespace

std::uint32_t SnapshotCodec::crc32(const std::uint8_t* data, std::size_t n) noexcept {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1u)));
  }
  return ~crc;
}

std::vector<std::uint8_t> SnapshotCodec::encode(const DurableSnapshot& s) {
  Writer w;
  write_id(w, s.epoch); write_policy(w, s.policy);
  write_id(w, s.price_schedule.id); write_id(w, s.price_schedule.generation); w.i64(s.price_schedule.created_at_ms);
  w.u32(static_cast<std::uint32_t>(s.price_schedule.observations.size()));
  for (const auto& o : s.price_schedule.observations) write_obs(w, o);
  w.u32(static_cast<std::uint32_t>(s.budgets.size()));
  for (const auto& b : s.budgets) write_budget(w, b);
  w.u32(static_cast<std::uint32_t>(s.history.size()));
  for (const auto& h : s.history) write_history(w, h);
  w.u32(static_cast<std::uint32_t>(s.interventions.size()));
  for (const auto& i : s.interventions) write_interv(w, i);

  const auto& payload = w.data();
  std::vector<std::uint8_t> out;
  out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(kMagic), reinterpret_cast<const std::uint8_t*>(kMagic) + 8);
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((kVersion >> (8 * i)) & 0xFF));
  std::uint64_t len = payload.size();
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((len >> (8 * i)) & 0xFF));
  std::uint32_t crc = crc32(payload.data(), payload.size());
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFF));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

DurableSnapshot SnapshotCodec::decode(const std::vector<std::uint8_t>& bytes) {
  constexpr std::size_t kHeader = 8 + 4 + 8 + 4;
  if (bytes.size() < kHeader) throw_status(Status::PERSISTENCE_CORRUPT, "snapshot too short");
  if (std::memcmp(bytes.data(), kMagic, 8) != 0) throw_status(Status::PERSISTENCE_CORRUPT, "bad snapshot magic");
  std::uint32_t version = 0;
  for (int i = 0; i < 4; ++i) version |= static_cast<std::uint32_t>(bytes[8 + i]) << (8 * i);
  if (version > kVersion) throw_status(Status::PERSISTENCE_CORRUPT, "unknown snapshot version");
  std::uint64_t len = 0;
  for (int i = 0; i < 8; ++i) len |= static_cast<std::uint64_t>(bytes[12 + i]) << (8 * i);
  std::uint32_t stored_crc = 0;
  for (int i = 0; i < 4; ++i) stored_crc |= static_cast<std::uint32_t>(bytes[20 + i]) << (8 * i);
  if (len != bytes.size() - kHeader) throw_status(Status::PERSISTENCE_CORRUPT, len > bytes.size() - kHeader ? "truncated snapshot" : "trailing garbage");
  std::uint32_t calc = crc32(bytes.data() + kHeader, static_cast<std::size_t>(len));
  if (calc != stored_crc) throw_status(Status::PERSISTENCE_CORRUPT, "snapshot checksum mismatch");

  Reader r(bytes.data() + kHeader, static_cast<std::size_t>(len));
  DurableSnapshot s;
  s.epoch = read_id<CoordinatorEpoch>(r); s.policy = read_policy(r);
  s.price_schedule.id = read_id<PriceScheduleId>(r); s.price_schedule.generation = read_id<PriceScheduleGeneration>(r);
  s.price_schedule.created_at_ms = r.i64();
  std::uint32_t n_obs = r.u32();
  if (n_obs > 1'000'000) throw_status(Status::PERSISTENCE_CORRUPT, "observation count exceeds bound");
  s.price_schedule.observations.reserve(n_obs);
  for (std::uint32_t i = 0; i < n_obs; ++i) s.price_schedule.observations.push_back(read_obs(r));
  std::uint32_t n_bud = r.u32();
  if (n_bud > 1'000'000) throw_status(Status::PERSISTENCE_CORRUPT, "budget count exceeds bound");
  s.budgets.reserve(n_bud);
  for (std::uint32_t i = 0; i < n_bud; ++i) s.budgets.push_back(read_budget(r));
  std::uint32_t n_hist = r.u32();
  if (n_hist > 1'000'000) throw_status(Status::PERSISTENCE_CORRUPT, "history count exceeds bound");
  s.history.reserve(n_hist);
  for (std::uint32_t i = 0; i < n_hist; ++i) s.history.push_back(read_history(r));
  std::uint32_t n_int = r.u32();
  if (n_int > 1'000'000) throw_status(Status::PERSISTENCE_CORRUPT, "intervention count exceeds bound");
  s.interventions.reserve(n_int);
  for (std::uint32_t i = 0; i < n_int; ++i) s.interventions.push_back(read_interv(r));
  r.expect_end();
  return s;
}

StateStore::StateStore(std::string path) : path_(std::move(path)) {}

bool StateStore::exists(const std::string& path) { std::ifstream f(path, std::ios::binary); return f.good(); }
bool StateStore::remove(const std::string& path) { std::error_code ec; std::filesystem::remove(path, ec); return !ec; }

Status StateStore::save(const DurableSnapshot& s) const {
  std::vector<std::uint8_t> bytes = SnapshotCodec::encode(s);
  std::string tmp = path_ + ".tmp";
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return Status::INTERVENTION_FAILED;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f.good()) return Status::INTERVENTION_FAILED;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path_, ec);
  if (ec) return Status::INTERVENTION_FAILED;
  return Status::OK;
}

DurableSnapshot StateStore::load() const {
  std::ifstream f(path_, std::ios::binary | std::ios::ate);
  if (!f) throw_status(Status::PERSISTENCE_CORRUPT, "cannot open snapshot");
  std::streamsize size = f.tellg();
  if (size < 0) throw_status(Status::PERSISTENCE_CORRUPT, "snapshot size unavailable");
  f.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  f.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!f.good() && !f.eof()) throw_status(Status::PERSISTENCE_CORRUPT, "snapshot read failed");
  return SnapshotCodec::decode(bytes);
}

}  // namespace costgovernor
