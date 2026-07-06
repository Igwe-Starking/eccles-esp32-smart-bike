//implementation of hardwaredevice.h
//PORT NOTE: pure C++ logic, no framework dependency, carried over unchanged

#include "HardwareDevice.h"

ECCLES_API {

  Device::Device(e_uint8 p) : pin(p) {
    //auto id assignment from pinMap,this will cause some processor clock but avoid doing it in deviceManager to reduce
    //more boiler plates and we avoid using the pinMap as ID to avoid exposing the hardware pins,if we have more devices
    //we have to override this to match our devices
    switch(pin){

      //digital ids
      case Pins::IGNITION_CONTROL_PIN: id = DeviceID::IGNITION; return;
      case Pins::HEADLAMP_CONTROL_PIN: id = DeviceID::HEADLAMP; return;
      case Pins::HORN_CONTROL_PIN: id = DeviceID::HORN; return;
      case Pins::STARTER_CONTROL_PIN: id = DeviceID::STARTER; return;
      case Pins::LEFT_SIGNAL_CONTROL_PIN: id = DeviceID::LEFT_TURN; return;
      case Pins::RIGHT_SIGNAL_CONTROL_PIN: id = DeviceID::RIGHT_TURN; return;
      case Pins::ENGINE_LOCK_PIN: id = DeviceID::ENGINE; return;

      //analog ids
      case Pins::FUEL_GAUGE_PIN: id = DeviceID::FUEL_GAUGE; return;
      case Pins::TEMP_GAUGE_PIN: id = DeviceID::TEMP_GAUGE; return;
      case Pins::SHOCK_SENSOR_PIN: id = DeviceID::SHOCK_SENSOR; return;
      case Pins::IGNITION_FB_PIN: id = DeviceID::IGNITION_FB; return;

      //audio pins are not querried at runtime so we avoid them here.

      default: ECCLES_LOG_LINE("FATAL ERROR: unknown pin"); //this is irecoverable we cant initialize a device we dont know since all devices are known at compile time this should never happen.
    }
  }

  void Device::enable() {
    enabled = true;
  }

  void Device::disable() {
    enabled = false;
  }

  DEVICE_TYPE Device::getType(){
    return type;
  }

};
