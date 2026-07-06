#include "Transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>

#include <android/log.h>

#include <cstring>
#include <ctime>
#include <random>

#define TTAG "EcclesTransport"
#define TLOG(...) __android_log_print(ANDROID_LOG_DEBUG, TTAG, __VA_ARGS__)

ECCLES_API {

namespace {

std::string base64Encode(const e_uint8* data, size_t len) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    e_uint32 n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += table[(n >> 18) & 0x3F]; out += table[(n >> 12) & 0x3F];
    out += table[(n >> 6) & 0x3F];  out += table[n & 0x3F];
    i += 3;
  }
  const size_t rem = len - i;
  if (rem == 1) {
    e_uint32 n = data[i] << 16;
    out += table[(n >> 18) & 0x3F]; out += table[(n >> 12) & 0x3F]; out += "==";
  } else if (rem == 2) {
    e_uint32 n = (data[i] << 16) | (data[i + 1] << 8);
    out += table[(n >> 18) & 0x3F]; out += table[(n >> 12) & 0x3F]; out += table[(n >> 6) & 0x3F]; out += "=";
  }
  return out;
}

std::string randomWebSocketKey() {
  e_uint8 raw[16];
  std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)) ^ static_cast<unsigned>(getpid()));
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& b : raw) b = static_cast<e_uint8>(dist(rng));
  return base64Encode(raw, sizeof(raw));
}

}  // namespace

WebSocketTransport::WebSocketTransport(std::string host, uint16_t port, std::string path)
    : host_(std::move(host)), port_(port), path_(std::move(path)) {}

WebSocketTransport::~WebSocketTransport() { disconnect(); }

e_boolean WebSocketTransport::connect() {
  if (connected_) return true;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* resolved = nullptr;
  char portStr[6];
  snprintf(portStr, sizeof(portStr), "%u", port_);

  const int rc = getaddrinfo(host_.c_str(), portStr, &hints, &resolved);
  if (rc != 0 || resolved == nullptr) {
    TLOG("getaddrinfo(%s) failed: %s", host_.c_str(), gai_strerror(rc));
    return false;
  }

  int fd = -1;
  for (addrinfo* p = resolved; p != nullptr; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(resolved);

  if (fd < 0) {
    TLOG("connect() to %s:%u failed: %s", host_.c_str(), port_, strerror(errno));
    return false;
  }

  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  socketFd_ = fd;
  if (!performHandshake()) {
    ::close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  connected_ = true;
  TLOG("websocket connected to %s:%u%s", host_.c_str(), port_, path_.c_str());
  return true;
}

e_boolean WebSocketTransport::performHandshake() {
  const std::string key = randomWebSocketKey();

  std::string request;
  request += "GET " + path_ + " HTTP/1.1\r\n";
  request += "Host: " + host_ + "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: " + key + "\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += "\r\n";

  if (::send(socketFd_, request.data(), request.size(), 0) < 0) {
    TLOG("handshake send() failed: %s", strerror(errno));
    return false;
  }

  char buf[512];
  const ssize_t n = recv(socketFd_, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    TLOG("handshake recv() failed: %s", strerror(errno));
    return false;
  }
  buf[n] = '\0';

  if (std::strstr(buf, "101") == nullptr || std::strstr(buf, "Upgrade") == nullptr) {
    TLOG("handshake rejected, response: %s", buf);
    return false;
  }
  return true;
}

e_boolean WebSocketTransport::sendFrame(const e_uint8* data, size_t len, e_uint8 opcode) {
  if (socketFd_ < 0) return false;

  std::vector<e_uint8> frame;
  frame.push_back(0x80 | (opcode & 0x0F)); // FIN=1

  if (len <= 125) {
    frame.push_back(0x80 | static_cast<e_uint8>(len));
  } else if (len <= 0xFFFF) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<e_uint8>((len >> 8) & 0xFF));
    frame.push_back(static_cast<e_uint8>(len & 0xFF));
  } else {
    frame.push_back(0x80 | 127);
    for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<e_uint8>((len >> shift) & 0xFF));
  }

  e_uint8 mask[4];
  std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)) ^ static_cast<unsigned>(reinterpret_cast<uintptr_t>(data)));
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& m : mask) m = static_cast<e_uint8>(dist(rng));
  for (auto m : mask) frame.push_back(m);

  const size_t headerEnd = frame.size();
  frame.resize(headerEnd + len);
  for (size_t i = 0; i < len; ++i) frame[headerEnd + i] = data[i] ^ mask[i % 4];

  size_t sent = 0;
  while (sent < frame.size()) {
    const ssize_t n = ::send(socketFd_, frame.data() + sent, frame.size() - sent, 0);
    if (n <= 0) { TLOG("frame send() failed: %s", strerror(errno)); return false; }
    sent += static_cast<size_t>(n);
  }
  return true;
}

e_boolean WebSocketTransport::send(const std::vector<e_uint8>& payload) {
  if (!connected_ && !connect()) return false;
  constexpr e_uint8 kOpcodeBinary = 0x02; // BinaryCommand::parse expects raw bytes, not text
  return sendFrame(payload.data(), payload.size(), kOpcodeBinary);
}

// reads exactly `n` bytes or fails - recv() can return short reads on a TCP stream
static e_boolean readExact(int fd, e_uint8* buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    ssize_t r = recv(fd, buf + got, n - got, 0);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

e_boolean WebSocketTransport::readFrame(std::vector<e_uint8>& outPayload, e_uint8& outOpcode) {
  e_uint8 hdr[2];
  if (!readExact(socketFd_, hdr, 2)) return false;

  outOpcode = hdr[0] & 0x0F;
  e_boolean masked = (hdr[1] & 0x80) != 0; // server frames are never masked per RFC 6455, kept defensive
  e_uint64 len = hdr[1] & 0x7F;

  if (len == 126) {
    e_uint8 ext[2];
    if (!readExact(socketFd_, ext, 2)) return false;
    len = (static_cast<e_uint16>(ext[0]) << 8) | ext[1];
  } else if (len == 127) {
    e_uint8 ext[8];
    if (!readExact(socketFd_, ext, 8)) return false;
    len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
  }

  e_uint8 mask[4] = {0, 0, 0, 0};
  if (masked && !readExact(socketFd_, mask, 4)) return false;

  outPayload.resize(static_cast<size_t>(len));
  if (len > 0 && !readExact(socketFd_, outPayload.data(), static_cast<size_t>(len))) return false;
  if (masked) for (size_t i = 0; i < outPayload.size(); ++i) outPayload[i] ^= mask[i % 4];
  return true;
}

void WebSocketTransport::runReceiveLoop(const FrameHandler& onFrame, std::atomic<bool>& keepRunning) {
  while (keepRunning.load() && socketFd_ >= 0) {
    // poll with a timeout so keepRunning is re-checked periodically instead of blocking forever
    pollfd pfd{socketFd_, POLLIN, 0};
    int rc = poll(&pfd, 1, 500);
    if (rc < 0) break;
    if (rc == 0) continue; // timed out, loop back and re-check keepRunning

    std::vector<e_uint8> payload;
    e_uint8 opcode = 0;
    if (!readFrame(payload, opcode)) { TLOG("readFrame failed, connection likely closed"); break; }

    switch (opcode) {
      case 0x2: // binary - the only kind of frame BinaryCommand/CommandResult ever produces
        if (onFrame) onFrame(payload.data(), payload.size());
        break;
      case 0x9: // ping -> pong
        sendFrame(payload.data(), payload.size(), 0x0A);
        break;
      case 0x8: // close
        TLOG("server sent close frame");
        keepRunning.store(false);
        break;
      default:
        break; // text/pong frames aren't part of this protocol, ignored
    }
  }
  connected_ = false;
}

void WebSocketTransport::disconnect() {
  if (socketFd_ >= 0) {
    e_uint8 empty[1] = {0};
    sendFrame(empty, 0, 0x08);
    ::close(socketFd_);
    socketFd_ = -1;
  }
  connected_ = false;
}

e_boolean WebSocketTransport::isConnected() const { return connected_; }

};  // ECCLES_API
