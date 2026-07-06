#include "EcclesTypes.h"
#include <cstdlib>

#ifndef ADC_CHANNEL_MAX
#define ADC_CHANNEL_MAX (10) // Maximum channels supported per ADC unit on ESP32
#endif


ECCLES_API {

   e_boolean eccles_compareString(e_string a,e_string b){
    if(a == nullptr || b == nullptr) return false;
    while(*a == *b){
      if(*a == '\0') return true;
      ++a;
      ++b;
    }
    return false;
  }

  //runtime hashing function
  const e_uint32 eccles_hashRT(e_string s){
    e_uint32 h = 5381; //the hash modular
    if(s == nullptr) return h;
    
    while(*s){
      h = ((h << 5) + h) + (e_uint8)(*s);
      s++;
    }
    return h;
  }

  GLOBAL_STATE globalState;

  /*
    PORT NOTE: everything below here is new, it backs the eccles_* macros declared in
    EcclesTypes.h that previously called straight into the arduino core
  */

  //--- analog input --------------------------------------------------------
  //lazily creates one shared ADC1 oneshot unit handle the first time any pin is read,
  //then lazily configures whichever channel that pin maps to the first time it's seen,
  //mirroring how arduino's analogRead(pin) "just worked" the first time you called it
  static adc_oneshot_unit_handle_t adcUnit = nullptr;
  static e_boolean adcChannelConfigured[ADC_CHANNEL_MAX] = {false};

  //maps an esp32 gpio number to its ADC1 channel, only the channels actually wired in
  //Pins.h are listed here, this mirrors arduino-esp32's internal pin->channel table for
  //the handful of pins this project actually uses as analog inputs
  static e_boolean gpioToAdc1Channel(e_uint8 pin,adc_channel_t* outCh){
    switch(pin){
      case 32: *outCh = ADC_CHANNEL_4; return true; //IGNITION_FB_PIN
      case 33: *outCh = ADC_CHANNEL_5; return true; //TEMP_GAUGE_PIN
      case 34: *outCh = ADC_CHANNEL_6; return true; //STARTER_FB_PIN
      case 35: *outCh = ADC_CHANNEL_7; return true; //FUEL_GAUGE_PIN
      case 36: *outCh = ADC_CHANNEL_0; return true; //MIC_PIN
      case 39: *outCh = ADC_CHANNEL_3; return true; //SHOCK_SENSOR_PIN
      default: return false; //pin is not wired to ADC1 on the esp32, caller treats as invalid
    }
  }

  e_uint16 eccles_analogReadRaw(e_uint8 pin){
    adc_channel_t ch;
    if(!gpioToAdc1Channel(pin,&ch)){
      ECCLES_LOG_LINE("eccles_analogReadRaw: pin is not a valid ADC1 channel");
      return 0;
    }

    if(adcUnit == nullptr){
      adc_oneshot_unit_init_cfg_t initCfg = {};
      initCfg.unit_id = ADC_UNIT_1;
      if(adc_oneshot_new_unit(&initCfg,&adcUnit) != ESP_OK){
        ECCLES_LOG_LINE("eccles_analogReadRaw: failed to create ADC1 unit");
        return 0;
      }
    }

    if(!adcChannelConfigured[ch]){
      adc_oneshot_chan_cfg_t chCfg = {};
      chCfg.bitwidth = ADC_BITWIDTH_DEFAULT; //12 bit on the esp32, matches arduino's default analogRead resolution
      chCfg.atten = ADC_ATTEN_DB_11; //full ~3.3v range, matches arduino-esp32's default attenuation
      if(adc_oneshot_config_channel(adcUnit,ch,&chCfg) != ESP_OK){
        ECCLES_LOG_LINE("eccles_analogReadRaw: failed to configure ADC channel");
        return 0;
      }
      adcChannelConfigured[ch] = true;
    }

    int raw = 0;
    if(adc_oneshot_read(adcUnit,ch,&raw) != ESP_OK) return 0;
    return (e_uint16) raw;
  }

  //--- pwm style analog output ----------------------------------------------
  //the original codebase never actually calls eccles_write()/analogWrite() on any real pin
  //(every output pin in Pins.h is toggled with eccles_enable/eccles_disable instead), this
  //is kept only for api parity with the original wrapper macro, implemented with ledc so it
  //behaves like a real pwm write if anything ever does call it
  void eccles_analogWrite(e_uint8 pin,e_uint8 value){
    static e_uint8 nextChannel = 0;
    static e_int8 pinChannel[40]; //esp32 gpio count
    static e_boolean pinChannelInit = false;

    if(!pinChannelInit){
      for(auto& c : pinChannel) c = -1;
      pinChannelInit = true;
    }

    e_int8 ch = pinChannel[pin];
    if(ch < 0){
      if(nextChannel >= 8){
        ECCLES_LOG_LINE("eccles_analogWrite: all 8 ledc channels already in use");
        return;
      }
      ch = nextChannel++;
      pinChannel[pin] = ch;

      ledc_timer_config_t timerCfg = {};
      timerCfg.speed_mode = LEDC_LOW_SPEED_MODE;
      timerCfg.duty_resolution = LEDC_TIMER_8_BIT;
      timerCfg.timer_num = LEDC_TIMER_0;
      timerCfg.freq_hz = 5000;
      timerCfg.clk_cfg = LEDC_AUTO_CLK;
      ledc_timer_config(&timerCfg);

      ledc_channel_config_t chanCfg = {};
      chanCfg.gpio_num = pin;
      chanCfg.speed_mode = LEDC_LOW_SPEED_MODE;
      chanCfg.channel = (ledc_channel_t) ch;
      chanCfg.timer_sel = LEDC_TIMER_0;
      chanCfg.duty = 0;
      chanCfg.hpoint = 0;
      ledc_channel_config(&chanCfg);
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t) ch,value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t) ch);
  }

  //--- watchdog --------------------------------------------------------------
  //idf 5.x replaces the two-argument esp_task_wdt_init(timeout,panic) with a config struct
  void eccles_wdtInit(e_uint32 timeoutSeconds){
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = timeoutSeconds * 1000;
   #ifdef CONFIG_FREERTOS_UNICORE
    cfg.idle_core_mask = 1;
   #else
    cfg.idle_core_mask = (1 << 2) - 1; // Watch both cores on standard ESP32
   #endif

    cfg.trigger_panic = true;
    esp_task_wdt_init(&cfg);
  }

  //--- debug uart / logging ---------------------------------------------------
  //replaces Serial.begin/print/println, uses uart driver on UART_NUM_0 which is the same
  //physical port arduino's Serial object talked to (the usb-serial bridge on most esp32 boards)
  static e_boolean uartReady = false;

  void eccles_uartInit(e_uint32 baud){
    if(uartReady) return;
    uart_config_t cfg = {};
    cfg.baud_rate = (e_int) baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_driver_install(UART_NUM_0,1024,1024,0,nullptr,0);
    uart_param_config(UART_NUM_0,&cfg);
    uart_set_pin(UART_NUM_0,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    uartReady = true;
  }

  void eccles_printRaw(e_string s){
    if(!uartReady || s == nullptr) return;
    uart_write_bytes(UART_NUM_0,s,strlen(s));
  }

  void eccles_printLine(e_string s){
    if(!uartReady || s == nullptr) return;
    uart_write_bytes(UART_NUM_0,s,strlen(s));
    uart_write_bytes(UART_NUM_0,"\r\n",2);
  }
};
