/*
  this header manages the default IO pins for eccles bike project
*/

#ifndef ECCLES_PINS
#define ECCLES_PINS
#include "EcclesTypes.h"
#include <driver/i2s_std.h>
#include <driver/dac_continuous.h>

ECCLES_API {

    typedef enum {
      ECCLES_INTERNAL = 1 << 0,  //output via internal DAC (dac_continuous on GPIO25)
      ECCLES_EXTERNAL = 1 << 1,  //output via external I2S codec (i2s_std TX)
      ECCLES_INPUT_INTERNAL = 1 << 2, //input via built-in ADC (esp_adc oneshot)
      ECCLES_INPUT = 1 << 3      //input via external I2S mic (i2s_std RX)
    } I2S_PLAY_MODE;

    //audio channel handles for the new IDF 5.x i2s_std driver
    //one TX and one RX channel, installed/uninstalled by Pins::initializeAudioPins
    struct AudioHandles {
      i2s_chan_handle_t tx = nullptr; //used for ECCLES_EXTERNAL playback
      i2s_chan_handle_t rx = nullptr; //used for ECCLES_INPUT recording
      dac_continuous_handle_t dac = nullptr; //used for ECCLES_INTERNAL DAC playback
    };

    extern AudioHandles audioHandles;

    struct i2sConfig
    {
      e_uint16 rate = 16000;
      I2S_PLAY_MODE mode = ECCLES_INTERNAL;
      e_uint8 depth = 16;
      e_uint8 bufferCount = 8;
      e_uint8 bufferLen = 250;
      e_boolean stereo = false;
      e_boolean exit = false;
    };

  namespace Pins {

    constexpr e_uint8 IGNITION_CONTROL_PIN   = 16;
    constexpr e_uint8 HORN_CONTROL_PIN       = 17;
    constexpr e_uint8 HEADLAMP_CONTROL_PIN   = 18;
    constexpr e_uint8 LEFT_SIGNAL_CONTROL_PIN= 19;
    constexpr e_uint8 RIGHT_SIGNAL_CONTROL_PIN= 23;
    constexpr e_uint8 STARTER_CONTROL_PIN    = 5;
    constexpr e_uint8 ENGINE_LOCK_PIN        = 4;

    constexpr e_uint8 I2S_CLOCK_PIN = 26;
    constexpr e_uint8 I2S_DATA_PIN  = 25;
    constexpr e_uint8 I2S_WORD_PIN  = 22;

    constexpr e_uint8 IGNITION_FB_PIN  = 32;
    constexpr e_uint8 FUEL_GAUGE_PIN   = 35;
    constexpr e_uint8 TEMP_GAUGE_PIN   = 33;
    constexpr e_uint8 MIC_PIN          = 36;

    constexpr e_uint8 HORN_FB_PIN          = 14;
    constexpr e_uint8 HEADLAMP_FB_PIN      = 27;
    constexpr e_uint8 STARTER_FB_PIN       = 34;
    constexpr e_uint8 LEFT_SIGNAL_FB_PIN   = 13;
    constexpr e_uint8 RIGHT_SIGNAL_FB_PIN  = 21;
    constexpr e_uint8 SHOCK_SENSOR_PIN     = 39;
    constexpr e_uint8 ENGINE_LOCK_FB_PIN   = 2;

    constexpr e_uint8 OUTPUT_PINS[]  = {IGNITION_CONTROL_PIN,HORN_CONTROL_PIN,HEADLAMP_CONTROL_PIN,LEFT_SIGNAL_CONTROL_PIN,RIGHT_SIGNAL_CONTROL_PIN,STARTER_CONTROL_PIN,ENGINE_LOCK_PIN};
    constexpr e_uint8 AUDIO_PINS[]   = {I2S_CLOCK_PIN,I2S_WORD_PIN,I2S_DATA_PIN};
    constexpr e_uint8 SENSOR_PINS[]  = {IGNITION_FB_PIN,FUEL_GAUGE_PIN,TEMP_GAUGE_PIN,MIC_PIN,SHOCK_SENSOR_PIN};
    constexpr e_uint8 FEEDBACK_PINS[]= {IGNITION_FB_PIN,HORN_FB_PIN,HEADLAMP_FB_PIN,LEFT_SIGNAL_FB_PIN,RIGHT_SIGNAL_FB_PIN,STARTER_FB_PIN,ENGINE_LOCK_FB_PIN};

    void initializeAll();
    e_boolean initializeAudioPins(i2sConfig& config);

    e_string getPinName(e_uint8 pin);
    extern e_boolean isInitialized;
  };
};

#endif
