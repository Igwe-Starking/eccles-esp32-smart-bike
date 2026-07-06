//battery life management for the eccles bike control system
//monitors WiFi and Bluetooth connection state and steps the cpu through four
//power stages whenever both are idle,connection activity immediately restores
//full power

#ifndef ECCLES_POWER_MANAGER
#define ECCLES_POWER_MANAGER

#include "EcclesTypes.h"

ECCLES_API {

    //four stage power policy,transitions are one way downward on idle and instantly
    //reversed when any connection becomes active
    enum class PowerStage : e_uint8 {
        FULL,        //240 MHz, 5 ms yield — both or either connection active, or idle < 1 s
        REDUCED,     //80 MHz, yield scales 5..50 ms linearly — idle 1–30 s
        LIGHT_SLEEP, //CPU halted between ticks via esp_light_sleep_start, WiFi modem sleep
                     //keeps the AP association so clients can reconnect — idle 30 s–5 min
        DEEP_SLEEP   //esp_deep_sleep with 60 s RTC timer wakeup, WiFi re-inits on resume
                     //so the AP and WebSocket endpoint come back automatically each minute
    };

    class PowerManager {

        PowerStage stage    = PowerStage::FULL;
        e_uint32 idleStart  = 0;
        e_uint32 tickCount  = 0;
        e_boolean deepSleepArmed = false;

        //true when ignition physical state is ON but we did not command it
        //set/cleared by PowerManager::setIgnitionDesired() called from DigitalIODevice
        static e_boolean ignitionExternalFlag;

        void enterFull();
        void enterReduced();
        void enterLightSleep();
        void enterDeepSleep();

        e_boolean anyConnected() const;
        e_boolean externalIgnition() const;

        public:

        PowerManager() = default;

        void init();
        e_uint32 tick();
        PowerStage getStage() const { return stage; }

        //called by DigitalIODevice::on()/off() for the IGNITION device so PowerManager
        //knows whether the ignition was commanded by us or is externally powered
        static void setIgnitionDesired(e_boolean weCommandedIt){
            //external = ignition physically on but NOT commanded by us
            //when weCommandedIt=true the flag is false (not external)
            //when weCommandedIt=false and ignition is still ON it is external
            ignitionExternalFlag = !weCommandedIt && (globalState.IGNITION_STATE == ON);
        }
    };

};

#endif
