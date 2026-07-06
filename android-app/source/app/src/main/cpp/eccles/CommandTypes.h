/*
  CommandTypes.h
  ----------------
  CommandAction / DeviceID / magic numbers, copied byte-for-byte from the firmware:
    components/eccles/include/Executor.h        -> CommandAction, magic numbers
    components/eccles/include/HardwareDevice.h  -> DeviceID
    components/eccles/include/DeviceManager.h   -> MonitorType (kept in sync with
                                                    CommandAction values, see firmware comment)
  Order matters. Do not reorder independently of the firmware - these are transmitted as
  raw e_uint8 values over the wire, not by name.
*/

#ifndef ECCLES_COMMAND_TYPES_H
#define ECCLES_COMMAND_TYPES_H

#include "EcclesTypes.h"

ECCLES_API {

// binary command header size (id,action,target,delay,interval,dataType,size:2) before payload
constexpr e_uint8 BINARY_CMD_SIZE = 8;

// dataType magic numbers
constexpr e_uint8 COND_MGC   = 21; // condition embedded in this command's data buffer
constexpr e_uint8 SRL_RT_MGC = 128;// serial route sender id (unused on the wire, kept for parity)
constexpr e_uint8 CONF_MGC_I = 22;
constexpr e_uint8 CONF_MGC_F = 23;
constexpr e_uint8 CONF_MGC_S = 24;
constexpr e_uint8 CONF_MGC_B = 25;
constexpr e_uint8 MNT_MGC    = 26; // this command registers a monitor
constexpr e_uint8 MNTC_MGC   = 27; // this command cancels a monitor

enum class CommandAction : e_uint8 {
  NO_OP,
  ON,
  OFF,
  ENABLE,
  TOGGLE,
  DISABLE,
  SILENCE,
  VOICE,
  QUERY_ON,
  QUERY_OFF,
  QUERY_STATE,
  GET_STATE,
  QUERY_DATA,
  GET_DATA,
  VOICE_DATA,
  QUERY_DATA_G,
  QUERY_DATA_L,
  WRITE,
  READ,
  START_AI,
  START_REAL,
  CANCEL,
  NEXT,
  PLAY,
  PAUSE,
  SET_VOLUME,
  VOLUME_UP,
  VOLUME_DOWN,
  PREV
};

enum class DeviceID : e_uint8 {
  UNKNOWN_DEVICE,

  IGNITION, HORN, HEADLAMP, LEFT_TURN, RIGHT_TURN, STARTER, ENGINE,
  IGNITION_FB, FUEL_GAUGE, TEMP_GAUGE, MICROPHONE,
  SHOCK_SENSOR,
  ALL,

  CONFIG,
  BLUETOOTH,
  CONVERSATION
};

// kept in sync with CommandAction, see DeviceManager.h on the firmware
enum class MonitorType : e_uint8 {
  UNKOWN, ON, OFF, VALUE = 12, CHANGE_M = 4, VALUE_L = 16, VALUE_G = 15
};

struct Condition {
  DeviceID      target = DeviceID::UNKNOWN_DEVICE;
  CommandAction action = CommandAction::NO_OP;
  e_uint8       value  = 0;
  e_boolean     exists = false;
};

// Fields that actually travel over the wire. Pool bookkeeping (valid flag, duration timers)
// is device-side only and lives in the firmware's own Command struct, not here.
struct Command {
  e_uint8       id       = 0;
  CommandAction action   = CommandAction::NO_OP;
  DeviceID      target   = DeviceID::UNKNOWN_DEVICE;
  e_uint8       delay    = 0; // seconds
  e_uint8       interval = 0; // repeat count, 0 = one shot
  Condition     condition = {};

  // monitor support: when monitor.active is true, this command registers a persistent
  // watch on `target` rather than an immediate action. `action` above doubles as the
  // MonitorType per the firmware's convention (ON/OFF/TOGGLE-for-change/QUERY_DATA*).
  struct MonitorSpec {
    e_boolean     active     = false;
    e_uint8       value      = 0;             // threshold for VALUE/VALUE_G/VALUE_L
    CommandAction thenAction = CommandAction::NO_OP; // action to run once the monitor fires
    DeviceID      thenTarget = DeviceID::UNKNOWN_DEVICE;
  } monitor;
};

};  // ECCLES_API

#endif  // ECCLES_COMMAND_TYPES_H
