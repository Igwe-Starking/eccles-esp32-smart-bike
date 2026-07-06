/*
  this holds reference to the actual hardware we are controlling, each hardware object has a hardware device 
  object associated with it and there is a universal deviceManager that controls all of them
*/

#include "EcclesTypes.h"
#include "Pins.h"

#ifndef ECCLES_HARDWARE_DEVICE
#define ECCLES_HARDWARE_DEVICE

ECCLES_API {

  //different types of hardware devices

  enum class DEVICE_TYPE {
    DIGITAL_INPUT,DIGITAL_OUTPUT,DIGITAL_IO,ANALOG_INPUT,ANALOG_OUTPUT,SERIAL_INPUT,SERIAL_OUTPUT,DIGITAL_OUTPUT_ANALOG_INPUT
  };

  constexpr DEVICE_TYPE DIGITAL_INPUT = DEVICE_TYPE::DIGITAL_INPUT; //digital input only devices
  constexpr DEVICE_TYPE DIGITAL_OUTPUT = DEVICE_TYPE::DIGITAL_OUTPUT; //digital output only devices
  constexpr DEVICE_TYPE DIGITAL_IO = DEVICE_TYPE::DIGITAL_IO; //digital input output devices
  constexpr DEVICE_TYPE ANALOG_INPUT = DEVICE_TYPE::ANALOG_INPUT;//analog input only devices
  constexpr DEVICE_TYPE ANALOG_OUTPUT = DEVICE_TYPE::ANALOG_OUTPUT;//analog output only devices
  constexpr DEVICE_TYPE SERIAL_INPUT = DEVICE_TYPE::SERIAL_INPUT;//pulse input only devices
  constexpr DEVICE_TYPE SERIAL_OUTPUT = DEVICE_TYPE::SERIAL_OUTPUT;//pulse output only devices
  constexpr DEVICE_TYPE DIGITAL_OUTPUT_ANALOG_INPUT = DEVICE_TYPE::DIGITAL_OUTPUT_ANALOG_INPUT; //digital devices with analog feedback
  

  //device id wrapper,used for fast access of devices,this must be changed prior to any update to the real hardware
  //device number

  enum class DeviceID : e_uint8 {
    UNKNOWN_DEVICE,

    IGNITION,HORN,HEADLAMP,LEFT_TURN,RIGHT_TURN ,STARTER,ENGINE, //output devices
    IGNITION_FB,FUEL_GAUGE,TEMP_GAUGE,MICROPHONE, //analog device ids
    SHOCK_SENSOR, //serial devices
    ALL, //used to request action on all devices

    //non device ID's
    CONFIG,
    BLUETOOTH,
    CONVERSATION

    
  };

  //base class for hardware controls

  class HardwareDevice {
    public:

    virtual void on() = 0;
    virtual void off() = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual State getState() = 0;
    virtual e_uint8 read() = 0; //for analog devices
    virtual DEVICE_TYPE getType() = 0;
    virtual ~HardwareDevice()= default;
    
  };

  //implementation of hardware device layer
  class Device : public HardwareDevice {
    public:
    
    e_uint8 pin = 0;
    e_boolean enabled = true; //we enable all de
    DEVICE_TYPE type;
    DeviceID id;

    Device(e_uint8 p);
    virtual void on() = 0;//must be implemented by subclass
    virtual void off() = 0;//must be implemented by device types
    virtual State getState() = 0;
    virtual e_uint8 read() = 0;
    DEVICE_TYPE getType() override;
    virtual void enable() override;
    virtual void disable() override;

  };

  //input output devices,this devices has two hardware pins one for output and one for input hence ther name 
  //input output devices.




};

#endif
