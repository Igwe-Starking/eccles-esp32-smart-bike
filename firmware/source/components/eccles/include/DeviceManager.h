//this class manages all devices in the system and track real time state of sensors
//all rights reserved by nwobodo Ecclesiastes

#ifndef ECCLES_DEVICE_MANAGER
#define ECCLES_DEVICE_MANAGER

#include "Devices.h" //base class for devices

ECCLES_API {

  //enum used in device monitoring, it's made in sync with the commandAction value of similar actions
  enum class MonitorType : e_uint8{
    UNKOWN,ON,OFF,VALUE = 12,CHANGE_M = 4 /*used instead of change because CHANGE conflicts with arduino lib core*/,VALUE_L = 16,VALUE_G = 15
  };

  //used in LiveData to monitor devices
  typedef struct MonitorDefinition {
    e_uint8 id = 0; //id of the monitor needed to stop it
    DeviceID target = DeviceID::UNKNOWN_DEVICE; //the device we are monitoring
    MonitorType type = MonitorType::UNKOWN; //what characteristic should we monitor
    e_uint8 value = 0; //value for value monitor
    e_uint8 action[8]; //command to execute if this monitor is reached
    e_boolean valid = false;
    State prevState = UNKNOWN;
  } ld_monitor;

  //called whenever a monitor's condition is reached
  struct MonitorHandler {
    virtual void onPassed(e_uint8* com,e_uint8 len) = 0;
  };

  #define MAX_MONITORS 4 //max 4 monitors for now to save RAM
  #define MAX_DEVICES 7//change to any max number of devices

  //base class for device control and managements,this class handles the independent controls of IO device hardware

  class DeviceManager {
    
    //array of devices
    private:
    e_uint8 deviceCount = 0; //this must be the first variable because it is use in the preinit function
    //we are not supposed to do this here but for the sake of global availability we have to.
    //initializing all DigitalIODevices
    DigitalIODevice headlamp,horn,left_turn,right_turn,starter;

    //Digital output but analog feedback devices
    DigitalOutputAnalogFeedbackDevice ignition;

    //DigitalOutput only devices
    DigitalOutputDevice engine;

    protected:

    Device* devices[sizeof(Pins::OUTPUT_PINS)]; //holds only digitalio devices.
    

    //e_uint8 analogOffset = 0; //offset to sensor pins in devices array, not needed creates local array bug
    //e_uint8 sensorCount = 0; //how many sensors we have, not needed create bugs

    const e_uint8 r1= 39; //the value of the first resistor in ignition divider
    const e_uint8 r2= 10; //the value of the second resistor in ignition divider


    public:

    DeviceManager();
    ~DeviceManager();

    //resolves device id from string
    DeviceID resolveId(e_string id);

    //search and locate device by its id
    virtual Device* findDevice(DeviceID id);

    //ask a device to turn on
    void turnOnDevice(Device& d);
    void turnOnDevice(DeviceID id);
    void turnOnDevice(e_string name);

    //ask a device to turn off
    void turnOffDevice(Device& d);
    void turnOffDevice(DeviceID id);
    void turnOffDevice(e_string dn);

    //check if the device is on
    e_boolean isDeviceOn(Device& d);
    e_boolean isDeviceOn(DeviceID id);
    e_boolean isDeviceOn(e_string dn);


    //check the state of a device
    State getDeviceState(Device& d);
    State getDeviceState(DeviceID id);
    State getDeviceState(e_string dn);

    //check if any device is on, returns the number of active devices
    e_uint8 isAnyDeviceOn();

    //enable device,devices can only be turned on if they are enabled
    void enableDevice(Device& d);
    void enableDevice(DeviceID id);
    void enableDevice(e_string dn);

    //disable device, once device is disabled it cant be turned on
    void disableDevice(Device& d);
    void disableDevice(DeviceID id);
    void disableDevice(e_string dn);

    //silence device,device can still be turned on/off but wont give any response
    void silenceDevice(Device& d,e_boolean silent);
    void silenceDevice(DeviceID id,e_boolean silent);
    void silenceDevice(e_string dn,e_boolean silent);

    //toggling device
    void toggleDevice(DeviceID id);

    //enabling all devices
    void turnOnAll();

    //checks the corresponding id and return true if any device in the list has it
    e_boolean checkID(DeviceID id);

    //disabling all devices
    void turnOffAll();


  };


  /*
    an extension of DeviceManager that reads and update global events on real time,this class is named livedata which seems
    misleading of its capabilities to make it simple,the actual name should be LiveDataDeviceManager or LiveDeviceManager
    since its actually a fully deviceMananger that adds sensor readings and live event update
  */

  //we defined this here to avoid intefering with class members.

  constexpr e_float R0 = 390000.0f; //value of the first divider resistor
  constexpr e_float R1 = 10000.0f; //value of the second divider resistor or pull down

  constexpr e_float VREF = 3.3f; //the module's reference voltage.
  constexpr e_float IGNITION_THRSH = 10.0f; //ignition on threshhold
  constexpr e_float ENGINE_RUN_THRSH = 13.0f; //voltage when engine is running

  class LiveData : public DeviceManager {
    //the number of analog devices we have
    //analoginput devices are used instead of device because all sensors are analog
    //and sizeof is not devided because Pins::SENSOR_PINS holds 8 bit integer
    
    private:
    //we do this here to get a global reference to live devices
    AnalogInputDevice fuel,temp,ignition_fb;
    AnalogInputDevice shock;

    Device* liveDevices[sizeof(Pins::SENSOR_PINS)];
    ld_monitor monitors[MAX_MONITORS];

    MonitorHandler* handler = nullptr;
    e_uint32 ldm_d = 0; //monitor delay
    
    //default constructor
    
    public:

    LiveData();
    ~LiveData();
    

    void writeGlobal(); //read and writes all sensor data to global data
    e_string prepareLive(e_uint16* len); //reads and create json string of sensors and there current value
    void respondData(e_string p);
    e_boolean checkID(DeviceID id);
    Device* findDevice(DeviceID id) override;
    e_uint8 normalize(e_uint8 data); //converts data to percentage value.
    e_uint8 getData(DeviceID id); //returns live data from the given device whose id is specified.
    e_float voltageLevel(); //reads and inteprets the battery voltage
    void startMonitor(ld_monitor monitor); //start monitoring a device
    void stopMonitor(e_uint8 id); //stop monitoring a device
    void monitor(); //runs to check monitor conditions
    void setHandler(MonitorHandler *mnh);
    
  };

};

#endif
