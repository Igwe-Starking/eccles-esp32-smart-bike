/*
  Engine.h
  ---------
  This is the "engine" the spec refers to: it owns the websocket Transport, runs the receive
  loop, drains the Java->native text queue through CommandFactory + CommandApi, and pushes
  UI-relevant results back out through the native->Java queue that MessageThread.java drains
  via CommandBridge.requestMessage().

  eccles_main() is the thread entry point CommandBridge.initEngine() launches on its own
  pthread, matching the spec's "call the engine's entry point, eccles_main a.k.a. start".
*/

#ifndef ECCLES_ENGINE_H
#define ECCLES_ENGINE_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Command.h"
#include "CommandTypes.h"
#include "MessageQueue.h"
#include "Transport.h"

ECCLES_API {

// Mirrors what Java sends down through CommandBridge.send(). Only one kind exists today
// (free-text, from voice/typed/button sources) but this is a struct rather than a bare
// std::string so new inbound kinds don't require an ABI change to the queue itself.
enum class CommandType : e_uint8 {
  TEXT_BINARY = 0,  // sentence to hand to CommandFactory::build()
};

struct InboundText {
  CommandType type = CommandType::TEXT_BINARY;
  std::string text;
};

// Mirrors what MessageThread.java receives back from CommandBridge.requestMessage().
enum class MessageType : e_uint8 {
  CONNECTED          = 0, // engine has a live websocket to the bike
  DISCONNECTED       = 1,
  DEVICE_STATE       = 2, // a device's on/off state, e.g. from a GET_STATE/QUERY_STATE result
  DEVICE_VALUE       = 3, // a sensor reading, e.g. from a GET_DATA result
  LOG                = 4, // human-readable status line for the command terminal / toast
  ERROR              = 5, // a sent command failed or timed out
  CONVERSATION_AUDIO_CONFIG = 6, // sampleRate/bitDepth/channels the firmware reported back
                                  // for conversation.real, needed before AAudio streams can open
};

struct OutboundMessage {
  MessageType type = MessageType::LOG;
  DeviceID    device = DeviceID::UNKNOWN_DEVICE;
  e_int32     value = 0;      // device state (0/1) or sensor value, meaning depends on `type`
  std::string text;           // used for LOG/ERROR
  e_int32     extra1 = 0;     // CONVERSATION_AUDIO_CONFIG: sample rate
  e_int32     extra2 = 0;     // CONVERSATION_AUDIO_CONFIG: bits per sample
  e_int32     extra3 = 0;     // CONVERSATION_AUDIO_CONFIG: channel count
};

class Engine {
 public:
  static Engine& instance();

  // Called once, from the pthread CommandBridge.initEngine() starts. `firmwareIp` is what
  // UdpDiscovery.java read out of the "ECCLES_IP:x.x.x.x" broadcast. Blocks for the lifetime
  // of the connection; returns when stop() is called.
  void run(const std::string& firmwareIp);

  void stop();

  // Java -> native: called from CommandBridge.send() (JNI).
  void submitText(const std::string& text);

  // native -> Java: called from CommandBridge.requestMessage() (JNI). Blocks until a message
  // is ready or the engine is stopped.
  e_boolean nextOutboundMessage(OutboundMessage& out);

  ICommandTransport* transport() { return transport_.get(); }

 private:
  Engine() = default;

  void handleText(const std::string& text);
  void pushLog(const std::string& text);
  void pushError(const std::string& text);
  void interpretResult(const Command& cmd, const CommandResult& result);

  std::unique_ptr<WebSocketTransport> transport_;
  MessageQueue<InboundText> inbound_;
  MessageQueue<OutboundMessage> outbound_;
  std::atomic<bool> running_{true};
  std::string firmwareIp_;
};

};  // ECCLES_API

#endif  // ECCLES_ENGINE_H
