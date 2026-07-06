// Umbrella header - include this to pull in the whole native Eccles engine surface.
#ifndef ECCLES_H
#define ECCLES_H

#include "EcclesTypes.h"
#include "CommandTypes.h"
#include "Command.h"
#include "CommandFactory.h"
#include "ConversationAudio.h"
#include "Engine.h"
#include "MessageQueue.h"
#include "Transport.h"

#include <string>

ECCLES_API {
  // defined in main.cpp - the engine's thread entry point ("start")
  void eccles_main(const std::string& firmwareIp);
};

#endif  // ECCLES_H
