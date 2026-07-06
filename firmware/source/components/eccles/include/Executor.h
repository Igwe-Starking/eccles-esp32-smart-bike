//the base class for executing binary commands,this class holds the definition and executing of commands.
//Command is the base instruction given to esp from another device to execute, this commands can be given by any device including
//android,ios,pc and even other microcontrollers,can be given through bluetooth,wifi,usb,uart or even via web browser,
//the commands are in binary form to avoid string parsing

#ifndef ECCLES_ESP_EXECUTOR
#define ECCLES_ESP_EXECUTOR

//dependencies
#include "EcclesTypes.h"
#include "HardwareDevice.h" //we don't want this dependency but need for DeviceID,we will still find a way to resolve this

//holds different types of command actions,an action indicates what the command requested to be done

ECCLES_API {

#define COND_MGC 21//condition magic number used to detect condition
#define SRL_RT_MGC 128 //serial route magic number used to route result to serial
#define BINARY_CMD_SIZE 8 //binary command minimum size
#define CONF_MGC_I 22 //configuration magic number for int
#define CONF_MGC_F 23 //configuration magic number for float
#define CONF_MGC_S 24 //configuration magic number for String
#define CONF_MGC_B 25 //configuration magic number for boolean
#define MNT_MGC 26 //monitor magic number for ld monitor
#define MNTC_MGC 27 //monitor cancel magic number

enum class CommandAction : e_uint8 {
  NO_OP, //no activity
  ON , //action turn on
  OFF, //action turn off
  ENABLE, //action enable
  TOGGLE, //toggles an io device
  DISABLE, //action disable
  SILENCE, //action turn audio off
  VOICE, //action turn audio on
  QUERY_ON, //action query if target is on
  QUERY_OFF, //action query if target is off
  QUERY_STATE, //action query the state of the target
  GET_STATE, //action returns the state of the target
  QUERY_DATA, //action query sensor's data
  GET_DATA, //action gets data from sensors
  VOICE_DATA, //action speaks data value
  QUERY_DATA_G, //action check if data is greater
  QUERY_DATA_L, //action check if data is lesser
  WRITE, //action write data used by configuration
  READ, //action read used by configuration
  START_AI, //start the conversation in AI mode
  START_REAL, //start the conversation in real mode
  CANCEL, //action cancel a command
  NEXT, //action next for bluetooth
  PLAY, //action play for bluetooth
  PAUSE, //action pause for bluetooth
  SET_VOLUME, //used by bluetooth api
  VOLUME_UP, //used by bluetooth api
  VOLUME_DOWN, //used by bluetooth api
  PREV //action go back used by bluetooth api
};

//this class routes result back to the client that sends it
struct ResultHandler {
  virtual void sendResult(e_uint8* res,e_uint16 size,e_uint32 senderID) const = 0; //must be implemented
};

//this struct holds results from commands that requested result, command results are sent back to whatever that sent the command
//to deliver feedback of the command operation

struct CommandResult {
  e_uint8 id = 0; //the id of the command sent,this id must match with the id of the command that produces this result,the sender verifies this id to know which command gave the result
  e_uint16 size = 0; //the size of the result data, the command result are in binary format so the receiver have to parse and interprep them
  e_string data = nullptr; //the actual result data in bytes.
  e_uint16 sender = 0;
};

//this holds the Command Condition object,if condition is present its
//evaluated before the command is executed
struct Condition {
  DeviceID target = DeviceID::UNKNOWN_DEVICE; //the device we are checking its condition
  CommandAction action = CommandAction::NO_OP; //the condition action we are checking
  e_uint8 value = 0; //value for valued conditions
  e_boolean exists = false; //flag showing if this is a valid condition
};

//the base of all command objects, NOTE: we currentlly support binary commands but we are considering things like string commands also 
//especially since we are planning to support uart and bluetooth protocols for commands also.

struct Command {
  e_uint8 id = 0; //the command id used to distinguish commands and to detects command results
  CommandAction action = CommandAction::NO_OP; //what we are commanding to happen
  DeviceID target = DeviceID::UNKNOWN_DEVICE; //what we are giving the command
  e_uint8 delay = 0; //time to delay or interval in seconds
  e_uint8 interval = 0; //interval times 0 means no interval, 100 means unlimited interval
  e_uint8 dataType = 0; //type of supplied data 0 means no data,1 means audio data
  e_uint16 size = 0; //size of supplied data in bytes
  e_uint8* data = nullptr; //data that follows the command if any
  e_boolean valid = false; //this marks the state of command for executor
  e_uint32 duration = 0; //needed for interval updates.
  e_uint32 sender = 0;
  Condition condition = {}; //condition for this command to execute
};

//Command executor,this class manages commands and executes them in main thread, NOTE:commands can be sent from any thread/task
//but only the main thread or the thread that calls loop can execute those commands and this class manages them.
#define MAX_COMMANDS 10
#define MAX_EXECUTORS 5 //max number of executors

class Executor {

  //creating the Command lock

  static eccles_mutex mux;

  //max number of commands to create,NOTE: to avoid new we precreate and reuse commands at runtime,this means that only the 
  //CommandExecutor can truelly create and destroy commands,we set max created commands to 10 for now

  static Command commands[MAX_COMMANDS]; //Command pool
  static Executor* executors[MAX_EXECUTORS]; //max number of executors
  //Command* commandPtr = nullptr; //used to abstract real command address;
  //CommandResult* resultPtr = nullptr; //used to abstract real result address;
  static ResultHandler* handler;
  e_uint8 index = MAX_EXECUTORS; //holds the executor's index number

  protected:
  virtual e_boolean execute(Command& com) __attribute__((hot)) = 0; //executes a command returns true if the command belongs to us
  virtual e_boolean checkCondition(Condition& con) __attribute__((hot)) = 0; //checks a condition and return true if the condition belongs to us and it's true
  void sendResult(CommandResult& r); //sends a result back to the caller if required

  public:
  Executor();
  ~Executor();

  static void start(); //initialize the executor core
  static Command* obtainCommand(); //request a command from the command pool
  static void send(Command& com); //sends command to the pool
  static void checkIncomingCommands() __attribute__((hot)); //called to check for command in the queue
  static void setHandler(ResultHandler* h) __attribute__((nonnull)); //sets a handler that receives results
  static void recycleCommand(Command& com); //recycles commands back to the command pool

  private:
  //calls the corresponding executor to execute the command
  static void callExecute(Command& com);

  //cancels a delayed or interval command,NOTE: the command ID is essential here
  //the id of the command must match the id of the command requested to cancel
  static void cancelCommand(Command& com);
  
  //call the corresponding executor to check a condition
  static e_boolean callCheckCondition(Condition& con); //return true if any executor fulfilled the condition
  void add(); //adds to the executors 
  void remove(); //remove executor from the list
  static void onReceive(Command& com) __attribute__((hot)); //called whenever a command is received
};


//modes of parsing commands,we used namespaces to avoid too much objects

//binary command parser,this model knows how to intepret and parse a binary command

namespace BinaryCommand {
  //parse a raw binary command and create a command object
  Command* parse(e_uint8* data,e_uint16 len);
};

//string command parser,we don't yet implement this but needs this for uart commands

namespace StringCommand {
  //parse a pure text command and create a command object
  Command* parse(e_char* data,e_uint16* len);
};

};
#endif
