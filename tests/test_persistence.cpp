#include "test_helpers.hpp"
#include "costgovernor/persistence.hpp"
#include <fstream>
#include <vector>

using namespace costgovernor;

namespace {
std::string tmp_path() { return "cgtest_state_" + std::to_string(reinterpret_cast<std::uintptr_t>(&tmp_path)) + ".bin"; }
DurableSnapshot make_snap() {
  DurableSnapshot s;
  s.epoch = CoordinatorEpoch(3);
  s.policy = th::policy(CoordinatorEpoch(3), CostPolicyGeneration(5));
  s.price_schedule.id = PriceScheduleId(1); s.price_schedule.generation = PriceScheduleGeneration(2); s.price_schedule.created_at_ms = 1000;
  s.price_schedule.observations.push_back(th::price(EvidenceId(1), PriceKind::ACCELERATOR_TIME, MoneyMicros::from_micros(1000), 1000));
  Budget b = th::budget(BudgetId(1), BudgetGeneration(1), CostPolicyGeneration(5), CoordinatorEpoch(3), MoneyMicros::from_units(100));
  b.consumed = MoneyMicros::from_units(40);
  s.budgets.push_back(b);
  HistoryEntry h; h.request = RequestId(1); h.workload = WorkloadId(1); h.completed_at_ms = 2000; h.realized_cost = MoneyMicros::from_units(40); h.attempts = 3; h.tokens = 500;
  s.history.push_back(h);
  return s;
}
std::vector<std::uint8_t> read_file(const std::string& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  std::streamsize n = f.tellg(); f.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> v(static_cast<std::size_t>(n));
  f.read(reinterpret_cast<char*>(v.data()), n);
  return v;
}
void write_file(const std::string& p, const std::vector<std::uint8_t>& v) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
}
}

CG_TEST_CASE(Persistence_roundtrip) {
  std::string path = "cgtest_roundtrip.bin";
  if (StateStore::exists(path)) StateStore::remove(path);
  StateStore store(path);
  DurableSnapshot s = make_snap();
  CG_CHECK(store.save(s) == Status::OK);
  DurableSnapshot loaded = store.load();
  CG_CHECK(loaded.epoch == CoordinatorEpoch(3));
  CG_CHECK(loaded.policy.generation == CostPolicyGeneration(5));
  CG_CHECK(loaded.price_schedule.generation == PriceScheduleGeneration(2));
  CG_CHECK(loaded.budgets.size() == 1);
  CG_CHECK(loaded.budgets[0].id == BudgetId(1));
  CG_CHECK(loaded.budgets[0].consumed == MoneyMicros::from_units(40));
  CG_CHECK(loaded.history.size() == 1);
  CG_CHECK(loaded.history[0].request == RequestId(1));
  StateStore::remove(path);
}

CG_TEST_CASE(Persistence_corruption_rejected) {
  std::string path = "cgtest_corrupt.bin";
  StateStore store(path);
  CG_CHECK(store.save(make_snap()) == Status::OK);
  auto bytes = read_file(path);
  bytes[24] ^= 0xFF;  // flip a payload byte; CRC must mismatch
  write_file(path, bytes);
  bool threw = false;
  try { (void)store.load(); } catch (const StatusError& e) { threw = e.status() == Status::PERSISTENCE_CORRUPT; }
  CG_CHECK(threw);
  StateStore::remove(path);
}

CG_TEST_CASE(Persistence_truncation_rejected) {
  std::string path = "cgtest_trunc.bin";
  StateStore store(path);
  CG_CHECK(store.save(make_snap()) == Status::OK);
  auto bytes = read_file(path);
  bytes.resize(bytes.size() / 2);
  write_file(path, bytes);
  bool threw = false;
  try { (void)store.load(); } catch (const StatusError& e) { threw = e.status() == Status::PERSISTENCE_CORRUPT; }
  CG_CHECK(threw);
  StateStore::remove(path);
}

CG_TEST_CASE(Persistence_trailing_garbage_rejected) {
  std::string path = "cgtest_garbage.bin";
  StateStore store(path);
  CG_CHECK(store.save(make_snap()) == Status::OK);
  auto bytes = read_file(path);
  bytes.push_back(0xAA); bytes.push_back(0xBB); bytes.push_back(0xCC);
  write_file(path, bytes);
  bool threw = false;
  try { (void)store.load(); } catch (const StatusError& e) { threw = e.status() == Status::PERSISTENCE_CORRUPT; }
  CG_CHECK(threw);
  StateStore::remove(path);
}

CG_TEST_CASE(Persistence_unknown_version_rejected) {
  std::string path = "cgtest_version.bin";
  StateStore store(path);
  CG_CHECK(store.save(make_snap()) == Status::OK);
  auto bytes = read_file(path);
  // version is 4 bytes LE at offset 8
  bytes[8] = 0xFF; bytes[9] = 0xFF; bytes[10] = 0xFF; bytes[11] = 0xFF;
  write_file(path, bytes);
  bool threw = false;
  try { (void)store.load(); } catch (const StatusError& e) { threw = e.status() == Status::PERSISTENCE_CORRUPT; }
  CG_CHECK(threw);
  StateStore::remove(path);
}
