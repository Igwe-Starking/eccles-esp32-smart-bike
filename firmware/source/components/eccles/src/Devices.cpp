#include "Devices.h"
#include "EcclesTTS.h"
#include "PowerManager.h"

ECCLES_API {

  //inline helper: copies key into a 64 byte stack buffer then appends suffix, no heap
  #define SPEAK(key,suffix) do { \
    e_char _buf[64]; \
    strncpy(_buf,(key),sizeof(_buf)-1); _buf[sizeof(_buf)-1]='\0'; \
    strncat(_buf,(suffix),sizeof(_buf)-strlen(_buf)-1); \
    respond(_buf); \
  } while(0)

  //digital io devices
  DigitalIODevice::DigitalIODevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK) : Device(p),fb_pin(fb_p),activeLow(activeL),audioKey(audioK){
    type = DIGITAL_IO;
  }
  
  //turn on device and check feedback to confirm its actualy on
  void DigitalIODevice::on(){
    if(enabled){
      if(audible) respond("ok");
      eccles_enable(pin);
      desiredState = ON;
      update();
      //notify PowerManager that ignition was commanded by us so it can allow idle
      if(id == DeviceID::IGNITION) PowerManager::setIgnitionDesired(true);
      if(audible){
        if(actualState == ON){
          SPEAK(audioKey," turned on successfully");
        } else {
          SPEAK(audioKey," activation failed");
        }
      }
    }
  } 

  //turn off device and check feedback to confirm its actualy off;
  void DigitalIODevice::off(){
    if(audible) respond("ok");
    eccles_disable(pin);
    desiredState = OFF;
    update();
    //notify PowerManager ignition is off so external detection resets
    if(id == DeviceID::IGNITION) PowerManager::setIgnitionDesired(false);
    if(audible){
      if(actualState == OFF){
        SPEAK(audioKey," turned off successfully");
      } else {
        SPEAK(audioKey," failed to turn off");
      }
    }
  }

  //enable device
  void DigitalIODevice::enable(){
    Device::enable();
    SPEAK(audioKey," enabled successfully");
  }

  //disable device
  void DigitalIODevice::disable(){
    Device::disable();
    SPEAK(audioKey," disabled successfully");
  }

  //make the device silent
  void DigitalIODevice::silent(e_boolean s){
    audible = !s;
  }

  //respond the current state of device
  void DigitalIODevice::respondState(){
    if(audible){
      State s = getState();
      if(s == ON){
        SPEAK(audioKey," is on");
      } else {
        SPEAK(audioKey," is off");
      }
    }
  }

  //get the State of the hardware device,NOTE: the state is the actual state.
  State DigitalIODevice::getState(){
    update();
    return actualState;
  }

  //read data from the sensor input, this is only useful for analog device we dont need it here
  e_uint8 DigitalIODevice::read(){ return 0;}

  //update the state of the device
  void DigitalIODevice::update(){
    //ECCLES_LOG_LINE("updating the state of device");
    actualState = eccles_pinState(fb_pin) == (activeLow ? LOW : HIGH) ? ON : OFF;
  }

  //audio response
  void DigitalIODevice::respond(e_string s){
    EcclesTTS::speak(s);
  }

  //DigitalOutput devices,this device controls hardware that can only be toggled by us and in that case
  //we dont need to read feedback from external events.

  DigitalOutputDevice::DigitalOutputDevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK) : DigitalIODevice(p,fb_p,activeL,audioK) {
    type = DIGITAL_OUTPUT;
  }

  void DigitalOutputDevice::update(){
    actualState = desiredState; //we control the toggling alone so the feedback depends on what we asked it to do.
  }

  //DigitalOutputAnalogFeedbackDevice, the device that process analog feedback but controlls
  //digital devices.

  DigitalOutputAnalogFeedbackDevice::DigitalOutputAnalogFeedbackDevice(const e_uint8 p,const e_uint8 fb_p,e_boolean activeL,e_string audioK) : DigitalIODevice(p,fb_p,activeL,audioK) {
    type = DIGITAL_OUTPUT_ANALOG_INPUT;
  }
  void DigitalOutputAnalogFeedbackDevice::update(){
    //ECCLES_LOG_LINE("updating state of a DigitalOutputAnalogFeedbackDevice");
    actualState = value > 0 ? ON : OFF;
  }
  e_uint8 DigitalOutputAnalogFeedbackDevice::read(){
    value = eccles_read(fb_pin);
    return value;
  }


  //analog devices, this device reads data and update global variables
  AnalogInputDevice::AnalogInputDevice(e_uint8 p) : Device(p){
    type = ANALOG_INPUT;
  }

  //on/off only apply to digital devices,no-op here
  void AnalogInputDevice::on(){}
  void AnalogInputDevice::off(){}

  //analog devices have no on/off feedback to report
  State AnalogInputDevice::getState(){return ON;}

  //read the current analog input from the device
  e_uint8 AnalogInputDevice::read(){
    data = eccles_read(pin);
    return data;
  }

  //unimplemented,reserved for a future continuous streaming mode
  void AnalogInputDevice::stream(){}

  //SerialInput devices this type of device reads digital pulses per time
  SerialInputDevice::SerialInputDevice(e_uint8 p) : AnalogInputDevice(p) {
    type = SERIAL_INPUT;
    start();
  }

  SerialInputDevice::~SerialInputDevice(){
    stop();
  }

  //PORT NOTE: arduino's micros() (microseconds since boot) is replaced with esp_timer_get_time(),
  //which returns int64_t microseconds since boot, the original assigned this into a (volatile
  //e_uint) deltaTime/lastTime so we keep that narrowing exactly as the original did
  void IRAM_ATTR SerialInputDevice::isrRouter(void* a){

    SerialInputDevice* self = static_cast<SerialInputDevice*>(a);
    e_uint now = (e_uint) esp_timer_get_time();
    self->deltaTime = now - self->lastTime;
    self->lastTime = now;
    self->pulseCount++;
  }

  //reads pulses and frequency from serial device
  //esp_timer_get_time() replaces arduino's micros()/millis(),scaled to milliseconds below
  e_uint8 SerialInputDevice::read(){
    static e_uint8 smoothed = 0;
    e_uint dt,pulses;
    e_boolean stale;

    portENTER_CRITICAL(&mux);
    dt = deltaTime;
    pulses = pulseCount;
    pulseCount = 0; //FIX: this was never reset, so it grew for the device's entire lifetime;
                     //once total pulses passed ~64, (pulses<<2) alone exceeded 255 and this
                     //sensor's reading was permanently pinned at max shortly after boot
    stale = ((e_uint)(esp_timer_get_time() / 1000)) - (lastTime / 1000) > 200;
    portEXIT_CRITICAL(&mux);

    if(stale){
      smoothed = 0;
      return 0;
    }

    e_uint freq = (dt > 0) ? (1000000UL / dt) : 0;
    e_uint raw = (freq >> 4) + (pulses << 2);

    if(raw > 255) raw = 255;
    e_uint8 value = (e_uint8) raw;

    smoothed = (smoothed * 3 + value) >> 2;
    return smoothed;
  }

  //PORT NOTE: arduino's attachInterruptArg(pin,handler,arg,mode) is replaced with esp-idf's
  //gpio isr service. the gpio must first be (re)configured for interrupt-on-rising-edge before
  //a per-pin isr handler can be installed, the global isr service is installed once and reused
  //for every SerialInputDevice instance (installing it twice is harmless, esp-idf no-ops a
  //second gpio_install_isr_service call with the same flags and returns ESP_ERR_INVALID_STATE
  //which we deliberately ignore here)
  void SerialInputDevice::start(){
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_POSEDGE; //RISING, matches the original attachInterruptArg(...,RISING)
    gpio_config(&cfg);

    gpio_install_isr_service(0); //ignored if already installed elsewhere
    gpio_isr_handler_add((gpio_num_t) pin,isrRouter,this);
  }

  void SerialInputDevice::stop(){
    gpio_isr_handler_remove((gpio_num_t) pin);
  }

  void SerialInputDevice::stream(){
    
  }

};
