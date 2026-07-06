//base implementation of device manager class
//PORT NOTE: pure C++ business logic, unchanged except millis() -> eccles_millis()

#include "DeviceManager.h"
#include "EcclesTTS.h"
#include "PowerManager.h"

ECCLES_API {

    /*
      this is a pre initialization function that runs before DeviceManager constructor runs
      the purpose of this code is to pre-initialize all pins before DeviceManager constructor
      initializes devices,we want Pins to initialize first before any device is constructed 
      because some device might reconfigure their pin in their constructor and to avoid overriding
      their custom configuration we pre-initialize pins before creating devices
    */

    e_uint8 preInit(){
      //initializing all pins 
      Pins::initializeAll();

      //other preinit initialization that might be needed
      return 0;
    }

    DeviceManager::DeviceManager() : deviceCount(preInit()),headlamp(Pins::HEADLAMP_CONTROL_PIN,Pins::HEADLAMP_FB_PIN,true,"headlamp"),horn(Pins::HORN_CONTROL_PIN,Pins::HORN_FB_PIN,true,"horn"),left_turn(Pins::LEFT_SIGNAL_CONTROL_PIN,Pins::LEFT_SIGNAL_FB_PIN,true,"left signal"),right_turn(Pins::RIGHT_SIGNAL_CONTROL_PIN,Pins::RIGHT_SIGNAL_FB_PIN,true,"right signal"),starter(Pins::STARTER_CONTROL_PIN,Pins::STARTER_FB_PIN,true,"starter"),ignition(Pins::IGNITION_CONTROL_PIN,Pins::IGNITION_FB_PIN,true,"ignition"),engine(Pins::ENGINE_LOCK_PIN,Pins::ENGINE_LOCK_FB_PIN,true,"engine lock"){
    
      //attaching devices to device array
      devices[0] = &headlamp;
      devices[1] = &horn;
      devices[2] = &left_turn;
      devices[3] = &right_turn;
      devices[4] = &starter;
      devices[5] = &ignition;
      devices[6] = &engine;

      //pre initializing all pins we do this so that all pins are set to their default states
      //Pins::initializeAll(); we should'nt do this here in case any device want to re-initialize their pins state to avoid overriding them.

      //turninig of all devices, we do this on each we boot to set the devicess in their correct off state

      turnOffAll();

      deviceCount = sizeof(devices) / sizeof(devices[0]); //hardcoded assignment of devices not supposed but we are still working to optimize this.
    }
    DeviceManager::~DeviceManager(){}

    //resolves the id of all known devices
    DeviceID DeviceManager::resolveId(e_string id){
     // ECCLES_LOG("device manager resolving id of device: ");
      //ECCLES_LOG_LINE(id);
      if(eccles_compareString(id,"headlamp")) return DeviceID::HEADLAMP;
      if(eccles_compareString(id,"horn")) return DeviceID::HORN;
      if(eccles_compareString(id,"ignition")) return DeviceID::IGNITION;
      if(eccles_compareString(id,"starter")) return DeviceID::STARTER;
      if(eccles_compareString(id,"left_turn")) return DeviceID::LEFT_TURN;
      if(eccles_compareString(id,"right_turn")) return DeviceID::RIGHT_TURN;
      if(eccles_compareString(id,"fuel_gauge")) return DeviceID::FUEL_GAUGE;
      if(eccles_compareString(id,"temp_gauge")) return DeviceID::TEMP_GAUGE;
      if(eccles_compareString(id,"shock_sensor")) return DeviceID::SHOCK_SENSOR;
      if(eccles_compareString(id,"microphone")) return DeviceID::MICROPHONE;
      return DeviceID::UNKNOWN_DEVICE;
    }

    //search and locate device by its id, NOTE: this only searches for digitalio devices as that is what this class specialized on
    //we return Device* instead of Device& here for error checking purposes.

    Device* DeviceManager::findDevice(DeviceID id){
      if(id == DeviceID::UNKNOWN_DEVICE){
        ECCLES_LOG_LINE("unknown device id supplied exiting: on DeviceManager::findDevice()");
        return nullptr;
      }
      //ECCLES_LOG_LINE("finding id of a digitaldevice");
      for(Device* d:devices){
        if(!d) continue;
        if(d->id == id) return d;
      }
      ECCLES_LOG_LINE("unable to find the specified id");
      return nullptr;
    }

    //ask a device to turn on
    void DeviceManager::turnOnDevice(Device& d){
      ECCLES_LOG("turning on device:");
      ECCLES_LOG_LINE(Pins::getPinName(d.pin));
      d.on();
    }
    void DeviceManager::turnOnDevice(DeviceID id){
      ECCLES_LOG_LINE("turning on device by id");
      if(id == DeviceID::ALL){
        turnOnAll();
        return;
      }
      Device* dv = findDevice(id);
      if(dv == nullptr) return; //we would have alert but we have already done that in findDevice
      turnOnDevice(*dv);
    }
    void DeviceManager::turnOnDevice(e_string name){
      ECCLES_LOG("turning on device: ");
      ECCLES_LOG_LINE(name);
      turnOnDevice(resolveId(name));
    }

    //checks if any device in the device list has this id
    e_boolean DeviceManager::checkID(DeviceID id){
      for(Device *d : devices){
        if((d != nullptr) && (d->id == id)) return true;
      }
      return false;
    }

    //ask a device to turn off
    void DeviceManager::turnOffDevice(Device& d){
      d.off();
    }
    void DeviceManager::turnOffDevice(DeviceID id){
      ECCLES_LOG_LINE("turning of device by id");
      if(id == DeviceID::ALL){
        turnOffAll();
        return;
      }
      Device* dv = findDevice(id);
      if(dv == nullptr) return;
      turnOffDevice(*dv);
    }
    void DeviceManager::turnOffDevice(e_string dn){
      ECCLES_LOG("turning off device: ");
      ECCLES_LOG_LINE(dn);
      turnOffDevice(resolveId(dn));
    }

    //check if the device is on,only digital io device can respond their state
    e_boolean DeviceManager::isDeviceOn(Device& d){
      if(d.type != DIGITAL_IO) return false;
      DigitalIODevice& de = static_cast<DigitalIODevice&>(d);
      de.respondState();
      return de.getState() == ON;
    }

    e_boolean DeviceManager::isDeviceOn(DeviceID id){
      Device* dv = findDevice(id);
      if(dv == nullptr) return false;
      return isDeviceOn(*dv);
    }
    e_boolean DeviceManager::isDeviceOn(e_string dn){
      ECCLES_LOG("checking if device is on: ");
      ECCLES_LOG_LINE(dn);
      return isDeviceOn(resolveId(dn));
    }


    //check the state of a device without voice response
    State DeviceManager::getDeviceState(Device& d){
      return d.getState();
    }


    State DeviceManager::getDeviceState(DeviceID id){
      Device* dv = findDevice(id);
      if(dv == nullptr) return OFF;
      return getDeviceState(*dv);
    }
    State DeviceManager::getDeviceState(e_string dn){
      ECCLES_LOG("checking the state of device: ");
      ECCLES_LOG_LINE(dn);
      return getDeviceState(resolveId(dn));
    }

    //check if any device is on, returns the number of active devices
    e_uint8 DeviceManager::isAnyDeviceOn(){
      e_uint8 count = 0;
      for(int i = 0;i<deviceCount;i++){
        Device& d = *(devices[i]);
        if(getDeviceState(d) == ON && d.getType() == DIGITAL_IO){
          count++;
          DigitalIODevice& dio = static_cast<DigitalIODevice&>(d);
          dio.respondState();
        }
      }
      return count;
    }


    //enable device,devices can only be turned on if they are enabled
    void DeviceManager::enableDevice(Device& d){
      d.enable();
    }
    void DeviceManager::enableDevice(DeviceID id){
      Device* dv = findDevice(id);
      if(dv == nullptr) return;
      enableDevice(*dv);
    }
    void DeviceManager::enableDevice(e_string dn){
      ECCLES_LOG("enabling device: ");
      ECCLES_LOG_LINE(dn);
      enableDevice(resolveId(dn));
    }

    //disable device, once device is disabled it cant be turned on
    void DeviceManager::disableDevice(Device& d){
      d.disable();
    }
    void DeviceManager::disableDevice(DeviceID id){
      Device* dv = findDevice(id);
      if(dv == nullptr) return;
      disableDevice(*dv);
    }
    void DeviceManager::disableDevice(e_string dn){
      ECCLES_LOG("disabling device: ");
      ECCLES_LOG_LINE(dn);
      disableDevice(resolveId(dn));
    }

    void DeviceManager::toggleDevice(DeviceID id){
      Device* d = findDevice(id);
      if(d == nullptr) return;
      if(d->type != DIGITAL_IO && d->type != DIGITAL_OUTPUT){
        ECCLES_LOG_LINE("attempting to toggle a not digital io device");
        return;
      }

      ECCLES_LOG_LINE("toggling device");
      DigitalIODevice* dio = static_cast<DigitalIODevice*>(d);
      if(dio->desiredState == ON){
        turnOffDevice(id);
      } else {
        turnOnDevice(id);
      }
    }

    //silence device,device can still be turned on/off but wont give any response
    void DeviceManager::silenceDevice(Device& de,e_boolean silent){
      //only works on digital io devices
      if(de.getType() != DIGITAL_IO){
        ECCLES_LOG_LINE("attempting to silence a non digital device exiting...");
        return;
      }
      DigitalIODevice& d = static_cast<DigitalIODevice&>(de);
      d.silent(silent);
    }
    void DeviceManager::silenceDevice(DeviceID id,e_boolean silent){
      Device* dv = findDevice(id);
      if(dv == nullptr) return;
      silenceDevice(*dv,silent);
    }
    void DeviceManager::silenceDevice(e_string dn,e_boolean silent){
      ECCLES_LOG("silencing device: ");
      ECCLES_LOG_LINE(dn);
      silenceDevice(resolveId(dn),silent);
    }

    //turning all devices on
    void DeviceManager::turnOnAll(){
      for(int i = 0;i<MAX_DEVICES;i++){
        if(devices[i] == nullptr) continue;
        Device& d = *(devices[i]);
          if(d.getType() != DIGITAL_IO) continue;
          
          //we needed to silence the device before running buck on
          silenceDevice(d,true);
          turnOnDevice(d);
          silenceDevice(d,false);
        
      }
    }

    //turning off all devices
    void DeviceManager::turnOffAll(){
      for(int i = 0;i<MAX_DEVICES;i++){
        if(devices[i] == nullptr) continue;
        Device& d = *(devices[i]);
          if(d.getType() != DIGITAL_IO) continue;
          
          //we needed to silence the device before running buck on
          silenceDevice(d,true);
          turnOffDevice(d);
          silenceDevice(d,false);
        
      }
    }

    //LiveData manager,this class manages live streams from sensors and update global data

    LiveData::LiveData() : fuel(Pins::FUEL_GAUGE_PIN),temp(Pins::TEMP_GAUGE_PIN),ignition_fb(Pins::IGNITION_FB_PIN),shock(Pins::SHOCK_SENSOR_PIN) {
      //assigning to live devices
      liveDevices[0] = &fuel;
      liveDevices[1] = &temp;
      liveDevices[2] = &ignition_fb;
      liveDevices[3] = &shock;
    }

    LiveData::~LiveData(){
      
    }

    //read from all sensors and update all fields in global data.
    void LiveData::writeGlobal(){
      //updating ignition data,very important data
      e_float BV = voltageLevel(); //we read the battery voltage from ignition fb pin to determine whats happening
      if(BV > IGNITION_THRSH){
        globalState.IGNITION_STATE = ON;
      } else {
        globalState.IGNITION_STATE = OFF;
      }

      //updating the engine run data
      if(BV > ENGINE_RUN_THRSH){
        globalState.ENGINE_RUNNING = ON;
      } else {
        globalState.ENGINE_RUNNING = OFF;
      }

      //detect external ignition: physical ignition ON but desired state is OFF means
      //the bike was started externally — tell PowerManager to suppress idle
      Device* ignDev = findDevice(DeviceID::IGNITION);
      if(ignDev){
        DigitalIODevice* d = static_cast<DigitalIODevice*>(ignDev);
        e_boolean externallyOn = (globalState.IGNITION_STATE == ON) && (d->desiredState == OFF);
        PowerManager::setIgnitionDesired(!externallyOn);
      }

      //updating engine lock state
      Device* engineLockD = findDevice(DeviceID::ENGINE);
      if(engineLockD){
        globalState.ENGINE_LOCK = engineLockD->getState();
      }

      //voltaage level update
      globalState.VOLTAGE_LEVEL = BV;

      //updating fuel data
      Device* fuelD = findDevice(DeviceID::FUEL_GAUGE);
      if(fuelD){
        globalState.FUEL_DATA = normalize(fuelD->read());
      }

      //updating temp data
      Device* tempD = findDevice(DeviceID::TEMP_GAUGE);
      if(tempD){
        globalState.TEMP_DATA = normalize(tempD->read());
      }

      //updating shock sensor data
      Device* shockD = findDevice(DeviceID::SHOCK_SENSOR);
      if(shockD){
        globalState.SHOCK_DATA = shockD->read(); //we dont normalize this because it is not really an analog device
      }
    }
    
    e_string LiveData::prepareLive(e_uint16* len){
      return "in progress";
    }
    void LiveData::respondData(e_string p){
      EcclesTTS::speak(p);
    }

    //checks if any device in the liveDevices has this id and call super when non is found
    e_boolean LiveData::checkID(DeviceID id){
      for(Device* d : liveDevices){
        if((d != nullptr) && (d->id == id)) return true;
      }
      //maybe the device is a digital device in the base class we invoke it
      return DeviceManager::checkID(id);
    }

    //returns the value of the sensor specified by ID in normalized form NOTE: sensor reads 0 - 255 range but normalized form
    //reads 0 - 100 in percentage format
    e_uint8 LiveData::getData(DeviceID id){
      AnalogInputDevice* d = static_cast<AnalogInputDevice*>(findDevice(id));
      if(d == nullptr){
        //shouldnt happen
        ECCLES_LOG_LINE("failed to retieve analog device for data");
        return 0;
      }

      e_uint8 data = d->read();
      if(globalState.AUDIBLE == ON){
        respondData("ok"); 
      }
      return data;
    }

    //searches and returns sensor device or calls Base class to return digital device
    Device* LiveData::findDevice(DeviceID id){
      for(Device* d: liveDevices){
        if(!d) continue;
        if(d->id == id) return d;
      }
      //if we get here call super
      return DeviceManager::findDevice(id); //the device is not a sensor device.
    }

    //converting raw data into a readable percantage.
    e_uint8 LiveData::normalize(e_uint8 data){
      //we read the battery voltage from ignition fb pin first.
      AnalogInputDevice* d = static_cast<AnalogInputDevice*>(findDevice(DeviceID::IGNITION_FB));
      if(d == nullptr){
        ECCLES_LOG_LINE("FATAL ignition input not found"); //this should never happen.
        return 0;
      }
      e_uint8 value = d->read(); //reads ignition value
      if(value < 5){
        //ECCLES_LOG_LINE("FATAL: ignition input not recognized or battery too low");
        //we used ignition input to normalize sensor readings inorder to account for
        //battery voltage flunctuation since ignition input is our true source of battery voltage
        //but if ignition input is not present we default to use the normal normalize adc reading instead
        value = 255;
      }
      e_uint16 p = (data * 100) / value;
      //FIX: this used to reset an out-of-range percentage to 0 (empty/cold) instead of
      //clamping it to 100 (full/hot). A reading just above the reference (ADC noise near a
      //genuinely full tank, for example) was silently reported as completely empty -- the
      //worst possible failure direction for a gauge.
      if(p > 100) p = 100;
      return (e_uint8) p;
    }

    e_float LiveData::voltageLevel(){
      //we get voltage level from ignition fb pin so we read that first.
      AnalogInputDevice* d = static_cast<AnalogInputDevice*>(findDevice(DeviceID::IGNITION_FB));
      if(d == nullptr){
        //this should never happen
        ECCLES_LOG_LINE("ignition input not valid");
        return 0;
      }
      //reading ignition fb value NOTE: igniton fb is used to determine battery voltage
      e_uint8 value = d->read();

      /*
        to know the exact battery voltage which is what we are looking for here we need to undo all divisions we have done
        NOTE: our esp cant safely read 12-14v so we used a voltage divider to scale the voltage down, the value of the resistor
        that connects directly to the voltage input is stored in R0 and the resistor that connect to ground stored in R1,
        VREF 3.3v this is exactly what esp can handle so we undo all of this to get the real supplied voltage
      */
      return (value / 255.0f) * VREF * ((R0 + R1) / R1);

    }

    //monitor specific functions

    //start a monitor on a device here we can ask the livedata to monitor a device 
    //and do something whenever the device met a condition we set
    void LiveData::startMonitor(ld_monitor m){
      //check if monitor handler is not null
      if(handler == nullptr){
        ECCLES_LOG_LINE("can't start monitor on a null monitor handler exiting...");
        return;
      }
      //we check if the monitor lists has an empty slot
      for(ld_monitor& mn : monitors){
        if(!mn.valid){
          mn = m; //copy the given ld_monitor to our list
          mn.valid = true;
          return;
        }
      }
      ECCLES_LOG_LINE("monitor list full monitoring ignored");
    }

    //stop the given monitor
    void LiveData::stopMonitor(e_uint8 id){
      for(ld_monitor& m : monitors){
        if(m.id == id){
          m = {};
          return;
        }
      }
    }

    //sets the monitor handler
    void LiveData::setHandler(MonitorHandler* mnh){
      handler = mnh;
    }

    //the main monitoring function,here we run a condition check to see if any
    //monitor passed the condition and call monitor handler to handle it
    void LiveData::monitor(){
      //check and update the duration
      //NOTE: checking monitors and updating sensors in a free cycle will waste the cpu
      //so we only check every 500ms that's enough to update sensors since they are not critical
      //sensors any way and execute a monitor command to avoid hunting the cpu

      if(ldm_d == 0){
        ldm_d = (eccles_millis() + 500); //500ms
        return;
      }

      if(ldm_d > eccles_millis()) return; //time not reached

      writeGlobal(); //refresh ignition, engine-run, voltage and fuel state every 500 ms
      for(ld_monitor& m : monitors){
        if(m.valid){ //we got a monitor lets check the conditions
          if(m.type == MonitorType::ON || m.type == MonitorType::OFF){
            //this is a digital device analog can't be on'ed or off'ed
            DigitalIODevice* d = (DigitalIODevice*) findDevice(m.target);
            if(d != nullptr){
              //check if the monitor condition is met
              State st = d->getState();
              if((m.type == MonitorType::ON && st == ON) || (m.type == MonitorType::OFF && st == OFF) || (m.type == MonitorType::CHANGE_M && (m.prevState != st))){
                //here we go the condition is true

                //we check if the condition has already been evaluated so that we don't have to 
                //call the handler multiple times for and already passed condition leading to hugging the command system
                if(m.prevState != st){ //we can only call this again when the condition state actually changes not all the time the condition is true
                  if(handler != nullptr){ //should't be we checked earlier on startMonitor
                   handler->onPassed(m.action,8);
                  }
                }
              }
              m.prevState = st;
            }
          } else if((m.type == MonitorType::VALUE) || (m.type == MonitorType::VALUE_G) || (m.type == MonitorType::VALUE_L)){
            //probably a sensor 
            AnalogInputDevice* aid = (AnalogInputDevice*) findDevice(m.target);
            if(aid){
              e_uint8 vl = normalize(aid->read()); //convert to percentage
              //checking the condition
              if((m.type == MonitorType::VALUE && (m.value == vl)) || (m.type == MonitorType::VALUE_G && (m.value < vl)) || (m.type == MonitorType::VALUE_L && (m.value > vl))){
                //condition met m.prevState is set to on whenever the conditin is met to avoid calling repeatedly
                if(m.prevState == OFF){
                  if(handler) handler->onPassed(m.action,8);
                  m.prevState = ON;
                }
              } else {
                //if condition changed to false reset the state so we monitor next time it becomes true
                m.prevState = OFF;
              }
            }
          }
        }
      }

    }

};
