#include "ConversationAudio.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <vector>

#define CTAG "EcclesConversationAudio"
#define CLOG(...) __android_log_print(ANDROID_LOG_DEBUG, CTAG, __VA_ARGS__)

// Same UDP port the firmware's Transport.h reserves for its IP-broadcast/audio traffic
// (see UDP_PORT in components/eccles/include/Transport.h) - conversation.real rides this
// exact port, separate from the websocket command channel on port 80.
static constexpr int kConversationUdpPort = 4210;
static constexpr int kFrameSamples = 320; // 20ms @ 16kHz mono, a conventional VoIP-style chunk

ECCLES_API {

ConversationAudio& ConversationAudio::instance() {
  static ConversationAudio instance;
  return instance;
}

void ConversationAudio::start(const std::string& host, e_int32 sampleRate, e_int32 bitDepth, e_int32 channels) {
  if (active_.load()) stop();
  if (bitDepth != 16) {
    CLOG("unsupported bit depth %d, only 16-bit PCM is wired up", bitDepth);
    return;
  }

  udpSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udpSocket_ < 0) { CLOG("socket() failed: %s", strerror(errno)); return; }

  timeval recvTimeout{0, 100000}; // 100ms, so the playback loop re-checks active_ on stop()
  setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(kConversationUdpPort);
  if (bind(udpSocket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
    CLOG("bind() failed (continuing anyway, uplink-only): %s", strerror(errno));
  }

  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(kConversationUdpPort);
  inet_pton(AF_INET, host.c_str(), &remote.sin_addr);
  if (::connect(udpSocket_, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0) {
    CLOG("connect() (UDP) failed: %s", strerror(errno));
  }

  active_.store(true);
  micThread_ = std::thread(&ConversationAudio::micThreadLoop, this, host, sampleRate, channels);
  playbackThread_ = std::thread(&ConversationAudio::playbackThreadLoop, this, sampleRate, channels);
  CLOG("conversation.real started: %s:%d, %dHz %dch", host.c_str(), kConversationUdpPort, sampleRate, channels);
}

void ConversationAudio::stop() {
  if (!active_.exchange(false)) return;
  if (micThread_.joinable()) micThread_.join();
  if (playbackThread_.joinable()) playbackThread_.join();
  if (udpSocket_ >= 0) { ::close(udpSocket_); udpSocket_ = -1; }
  CLOG("conversation.real stopped");
}

void ConversationAudio::micThreadLoop(std::string host, e_int32 sampleRate, e_int32 channels) {
  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
    CLOG("mic: createStreamBuilder failed");
    return;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setSampleRate(builder, sampleRate);
  AAudioStreamBuilder_setChannelCount(builder, channels);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

  AAudioStream* stream = nullptr;
  aaudio_result_t rc = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (rc != AAUDIO_OK || stream == nullptr) {
    CLOG("mic: openStream failed (%d) - is RECORD_AUDIO granted?", rc);
    return;
  }
  inputStream_ = stream;
  AAudioStream_requestStart(stream);

  std::vector<int16_t> frame(static_cast<size_t>(kFrameSamples) * channels);
  while (active_.load()) {
    aaudio_result_t n = AAudioStream_read(stream, frame.data(), kFrameSamples,
                                           /*timeoutNanos=*/20'000'000);
    if (n < 0) { CLOG("mic: read error %d", n); break; }
    if (n == 0 || udpSocket_ < 0) continue;
    const size_t bytes = static_cast<size_t>(n) * channels * sizeof(int16_t);
    ::send(udpSocket_, frame.data(), bytes, 0); // uplink to the firmware's UDP audio socket
  }

  AAudioStream_requestStop(stream);
  AAudioStream_close(stream);
  inputStream_ = nullptr;
}

void ConversationAudio::playbackThreadLoop(e_int32 sampleRate, e_int32 channels) {
  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
    CLOG("playback: createStreamBuilder failed");
    return;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setSampleRate(builder, sampleRate);
  AAudioStreamBuilder_setChannelCount(builder, channels);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

  AAudioStream* stream = nullptr;
  aaudio_result_t rc = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (rc != AAUDIO_OK || stream == nullptr) {
    CLOG("playback: openStream failed (%d)", rc);
    return;
  }
  outputStream_ = stream;
  AAudioStream_requestStart(stream);

  std::vector<int16_t> buf(static_cast<size_t>(kFrameSamples) * channels * 4);
  while (active_.load()) {
    if (udpSocket_ < 0) break;
    ssize_t n = recv(udpSocket_, buf.data(), buf.size() * sizeof(int16_t), 0);
    if (n <= 0) continue; // no downlink audio yet, or socket idle - not fatal
    const int32_t samples = static_cast<int32_t>(n / sizeof(int16_t) / channels);
    if (samples > 0) AAudioStream_write(stream, buf.data(), samples, /*timeoutNanos=*/20'000'000);
  }

  AAudioStream_requestStop(stream);
  AAudioStream_close(stream);
  outputStream_ = nullptr;
}

};  // ECCLES_API
