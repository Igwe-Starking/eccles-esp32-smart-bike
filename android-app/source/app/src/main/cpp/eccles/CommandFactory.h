/*
  CommandFactory.h
  -----------------
  Turns natural, conversational "Eccles ..." sentences (typed, spoken, or synthesized from a
  UI button tap - see MainActivity.java, every control path funnels through the same text
  pipeline per the integration spec) into the exact binary Command the firmware expects.

  This intentionally does NOT reuse the firmware's StringCommand grammar (Executor.cpp),
  which is a terse single-keyword-per-token grammar built for a serial monitor ("on headlamp
  for 5"). Instead this accepts full phrases ("turn on the headlamp for 5 seconds", "whenever
  the fuel level drops below 20 turn on the headlamp") and maps them to the same
  CommandAction / DeviceID byte values, using the exact same eccles_hashCT / eccles_hashRT
  functions the firmware itself uses - never a reimplementation.
*/

#ifndef ECCLES_COMMAND_FACTORY_H
#define ECCLES_COMMAND_FACTORY_H

#include <optional>
#include <string>

#include "CommandTypes.h"

ECCLES_API {
namespace CommandFactory {

  // Requires the sentence to begin with the "Eccles" wake word (case-insensitive, matching
  // the device's own voice branding, see eccles/resources/models/eccles.wav) and returns the
  // remainder if so. Speech-recognition input MUST pass this check before reaching build();
  // typed/button-generated text is produced with the wake word already attached (see
  // MainActivity.java's phraseFor()).
  e_boolean stripWakeWord(const std::string& input, std::string& remainder);

  // Parses a full natural-language sentence (already stripped of the wake word) into a
  // Command. On failure returns std::nullopt and fills `error` with a human readable reason.
  std::optional<Command> parseSentence(const std::string& sentence, std::string& error);

  // Full pipeline: raw text -> parsed Command, or nullopt with `error` set. Rejects input
  // that doesn't start with the wake word.
  std::optional<Command> build(const std::string& userInput, std::string& error);

  // Fresh command id, 1..255 wrapping past 255 back to 1 (0 is reserved - "no id assigned",
  // matching the firmware's own convention).
  e_uint8 nextCommandId();

};  // namespace CommandFactory
};  // ECCLES_API

#endif  // ECCLES_COMMAND_FACTORY_H
