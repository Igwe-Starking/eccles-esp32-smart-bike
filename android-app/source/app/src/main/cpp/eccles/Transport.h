/*
  Transport.h
  ------------
  Mirrors the naming/spirit of the firmware's components/eccles/include/Transport.h
  ("Transport manages connections... routes results back to the appropriate transport") but
  for the Android side: sending finished Command binaries out to the bike's existing
  esp_http_server websocket endpoint (see WebTransport::prepare/run + wsHandler on the
  firmware) and reading whatever comes back.

  Plain POSIX sockets (getaddrinfo/socket/connect from bionic libc) + a hand-rolled RFC 6455
  client handshake and frame (un)masking. No JNI, no OkHttp - just sockets, exactly like the
  firmware's own side of the same connection.
*/

#ifndef ECCLES_TRANSPORT_H
#define ECCLES_TRANSPORT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "EcclesTypes.h"

ECCLES_API {

// Outbound side of the connection, mirrors the firmware's own ResultHandler pattern
// (Executor.h) but in the send direction.
struct ICommandTransport {
  virtual ~ICommandTransport() = default;
  virtual e_boolean connect() = 0;
  virtual e_boolean send(const std::vector<e_uint8>& payload) = 0;
  virtual void disconnect() = 0;
  virtual e_boolean isConnected() const = 0;
};

// Callback invoked once per complete binary websocket frame received from the firmware.
using FrameHandler = std::function<void(const e_uint8* data, size_t len)>;

/*
  WebSocketTransport
  -------------------
  Minimal RFC 6455 client sufficient to talk to esp_http_server's websocket implementation:
  plaintext ws:// only (the firmware never serves wss://), single outstanding connection,
  client->server frames masked per spec and sent as binary opcodes since BinaryCommand::parse
  expects raw bytes, not text.
*/
class WebSocketTransport : public ICommandTransport {
 public:
  // host: the IP the firmware just broadcast (see UdpDiscovery.java / "ECCLES_IP:x.x.x.x"),
  // port: 80 (esp_http_server's default), path: "/ws" (see Transport.cpp on the firmware).
  explicit WebSocketTransport(std::string host, uint16_t port = 80, std::string path = "/ws");
  ~WebSocketTransport() override;

  e_boolean connect() override;
  e_boolean send(const std::vector<e_uint8>& payload) override;
  void disconnect() override;
  e_boolean isConnected() const override;

  // Blocks the calling thread reading complete binary frames off the socket and invoking
  // `onFrame` for each one, until `keepRunning` is cleared or the socket drops. Meant to be
  // run on its own dedicated thread by Engine.cpp, exactly like the firmware's Transport::run()
  // is driven from its own loop iteration.
  void runReceiveLoop(const FrameHandler& onFrame, std::atomic<bool>& keepRunning);

 private:
  std::string host_;
  uint16_t port_;
  std::string path_;
  int socketFd_ = -1;
  e_boolean connected_ = false;

  e_boolean performHandshake();
  e_boolean sendFrame(const e_uint8* data, size_t len, e_uint8 opcode);
  // reads exactly one websocket frame (handling fragmentation into a single logical frame),
  // returns false on socket error/close
  e_boolean readFrame(std::vector<e_uint8>& outPayload, e_uint8& outOpcode);
};

};  // ECCLES_API

#endif  // ECCLES_TRANSPORT_H
