/*
  ConversationAudio.h
  ----------------------
  The phone-call-style leg of conversation.real: mirrors what Conversation.cpp does on the
  firmware (its own dedicated UDP socket on port 4210 for raw PCM, separate from the
  websocket command channel - see Transport.cpp's UDP_PORT constant) but using AAudio
  (<aaudio/AAudio.h>) instead of Java's AudioTrack/AudioRecord, since AAudio is the NDK-native
  equivalent and lets this stay in the same native engine thread family as the rest of the
  command pipeline rather than bouncing back through JNI for every audio buffer.
*/

#ifndef ECCLES_CONVERSATION_AUDIO_H
#define ECCLES_CONVERSATION_AUDIO_H

#include <atomic>
#include <string>
#include <thread>

#include "EcclesTypes.h"

ECCLES_API {

class ConversationAudio {
 public:
  static ConversationAudio& instance();

  // Opens a mic-capture stream and a playback stream at the format the firmware reported
  // (see Engine::handleText's START_REAL handling), and starts a UDP socket to `host` on
  // the well-known conversation port. Non-blocking - work happens on internal threads.
  void start(const std::string& host, e_int32 sampleRate, e_int32 bitDepth, e_int32 channels);

  void stop();
  e_boolean isActive() const { return active_.load(); }

 private:
  ConversationAudio() = default;

  void micThreadLoop(std::string host, e_int32 sampleRate, e_int32 channels);
  void playbackThreadLoop(e_int32 sampleRate, e_int32 channels);

  std::atomic<bool> active_{false};
  int udpSocket_ = -1;
  std::thread micThread_;
  std::thread playbackThread_;
  void* inputStream_ = nullptr;   // AAudioStream*, opaque here so <aaudio/AAudio.h> stays out of the header
  void* outputStream_ = nullptr;  // AAudioStream*
};

};  // ECCLES_API

#endif  // ECCLES_CONVERSATION_AUDIO_H
