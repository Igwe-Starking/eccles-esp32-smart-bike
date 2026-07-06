#include "Engine.h"

#include <thread>

#include "Command.h"
#include "CommandFactory.h"
#include "ConversationAudio.h"

ECCLES_API {

Engine& Engine::instance() {
  static Engine engine;
  return engine;
}

void Engine::pushLog(const std::string& text) {
  OutboundMessage m; m.type = MessageType::LOG; m.text = text;
  outbound_.push(std::move(m));
}

void Engine::pushError(const std::string& text) {
  OutboundMessage m; m.type = MessageType::ERROR; m.text = text;
  outbound_.push(std::move(m));
}

void Engine::run(const std::string& firmwareIp) {
  running_.store(true);
  firmwareIp_ = firmwareIp;
  transport_ = std::make_unique<WebSocketTransport>(firmwareIp, 80, "/ws");

  pushLog(std::string("connecting to ") + firmwareIp + " ...");
  if (!transport_->connect()) {
    pushError("could not connect to the bike's websocket endpoint");
    OutboundMessage m; m.type = MessageType::DISCONNECTED;
    outbound_.push(std::move(m));
    running_.store(false);
    return;
  }

  OutboundMessage connectedMsg; connectedMsg.type = MessageType::CONNECTED;
  outbound_.push(std::move(connectedMsg));
  pushLog("connected");

  // receive loop runs on its own thread; CommandApi::onFrameReceived wakes anyone blocked
  // in CommandApi::awaitResult() as soon as a matching result frame lands.
  std::atomic<bool> receiverKeepRunning{true};
  std::thread receiverThread([this, &receiverKeepRunning] {
    transport_->runReceiveLoop(
        [](const e_uint8* data, size_t len) { CommandApi::onFrameReceived(data, len); },
        receiverKeepRunning);
  });

  while (running_.load()) {
    InboundText incoming;
    if (!inbound_.pop(incoming, &running_)) {
      break; // pop() only returns false once running_ has actually been cleared
    }
    handleText(incoming.text);
  }

  receiverKeepRunning.store(false);
  if (receiverThread.joinable()) receiverThread.join();
  transport_->disconnect();

  OutboundMessage disconnectedMsg; disconnectedMsg.type = MessageType::DISCONNECTED;
  outbound_.push(std::move(disconnectedMsg));
}

void Engine::stop() {
  running_.store(false);
  inbound_.wakeAll();
  outbound_.wakeAll();
  ConversationAudio::instance().stop();
}

void Engine::submitText(const std::string& text) {
  InboundText msg; msg.type = CommandType::TEXT_BINARY; msg.text = text;
  inbound_.push(std::move(msg));
}

e_boolean Engine::nextOutboundMessage(OutboundMessage& out) {
  return outbound_.pop(out, &running_);
}

void Engine::handleText(const std::string& text) {
  std::string error;
  auto parsed = CommandFactory::build(text, error);
  if (!parsed.has_value()) {
    pushError("couldn't understand: " + error);
    return;
  }

  Command& cmd = *parsed;

  if (cmd.monitor.active) {
    if (!transport_ || !CommandApi::send(cmd, *transport_)) {
      pushError("failed to register monitor");
      return;
    }
    pushLog("monitor registered");
    return;
  }

  // conversation.real is a live audio session, not a fire-and-forget action: start it once
  // the firmware confirms it's ready, using whatever audio format it reports back.
  if (cmd.target == DeviceID::CONVERSATION && cmd.action == CommandAction::START_REAL) {
    CommandResult result = CommandApi::sendAndAwait(cmd, *transport_, 200);
    if (result.timedOut) { pushError("bike didn't confirm conversation.real in time"); return; }

    e_int32 sampleRate = 16000, bitDepth = 16, channels = 1; // fallback if firmware sends no config
    if (result.data.size() >= 4) {
      sampleRate = (result.data[0] << 8) | result.data[1];
      bitDepth = result.data[2];
      channels = result.data[3];
    }
    OutboundMessage cfg;
    cfg.type = MessageType::CONVERSATION_AUDIO_CONFIG;
    cfg.extra1 = sampleRate; cfg.extra2 = bitDepth; cfg.extra3 = channels;
    outbound_.push(cfg);

    // conversation audio itself rides its own UDP socket to the firmware, exactly like
    // Conversation.cpp does on the device side - not the websocket command channel.
    ConversationAudio::instance().start(firmwareIp_, sampleRate, bitDepth, channels);
    return;
  }
  if (cmd.target == DeviceID::CONVERSATION && cmd.action == CommandAction::CANCEL) {
    ConversationAudio::instance().stop();
    if (transport_) CommandApi::send(cmd, *transport_);
    pushLog("conversation ended");
    return;
  }

  if (!transport_) { pushError("not connected"); return; }
  CommandResult result = CommandApi::sendAndAwait(cmd, *transport_, 200);
  if (result.timedOut) {
    // not every action produces a result (e.g. plain ON/OFF may be fire-and-forget on some
    // firmware paths) so a timeout is logged, not necessarily treated as a hard failure
    pushLog("sent (no confirmation received)");
    return;
  }
  interpretResult(cmd, result);
}

void Engine::interpretResult(const Command& cmd, const CommandResult& result) {
  const auto action = cmd.action;
  const e_boolean isStateQuery =
      action == CommandAction::GET_STATE || action == CommandAction::QUERY_STATE ||
      action == CommandAction::QUERY_ON  || action == CommandAction::QUERY_OFF   ||
      action == CommandAction::ON || action == CommandAction::OFF || action == CommandAction::TOGGLE ||
      action == CommandAction::ENABLE || action == CommandAction::DISABLE;

  const e_boolean isValueQuery =
      action == CommandAction::GET_DATA || action == CommandAction::QUERY_DATA ||
      action == CommandAction::QUERY_DATA_G || action == CommandAction::QUERY_DATA_L ||
      action == CommandAction::VOICE_DATA;

  if (isStateQuery && !result.data.empty()) {
    OutboundMessage m;
    m.type = MessageType::DEVICE_STATE;
    m.device = cmd.target;
    m.value = result.data[0];
    outbound_.push(std::move(m));
    return;
  }

  if (isValueQuery && !result.data.empty()) {
    OutboundMessage m;
    m.type = MessageType::DEVICE_VALUE;
    m.device = cmd.target;
    m.value = (result.data.size() >= 2) ? ((result.data[0] << 8) | result.data[1]) : result.data[0];
    outbound_.push(std::move(m));
    return;
  }

  pushLog("command acknowledged");
}

};  // ECCLES_API
