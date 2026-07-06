#include "Command.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

ECCLES_API {
namespace CommandApi {

namespace {

// One slot per in-flight command id waiting on a result. Guarded by gMutex.
struct PendingSlot {
  e_boolean            ready = false;
  std::vector<e_uint8> data;
};

std::mutex gMutex;
std::condition_variable gCv;
std::unordered_map<e_uint8, PendingSlot> gPending;

}  // namespace

std::vector<e_uint8> serialize(const Command& command) {
  std::vector<e_uint8> buf;
  buf.reserve(BINARY_CMD_SIZE + 9);

  buf.push_back(command.id);

  if (command.monitor.active) {
    // registering a persistent watch: `action` doubles as the MonitorType, `dataType` is
    // MNT_MGC, and the 9 byte payload is [value][8-byte embedded action header] - see
    // DeviceExecutor::execute()'s MNT_MGC branch on the firmware.
    buf.push_back(static_cast<e_uint8>(command.action));
    buf.push_back(static_cast<e_uint8>(command.target));
    buf.push_back(command.delay);
    buf.push_back(command.interval);
    buf.push_back(MNT_MGC);
    buf.push_back(0x00); // size hi (payload is always 9 bytes)
    buf.push_back(0x09); // size lo

    buf.push_back(command.monitor.value);
    // embedded 8-byte action header, run via BinaryCommand::parse(action,8) once triggered
    buf.push_back(command.id);                                     // reuse the same id
    buf.push_back(static_cast<e_uint8>(command.monitor.thenAction));
    buf.push_back(static_cast<e_uint8>(command.monitor.thenTarget));
    buf.push_back(0x00); // delay
    buf.push_back(0x00); // interval
    buf.push_back(0x00); // dataType
    buf.push_back(0x00); // size hi
    buf.push_back(0x00); // size lo
    return buf;
  }

  buf.push_back(static_cast<e_uint8>(command.action));
  buf.push_back(static_cast<e_uint8>(command.target));
  buf.push_back(command.delay);
  buf.push_back(command.interval);

  if (command.condition.exists) {
    buf.push_back(COND_MGC);
    buf.push_back(0x00);
    buf.push_back(0x03);
    buf.push_back(static_cast<e_uint8>(command.condition.action));
    buf.push_back(static_cast<e_uint8>(command.condition.target));
    buf.push_back(command.condition.value);
  } else {
    buf.push_back(0x00); // dataType: none
    buf.push_back(0x00);
    buf.push_back(0x00);
  }
  return buf;
}

e_boolean send(const Command& command, ICommandTransport& transport) {
  {
    std::lock_guard<std::mutex> lock(gMutex);
    gPending[command.id] = PendingSlot{};
  }
  auto bytes = serialize(command);
  e_boolean ok = transport.send(bytes);
  if (!ok) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPending.erase(command.id);
  }
  return ok;
}

void onFrameReceived(const e_uint8* data, size_t len) {
  // result frame layout from Executor::sendResult(): [id][sizeHi][sizeLo][data...]
  if (len < 3) return;
  e_uint8 id = data[0];
  e_uint16 size = (e_uint16)((data[1] << 8) | data[2]);
  size_t avail = len - 3;
  if (size > avail) size = (e_uint16)avail; // defensive: never read past what we actually got

  std::lock_guard<std::mutex> lock(gMutex);
  auto it = gPending.find(id);
  if (it == gPending.end()) return; // nobody is waiting on this id (e.g. it already timed out)
  it->second.data.assign(data + 3, data + 3 + size);
  it->second.ready = true;
  gCv.notify_all();
}

CommandResult awaitResult(e_uint8 commandId, e_uint32 timeoutMs) {
  CommandResult result;
  result.id = commandId;

  std::unique_lock<std::mutex> lock(gMutex);
  auto it = gPending.find(commandId);
  if (it == gPending.end()) {
    // send() was never called for this id, or it was already collected - nothing to wait for
    result.timedOut = true;
    return result;
  }

  e_boolean gotIt = gCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                  [&] { return gPending[commandId].ready; });

  if (gotIt) {
    result.data = gPending[commandId].data;
  } else {
    result.timedOut = true;
  }
  gPending.erase(commandId);
  return result;
}

CommandResult sendAndAwait(const Command& command, ICommandTransport& transport, e_uint32 timeoutMs) {
  if (!send(command, transport)) {
    CommandResult r;
    r.id = command.id;
    r.timedOut = true;
    return r;
  }
  return awaitResult(command.id, timeoutMs);
}

};  // namespace CommandApi
};  // ECCLES_API
