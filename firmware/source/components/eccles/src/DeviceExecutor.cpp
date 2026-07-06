//Base implementation of DeviceExecutor
//PORT NOTE: pure C++ logic, no framework dependency, carried over unchanged

#include "Executors.h"

ECCLES_API {

  //handles the monitor passed conditions
  struct DEHandler : public MonitorHandler {
    void onPassed(e_uint8 *action,e_uint8 len) override {
      //pass the command and send it to the pool
      Command* com = BinaryCommand::parse(action,len);
      if(com) Executor::send(*com);
    }
  };

  DEHandler hd;

  DeviceExecutor::DeviceExecutor() {
    ld.setHandler(&hd);
  }

  //here our command object is brought to life 
  e_boolean DeviceExecutor::execute(Command& com){
    DeviceID id = com.target;

    if(id == DeviceID::UNKNOWN_DEVICE) return false; //invalid device
    
    //we must also make sure this DeviceID is managed by us
    if(!ld.checkID(id)) return false; //the id is not ours so check the next executor
    
    CommandAction action = com.action;
    if(action == CommandAction::NO_OP) return false; //no operation

    ECCLES_LOG("executing command of target:");
    ECCLES_LOG(static_cast<e_uint8>(com.target));
    ECCLES_LOG(" action:");
    ECCLES_LOG(static_cast<e_uint8>(action));
    ECCLES_LOG_LINE("...");

    //check if this is a monitor command a monitor flag is store in the 
    //command dataType to indicate that the device should be monitored
    if(com.dataType == MNT_MGC){
      //FIX: BinaryCommand::parse never validates that dataType==MNT_MGC implies a data
      //buffer of at least 9 bytes (that check only ever existed on the string-command path).
      //A malformed/short binary command reaching us here with com.data==nullptr or
      //com.size<9 would otherwise null-deref or read past the end of the buffer below.
      if(com.data == nullptr || com.size < 9){
        ECCLES_LOG_LINE("monitor command missing or too small its data, ignoring");
        return true; //still ours, tell the list to stop searching
      }
      ld_monitor mnt;
      mnt.id = com.id;
      mnt.target = com.target;
      mnt.type = static_cast<MonitorType>(com.action); //monitorType is written in sync with the commandAction we do this so that we don't have to pull executor.h inside DeviceManger just because of CommandAction
      mnt.value = com.data[0]; //monitor value is the first byte stord in the command data buffer
      memcpy(mnt.action,com.data + 1,8); //mnt.action is just command format in binary sent to livedata we do this to avoid depending on executor for command definition
      ld.startMonitor(mnt);
      return true;
    }

    if(com.dataType == MNTC_MGC){
      ld.stopMonitor(com.id);
      return true;
    }

    //simple io instructions
    switch (action){
      case CommandAction::ON: ld.turnOnDevice(id); return true;
      case CommandAction::OFF: ld.turnOffDevice(id); return true;
      case CommandAction::ENABLE: ld.enableDevice(id); return true;
      case CommandAction::DISABLE: ld.disableDevice(id); return true;
      case CommandAction::SILENCE: ld.silenceDevice(id,true); return true;
      case CommandAction::VOICE: ld.silenceDevice(id,false); return true;
      case CommandAction::TOGGLE: ld.toggleDevice(id); return true;
      default: break; //not an io instruction
    }

    //analog instructions
    
      //if we get here maybe an analog/sensor instruction or result is required
      CommandResult r;
      if(action == CommandAction::QUERY_ON){
        if(id == DeviceID::ALL){
          e_uint8 on_d = ld.isAnyDeviceOn();
          r.id = com.id;
          r.size = on_d;
          r.data = nullptr;
          r.sender = com.sender;
          sendResult(r);
          return true; //we are done here
        }
        State s= ld.getDeviceState(id);
        if(s == ON) ld.respondData("yes"); else ld.respondData("no");
        e_boolean d_on = ld.isDeviceOn(id);
        r.id = com.id;
        r.size = (d_on) ? 1 : 0;
        r.data = nullptr;
        r.sender = com.sender;
        sendResult(r);
        return true; //we are done here
      }
      if(action == CommandAction::QUERY_OFF){
        State s = ld.getDeviceState(id);
        if(s == OFF) ld.respondData("Yes"); else ld.respondData("no");
        e_boolean d_off = !(ld.isDeviceOn(id));
        r.id = com.id;
        r.size = (d_off) ? 1 : 0;
        r.data = nullptr;
        r.sender = com.sender;
        sendResult(r);
        return true; //we are done if we get here
      }
      if(action == CommandAction::QUERY_STATE){
        r.id = com.id;
        r.size = (ld.getDeviceState(id) == ON) ? 1 : 0;
        r.data = nullptr;
        r.sender = com.sender;
        sendResult(r);
        return true; //we are done here
      }

      if(action == CommandAction::GET_DATA || action == CommandAction::QUERY_DATA){
        if(id == DeviceID::ALL){
          e_uint16 len = 0;
          r.id = com.id;
          r.data = ld.prepareLive(&len);
          r.size = len;
          r.sender = com.sender;
          sendResult(r);
          return true; //we are done
        }
        r.id = com.id;
        r.size = ld.getData(id);
        r.data = nullptr;
        r.sender = com.sender;
        sendResult(r);
        return true; //we are done here
      }
      if(action == CommandAction::VOICE_DATA){
        e_uint8 dtn = ld.getData(id);
        r.id = com.id;
        r.size = dtn;
        r.data = nullptr;
        r.sender = com.sender;
        sendResult(r);
        return true; //we done here
      }

    return false;
  }

  //checks the corresponding condition
  e_boolean DeviceExecutor::checkCondition(Condition& con){
    //we verify if we are actually the one managing the condition's target device
    if(!ld.checkID(con.target)) return false; //the device is not ours try the next executor

    //the device is ours so we query the condition
    switch(con.action){
      case CommandAction::QUERY_ON : return ld.isDeviceOn(con.target);
      case CommandAction::QUERY_OFF : return !(ld.isDeviceOn(con.target));
      case CommandAction::QUERY_DATA : return (ld.getData(con.target) == con.value);
      case CommandAction::QUERY_DATA_G : return (ld.getData(con.target) > con.value);
      case CommandAction::QUERY_DATA_L : return (ld.getData(con.target) < con.value);
      default: return false;
    }
  }

  //runs the device monitor this should be called repeatedly by the ExecutorManager
  void DeviceExecutor::runMonitor(){
    //call the live data to monitor devices
    ld.monitor();
  }

};
