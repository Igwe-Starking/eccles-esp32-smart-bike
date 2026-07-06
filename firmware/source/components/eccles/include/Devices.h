//different types of hardware device layer
#include "HardwareDevice.h"

#ifndef ECCLES_DEVICE_H
#define ECCLES_DEVICE_H

ECCLES_API {

/*
  Digital input and output devices, this devices has one hardware pin for input and one hardware pin for output
  this device should be called digitalinputfeedback device
*/

  class DigitalIODevice : public Device {
    public:

    e_uint8 fb_pin = 0; //feedback pin
    State desiredState = OFF; //the state we commanded it to be in
    State actualState = OFF; //the state the feedback gives us
    e_boolean activeLow = false; //whether its activated py pulling voltage low or high
    e_string audioKey = ""; //the audio path for voice response
    e_boolean audible = true;

    DigitalIODevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK);
    void respond(e_string audiop);
    void respondState();
    virtual void update();
    void on() override;
    void off() override;
    void enable() override;
    void disable() override;
    void silent(e_boolean s);
    e_uint8 read() override;
    State getState() override;


  };

  //DigitalOutput devices,this type of device has output pins but no feedback pin, in the sense that the hardware can
  //only be toggle by us and not by any other external event

  class DigitalOutputDevice : public DigitalIODevice {
    public:
    DigitalOutputDevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK);
    void update() override; //this is the only function we needed to override here.
  };

  //Digital output analog feedback device,this type of device has Digital control but has analog
  //feedback pin

  class DigitalOutputAnalogFeedbackDevice : public DigitalIODevice {

    public:

    e_uint8 value = 0;
    
    DigitalOutputAnalogFeedbackDevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK);
    void update() override;
    e_uint8 read() override;
  };

  //base class for sensors and analog devices

  class AnalogInputDevice : public Device {

    public:
    e_uint8 data = 0; //the current value of the sensor

    AnalogInputDevice(const e_uint8 p);

    void on() override; 
    void off() override;
    e_uint8 read() override;//reads from the device hardware
    virtual void stream(); //read and write data to continuos stream
    State getState() override;

  };

  //base class for pulse in devices
  //this devices has close behavior with analog input devices

  class SerialInputDevice : public AnalogInputDevice {

    private:
    volatile e_uint pulseCount = 0;
    volatile e_uint lastTime = 0;
    volatile e_uint deltaTime = 0;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

    static void IRAM_ATTR isrRouter(void* a); //isr is used to count serial pulses
    void start();
    void stop();

    public:
    SerialInputDevice(const e_uint8 p);
    ~SerialInputDevice();
    e_uint8 read() override;
    void stream() override;
  };

};
#endif
