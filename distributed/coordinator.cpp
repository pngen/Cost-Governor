#include "msgs.hpp"
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace costgovernor;
using namespace cgproto;

namespace {

struct Session {
  CostGovernor& gov;
  std::mutex& res_mtx;
  std::map<std::uint64_t, BudgetReservation>& reservations;
  std::atomic<std::uint64_t>& next_res;
  CostDecision& last_decision;
  std::mutex& last_mtx;
  FramedEndpoint ep;

  void reply(Msg type, const std::vector<std::uint8_t>& payload) { (void)ep.send(type, payload); }
  void reply_status(Msg type, Status st) {
    Writer w; w.u8(static_cast<std::uint8_t>(st)); w.u64(0);
    reply(type, w.data());
  }
  void run() {
    while (true) {
      Msg type; std::vector<std::uint8_t> payload;
      try {
        if (!ep.recv(type, payload)) break;
      } catch (const std::exception& e) {
        std::cerr << "[coord] recv protocol error: " << e.what() << "\n";
        // malformed frame: reply an explicit status and keep going
        Writer w; w.u8(static_cast<std::uint8_t>(Status::PROTOCOL_ERROR)); w.u64(0);
        reply(Msg::PONG, w.data());
        continue;
      }
      try {
        handle(type, payload);
      } catch (const std::exception& e) {
        std::cerr << "[coord] handle error: " << e.what() << "\n";
        Writer w; w.u8(static_cast<std::uint8_t>(Status::INTERVENTION_FAILED)); w.u64(0);
        reply(Msg::PONG, w.data());
      }
    }
  }
  void handle(Msg type, const std::vector<std::uint8_t>& payload) {
    Reader r(payload.data(), payload.size());
    switch (type) {
      case Msg::HELLO: {
        WorkerBootId boot = get_id<WorkerBootId>(r);
        gov.set_worker_boot(boot);
        Writer w; w.u64(gov.epoch().value()); w.u64(boot.value());
        reply(Msg::PONG, w.data());
        break;
      }
      case Msg::PUBLISH_PRICE: {
        PriceObservation o = decode_price(r);
        Status st = gov.publish_price(o);
        if (st == Status::OK) gov.persist();
        reply_status(Msg::PONG, st);
        break;
      }
      case Msg::REQUEST_EVAL: {
        WorkloadId wl = get_id<WorkloadId>(r);
        WorkloadGeneration wg = get_id<WorkloadGeneration>(r);
        std::uint32_t n = r.u32();
        if (n > 4096) throw std::runtime_error("plan count too large");
        std::vector<ExecutionPlan> plans; plans.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) plans.push_back(decode_plan(r));
        CostDecision d = gov.evaluate(wl, wg, plans);
        { std::lock_guard<std::mutex> lk(last_mtx); last_decision = d; }
        Writer w; encode_decision(w, d);
        reply(Msg::EVAL_RESULT, w.data());
        break;
      }
      case Msg::RECORD_ATTEMPT: {
        CostEvidence e = decode_evidence(r);
        Status st = gov.record_attempt(e);
        if (st == Status::OK) gov.persist();
        reply_status(Msg::PONG, st);
        break;
      }
      case Msg::RESERVE: {
        BudgetId bid = get_id<BudgetId>(r); BudgetGeneration bg = get_id<BudgetGeneration>(r);
        CoordinatorEpoch epch = get_id<CoordinatorEpoch>(r); MoneyMicros amt = MoneyMicros::from_micros(r.i64());
        BudgetReservation res;
        Status st = gov.reserve(bid, bg, epch, amt, res);
        Writer w; w.u8(static_cast<std::uint8_t>(st));
        if (st == Status::OK) { w.u64(res.id.value()); w.u64(res.budget_generation.value()); w.u64(res.epoch.value()); w.i64(res.amount.total_micros()); }
        else { w.u64(0); w.u64(0); w.u64(0); w.i64(0); }
        { std::lock_guard<std::mutex> lk(res_mtx); if (st == Status::OK) reservations[res.id.value()] = res; }
        reply(Msg::PONG, w.data());
        break;
      }
      case Msg::COMMIT: case Msg::RELEASE: {
        std::uint64_t token = r.u64();
        BudgetReservation res;
        { std::lock_guard<std::mutex> lk(res_mtx); auto it = reservations.find(token); if (it == reservations.end()) { reply_status(Msg::PONG, Status::INVALID_INPUT); return; } res = it->second; }
        Status st = (type == Msg::COMMIT) ? gov.commit(res) : gov.release(res);
        { std::lock_guard<std::mutex> lk(res_mtx); reservations[token] = res; }
        reply_status(Msg::PONG, st);
        break;
      }
      case Msg::REALIZE: {
        RequestId req = get_id<RequestId>(r); WorkloadId wl = get_id<WorkloadId>(r);
        Status st = gov.finalize_request(req, wl);
        Writer w; w.u8(static_cast<std::uint8_t>(st)); w.i64(gov.realized_request_cost(req).total_micros());
        reply(Msg::PONG, w.data());
        break;
      }
      case Msg::GET_STATE: {
        Writer w; w.u64(gov.epoch().value()); w.u64(gov.price_schedule().generation.value());
        w.u64(gov.policy().generation.value()); w.u64(gov.budgets().size());
        w.u64(gov.price_schedule().observations.size());
        reply(Msg::PONG, w.data());
        break;
      }
      case Msg::REVALIDATE: {
        std::uint32_t n = r.u32();
        if (n > 4096) throw std::runtime_error("plan count too large");
        std::vector<ExecutionPlan> plans; plans.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) plans.push_back(decode_plan(r));
        CostDecision last;
        { std::lock_guard<std::mutex> lk(last_mtx); last = last_decision; }
        Status st = gov.validate_for_dispatch(last, plans);
        reply_status(Msg::PONG, st);
        break;
      }
      case Msg::QUIT: break;
      default: reply_status(Msg::PONG, Status::PROTOCOL_ERROR); break;
    }
  }
};

void run_session(CostGovernor& gov, std::mutex& res_mtx, std::map<std::uint64_t, BudgetReservation>& res_map,
                 std::atomic<std::uint64_t>& next_res, CostDecision& last, std::mutex& last_mtx, Sock sock) {
  if (!sock.valid()) return;
  Session s{gov, res_mtx, res_map, next_res, last, last_mtx, FramedEndpoint{std::move(sock)}};
  s.run();
}

}  // namespace

int main(int argc, char** argv) {
  int port = argc > 1 ? std::atoi(argv[1]) : 0;
  std::string state_file = argc > 2 ? argv[2] : "cg_dist_state.bin";
  if (port == 0) { std::cerr << "coordinator: usage: coordinator <port> [state]" << "\n"; return 2; }

#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 3;
#endif

  std::int64_t kNow = 1'000'000'000;
  CostGovernor gov(std::make_shared<MockClock>(kNow));
  gov.set_state_path(state_file);
  CostPolicy p; p.currency = MoneyMicros::kDefaultCurrency;
  p.epoch = CoordinatorEpoch(1); p.generation = CostPolicyGeneration(1);
  p.max_evidence_age_ms = 120000; p.price_schedule_max_age_ms = 120000;
  p.allow_policy = true; p.allow_measured = true; p.allow_synthetic = true;
  gov.set_policy(p);
  if (CostGovernor::state_exists(state_file)) {
    gov.load();
    gov.advance_epoch();   // coordinator restart => fresh epoch
    std::cerr << "[coord] recovered state; epoch=" << gov.epoch().value() << "\n";
  } else {
    PriceSchedule s; s.id = PriceScheduleId(1); s.generation = PriceScheduleGeneration(1); s.created_at_ms = kNow;
    gov.set_price_schedule(s);
    Budget b; b.id = BudgetId(1); b.generation = BudgetGeneration(1); b.policy_generation = gov.policy().generation;
    b.epoch = gov.epoch(); b.limit = MoneyMicros::from_units(1); b.mode = BudgetMode::HARD;
    gov.create_budget(b);
    std::cerr << "[coord] fresh state; epoch=" << gov.epoch().value() << "\n";
  }

  // listen
  SockHandle ls = kInvalid;
#ifdef _WIN32
  ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  ls = socket(AF_INET, SOCK_STREAM, 0);
#endif
  if (ls == kInvalid) return 4;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<unsigned short>(port));
  if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return 5;
  if (listen(ls, 64) != 0) return 6;

  std::mutex res_mtx;
  std::map<std::uint64_t, BudgetReservation> res_map;
  std::atomic<std::uint64_t> next_res{1000};
  CostDecision last_decision;
  std::mutex last_mtx;

  std::cerr << "[coord] listening on 127.0.0.1:" << port << "\n";
  while (true) {
    sockaddr_in peer{}; int plen = sizeof(peer);
#ifdef _WIN32
    SOCKET c = accept(ls, reinterpret_cast<sockaddr*>(&peer), &plen);
#else
    int c = accept(ls, reinterpret_cast<sockaddr*>(&peer), reinterpret_cast<socklen_t*>(&plen));
#endif
    if (c == kInvalid) break;
    std::thread(&run_session, std::ref(gov), std::ref(res_mtx), std::ref(res_map), std::ref(next_res), std::ref(last_decision), std::ref(last_mtx), Sock(c)).detach();
  }
  return 0;
}
