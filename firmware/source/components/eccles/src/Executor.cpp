//base implementation of Executor
//pure C++ logic throughout, millis() is eccles_millis() (see EcclesTypes.h),
//StringCommand::parse is deduplicated/optimized below, everything else is unchanged

#include "Executor.h"
#include "RuntimeMemory.h" 
#include <cstring>
#include <cstdlib>

ECCLES_API {

  Command Executor::commands[MAX_COMMANDS] = {};
  Executor* Executor::executors[MAX_EXECUTORS] = {nullptr};
  ResultHandler* Executor::handler = nullptr;
  eccles_mutex Executor::mux = nullptr;

  Executor::Executor(){
    //register our self in the list automatically
    add();
  }

  //initialize the executor mutex must be called before any obtain or send
  void Executor::start(){
    if(mux == nullptr) mux = eccles_createLock();
  }

  Executor::~Executor(){
    //un-register from the list
    remove();
  }

  //this function is called from multiple threads so we lock it with a mutex for consistency
  Command* Executor::obtainCommand(){

    //check if the lock is valid
    if(mux){

    //hold the lock
    eccles_lock(mux,portMAX_DELAY); //wait until this lock is unlocked

    static e_uint8 comIndex = 0; //command index used to track index of last command sent from pool
    Command* com = &commands[comIndex];
    comIndex++;
    if(comIndex >= MAX_COMMANDS) comIndex = 0;
    if(!com->valid){
       eccles_unlock(mux); //unlock before returning to avoid deadlocks
       return com;
    }

    //if we got here we probably run out of command or we have a persistent command in the pool
    //but we will search though
    for(int i = 0;i<MAX_COMMANDS;i++){
      com = &commands[i];
      if(!com->valid){
        //if we found an unused command in the pool there is chances that the ones after it are unused too so we reset the counter here

        comIndex = i+1;
        eccles_unlock(mux); //unlock the lock
        return com;
      }
    }
    //if we get here commands in the pools are all used,raise alarm and return
    ECCLES_LOG_LINE("ERROR: all commands already used obtain failed");
    eccles_unlock(mux);
    return (com = nullptr);
    }
    ECCLES_LOG_LINE("ERROR: mutex creation failed obtain not successful");
    return nullptr;
  }

  //can also be called from another task so we lock it
  void Executor::send(Command& com){
    //we do this here because we want the command pool operation to be completely handled by the executor
    if(mux){
    eccles_lock(mux,portMAX_DELAY);

    if(com.target == DeviceID::UNKNOWN_DEVICE || com.action == CommandAction::NO_OP){
      //for a command to be accepted as valid it must have a target that is the device we want to control and a an anction
      //that is what we want the device to do for us, raise alarm and return if this are not present

      ECCLES_LOG_LINE("command without target or action are you sure you know what you are doing");
      recycleCommand(com);
      eccles_unlock(mux);
      return;
    }
    //command has valid target and action so we accept it as valid
    com.valid = true;
    ECCLES_LOG_LINE("executor added command to pool");

    eccles_unlock(mux);
    return;
    }
    ECCLES_LOG_LINE("ERROR: mutex creation failed send not successful");
  }

  //must be called in order to receive result
  void Executor::setHandler(ResultHandler* h){
    handler = h;
  }

  //cancels a delayed or interval command,whenever a delayed or interval command
  //is no longer needed before its elapsed time a command can be sent to cancel it
  //but not you must save the id of the command because thats what is used to cancel it
  void Executor::cancelCommand(Command& com){
    //we must make sure the given command has a valid id
    if(com.id == 0){
      ECCLES_LOG_LINE("you must assign a valid id to the command you want to cancel");
      return;
    }
    //we search the whole command for any one with the given id
    for(Command& cd : commands){
      if(cd.id == com.id){
        recycleCommand(cd);
        return;
      }
    }
  }

  //this method is called whenever we detected that any command in the pool is valid
  void Executor::onReceive(Command& com){
    //we check if this is a cancelation command and we handle it if so
    if(com.action == CommandAction::CANCEL){
      cancelCommand(com);
      recycleCommand(com);
      return;
    }
    //checking when the command should execute
    if(com.duration == 0 && com.interval ==0){
      //instant one time command
      if(!com.condition.exists) callExecute(com); //no condition
      else if(callCheckCondition(com.condition)) callExecute(com); 
    } else if(com.duration > 0) {

      //present time in millis
      e_uint32 t= eccles_millis(); 
      if(com.duration <= t){
        //command time reached
        if(!com.condition.exists) callExecute(com); //no condition
        else if(callCheckCondition(com.condition)) callExecute(com); //condition exists check it
        if(com.interval > 0){
          com.duration = eccles_millis() + (com.delay * 1000);
          com.interval--;
          return;
        }
      } else {
        return; //command time not reached
      }
    }
  
    recycleCommand(com); //we hand the object back to the command pool
  }

  //this function loops the command pool and check if any command is valid then hands the valid command to onReceive for processing
  void Executor::checkIncomingCommands(){
    //check if we are allowed to execute commands
    if(!globalState.EXECUTION_MODE) return; //execution disabled

    //looping through commands
    for(int i = 0;i<MAX_COMMANDS;i++){
      Command& com = commands[i];
      if(com.valid) onReceive(com); //we got a command
    }
  }

  //here we hand over the command back to the pool
  void Executor::recycleCommand(Command& com){
    com.valid = false; //very important pool flag

    /*
      this should have been treated as optional but we reset everything for security purpose, for instance if the previous
      command had size = 34 and the later sender ignores it because it had no data,the parser will still see the previous
      size and parse it wrongly so we reset everything
    */

    com.id = 0;
    com.target = DeviceID::UNKNOWN_DEVICE;
    com.action = CommandAction::NO_OP;
    com.duration = 0;
    com.interval = 0;
    com.delay = 0;
    com.dataType = 0;
    com.size = 0;
    if(com.data != nullptr){
      e_free(com.data);
      com.data = nullptr;
    }
    com.sender = 0;
    com.condition = {};
    ECCLES_LOG_LINE("command recycled successfully");
  }

  //prepares and transfer the result data to the result handler,the handler is the only one that knows how to handle the result data
  void Executor::sendResult(CommandResult& r){

    if(handler == nullptr) return; //no need of wasting time
    //we serialize the result object here
    constexpr e_uint8 RES_MAX = 3; //the size of result without data
    constexpr e_uint8 D_MAX = 253; //the maximum amount of data we can send totaling everything to 256

    //NOTE: to avoid using malloc we use a fixed sized array here with maximumum acceptable data,if the data size is greater
    //than our maximum payload size we reject it

    if(r.data == nullptr){
      //the result has no data,so we write its statics
      e_uint8 res[RES_MAX]; //the result buffer
      res[0] = r.id; //id is the first element in the buffer
      res[1] = (r.size >> 8) & 0xFF; //size element in big endion style
      res[2] = r.size & 0xFF; //note even when data is null size is still used to send small query datas
      handler->sendResult(res,RES_MAX,r.sender);
      return; 
    }

    //result with data, NOTE: if data size > D_MAX we reject it,our microcontroller is to limited for large data
    if(r.size > D_MAX) return; //data too large
    e_uint8 res[RES_MAX + D_MAX];
    res[0] = r.id;
    res[1] = (r.size >> 8) & 0xFF;
    res[2] = r.size & 0xFF;

    //at this stage the static values are written so we add the data
    memcpy(res + RES_MAX,r.data,r.size); //copies everything in the Result data into the buffer

    /*
      PREPAIRING SIZE: to avoid copying unused binaries in the buffer to the handler we made sure the size we specified is the exact
      data size that is valid, like we have R_MAX+D_MAX which totals 256 as the buffer size but we might have data thats just 70 in
      size + static values which is 3 totaling 73 so instead of writing 256 bytes in the output buffer whereas it contains only
      73 valid data we pass exact valid data size to be written to the output buffer
    */
    e_uint16 s = RES_MAX + r.size;

    handler->sendResult(res,s,r.sender);
  }

  //call the corresponding executor to execute the command
  void Executor::callExecute(Command& com){
    //we loop through all the present executors to find the ones that the command
    //belongs to if any returns through the command belong to them
    for(Executor* e : executors){
        if((e != nullptr) && (e->execute(com))) return;
    }

    //if we got here probably no executor to handle the command
    //what a mess we raise alarm
    ECCLES_LOG_LINE("no executor present to handle the command");
  }

  //call an executor to check a condition
  //returns true if any executor met the condition
  e_boolean Executor::callCheckCondition(Condition& con){
    //check if any executor has and matches this condition
    for(Executor* e : executors){
      if((e != nullptr) && (e->checkCondition(con))) return true;
    }
    //the condition is not met
    return false;
  }

  //adds an executor the list of executors
  void Executor::add(){
    for(e_uint8 i = 0;i<MAX_EXECUTORS;i++){
        if(executors[i] == nullptr){
            executors[i] = this;

            //update the index
            index = i;
            return;
        }
    }
    //if we got here executors list has probably filled up raise an alarm
    ECCLES_LOG_LINE("executors lists full please expand MAX_EXECUTORS to contain more");
  }

  //remove an executor from the list of executors
  void Executor::remove(){
    //check if we were added in the first place
    if(index == MAX_EXECUTORS) return; //we were not added, probably we were created when list is already full

    //reset our position to default to avoid being called when we are gone
    executors[index] = nullptr;
  }



  //binary commands

  Command* BinaryCommand::parse(e_uint8* data,e_uint16 len){
    /*
      this converts buffer binaries into a command object
      command stream looks like this
      [id= 1byte][action= 1byte][target= 1byte][delay= 1bytes][interval= 1byte][dataType=1byte][size= 2byte][data = nbytes]

      NOTE: valid,sender and delay are not in the stream they are pool flags
    */
    //check if the data is valid
    if(len < BINARY_CMD_SIZE) return nullptr; //corrupted or invalid data,we reject if

    //obtaining command from executor
    Command* com = Executor::obtainCommand();
    if(com == nullptr) {
      ECCLES_LOG_LINE("command executor obtain returned null command binary parser exiting");
      return nullptr;
    }

    //parsing, NOTE: we dont really need the len variable because the command size is already deterministic
    e_uint8 index = 0;

    com->id = data[index++]; //id 1 byte,first data
    com->action = static_cast<CommandAction>(data[index++]);
    com->target = static_cast<DeviceID>(data[index++]);
    com->delay = (data[index++]); //delay 1 bytes
    com->interval = data[index++];
    com->dataType = data[index++];
    com->size = (data[index] << 8) | data[index + 1]; //size 2 bytes
    index+=2;

    if(com->size > 0){

      //we first check the integrity of the command data buffer
      if(len < (com->size + 8)){ //if this command specify a data it must contain all of it
        ECCLES_LOG_LINE("command data currupted or invalid exiting...");

        //we have already filled some part of the command we reset it so that next obtain won't have access to those values
        *com = {}; //we have'nt set data yet so we don't have issue with free
        return nullptr;
      }
      //data available, we copy them

      
      com->data = e_malloc(com->size);
      //FIX: com->size comes straight off the wire (network/serial), so a malformed or
      //oversized command could exhaust the fixed runtime memory pool and make e_malloc
      //return nullptr. The old code memcpy'd into that null pointer unconditionally,
      //crashing the device on any such command. We now fail the parse cleanly instead.
      if(com->data == nullptr){
        ECCLES_LOG_LINE("out of memory allocating command data, dropping command");
        *com = {};
        return nullptr;
      }
      memcpy(com->data,data+index,com->size); //we must free this in recycle
      //com->data = data + index; //NOTE: we are still looking for a better way to handle this,we dont want to use malloc
      //to avoid fragmentation and we dont also want to use fixed sized arrays too to avoid RAM blow up 
      //but the truth is that we dont own data and since commands are executed after the supplier function returns 
      //there is high chance that data has already been garbagged
    }
    if(com->delay > 0){
      //we do this here so the delay timer starts immediately
      //duration are always gotten in seconds so we scale it here

      com->duration = eccles_millis() + (com->delay * 1000);
    }

    //checking for condition command
    /*
      condition are embeded into the data buffer of the command binary
      if dataType == COND_MGC we know that the command came with a condition and we need to extract it
      the condtion is packed as follows from the command data buffer

      buffer[0] = condition.action;
      buffer[1] = condition.target;
      buffer[2] = condition.value;
    */
    if(com->dataType == COND_MGC){ //condition command detected
      //we must make sure that the size of the command data buffer is 3 or above if not we discard the condition
      if(com->size >= 3){
        com->condition.action = static_cast<CommandAction>(com->data[0]);
        com->condition.target = static_cast<DeviceID>(com->data[1]);
        com->condition.value = com->data[2];
        com->condition.exists = true;
      }
    }
    return com;
  }

  //String Commands

  //lazily allocates the 9 byte monitor buffer on first touch and writes one field into it,
  //used by the target/action branches below,the value branch needs its own conditional write
  static e_boolean fillMonitorField(Command* com,e_uint8*& m_data,e_uint8 fieldIndex,e_uint8 value){
    if(m_data == nullptr){
      m_data = (e_uint8*) e_malloc(9);
      if(!m_data) return false;
      memset(m_data,0,9);
      com->size = 9;
      com->data = m_data;
    }
    m_data[fieldIndex] = value;
    com->dataType = MNT_MGC;
    return true;
  }

  Command* StringCommand::parse(e_char* data,e_uint16* len){
    /*
      this function parses your text input from serial monitor to 
      create a valid command,there are specific words we care about here
      others are discarded,NOTE: your input has already been converted to lower case befor now
      the words we care about which is exactly what we are looking for in your serial message are

      /ACTION WORDS: this is the main action to execute one must be present in your message
      ON: turn a device on
      OFF: turn a device off
      DISABLE: disable a device
      ENABLE: enable a device
      SILENCE: silence a device
      VOICE: make a device speak
      GREATER: check if greater for conditions
      LESSER: check if lesser for conditions
      IF: used to initiate a condition
      WHENEVER: start monitoring a device

      /TARGET WORDS: this is what you want to control this is required in your message
      HEADLAMP: control headlamp
      HORN: control horn
      IGNITION: control ignition
      LEFT_TURN or LEFT: control left signal
      RIGHT_TURN or RIGHT: control right signal
      START: control starter
      ENGINE: control engine

      FUEL: reads fuel data
      TEMP: reads temperature data
      BLUETOOTH: controls bluetooth
      COMMUNICATION: controls communication

      /DELAY and INTERVALS: this words control the delay and interval of instructions optional
      FOR: tells how long to delay this word must be preceed a number
      TIMES: tells interval or how long to repeat must preceed a number too

    */

    //actions
    constexpr e_uint32 H_ON = eccles_hashCT("on");
    constexpr e_uint32 H_OFF = eccles_hashCT("off");
    constexpr e_uint32 H_ENABLE = eccles_hashCT("enable");
    constexpr e_uint32 H_DISABLE = eccles_hashCT("disable");
    constexpr e_uint32 H_SILENCE = eccles_hashCT("silence");
    constexpr e_uint32 H_VOICE = eccles_hashCT("voice");
    constexpr e_uint32 H_QUERY = eccles_hashCT("query");
    constexpr e_uint32 H_GET = eccles_hashCT("get");
    constexpr e_uint32 H_GREATER = eccles_hashCT("greater_than");
    constexpr e_uint32 H_LESSER = eccles_hashCT("less_than");
    constexpr e_uint32 H_IF = eccles_hashCT("if");
    constexpr e_uint32 H_WHENEVER = eccles_hashCT("whenever");
    constexpr e_uint32 H_FOR = eccles_hashCT("for");
    constexpr e_uint32 H_QON = eccles_hashCT("query_on");
    constexpr e_uint32 H_QOFF = eccles_hashCT("query_off");
    constexpr e_uint32 H_TIMES = eccles_hashCT("times");

    //targets
    constexpr e_uint32 H_HEADLAMP = eccles_hashCT("headlamp");
    constexpr e_uint32 H_HORN = eccles_hashCT("horn");
    constexpr e_uint32 H_IGNITION = eccles_hashCT("ignition");
    constexpr e_uint32 H_ENGINE = eccles_hashCT("engine");
    constexpr e_uint32 H_RTURN = eccles_hashCT("right-turn");
    constexpr e_uint32 H_LTURN = eccles_hashCT("left-turn");
    constexpr e_uint32 H_START = eccles_hashCT("starter");
    constexpr e_uint32 H_FUEL = eccles_hashCT("fuel");
    constexpr e_uint32 H_TEMP = eccles_hashCT("temp");
    constexpr e_uint32 H_BLUE = eccles_hashCT("bluetooth");
    constexpr e_uint32 H_CONV = eccles_hashCT("conversation");
    constexpr e_uint32 H_CONF = eccles_hashCT("configuration");
    constexpr e_uint32 H_PLAY = eccles_hashCT("play");
    constexpr e_uint32 H_PAUSE = eccles_hashCT("pause");
    constexpr e_uint32 H_NEXT = eccles_hashCT("next");
    constexpr e_uint32 H_PREV = eccles_hashCT("prev");

    //obtaining command this is the main root of what we are doing here
    Command* com = Executor::obtainCommand();
    if(!com) return nullptr; //executor fails to give us a command so we quit here

    //begin parsing, we split our words here and process them individualy

    DeviceID d_id = DeviceID::UNKNOWN_DEVICE;
    CommandAction c_at = CommandAction::NO_OP;

    //flags used to detect command pattern
    e_boolean conditionRequest = false;
    e_boolean monitorRequest = false;

    e_uint8 *m_data = nullptr; //holds monitor data

    e_char* word = strtok(data," "); //split from spaces

    while(word != nullptr){
      //process imediatly we initialy wanted to create an array and fill them but now we consider immediate processing is better

      const e_uint32 input = eccles_hashRT(word);

      switch (input){
        //actions
        case H_ON : c_at = CommandAction::ON; break;
        case H_OFF : c_at = CommandAction::OFF; break;
        case H_ENABLE : c_at = CommandAction::ENABLE; break;
        case H_DISABLE : c_at = CommandAction::DISABLE; break;
        case H_SILENCE : c_at = CommandAction::SILENCE; break;
        case H_VOICE : c_at = CommandAction::VOICE; break;
        case H_QUERY : c_at = CommandAction::QUERY_DATA; break;
        case H_GET : c_at = CommandAction::GET_DATA; break;
        case H_QON : c_at = CommandAction::QUERY_ON; break;
        case H_QOFF : c_at = CommandAction::QUERY_OFF; break;
        case H_PLAY : c_at = CommandAction::PLAY; break;
        case H_PAUSE : c_at = CommandAction::PAUSE; break;
        case H_NEXT : c_at = CommandAction::NEXT; break;
        case H_PREV : c_at = CommandAction::PREV; break;

        //greater_than/less_than share identical handling except for which comparison action
        //gets stored (FIX: previously neither of these ever set com->condition.action, so
        //DeviceExecutor::checkCondition() always fell through to its default/false case and
        //every "if X greater_than/less_than N" condition silently never fired, no matter what
        //N was. This also fixes conditions with a threshold of exactly 0, since "exists" used
        //to depend on com->condition.value > 0 as a stand-in for "value was actually parsed").
        case H_GREATER :
        case H_LESSER : {
          //can only be used in conditions or monitors
          if(conditionRequest){
            e_string svl = strtok(nullptr," "); //get the next data
            if(svl){
              //we can forgive condition anomalies
              com->condition.value = atoi(svl);
              com->condition.action = (input == H_GREATER) ? CommandAction::QUERY_DATA_G : CommandAction::QUERY_DATA_L;
              if(com->condition.target != DeviceID::UNKNOWN_DEVICE){
                //condition's target is already known and action/value are now both set
                com->condition.exists = true;
                conditionRequest = false;
              }
            }
          } else if(monitorRequest){
            e_string mst = strtok(nullptr," "); //get the next value
            e_boolean hadData = (m_data != nullptr);
            e_boolean created = (m_data != nullptr) || ((m_data = (e_uint8*) e_malloc(9)) != nullptr);
            if(!created) break;
            if(!hadData){ memset(m_data,0,9); com->size = 9; com->data = m_data; }
            if(mst) m_data[0] = (e_uint8) atoi(mst);
            com->size = 9;
            com->data = m_data;
            if(hadData){
              com->dataType = MNT_MGC;
              if(m_data[3] != 0) monitorRequest = false; //stop searching for monitor data
            }
          }
          break;
        }
        case H_IF : conditionRequest = true; break;
        case H_WHENEVER : monitorRequest = true; break;
        case H_FOR : {
          //delay command the user must make sure for is preceded by an int
          e_string fms = strtok(nullptr," ");
          if(fms == nullptr){
            ECCLES_LOG_LINE("asked for a delay without specifying the duration exiting...");
            if(com->data != nullptr) e_free(com->data);
            *com = {};
            return nullptr;
          }
          com->delay = (e_uint8) atoi(fms); //if you don't specify a valid digit here your delay stays at 0
          com->duration = eccles_millis() + (com->delay * 1000);
          break;
        }
        case H_TIMES : {
          //the interval keyword times must be preceded by an int
          e_string ims = strtok(nullptr," ");
          if(ims == nullptr){
            ECCLES_LOG_LINE("asked for interval without specifying the rate exiting...");
            if(com->data != nullptr) e_free(com->data);
            *com = {};
            return nullptr;
          }
          com->interval = (e_uint8) atoi(ims);
          break;
        }

        //targets
        default : switch (input) {
          case  H_HEADLAMP : d_id = DeviceID::HEADLAMP; break;
          case  H_HORN : d_id = DeviceID::HORN; break;
          case  H_ENGINE : d_id = DeviceID::ENGINE; break;
          case  H_IGNITION : d_id = DeviceID::IGNITION; break;
          case  H_RTURN : d_id = DeviceID::RIGHT_TURN; break;
          case  H_LTURN : d_id = DeviceID::LEFT_TURN; break;
          case  H_START : d_id = DeviceID::STARTER; break;
          case  H_FUEL : d_id = DeviceID::FUEL_GAUGE; break;
          case  H_TEMP : d_id = DeviceID::TEMP_GAUGE; break;
          case  H_BLUE : d_id = DeviceID::BLUETOOTH; break;
          case  H_CONV : d_id = DeviceID::CONVERSATION; break;
          case  H_CONF : d_id = DeviceID::CONFIG; break;
          default : break;
        }
      }

      //detecting wether we have an action or a target
      if(d_id != DeviceID::UNKNOWN_DEVICE){ //here we go,thats a target
        if(conditionRequest){
          //this target belongs to a condition
          com->condition.target = d_id;
          //the "value > 0" check remains as a fallback for any future condition kind that
          //doesn't set .action, but greater_than/less_than now always set .action (see fix above)
          if(com->condition.action != CommandAction::NO_OP || com->condition.value > 0){
            //condition's action/value is already set, we disable condition arg checking
            com->condition.exists = true;
            conditionRequest = false;
          }
        } else if(monitorRequest){
          //id belongs to a monitor
          e_boolean hadData = (m_data != nullptr);
          if(!fillMonitorField(com,m_data,3,static_cast<e_uint8>(d_id))){
            word = strtok(nullptr," ");
            continue;
          }
          if(hadData && ((m_data[2] != 0) || (m_data[0] > 0))) monitorRequest = false; //stop searching for monitor data
        } else {
          //no condition no monitor command
          com->target = d_id;
        }
        //reset the d_id for other use
        d_id = DeviceID::UNKNOWN_DEVICE;
      }

      //check if the command action is valid
      if(c_at != CommandAction::NO_OP){
        if(conditionRequest){
          //this action belongs to a condition
          com->condition.action = c_at;
          if(com->condition.target != DeviceID::UNKNOWN_DEVICE){ //when an action is specified value is never used
            //conditions target is already set  we disable condition arg checking
            com->condition.exists = true;
            conditionRequest = false;
          }
        } else if(monitorRequest){
          //id belongs to a monitor
          e_boolean hadData = (m_data != nullptr);
          if(!fillMonitorField(com,m_data,2,static_cast<e_uint8>(c_at))){
            word = strtok(nullptr," ");
            continue;
          }
          if(hadData && ((m_data[3] != 0) || (m_data[0] > 0))) monitorRequest = false; //stop searching for monitor data
        } else {
          com->action = c_at;
        }
        c_at = CommandAction::NO_OP; //reset the action for other uses
      }

      word = strtok(nullptr," ");
    }

    //monitor command is swapped in the processing so we invert it here
    //the actual monitor evaluation is written in the buffer that should contain the command
    if(com->dataType == MNT_MGC){
      if(com->size < 9 || com->data == nullptr){
        //corrupted monitor buffer
        if(com->data) e_free(com->data);
        *com = {};
        return nullptr;
      }
      //swap action
      CommandAction act = com->action;
      com->action = static_cast<CommandAction>(com->data[2]);
      com->data[2] = static_cast<e_uint8>(act);

      //swap target
      DeviceID idt = com->target;
      com->target = static_cast<DeviceID>(com->data[3]);
      com->data[3] = static_cast<e_uint8>(idt);

      //NOTE: command monitor buffer is just binary command embedded into the actual monitor evaluation command
    }

    //if we ask a device to on for sometime we need to turn it off after that time so to do this we need to create two
    //commands, one to turn on immediatly and one to turn off after the delay
    if(com->delay > 0 && com->action == CommandAction::ON){
      Command* com1 = Executor::obtainCommand();
      if(com1){
        com1->target = com->target;
        com1->action = com->interval == 0 ? CommandAction::OFF : CommandAction::TOGGLE; //we toggle the device if we ask it to repeat
        com1->delay = com->delay;
        com->delay = 0; //hand it over to the other command;
        com1->interval = com->interval;
        com->interval = 0; //hand over to the second command;
        com1->duration = com->duration;
        com->duration = 0;

        //we cant return an array created here since our commands are double we send them here,we assign 1 to sender to indicate
        //its a serial command
        com->sender = SRL_RT_MGC;
        com1->sender = SRL_RT_MGC;
        Executor::send(*com);
        Executor::send(*com1);
        //we indicate success by writing to len variable
        *len = 1;
        return nullptr;
      }
    }

    //thats it for now we send the command for processing and execution
    //if we have only one command return it
    return com;
  }
};
