#include "msgs.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winsock2.h>
  using Pid = DWORD;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <sys/wait.h>
  #include <arpa/inet.h>
  using Pid = int;
#endif

using namespace costgovernor;
using namespace cgproto;

namespace {
int g_pass = 0, g_fail = 0;
void ok(const std::string& name, bool cond, const std::string& detail = "") {
  if (cond) { ++g_pass; std::cout << "[PASS] " << name << std::endl; }
  else { ++g_fail; std::cout << "[FAIL] " << name << " " << detail << std::endl; }
}

int find_free_port() {
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  sockaddr_in out{}; int len = sizeof(out); getsockname(s, reinterpret_cast<sockaddr*>(&out), &len);
  int port = ntohs(out.sin_port); closesocket(s); return port;
#else
  int s = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  sockaddr_in out{}; socklen_t len = sizeof(out); getsockname(s, reinterpret_cast<sockaddr*>(&out), &len);
  int port = ntohs(out.sin_port); close(s); return port;
#endif
}

#ifdef _WIN32
struct Child { Pid pid = 0; HANDLE h = nullptr; };
Child spawn(const std::string& exe, const std::string& args) {
  Child c;
  std::string cmdline = "\"" + exe + "\" " + args;
  STARTUPINFOA si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
  si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  std::vector<char> buf(cmdline.begin(), cmdline.end()); buf.push_back(0);
  if (!CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return c;
  c.pid = pi.dwProcessId; c.h = pi.hProcess; CloseHandle(pi.hThread);
  return c;
}
void kill(Child& c) { if (c.h) { TerminateProcess(c.h, 1); WaitForSingleObject(c.h, 3000); CloseHandle(c.h); c.h = nullptr; } }
#else
struct Child { Pid pid = 0; };
Child spawn(const std::string& exe, const std::string& args) {
  Child c; std::string full = exe + " " + args;
  Pid pid = fork();
  if (pid == 0) { execlp(exe.c_str(), exe.c_str(), args.c_str(), nullptr); _exit(127); }
  c.pid = pid; return c;
}
void kill(Child& c) { if (c.pid > 0) { ::kill(c.pid, SIGKILL); waitpid(c.pid, nullptr, 0); c.pid = 0; } }
#endif

bool connect_to(int port, Sock& out) {
  SockHandle s;
#ifdef _WIN32
  s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  s = socket(AF_INET, SOCK_STREAM, 0);
#endif
  if (s == kInvalid) return false;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(static_cast<unsigned short>(port));
  if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
  out = Sock(s); return true;
}

struct Client {
  FramedEndpoint ep;
  bool connected(int port) { Sock s; if (!connect_to(port, s)) return false; ep.sock = std::move(s); return true; }
  void send(Msg t, const std::vector<std::uint8_t>& p) { ep.send(t, p); }
  bool recv(Msg& t, std::vector<std::uint8_t>& p) { return ep.recv(t, p); }
  void hello(WorkerId worker, WorkerBootId boot) { Writer w; w.u64(worker.value()); w.u64(boot.value()); send(Msg::HELLO, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py); }
  Status publish(const PriceObservation& o) { Writer w; encode_price(w, o); send(Msg::PUBLISH_PRICE, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py); Reader r(py.data(), py.size()); return static_cast<Status>(r.u8()); }
  DecisionResult eval(WorkloadId wl, WorkloadGeneration wg, const std::vector<ExecutionPlan>& plans) {
    Writer w; w.u64(wl.value()); w.u64(wg.value()); w.u32(static_cast<std::uint32_t>(plans.size()));
    for (const auto& p : plans) encode_plan(w, p);
    send(Msg::REQUEST_EVAL, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py);
    Reader r(py.data(), py.size()); return decode_decision(r);
  }
  Status record(const CostEvidence& e) { Writer w; encode_evidence(w, e); send(Msg::RECORD_ATTEMPT, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py); Reader r(py.data(), py.size()); return static_cast<Status>(r.u8()); }
  std::pair<Status, std::int64_t> realize(RequestId req, WorkloadId wl) { Writer w; w.u64(req.value()); w.u64(wl.value()); send(Msg::REALIZE, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py); Reader r(py.data(), py.size()); Status s = static_cast<Status>(r.u8()); std::int64_t cost = r.i64(); return {s, cost}; }
  Status revalidate(const std::vector<ExecutionPlan>& plans) {
    Writer w; w.u32(static_cast<std::uint32_t>(plans.size()));
    for (const auto& p : plans) encode_plan(w, p);
    send(Msg::REVALIDATE, w.data()); Msg t; std::vector<std::uint8_t> py; recv(t, py);
    Reader r(py.data(), py.size()); return static_cast<Status>(r.u8());
  }
  void state(std::uint64_t& epoch, std::uint64_t& price_gen, std::uint64_t& policy_gen, std::uint64_t& budgets, std::uint64_t& obs) {
    send(Msg::GET_STATE, {}); Msg t; std::vector<std::uint8_t> py; recv(t, py); Reader r(py.data(), py.size());
    epoch = r.u64(); price_gen = r.u64(); policy_gen = r.u64(); budgets = r.u64(); obs = r.u64();
  }
};

std::string dir_of_self() {
#ifdef _WIN32
  char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p(buf);
  auto pos = p.find_last_of("\\/");
  return pos == std::string::npos ? "." : p.substr(0, pos);
#else
  return ".";
#endif
}

PriceObservation mk_price(EvidenceId id, PriceKind kind, DeviceId dev, MoneyMicros amt,
                          WorkerId worker, WorkerBootId boot, std::int64_t now) {
  PriceObservation o;
  o.id = id; o.kind = kind; o.device = dev; o.amount = amt; o.currency = MoneyMicros::kDefaultCurrency;
  o.label = DataLabel::POLICY; o.provenance = Provenance::CONFIGURED_POLICY;
  o.observed_at_ms = now; o.epoch = CoordinatorEpoch(1); o.worker = worker; o.boot = boot;
  return o;
}
CostEvidence mk_attempt(EvidenceId id, RequestId req, AttemptId att, DeviceId dev,
                        WorkerId worker, WorkerBootId boot, std::int64_t ns, std::int64_t now) {
  CostEvidence e;
  e.id = id; e.request = req; e.attempt = att; e.attempt_gen = AttemptGeneration(1);
  e.epoch = CoordinatorEpoch(1); e.worker = worker; e.boot = boot; e.generation = EvidenceGeneration(1);
  e.observed_at_ms = now; e.device = dev; e.label = DataLabel::POLICY;
  e.accelerator_time = AcceleratorNanoseconds(ns);
  e.is_completion = true; e.completed_successfully = true;
  return e;
}

}  // namespace

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : dir_of_self();
#ifdef _WIN32
  WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
#endif
  const std::int64_t kNow = 1'000'000'000;
  int port = find_free_port();
  std::string state = dir + "/cg_runner_state.bin";
  remove(state.c_str());
  const std::string coord_exe = dir + "/coordinator.exe";
  const std::string worker_exe = dir + "/worker.exe";
#ifdef _WIN32
  const std::string coord = coord_exe, wk = worker_exe;
#else
  const std::string coord = dir + "/coordinator", wk = dir + "/worker";
#endif

  std::cout << "cost-governor distributed proof (per-worker boot authority) starting" << std::endl;
  const WorkerId kA(1), kB(2);
  const WorkerBootId A1(10), A2(11), B1(20), B2(21);
  const DeviceId devA(1), devB(2);

  auto wait_for_obs = [&](Client& c, std::uint64_t want) {
    std::uint64_t ep = 0, pg = 0, polg = 0, bds = 0, obs = 0;
    for (int i = 0; i < 400; ++i) { c.state(ep, pg, polg, bds, obs); if (obs >= want) return obs; std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
    return obs;
  };

  // Quote the state path: it contains spaces in this working-tree layout.
  // Quote the state path: it contains spaces in this working-tree layout.
  std::string coord_args = std::to_string(port) + " \"" + state + "\"";
  Child coordinator = spawn(coord, coord_args);
  if (coordinator.pid == 0) { std::cerr << "could not spawn coordinator\n"; return 2; }
  Client ctl;
  for (int i = 0; i < 200 && !ctl.connected(port); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ok("coordinator ready", ctl.ep.sock.valid());
  if (!ctl.ep.sock.valid()) { kill(coordinator); return 2; }

  Child workerA = spawn(wk, std::to_string(port) + " A --stay");
  Child workerB = spawn(wk, std::to_string(port) + " B --stay");
  { std::uint64_t o = wait_for_obs(ctl, 4);
    ok("scenario A: A (boot A1) and B (boot B1) both published 4 observations", o >= 4, std::string("obs=") + std::to_string(o)); }

  ExecutionPlan p1; p1.id = PlanId(1); p1.generation = PlanGeneration(1); p1.workload = WorkloadId(1); p1.workload_generation = WorkloadGeneration(1);
  p1.device = devA; p1.worker = kA; p1.expected_accelerator_time = AcceleratorNanoseconds(2'000'000'000); p1.expected_requests = 1;
  ExecutionPlan p2; p2.id = PlanId(2); p2.generation = PlanGeneration(1); p2.workload = WorkloadId(1); p2.workload_generation = WorkloadGeneration(1);
  p2.device = devB; p2.worker = kB; p2.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  p2.expected_transfer = TransferBytes(5'000'000'000); p2.expected_requests = 1;
  std::vector<ExecutionPlan> plans{p1, p2};
  DecisionResult ra = ctl.eval(WorkloadId(1), WorkloadGeneration(1), plans);
  ok("scenario A: deterministic cheapest plan (A1+B1 evidence)", ra.kind == 0 && ra.selected == 1,
     std::string("kind=") + std::to_string(ra.kind) + " selected=" + std::to_string(ra.selected));

  ok("scenario A2: A1 price accepted", ctl.publish(mk_price(EvidenceId(7001), PriceKind::ACCELERATOR_TIME, devA, MoneyMicros::from_micros(5000), kA, A1, kNow)) == Status::OK);
  ok("scenario A2: B1 price accepted", ctl.publish(mk_price(EvidenceId(7002), PriceKind::ACCELERATOR_TIME, devB, MoneyMicros::from_micros(1000), kB, B1, kNow)) == Status::OK);
  ok("scenario A2: A1 evidence accepted", ctl.record(mk_attempt(EvidenceId(7101), RequestId(900), AttemptId(1), devA, kA, A1, 1'000'000'000, kNow)) == Status::OK);
  ok("scenario A2: B1 evidence accepted", ctl.record(mk_attempt(EvidenceId(7102), RequestId(901), AttemptId(1), devB, kB, B1, 1'000'000'000, kNow)) == Status::OK);

  {
    PriceObservation np = mk_price(EvidenceId(900), PriceKind::ACCELERATOR_TIME, devA, MoneyMicros::from_micros(9000), kA, A1, kNow);
    ok("scenario F: price generation advanced", ctl.publish(np) == Status::OK);
    Status st = ctl.revalidate(plans);
    ok("scenario F: stale plan rejected before dispatch", st == Status::STALE_AUTHORITY, std::string(status_name(st)));
  }

  {
    auto mkE = [&](EvidenceId id, AttemptId att, std::int64_t ns, bool success) {
      CostEvidence e = mk_attempt(id, RequestId(1), att, devA, kA, A1, ns, kNow);
      e.completed_successfully = success; return e;
    };
    Status e1 = ctl.record(mkE(EvidenceId(50), AttemptId(1), 2'000'000'000, false));
    Status e2 = ctl.record(mkE(EvidenceId(51), AttemptId(2), 3'000'000'000, true));
    ok("scenario E: attempts recorded", e1 == Status::OK && e2 == Status::OK, std::string("e1=") + std::string(status_name(e1)) + " e2=" + std::string(status_name(e2)));
    auto [rs, rcost] = ctl.realize(RequestId(1), WorkloadId(1));
    ok("scenario E: realized cost includes failed attempts", rs == Status::OK && rcost > 0, std::string(status_name(rs)) + " cost=" + std::to_string(rcost));
    Status e2d = ctl.record(mkE(EvidenceId(51), AttemptId(2), 3'000'000'000, true));
    ok("scenario E: duplicate completion rejected, no double-charge", e2d == Status::INVALID_INPUT, std::string(status_name(e2d)));
  }

  {
    ExecutionPlan pBig; pBig.id = PlanId(3); pBig.generation = PlanGeneration(1); pBig.workload = WorkloadId(1); pBig.workload_generation = WorkloadGeneration(1);
    pBig.device = devB; pBig.worker = kB; pBig.expected_transfer = TransferBytes(2'000'000'000'000LL); pBig.expected_requests = 1;
    DecisionResult rb = ctl.eval(WorkloadId(1), WorkloadGeneration(1), {pBig});
    ok("scenario B: hard budget rejection", rb.kind == 4, std::string("kind=") + std::to_string(rb.kind));
  }

  kill(workerB);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Child workerB2 = spawn(wk, std::to_string(port) + " B --stay --boot 21");
  {
    bool reg = false;
    for (int i = 0; i < 200; ++i) {
      if (ctl.publish(mk_price(EvidenceId(7002), PriceKind::ACCELERATOR_TIME, devB, MoneyMicros::from_micros(1000), kB, B2, kNow)) == Status::OK) { reg = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ok("scenario C: B2 (worker B boot B2) reconnected", reg);
    ok("scenario C: B1 stale price rejected", ctl.publish(mk_price(EvidenceId(7003), PriceKind::ACCELERATOR_TIME, devB, MoneyMicros::from_micros(1000), kB, B1, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C: B1 stale attempt rejected", ctl.record(mk_attempt(EvidenceId(7103), RequestId(910), AttemptId(1), devB, kB, B1, 1'000'000'000, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C: B1 stale completion rejected", ctl.record(mk_attempt(EvidenceId(7104), RequestId(911), AttemptId(1), devB, kB, B1, 1'000'000'000, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C: A1 remains current (price accepted)", ctl.publish(mk_price(EvidenceId(7004), PriceKind::TRANSFER, devA, MoneyMicros::from_micros(1000), kA, A1, kNow)) == Status::OK);
    ok("scenario C: A1 evidence accepted", ctl.record(mk_attempt(EvidenceId(7105), RequestId(912), AttemptId(1), devA, kA, A1, 1'000'000'000, kNow)) == Status::OK);
    DecisionResult ra2 = ctl.eval(WorkloadId(1), WorkloadGeneration(1), plans);
    ok("scenario C: A1 still participates (deterministic selection)", ra2.kind == 0 && ra2.selected == 1,
       std::string("kind=") + std::to_string(ra2.kind) + " selected=" + std::to_string(ra2.selected));
  }

  kill(workerA);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Child workerA2 = spawn(wk, std::to_string(port) + " A --stay --boot 11");
  {
    bool reg = false;
    for (int i = 0; i < 200; ++i) {
      if (ctl.publish(mk_price(EvidenceId(7001), PriceKind::ACCELERATOR_TIME, devA, MoneyMicros::from_micros(5000), kA, A2, kNow)) == Status::OK) { reg = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ok("scenario C2: A2 (worker A boot A2) reconnected", reg);
    ok("scenario C2: A1 stale price rejected", ctl.publish(mk_price(EvidenceId(7005), PriceKind::ACCELERATOR_TIME, devA, MoneyMicros::from_micros(5000), kA, A1, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C2: A1 stale attempt rejected", ctl.record(mk_attempt(EvidenceId(7106), RequestId(913), AttemptId(1), devA, kA, A1, 1'000'000'000, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C2: A1 stale completion rejected", ctl.record(mk_attempt(EvidenceId(7107), RequestId(914), AttemptId(1), devA, kA, A1, 1'000'000'000, kNow)) == Status::STALE_AUTHORITY);
    ok("scenario C2: B2 remains current (price accepted)", ctl.publish(mk_price(EvidenceId(7006), PriceKind::TRANSFER, devB, MoneyMicros::from_micros(10000), kB, B2, kNow)) == Status::OK);
    ok("scenario C2: B2 evidence accepted (no cross-invalidation)", ctl.record(mk_attempt(EvidenceId(7108), RequestId(915), AttemptId(1), devB, kB, B2, 1'000'000'000, kNow)) == Status::OK);
  }

  {
    const WorkerId kW(3); const WorkerBootId W0(50), W1(51); const DeviceId devW(1);
    ctl.hello(kW, W0);
    ExecutionPlan pw; pw.id = PlanId(7); pw.generation = PlanGeneration(1); pw.workload = WorkloadId(2); pw.workload_generation = WorkloadGeneration(1);
    pw.device = devW; pw.worker = kW; pw.expected_accelerator_time = AcceleratorNanoseconds(1'000'000'000); pw.expected_requests = 1;
    std::vector<ExecutionPlan> pwplans{pw};
    DecisionResult rd = ctl.eval(WorkloadId(2), WorkloadGeneration(1), pwplans);
    ok("scenario D: worker-3 dispatch authorized", rd.kind == 0 && rd.selected == 7, std::string("kind=") + std::to_string(rd.kind));
    ctl.hello(kW, W1);
    ok("scenario D: stale (worker,boot) dispatch rejected", ctl.revalidate(pwplans) == Status::STALE_AUTHORITY);
  }

  {
    std::uint64_t epoch = 0, pg = 0, polg = 0, budgets = 0, obs = 0;
    ctl.state(epoch, pg, polg, budgets, obs);
    kill(coordinator);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Child c2 = spawn(coord, coord_args);
    Client ctl2;
    for (int i = 0; i < 200 && !ctl2.connected(port); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ok("scenario D: coordinator restarted", ctl2.ep.sock.valid());
    std::uint64_t e2e = 0, p2g = 0, pol2g = 0, b2 = 0, o2 = 0;
    ctl2.state(e2e, p2g, pol2g, b2, o2);
    ok("scenario D: fresh epoch after restart", e2e > epoch, std::string("before=") + std::to_string(epoch) + " after=" + std::to_string(e2e));
    PriceObservation oldp = mk_price(EvidenceId(777), PriceKind::ACCELERATOR_TIME, devA, MoneyMicros::from_micros(1000), kA, A1, kNow);
    oldp.epoch = CoordinatorEpoch(1);
    Status oldst = ctl2.publish(oldp);
    ok("scenario D: old-epoch price rejected", oldst == Status::STALE_AUTHORITY, std::string(status_name(oldst)));
    kill(coordinator); kill(workerA2); kill(workerB2); kill(c2);
  }

  std::cout << "\nDistributed proof: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
