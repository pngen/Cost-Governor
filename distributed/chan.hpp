#pragma once
// Distributed proof transport: framed, versioned, bounded, checksummed TCP.
// Handles partial reads/writes and concurrent writes. Type-safe over a small
// message set serialized with a compact LE binary codec.
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
  // nothing
#endif
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  using SockHandle = SOCKET;
  constexpr SockHandle kInvalid = INVALID_SOCKET;
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <cerrno>
  #include <cstring>
  using SockHandle = int;
  constexpr SockHandle kInvalid = -1;
#endif

namespace cgproto {

constexpr std::uint32_t kMagic = 0x43475301;  // "CGS1"
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kMaxPayload = 1u << 20;  // 1 MiB bound

enum class Msg : std::uint8_t {
  HELLO = 1,
  PUBLISH_PRICE = 2,
  REQUEST_EVAL = 3,
  EVAL_RESULT = 4,
  RECORD_ATTEMPT = 5,
  REALIZE = 6,
  GET_STATE = 7,
  QUIT = 8,
  RESERVE = 9,
  COMMIT = 10,
  RELEASE = 11,
  PING = 12,
  PONG = 13,
  REVALIDATE = 14,
};

// ---- compact LE binary codec -------------------------------------------------
struct Writer {
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) { for (int i = 0; i < 2; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); buf_.insert(buf_.end(), s.begin(), s.end()); }
  [[nodiscard]] const std::vector<std::uint8_t>& data() const { return buf_; }
 private:
  std::vector<std::uint8_t> buf_;
};
struct Reader {
  Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
  std::uint8_t u8() { need(1); return p_[pos_++]; }
  std::uint16_t u16() { need(2); std::uint16_t v = 0; for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(p_[pos_++]) << (8 * i); return v; }
  std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p_[pos_++]) << (8 * i); return v; }
  std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p_[pos_++]) << (8 * i); return v; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  std::string str() { std::uint32_t len = u32(); if (len > kMaxPayload) throw std::runtime_error("string too large"); need(len); std::string s(reinterpret_cast<const char*>(p_ + pos_), len); pos_ += len; return s; }
  [[nodiscard]] std::size_t remaining() const { return n_ - pos_; }
  [[nodiscard]] std::size_t pos() const { return pos_; }
  void expect_end() { if (pos_ != n_) throw std::runtime_error("protocol trailing bytes"); }
 private:
  void need(std::size_t k) { if (k > n_ - pos_) throw std::runtime_error("protocol truncated"); }
  const std::uint8_t* p_; std::size_t n_; std::size_t pos_ = 0;
};

inline std::uint32_t crc32(const std::uint8_t* d, std::size_t n) noexcept {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) { crc ^= d[i]; for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1u))); }
  return ~crc;
}

// ---- socket wrapper ----------------------------------------------------------
class Sock {
 public:
  Sock() = default;
  explicit Sock(SockHandle h) : h_(h) {}
  ~Sock() { close(); }
  Sock(const Sock&) = delete;
  Sock& operator=(const Sock&) = delete;
  Sock(Sock&& o) noexcept : h_(o.h_) { o.h_ = kInvalid; }
  Sock& operator=(Sock&& o) noexcept { if (this != &o) { close(); h_ = o.h_; o.h_ = kInvalid; } return *this; }
  [[nodiscard]] bool valid() const noexcept { return h_ != kInvalid; }
  void close() noexcept {
#ifdef _WIN32
    if (h_ != kInvalid) { closesocket(h_); h_ = kInvalid; }
#else
    if (h_ != kInvalid) { ::close(h_); h_ = kInvalid; }
#endif
  }
  [[nodiscard]] SockHandle raw() const noexcept { return h_; }
  // send all bytes; returns false on error
  bool send_all(const std::uint8_t* d, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
      int sent;
#ifdef _WIN32
      sent = ::send(h_, reinterpret_cast<const char*>(d + off), static_cast<int>(n - off), 0);
#else
      sent = ::send(h_, d + off, n - off, MSG_NOSIGNAL);
#endif
      if (sent == 0) return false;
      if (sent < 0) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
#endif
        return false;
      }
      off += static_cast<std::size_t>(sent);
    }
    return true;
  }
  // read exactly n bytes; returns false on EOF/error
  bool recv_all(std::uint8_t* d, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
      int r;
#ifdef _WIN32
      r = ::recv(h_, reinterpret_cast<char*>(d + off), static_cast<int>(n - off), 0);
#else
      r = ::recv(h_, d + off, n - off, 0);
#endif
      if (r == 0) return false;
      if (r < 0) return false;
      off += static_cast<std::size_t>(r);
    }
    return true;
  }
 private:
  SockHandle h_ = kInvalid;
};

struct FramedEndpoint {
  Sock sock;
  // read one frame; returns false on transport error/EOF. Throws on protocol error.
  bool recv(Msg& type, std::vector<std::uint8_t>& payload) {
    std::uint8_t hdr[16];
    if (!sock.recv_all(hdr, 16)) return false;
    Reader hr(hdr, 16);
    std::uint32_t magic = hr.u32();
    std::uint16_t ver = hr.u16();
    std::uint8_t t = hr.u8();
    hr.u8();                        // flags
    std::uint32_t len = hr.u32();
    std::uint32_t crc = hr.u32();
    if (magic != kMagic) throw std::runtime_error("frame magic mismatch");
    if (ver != kVersion) throw std::runtime_error("frame version mismatch");
    if (len > kMaxPayload) throw std::runtime_error("frame too large");
    payload.resize(len);
    if (len && !sock.recv_all(payload.data(), len)) return false;
    std::uint32_t calc = crc32(payload.data(), len);
    if (calc != crc) throw std::runtime_error("frame checksum mismatch");
    type = static_cast<Msg>(t);
    return true;
  }
  bool send(Msg type, const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxPayload) throw std::runtime_error("frame too large to send");
    std::uint8_t hdr[16];
    Writer w;
    w.u32(kMagic); w.u16(kVersion); w.u8(static_cast<std::uint8_t>(type)); w.u8(0);
    w.u32(static_cast<std::uint32_t>(payload.size()));
    std::uint32_t crc = crc32(payload.data(), payload.size());
    w.u32(crc);
    const auto& hb = w.data();
    for (int i = 0; i < 16; ++i) hdr[i] = hb[i];
    if (!sock.send_all(hdr, 16)) return false;
    if (!payload.empty() && !sock.send_all(payload.data(), payload.size())) return false;
    return true;
  }
};

}  // namespace cgproto
