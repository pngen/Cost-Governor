#include "msgs.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <chrono>

using namespace costgovernor;
using namespace cgproto;

namespace {
bool connect_to(int port, Sock& out) {
#ifdef _WIN32
  WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
#endif
  SockHandle s;
#ifdef _WIN32
  s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  s = socket(AF_INET, SOCK_STREAM, 0);
#endif
  if (s == kInvalid) return false;
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<unsigned short>(port));
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
  out = Sock(s);
  return true;
}
void publish(FramedEndpoint& ep, PriceKind kind, DeviceId dev, MoneyMicros per_unit, WorkerBootId boot, WorkerId worker, std::int64_t now) {
  PriceObservation o;
  o.id = EvidenceId(1000 + static_cast<std::uint64_t>(kind) * 100 + dev.value());
  o.kind = kind; o.device = dev; o.amount = per_unit; o.currency = MoneyMicros::kDefaultCurrency;
  o.label = DataLabel::POLICY; o.provenance = Provenance::CONFIGURED_POLICY;
  o.observed_at_ms = now; o.epoch = CoordinatorEpoch(1); o.worker = worker; o.boot = boot;
  Writer w; encode_price(w, o); ep.send(Msg::PUBLISH_PRICE, w.data());
  Msg t; std::vector<std::uint8_t> payload; ep.recv(t, payload);
  Reader r(payload.data(), payload.size());
  Status st = static_cast<Status>(r.u8());
  std::cerr << "[worker] publish kind=" << static_cast<int>(kind) << " device=" << dev.value() << " status=" << status_name(st) << "\n";
}
}

int main(int argc, char** argv) {
  int port = argc > 1 ? std::atoi(argv[1]) : 0;
  const std::int64_t kNow = 1'000'000'000;
  std::string kind = argc > 2 ? argv[2] : "A";
  bool stay = false;
  std::uint64_t boot_override = 0;
  for (int i = 3; i < argc; ++i) {
    if (std::string(argv[i]) == "--stay") stay = true;
    else if (std::string(argv[i]) == "--boot" && i + 1 < argc) boot_override = std::strtoull(argv[++i], nullptr, 10);
  }
  if (port == 0) return 2;
  WorkerBootId boot = boot_override ? WorkerBootId(boot_override) : (kind == "A" ? WorkerBootId(10) : WorkerBootId(20));
  WorkerId worker(kind == "A" ? 1 : 2);
  DeviceId device(kind == "A" ? 1 : 2);
  std::int64_t now = kNow;
  FramedEndpoint ep;
  Sock s;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (connect_to(port, s)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!s.valid()) { std::cerr << "[worker " << kind << "] cannot connect\n"; return 1; }
  ep.sock = std::move(s);
  // HELLO (worker identity + incarnation)
  {
    Writer w; w.u64(worker.value()); w.u64(boot.value()); ep.send(Msg::HELLO, w.data());
    Msg t; std::vector<std::uint8_t> payload; ep.recv(t, payload);
  }
  if (kind == "A") {
    publish(ep, PriceKind::ACCELERATOR_TIME, device, MoneyMicros::from_micros(5000), boot, worker, now);  // high accel price
    publish(ep, PriceKind::TRANSFER, device, MoneyMicros::from_micros(1000), boot, worker, now);          // low transfer price
  } else {
    publish(ep, PriceKind::ACCELERATOR_TIME, device, MoneyMicros::from_micros(1000), boot, worker, now);   // low accel price
    publish(ep, PriceKind::TRANSFER, device, MoneyMicros::from_micros(10000), boot, worker, now);          // high transfer price
  }
  std::cerr << "[worker " << kind << "] published (boot=" << boot.value() << ")\n";
  if (stay) {
    while (true) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); Writer w; ep.send(Msg::PING, w.data()); Msg t; std::vector<std::uint8_t> py; if (!ep.recv(t, py)) break; }
  }
  return 0;
}
