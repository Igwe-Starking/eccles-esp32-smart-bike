/*
  Command.h
  -----------
  CommandApi: turns a Eccles::Command (see CommandTypes.h) into the exact byte layout
  BinaryCommand::parse expects on the firmware, sends it out over whatever ICommandTransport
  it's given, and can wait up to a short timeout for the matching CommandResult frame to come
  back (see Executor::sendResult / TransportHandler on the firmware - result frames are
  [id][sizeHi][sizeLo][data...]).

  Wire layout (must match components/eccles/include/Executor.h -> BinaryCommand::parse):
    [0] id        1 byte
    [1] action    1 byte  CommandAction
    [2] target    1 byte  DeviceID
    [3] delay     1 byte  seconds, 0 = immediate
    [4] interval  1 byte  0 = one-shot
    [5] dataType  1 byte  0 = none, COND_MGC / MNT_MGC / CONF_MGC_*
    [6-7] size    2 bytes big-endian, length of the data that follows
    [8..] data    n bytes optional payload
*/

#ifndef ECCLES_COMMAND_H
#define ECCLES_COMMAND_H

#include <vector>
#include <cstdint>

#include "CommandTypes.h"
#include "Transport.h"

ECCLES_API {

// One received CommandResult, mirrors the firmware's CommandResult struct.
struct CommandResult {
  e_uint8              id = 0;
  std::vector<e_uint8> data;   // raw result bytes, interpretation depends on what was asked
  e_boolean            timedOut = false;
};

namespace CommandApi {

  // Serializes `command` into the exact BinaryCommand::parse wire layout.
  std::vector<e_uint8> serialize(const Command& command);

  // Sends the already-built command over `transport`. Returns false if the transport
  // isn't connected and reconnect fails.
  e_boolean send(const Command& command, ICommandTransport& transport);

  // Must be called once, from the engine's own receive loop, for every binary websocket
  // frame that arrives from the firmware. Result frames are [id][sizeHi][sizeLo][data...];
  // this matches the frame to any task blocked in awaitResult() and wakes it.
  void onFrameReceived(const e_uint8* data, size_t len);

  // Blocks the calling thread for up to `timeoutMs` (spec calls for 200ms) waiting for the
  // CommandResult whose id matches `commandId`. Returns immediately once it arrives.
  CommandResult awaitResult(e_uint8 commandId, e_uint32 timeoutMs = 200);

  // Convenience: send() then awaitResult() in one call, as described in the integration spec
  // ("if the sent is successful it should call command.awaitResult").
  CommandResult sendAndAwait(const Command& command, ICommandTransport& transport,
                              e_uint32 timeoutMs = 200);

};  // namespace CommandApi

};  // ECCLES_API

#endif  // ECCLES_COMMAND_H
