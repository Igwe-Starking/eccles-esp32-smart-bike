/*
  CommandFactory.cpp
  --------------------
  Phrase matching is longest-match-first: canonical phrases ("turn on", "left turn signal",
  "greater than", ...) are hashed with eccles_hashRT (the very function EcclesTypes.h /
  the firmware's Executor.cpp uses at runtime) and compared against the user's tokenized
  sentence, trying the longest candidate window first. This lets "turn on the headlamp" and
  "switch on the headlamp" both resolve to CommandAction::ON without a synonym engine bolted
  on outside the hashing scheme the rest of the codebase already relies on.
*/

#include "CommandFactory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <vector>

ECCLES_API {
namespace CommandFactory {

namespace {

constexpr const char* kFillerWords[] = {
  "the", "a", "an", "please", "my", "now", "it", "is", "to", "of", "me", "could", "you", "and"
};

bool isFillerWord(const std::string& w) {
  for (auto f : kFillerWords) if (w == f) return true;
  return false;
}

std::string toLower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, b - a + 1);
}

std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> tokens;
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok) tokens.push_back(tok);
  return tokens;
}

bool isNumber(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

template <typename ValueT>
struct PhraseEntry { e_string phrase; ValueT value; };

// --- action phrase table, longer/more-specific phrases first ---------------------------------
const std::array<PhraseEntry<CommandAction>, 33> kActionPhrases{{
  {"turn on",        CommandAction::ON},
  {"switch on",      CommandAction::ON},
  {"power on",       CommandAction::ON},
  {"turn off",       CommandAction::OFF},
  {"switch off",     CommandAction::OFF},
  {"power off",      CommandAction::OFF},
  {"shut off",       CommandAction::OFF},
  {"enable",         CommandAction::ENABLE},
  {"activate",       CommandAction::ENABLE},
  {"disable",        CommandAction::DISABLE},
  {"deactivate",     CommandAction::DISABLE},
  {"toggle",         CommandAction::TOGGLE},
  {"flip",           CommandAction::TOGGLE},
  {"silence",        CommandAction::SILENCE},
  {"mute",           CommandAction::SILENCE},
  {"speak",          CommandAction::VOICE},
  {"announce",       CommandAction::VOICE},
  {"say the value",  CommandAction::VOICE_DATA},
  {"read aloud",     CommandAction::VOICE_DATA},
  {"query on",       CommandAction::QUERY_ON},
  {"check if on",    CommandAction::QUERY_ON},
  {"query off",      CommandAction::QUERY_OFF},
  {"check if off",   CommandAction::QUERY_OFF},
  {"get state",      CommandAction::GET_STATE},
  {"check state",    CommandAction::QUERY_STATE},
  {"check",          CommandAction::QUERY_DATA},
  {"get",            CommandAction::GET_DATA},
  {"read",           CommandAction::GET_DATA},
  {"fetch",          CommandAction::GET_DATA},
  {"set",            CommandAction::WRITE},
  {"cancel",         CommandAction::CANCEL},
  {"stop",           CommandAction::CANCEL},
  {"skip",           CommandAction::NEXT},
}};

const std::array<PhraseEntry<CommandAction>, 8> kMoreActionPhrases{{
  {"next",           CommandAction::NEXT},
  {"play",           CommandAction::PLAY},
  {"resume",         CommandAction::PLAY},
  {"pause",          CommandAction::PAUSE},
  {"volume up",      CommandAction::VOLUME_UP},
  {"volume down",    CommandAction::VOLUME_DOWN},
  {"previous",       CommandAction::PREV},
  {"start ai",       CommandAction::START_AI},
}};

const std::array<PhraseEntry<DeviceID>, 21> kTargetPhrases{{
  {"headlamp",              DeviceID::HEADLAMP},
  {"headlight",             DeviceID::HEADLAMP},
  {"head lamp",             DeviceID::HEADLAMP},
  {"head light",            DeviceID::HEADLAMP},
  {"horn",                  DeviceID::HORN},
  {"ignition",              DeviceID::IGNITION},
  {"ignition feedback",     DeviceID::IGNITION_FB},
  {"engine",                DeviceID::ENGINE},
  {"engine lock",           DeviceID::ENGINE},
  {"left turn signal",      DeviceID::LEFT_TURN},
  {"left indicator",        DeviceID::LEFT_TURN},
  {"left blinker",          DeviceID::LEFT_TURN},
  {"right turn signal",     DeviceID::RIGHT_TURN},
  {"right indicator",       DeviceID::RIGHT_TURN},
  {"right blinker",         DeviceID::RIGHT_TURN},
  {"starter",               DeviceID::STARTER},
  {"starter motor",         DeviceID::STARTER},
  {"fuel gauge",            DeviceID::FUEL_GAUGE},
  {"fuel level",            DeviceID::FUEL_GAUGE},
  {"fuel",                  DeviceID::FUEL_GAUGE},
  {"temperature",           DeviceID::TEMP_GAUGE},
}};

const std::array<PhraseEntry<DeviceID>, 9> kMoreTargetPhrases{{
  {"engine temperature",    DeviceID::TEMP_GAUGE},
  {"temp gauge",            DeviceID::TEMP_GAUGE},
  {"microphone",            DeviceID::MICROPHONE},
  {"shock sensor",          DeviceID::SHOCK_SENSOR},
  {"everything",            DeviceID::ALL},
  {"all devices",           DeviceID::ALL},
  {"configuration",         DeviceID::CONFIG},
  {"bluetooth",             DeviceID::BLUETOOTH},
  {"conversation",          DeviceID::CONVERSATION},
}};

constexpr int kMaxPhraseWords = 3;

std::string joinWindow(const std::vector<std::string>& tokens, size_t start, size_t wordCount) {
  std::string out;
  for (size_t i = 0; i < wordCount; ++i) { if (i) out += ' '; out += tokens[start + i]; }
  return out;
}

template <typename ValueT, size_t N1, size_t N2>
size_t matchPhrase(const std::vector<std::string>& tokens, size_t start,
                    const std::array<PhraseEntry<ValueT>, N1>& tableA,
                    const std::array<PhraseEntry<ValueT>, N2>& tableB,
                    ValueT& out) {
  const size_t remaining = tokens.size() - start;
  const size_t maxWords = std::min<size_t>(kMaxPhraseWords, remaining);
  for (size_t words = maxWords; words >= 1; --words) {
    const std::string candidate = joinWindow(tokens, start, words);
    const e_uint32 candidateHash = eccles_hashRT(candidate.c_str());
    for (const auto& entry : tableA) if (eccles_hashRT(entry.phrase) == candidateHash) { out = entry.value; return words; }
    for (const auto& entry : tableB) if (eccles_hashRT(entry.phrase) == candidateHash) { out = entry.value; return words; }
    if (words == 1) break;
  }
  return 0;
}

const e_uint32 H_FOR      = eccles_hashCT("for");
const e_uint32 H_IN       = eccles_hashCT("in");
const e_uint32 H_AFTER    = eccles_hashCT("after");
const e_uint32 H_DELAY    = eccles_hashCT("delay");
const e_uint32 H_SECONDS  = eccles_hashCT("seconds");
const e_uint32 H_SECOND   = eccles_hashCT("second");
const e_uint32 H_REPEAT   = eccles_hashCT("repeat");
const e_uint32 H_TIMES    = eccles_hashCT("times");
const e_uint32 H_IF       = eccles_hashCT("if");
const e_uint32 H_WHEN     = eccles_hashCT("when");
const e_uint32 H_WHENEVER = eccles_hashCT("whenever");
const e_uint32 H_GREATER  = eccles_hashCT("greater");
const e_uint32 H_ABOVE    = eccles_hashCT("above");
const e_uint32 H_MORE     = eccles_hashCT("more");
const e_uint32 H_LESS     = eccles_hashCT("less");
const e_uint32 H_BELOW    = eccles_hashCT("below");
const e_uint32 H_UNDER    = eccles_hashCT("under");
const e_uint32 H_DROPS    = eccles_hashCT("drops");
const e_uint32 H_RISES    = eccles_hashCT("rises");
const e_uint32 H_THAN     = eccles_hashCT("than");
const e_uint32 H_CHANGES  = eccles_hashCT("changes");
const e_uint32 H_TURNS    = eccles_hashCT("turns");
const e_uint32 H_THEN     = eccles_hashCT("then");
const e_uint32 H_DO       = eccles_hashCT("do");

bool isFor(e_uint32 h)      { return h == H_FOR || h == H_IN || h == H_AFTER || h == H_DELAY; }
bool isSeconds(e_uint32 h)  { return h == H_SECONDS || h == H_SECOND; }
bool isRepeat(e_uint32 h)   { return h == H_REPEAT; }
bool isTimesWord(e_uint32 h){ return h == H_TIMES; }
bool isIf(e_uint32 h)       { return h == H_IF || h == H_WHEN; }
bool isWhenever(e_uint32 h) { return h == H_WHENEVER; }
bool isGreater(e_uint32 h)  { return h == H_GREATER || h == H_ABOVE || h == H_MORE || h == H_RISES; }
bool isLesser(e_uint32 h)   { return h == H_LESS || h == H_BELOW || h == H_UNDER || h == H_DROPS; }
bool isThan(e_uint32 h)     { return h == H_THAN; }
bool isChanges(e_uint32 h)  { return h == H_CHANGES || h == H_TURNS; }
bool isThenDo(e_uint32 h)   { return h == H_THEN || h == H_DO; }

}  // namespace

e_boolean stripWakeWord(const std::string& input, std::string& remainder) {
  const std::string normalized = toLower(trim(input));
  static const e_uint32 H_ECCLES = eccles_hashCT("eccles");

  const auto tokens = tokenize(normalized);
  if (tokens.empty()) return false;
  if (eccles_hashRT(tokens[0].c_str()) != H_ECCLES) return false;

  size_t pos = normalized.find(tokens[0]) + tokens[0].size();
  while (pos < normalized.size() && (normalized[pos] == ' ' || normalized[pos] == ',' || normalized[pos] == ':')) pos++;
  remainder = normalized.substr(pos);
  return true;
}

// --- monitor grammar: "whenever <target> turns on|off|changes [then|do] <action> <target2>"
//                       "whenever <target> is|drops greater/less than <N> [then|do] <action> <target2>"
static std::optional<Command> parseMonitorSentence(const std::vector<std::string>& tokens, std::string& error) {
  Command cmd;
  cmd.id = nextCommandId();
  cmd.monitor.active = true;

  size_t i = 0;
  DeviceID watchTarget;
  size_t consumed = matchPhrase(tokens, i, kTargetPhrases, kMoreTargetPhrases, watchTarget);
  if (consumed == 0) { error = "couldn't find a device to watch after \"whenever\""; return std::nullopt; }
  cmd.target = watchTarget;
  i += consumed;

  e_boolean sawTrigger = false;
  for (; i < tokens.size(); ++i) {
    const e_uint32 h = eccles_hashRT(tokens[i].c_str());
    if (h == eccles_hashCT("turns") || h == eccles_hashCT("is") || h == eccles_hashCT("goes")) continue;

    if (isChanges(h)) { cmd.action = CommandAction::TOGGLE; sawTrigger = true; ++i; break; }

    if (h == eccles_hashCT("on"))  { cmd.action = CommandAction::ON;  sawTrigger = true; ++i; break; }
    if (h == eccles_hashCT("off")) { cmd.action = CommandAction::OFF; sawTrigger = true; ++i; break; }

    if (isGreater(h) || isLesser(h)) {
      size_t j = i + 1;
      if (j < tokens.size() && isThan(eccles_hashRT(tokens[j].c_str()))) ++j;
      if (j >= tokens.size() || !isNumber(tokens[j])) { error = "expected a value after \"" + tokens[i] + "\""; return std::nullopt; }
      cmd.action = isGreater(h) ? CommandAction::QUERY_DATA_G : CommandAction::QUERY_DATA_L;
      cmd.monitor.value = static_cast<e_uint8>(std::atoi(tokens[j].c_str()));
      sawTrigger = true;
      i = j + 1;
      break;
    }
    // skip unrecognized filler between target and trigger word
  }

  if (!sawTrigger) { error = "couldn't find a trigger condition (on/off/changes/greater than/less than)"; return std::nullopt; }

  while (i < tokens.size() && isThenDo(eccles_hashRT(tokens[i].c_str()))) ++i;

  CommandAction thenAction = CommandAction::NO_OP;
  DeviceID thenTarget = DeviceID::UNKNOWN_DEVICE;
  while (i < tokens.size()) {
    CommandAction a;
    size_t c = matchPhrase(tokens, i, kActionPhrases, kMoreActionPhrases, a);
    if (c > 0 && thenAction == CommandAction::NO_OP) { thenAction = a; i += c; continue; }
    DeviceID d;
    c = matchPhrase(tokens, i, kTargetPhrases, kMoreTargetPhrases, d);
    if (c > 0 && thenTarget == DeviceID::UNKNOWN_DEVICE) { thenTarget = d; i += c; continue; }
    ++i;
  }

  if (thenAction == CommandAction::NO_OP || thenTarget == DeviceID::UNKNOWN_DEVICE) {
    error = "couldn't find what to do once the monitor triggers";
    return std::nullopt;
  }
  cmd.monitor.thenAction = thenAction;
  cmd.monitor.thenTarget = thenTarget;
  return cmd;
}

std::optional<Command> parseSentence(const std::string& sentence, std::string& error) {
  std::vector<std::string> raw = tokenize(sentence);

  // "whenever" starts the monitor grammar and is handled by its own parser, before filler
  // words are stripped, since "is"/"turns" carry meaning there that isFillerWord() doesn't know about
  if (!raw.empty() && isWhenever(eccles_hashRT(raw[0].c_str()))) {
    std::vector<std::string> rest(raw.begin() + 1, raw.end());
    std::vector<std::string> filtered;
    for (auto& w : rest) if (!isFillerWord(w)) filtered.push_back(w);
    return parseMonitorSentence(filtered, error);
  }

  std::vector<std::string> tokens;
  tokens.reserve(raw.size());
  for (auto& w : raw) if (!isFillerWord(w)) tokens.push_back(w);

  if (tokens.empty()) { error = "no words found after the Eccles wake word"; return std::nullopt; }

  Command cmd;
  cmd.id = nextCommandId();
  bool inCondition = false;

  for (size_t i = 0; i < tokens.size();) {
    const e_uint32 h = eccles_hashRT(tokens[i].c_str());

    if (isIf(h)) { inCondition = true; ++i; continue; }
    if (isThan(h)) { ++i; continue; }

    if (isFor(h)) {
      if (i + 1 >= tokens.size() || !isNumber(tokens[i + 1])) { error = "expected a number of seconds after \"" + tokens[i] + "\""; return std::nullopt; }
      cmd.delay = static_cast<e_uint8>(std::atoi(tokens[i + 1].c_str()));
      i += 2;
      if (i < tokens.size() && isSeconds(eccles_hashRT(tokens[i].c_str()))) ++i;
      continue;
    }

    if (isRepeat(h)) {
      if (i + 1 >= tokens.size() || !isNumber(tokens[i + 1])) { error = "expected a repeat count after \"repeat\""; return std::nullopt; }
      cmd.interval = static_cast<e_uint8>(std::atoi(tokens[i + 1].c_str()));
      i += 2;
      if (i < tokens.size() && isTimesWord(eccles_hashRT(tokens[i].c_str()))) ++i;
      continue;
    }

    if (isNumber(tokens[i]) && i + 1 < tokens.size() && isTimesWord(eccles_hashRT(tokens[i + 1].c_str()))) {
      cmd.interval = static_cast<e_uint8>(std::atoi(tokens[i].c_str()));
      i += 2;
      continue;
    }

    if (isGreater(h) || isLesser(h)) {
      if (!inCondition) { error = "\"" + tokens[i] + "\" used without a preceding \"if\""; return std::nullopt; }
      if (i + 1 >= tokens.size() || !isNumber(tokens[i + 1])) { error = "expected a value after \"" + tokens[i] + "\""; return std::nullopt; }
      cmd.condition.action = isGreater(h) ? CommandAction::QUERY_DATA_G : CommandAction::QUERY_DATA_L;
      cmd.condition.value = static_cast<e_uint8>(std::atoi(tokens[i + 1].c_str()));
      cmd.condition.exists = true;
      inCondition = false;
      i += 2;
      continue;
    }

    DeviceID device;
    size_t consumed = matchPhrase(tokens, i, kTargetPhrases, kMoreTargetPhrases, device);
    if (consumed > 0) {
      if (inCondition) cmd.condition.target = device; else cmd.target = device;
      i += consumed;
      continue;
    }

    CommandAction action;
    consumed = matchPhrase(tokens, i, kActionPhrases, kMoreActionPhrases, action);
    if (consumed > 0) { cmd.action = action; i += consumed; continue; }

    ++i; // unrecognized word, skip rather than fail outright
  }

  if (cmd.target == DeviceID::UNKNOWN_DEVICE) { error = "couldn't find a recognized device in that sentence"; return std::nullopt; }
  if (cmd.action == CommandAction::NO_OP)     { error = "couldn't find a recognized action in that sentence"; return std::nullopt; }
  return cmd;
}

e_uint8 nextCommandId() {
  static e_uint8 counter = 0;
  counter = static_cast<e_uint8>((counter % 255) + 1);
  return counter;
}

std::optional<Command> build(const std::string& userInput, std::string& error) {
  std::string remainder;
  if (!stripWakeWord(userInput, remainder)) { error = "input must start with the \"Eccles\" wake word"; return std::nullopt; }
  if (trim(remainder).empty()) { error = "no command found after the \"Eccles\" wake word"; return std::nullopt; }
  return parseSentence(remainder, error);
}

};  // namespace CommandFactory
};  // ECCLES_API
