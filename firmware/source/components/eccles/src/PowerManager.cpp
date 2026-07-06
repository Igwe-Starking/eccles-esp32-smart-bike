//battery life power management implementation

#include "PowerManager.h"
#include "Transport.h"
#include "Executors.h"

#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_log.h"

ECCLES_API {

    e_boolean PowerManager::ignitionExternalFlag = false;

    //idle thresholds
    constexpr e_uint32 IDLE_REDUCED_MS     =  1000;  //1 s  -> drop to 80 MHz
    constexpr e_uint32 IDLE_LIGHT_SLEEP_MS = 30000;  //30 s -> light sleep
    constexpr e_uint32 IDLE_DEEP_SLEEP_MS  = 300000; //5 min -> deep sleep

    //yield bounds for REDUCED stage linear ramp: starts at MIN_YIELD_MS when idle
    //first crosses IDLE_REDUCED_MS and reaches MAX_YIELD_MS at IDLE_LIGHT_SLEEP_MS
    constexpr e_uint32 MIN_YIELD_MS = 5;
    constexpr e_uint32 MAX_YIELD_MS = 50;

    //deep sleep wakes on a 60 s RTC timer so the WiFi stack can re-init and the AP
    //becomes reachable again for up to RECONNECT_WINDOW_MS before going back down
    constexpr e_uint64 DEEP_SLEEP_WAKEUP_US  = 60ULL * 1000000ULL; //60 s in microseconds
    constexpr e_uint32 RECONNECT_WINDOW_MS   = 10000; //10 s awake after deep sleep wakeup

    e_boolean PowerManager::anyConnected() const {
        e_boolean wifi = WebTransport::isWifiConnected();
        e_boolean bt   = (Bluetooth::instance != nullptr) && Bluetooth::instance->connected;
        if(wifi || bt) return true;

        //check ignition state to suppress idle when the bike is active
        //we need the DeviceManager to query ignition desired vs actual state
        //DeviceExecutor::ld is the LiveData instance — access via the global Executor chain
        //rather than a direct pointer, use globalState which is always up to date
        if(globalState.ENGINE_RUNNING == ON){
            //engine is running regardless of who started it — never idle during a ride
            return true;
        }

        if(globalState.IGNITION_STATE == ON){
            //ignition is on — check if we commanded it (desiredState ON) or it was external
            //we use globalState.EXECUTION_MODE as a proxy: if false, the device executor
            //disabled commands meaning the system is in a passive monitoring state
            //For the ignition desired-vs-actual check, we read the DigitalIODevice fields
            //directly through the Executor's DeviceExecutor instance via a small helper
            //declared below in PowerManager.h — externalIgnition() returns true when
            //the ignition actual is ON but desired is OFF (powered externally, not by us)
            if(externalIgnition()) return true; //external ignition: stay fully awake
            //we commanded the ignition on — safe to idle (engine is confirmed not running above)
        }

        return false;
    }

    //checks the ignition device's actual vs desired state to detect external power-on
    //returns true when the bike's ignition is physically on but we did not command it
    e_boolean PowerManager::externalIgnition() const {
        //walk the Executor list via the static executors array to reach DeviceExecutor::ld
        //DeviceExecutor is always the first executor registered (see ExecutorManager.h)
        //We reach it through Eccles::globalState plus the DeviceManager findDevice path
        //Since DeviceManager is not directly accessible here without creating a circular
        //dependency, we use a lightweight check: IGNITION_STATE is only set ON by LiveData
        //when the ADC reads voltage above the threshold. The ignition DigitalIODevice
        //desiredState is set to ON only when our executor calls turnOnDevice(IGNITION).
        //We detect external-on by checking whether we have active command-side state:
        //if EXECUTION_MODE is true (normal) and ignition voltage is present but we
        //have no pending ON command in the pool, it is external.
        //
        //The most reliable zero-coupling approach: expose a static flag set by
        //DigitalIODevice::on() for the IGNITION device and cleared by off().
        return ignitionExternalFlag;
    }

    void PowerManager::init(){
        esp_pm_config_t cfg = {};
        cfg.max_freq_mhz = 240;
        cfg.min_freq_mhz = 80;
        cfg.light_sleep_enable = false;
        esp_pm_configure(&cfg);

        stage = PowerStage::FULL;
        tickCount = 0;
        deepSleepArmed = false;

        //detect if we just woke from deep sleep — if so, start the idle timer immediately
        //so we return to deep sleep after RECONNECT_WINDOW_MS if nobody connects
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if(cause == ESP_SLEEP_WAKEUP_TIMER){
            //woke from the deep-sleep RTC timer: give WiFi RECONNECT_WINDOW_MS to reconnect
            //then go back down — idleStart set to a past time so the threshold fires quickly
            idleStart = eccles_millis() - (IDLE_DEEP_SLEEP_MS - RECONNECT_WINDOW_MS);
            ECCLES_LOG_LINE("PowerManager: resumed from deep sleep, reconnect window open");
        } else {
            idleStart = 0; //normal cold boot, full idle budget before any sleep
        }

        ECCLES_LOG_LINE("PowerManager: initialized, stage=FULL");
    }

    void PowerManager::enterFull(){
        if(stage == PowerStage::FULL) return;

        esp_pm_config_t cfg = {};
        cfg.max_freq_mhz = 240;
        cfg.min_freq_mhz = 240;
        cfg.light_sleep_enable = false;
        esp_pm_configure(&cfg);

        //disable WiFi power save so the radio is fully responsive
        esp_wifi_set_ps(WIFI_PS_NONE);

        stage = PowerStage::FULL;
        tickCount = 0;
        deepSleepArmed = false;
        ECCLES_LOG_LINE("PowerManager: stage=FULL (connection active or idle < 1 s)");
    }

    void PowerManager::enterReduced(){
        if(stage == PowerStage::REDUCED) return;

        esp_pm_config_t cfg = {};
        cfg.max_freq_mhz = 80;
        cfg.min_freq_mhz = 80;
        cfg.light_sleep_enable = false;
        esp_pm_configure(&cfg);

        //minimum modem sleep: WiFi wakes on every DTIM beacon,
        //stays associated with the AP, clients can still connect
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        stage = PowerStage::REDUCED;
        tickCount = 0;
        ECCLES_LOG_LINE("PowerManager: stage=REDUCED (80 MHz, modem sleep)");
    }

    void PowerManager::enterLightSleep(){
        if(stage == PowerStage::LIGHT_SLEEP) return;

        //keep 80 MHz during light sleep - it will be halted anyway so the
        //freq only matters during the brief active windows between sleeps
        esp_pm_config_t cfg = {};
        cfg.max_freq_mhz = 80;
        cfg.min_freq_mhz = 80;
        cfg.light_sleep_enable = false;
        esp_pm_configure(&cfg);

        //minimum modem sleep keeps the AP association alive through light sleep so
        //a client that connects during this stage finds the AP immediately
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        stage = PowerStage::LIGHT_SLEEP;
        tickCount = 0;
        ECCLES_LOG_LINE("PowerManager: stage=LIGHT_SLEEP (CPU halted, WiFi modem sleep)");
    }

    void PowerManager::enterDeepSleep(){
        //deep sleep kills the WiFi radio, RTC timer wakeup re-runs app_main which
        //re-inits WiFi and gives clients RECONNECT_WINDOW_MS to connect before
        //the system enters deep sleep again if nobody connects
        ECCLES_LOG_LINE("PowerManager: entering DEEP_SLEEP (60 s wakeup cycle)");

        //give the uart time to flush before we kill the CPU
        vTaskDelay(pdMS_TO_TICKS(50));

        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_WAKEUP_US);
        esp_deep_sleep_start(); //does not return, app_main runs on next wake
    }

    e_uint32 PowerManager::tick(){
        e_uint32 slept = 0;
        e_boolean connected = anyConnected();

        if(connected){
            //any connection resets idle timer and immediately restores full power
            idleStart = 0;
            enterFull();
            vTaskDelay(pdMS_TO_TICKS(5));
            return 5;
        }

        //first tick with no connection: record the idle start time
        e_uint32 now = eccles_millis();
        if(idleStart == 0) idleStart = now;
        e_uint32 idleMs = now - idleStart;
        tickCount++;

        if(idleMs < IDLE_REDUCED_MS){
            //stage 0: full power, short yield
            if(stage != PowerStage::FULL) enterFull();
            vTaskDelay(pdMS_TO_TICKS(5));
            return 5;
        }

        if(idleMs < IDLE_LIGHT_SLEEP_MS){
            //stage 1: 80 MHz, yield ramps linearly from MIN_YIELD_MS to MAX_YIELD_MS
            enterReduced();
            e_uint32 range = IDLE_LIGHT_SLEEP_MS - IDLE_REDUCED_MS;
            e_uint32 pos   = idleMs - IDLE_REDUCED_MS;
            e_uint32 yieldMs = MIN_YIELD_MS + ((MAX_YIELD_MS - MIN_YIELD_MS) * pos / range);
            if(yieldMs > MAX_YIELD_MS) yieldMs = MAX_YIELD_MS;
            vTaskDelay(pdMS_TO_TICKS(yieldMs));
            return yieldMs;
        }

        if(idleMs < IDLE_DEEP_SLEEP_MS){
            //stage 2: light sleep — CPU halts, WiFi modem sleep keeps AP association
            //esp_light_sleep_start() suspends the CPU and returns on any wakeup source
            //we configured (timer heartbeat or WiFi packet), so the loop still runs
            //periodically to check for new connections
            enterLightSleep();
            esp_sleep_enable_timer_wakeup(500ULL * 1000ULL); //re-arm each time: consumed on wake
            esp_light_sleep_start(); //blocks until timer or WiFi wakeup fires
            eccles_wdtReset(); //light sleep suspends the task; reset WDT immediately on resume
            slept = 500;

            if(anyConnected()){
                idleStart = 0;
                enterFull();
            }
            return slept;
        }

        //stage 3: deep sleep
        //enter deep sleep — does not return, app_main runs on wakeup
        //if the system just woke from deep sleep and a client connected during
        //RECONNECT_WINDOW_MS, anyConnected() above would have caught it already,
        //so reaching here means nobody connected in the window: go back down
        enterDeepSleep(); //does not return
        return 0; //unreachable, silences compiler warning
    }

};
