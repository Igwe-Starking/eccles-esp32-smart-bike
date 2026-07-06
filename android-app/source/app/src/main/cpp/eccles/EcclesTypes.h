/*
  EcclesTypes.h (Android NDK)
  ----------------------------
  Portable subset of components/eccles/include/EcclesTypes.h from
  EcclesBikeControl_ESP-IDF, kept byte-for-byte behaviourally identical where it matters:
  same fixed width aliases, same ECCLES_API namespace convention, same djb2 hash
  (eccles_hashCT / eccles_hashRT) so a word like "headlamp" hashes to the exact same
  32-bit value here as it does on the ESP32 - this is what lets CommandFactory.cpp
  and the firmware's own Executor.cpp/StringCommand::parse agree on meaning without
  sharing a single line of code.

  Left out on purpose (device-only, meaningless off-device): driver/gpio.h, ADC/DAC,
  FreeRTOS queues/tasks/mutexes, esp_task_wdt, GLOBAL_STATE.
*/

#ifndef ECCLES_TYPES
#define ECCLES_TYPES

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#define ECCLES_SYSTEM using namespace Eccles;
#define ECCLES_API namespace Eccles

#ifdef ECCLES_DEBUG
#define ECCLES_LOG(arg)      (Eccles::eccles_printRaw((arg)))
#define ECCLES_LOG_LINE(arg) (Eccles::eccles_printLine((arg)))
#else
#define ECCLES_LOG(arg)
#define ECCLES_LOG_LINE(arg)
#endif

ECCLES_API {

using e_uint    = unsigned int;
using e_int     = int;
using e_uint8   = uint8_t;
using e_int8    = int8_t;
using e_uint16  = uint16_t;
using e_uint32  = uint32_t;
using e_int32   = int32_t;
using e_float   = float;
using e_boolean = bool;
using e_uint64  = uint64_t;
using e_string  = const char*;
using e_char    = char;

#define e_true  true
#define e_false false

enum class State { OFF, ON, UNKNOWN };
constexpr State ON      = State::ON;
constexpr State OFF     = State::OFF;
constexpr State UNKNOWN = State::UNKNOWN;

// --- string compare, identical to Eccles::eccles_compareString on the firmware ---------------
inline e_boolean eccles_compareString(e_string a, e_string b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a == *b) {
    if (*a == '\0') return true;
    ++a; ++b;
  }
  return false;
}

// --- hashing, identical djb2 variant to EcclesTypes.h / EcclesTypes.cpp on the firmware ------
constexpr e_uint32 eccles_hashCT(e_string s, e_uint32 h = 5381) {
  return (*s == 0) ? h : eccles_hashCT(s + 1, ((h << 5) + h) + (e_uint8)(*s));
}
inline e_uint32 eccles_hashRT(e_string s) {
  e_uint32 h = 5381;
  if (s == nullptr) return h;
  while (*s) { h = ((h << 5) + h) + (e_uint8)(*s); s++; }
  return h;
}

// --- logging: routed to logcat from EcclesTypes.cpp ------------------------------------------
void eccles_printRaw(e_string s);
void eccles_printLine(e_string s);
inline void eccles_printRaw(e_uint32 v)  { e_char b[16]; snprintf(b, sizeof(b), "%lu", (unsigned long)v); eccles_printRaw(b); }
inline void eccles_printLine(e_uint32 v) { e_char b[16]; snprintf(b, sizeof(b), "%lu", (unsigned long)v); eccles_printLine(b); }
inline void eccles_printRaw(e_int v)     { e_char b[16]; snprintf(b, sizeof(b), "%d", v); eccles_printRaw(b); }
inline void eccles_printLine(e_int v)    { e_char b[16]; snprintf(b, sizeof(b), "%d", v); eccles_printLine(b); }

// --- fixed-size stack string, identical API surface to the firmware's FixedStr<N> ------------
template <e_uint8 N = 64>
class FixedStr {
  e_char buf[N];
  e_uint8 len = 0;
 public:
  FixedStr() { buf[0] = '\0'; }
  FixedStr(e_string s) { assign(s); }
  void assign(e_string s) {
    if (!s) { buf[0] = '\0'; len = 0; return; }
    len = 0;
    while (len < (e_uint8)(N - 1) && s[len]) { buf[len] = s[len]; len++; }
    buf[len] = '\0';
  }
  FixedStr& operator+=(e_string s) {
    if (!s) return *this;
    e_uint8 i = 0;
    while (len < (e_uint8)(N - 1) && s[i]) buf[len++] = s[i++];
    buf[len] = '\0';
    return *this;
  }
  e_string  c_str()  const { return buf; }
  e_uint8   length() const { return len; }
  e_boolean empty()  const { return len == 0; }
  e_boolean operator==(e_string s) const { return eccles_compareString(buf, s ? s : ""); }
  e_boolean operator!=(e_string s) const { return !(*this == s); }
};
using e_stringa = FixedStr<64>;

};  // ECCLES_API

#endif  // ECCLES_TYPES
