//implementation of pins.h using new IDF 5.x I2S (i2s_std) and DAC (dac_continuous) drivers
//replaces all legacy driver/i2s.h usage which conflicts with the new driver API in IDF 5.x

#include "Pins.h"
#include <driver/i2s_std.h>
#include <driver/dac_continuous.h>
#include <esp_intr_alloc.h>

ECCLES_API {

  AudioHandles audioHandles = {};

  namespace Pins {

    e_boolean isInitialized = false;

    e_string getPinName(e_uint8 pin){
      switch(pin){
        case IGNITION_CONTROL_PIN:  return "Ignition_control";
        case HEADLAMP_CONTROL_PIN:  return "headlamp_control";
        case HORN_CONTROL_PIN:      return "horn control";
        case LEFT_SIGNAL_CONTROL_PIN:  return "left signal control";
        case RIGHT_SIGNAL_CONTROL_PIN: return "right signal control";
        case STARTER_CONTROL_PIN:   return "starter control";
        case I2S_CLOCK_PIN:         return "i2s audio clock pin";
        case I2S_WORD_PIN:          return "i2s audio word pin";
        case I2S_DATA_PIN:          return "i2s audio data pin";
        case IGNITION_FB_PIN:       return "ignition feedback pin";
        case HORN_FB_PIN:           return "horn feedback pin";
        case HEADLAMP_FB_PIN:       return "headlamp feedback pin";
        case STARTER_FB_PIN:        return "starter feedback pin";
        case LEFT_SIGNAL_FB_PIN:    return "left signal feedback pin";
        case RIGHT_SIGNAL_FB_PIN:   return "right signal feedback pin";
        case FUEL_GAUGE_PIN:        return "fuel level pin";
        case TEMP_GAUGE_PIN:        return "temperature guage pin";
        //FIX: these four real pins fell through to "unknown_pin" in every log line
        case ENGINE_LOCK_PIN:       return "engine lock control";
        case ENGINE_LOCK_FB_PIN:    return "engine lock feedback pin";
        case MIC_PIN:               return "microphone input pin";
        case SHOCK_SENSOR_PIN:      return "shock sensor pin";
        default:                    return "unknown_pin";
      }
    }

    void initializeIOPins(){
      ECCLES_LOG_LINE("initializing IO pins");

      for(const e_uint8 pin : OUTPUT_PINS){
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        #ifdef ECCLES_DEBUG
        ECCLES_LOG(getPinName(pin));
        ECCLES_LOG_LINE(" successfully set to output");
        #endif
      }

      for(const e_uint8 pin : FEEDBACK_PINS){
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        #ifdef ECCLES_DEBUG
        ECCLES_LOG(getPinName(pin));
        ECCLES_LOG_LINE(" successfully set to input");
        #endif
      }
    }

    //tears down whatever is currently installed so a fresh install can follow
    static void teardown(i2sConfig& config){
      if(audioHandles.tx){
        i2s_channel_disable(audioHandles.tx);
        i2s_del_channel(audioHandles.tx);
        audioHandles.tx = nullptr;
      }
      if(audioHandles.rx){
        i2s_channel_disable(audioHandles.rx);
        i2s_del_channel(audioHandles.rx);
        audioHandles.rx = nullptr;
      }
      if(audioHandles.dac){
        dac_continuous_disable(audioHandles.dac);
        dac_continuous_del_channels(audioHandles.dac);
        audioHandles.dac = nullptr;
      }
      (void)config;
    }

    e_boolean initializeAudioPins(i2sConfig& config){

      if(config.exit){
        teardown(config);
        return true;
      }

      //resolve i2s_bits_per_chan_t from depth
      i2s_data_bit_width_t bw;
      switch(config.depth){
        case 8:  bw = I2S_DATA_BIT_WIDTH_8BIT;  break;
        case 16: bw = I2S_DATA_BIT_WIDTH_16BIT; break;
        case 24: bw = I2S_DATA_BIT_WIDTH_24BIT; break;
        case 32: bw = I2S_DATA_BIT_WIDTH_32BIT; break;
        default: return false;
      }

      i2s_slot_mode_t slotMode = config.stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;

      //compute DMA buffer sizing the same way the legacy driver did
      e_uint16 dmaLen = 64;
      e_uint16 dmaCount = 4;
      {
        e_uint32 totalSamples = config.rate / 10;
        dmaLen = 512;
        if(totalSamples <= 1024) dmaLen = 128;
        if(totalSamples <= 512)  dmaLen = 64;
        if(totalSamples <= 256)  dmaLen = 32;
        e_uint8 bytesPerSample = config.depth / 8;
        e_uint8 channels = config.stereo ? 2 : 1;
        e_uint32 bytesPerBuf = (e_uint32)dmaLen * channels * bytesPerSample;
        while(bytesPerBuf > 4092 && dmaLen > 8){ dmaLen >>= 1; bytesPerBuf = (e_uint32)dmaLen*channels*bytesPerSample; }
        dmaCount = totalSamples / dmaLen;
        if((totalSamples % dmaLen) != 0) dmaCount++;
        if(dmaCount < 4)  dmaCount = 4;
        if(dmaCount > 24) dmaCount = 24;
      }

      esp_err_t res = ESP_OK;

      //ECCLES_INTERNAL: PCM output via built-in DAC on GPIO25 using dac_continuous
      if(config.mode & ECCLES_INTERNAL){
        dac_continuous_config_t dacCfg = {};
        dacCfg.chan_mask  = DAC_CHANNEL_MASK_CH0; //GPIO25 = DAC channel 0
        dacCfg.desc_num   = dmaCount;
        dacCfg.buf_size   = dmaLen * (config.depth / 8);
        dacCfg.freq_hz    = config.rate;
        dacCfg.offset     = 0;
        dacCfg.clk_src    = DAC_DIGI_CLK_SRC_DEFAULT;
        dacCfg.chan_mode   = config.stereo ? DAC_CHANNEL_MODE_ALTER : DAC_CHANNEL_MODE_SIMUL;

        res = dac_continuous_new_channels(&dacCfg, &audioHandles.dac);
        if(res != ESP_OK){ ECCLES_LOG_LINE("dac_continuous_new_channels failed"); return false; }

        res = dac_continuous_enable(audioHandles.dac);
        if(res != ESP_OK){
          dac_continuous_del_channels(audioHandles.dac);
          audioHandles.dac = nullptr;
          ECCLES_LOG_LINE("dac_continuous_enable failed");
          return false;
        }
        ECCLES_LOG_LINE("DAC continuous output enabled on GPIO25");
      }

      //ECCLES_EXTERNAL: I2S standard TX to external codec
      if(config.mode & ECCLES_EXTERNAL){
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        chanCfg.dma_desc_num  = dmaCount;
        chanCfg.dma_frame_num = dmaLen;
        chanCfg.auto_clear    = true;

        res = i2s_new_channel(&chanCfg, &audioHandles.tx, nullptr);
        if(res != ESP_OK){ ECCLES_LOG_LINE("i2s_new_channel (TX) failed"); return false; }

        i2s_std_config_t stdCfg = {};
        stdCfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(config.rate);
        stdCfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bw, slotMode);
        stdCfg.gpio_cfg.bclk = (gpio_num_t)I2S_CLOCK_PIN;
        stdCfg.gpio_cfg.ws   = (gpio_num_t)I2S_WORD_PIN;
        stdCfg.gpio_cfg.dout = (gpio_num_t)I2S_DATA_PIN;
        stdCfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
        stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        stdCfg.gpio_cfg.invert_flags.mclk_inv = false;
        stdCfg.gpio_cfg.invert_flags.bclk_inv  = false;
        stdCfg.gpio_cfg.invert_flags.ws_inv    = false;

        res = i2s_channel_init_std_mode(audioHandles.tx, &stdCfg);
        if(res != ESP_OK){
          i2s_del_channel(audioHandles.tx);
          audioHandles.tx = nullptr;
          ECCLES_LOG_LINE("i2s_channel_init_std_mode (TX) failed");
          return false;
        }

        res = i2s_channel_enable(audioHandles.tx);
        if(res != ESP_OK){
          i2s_del_channel(audioHandles.tx);
          audioHandles.tx = nullptr;
          ECCLES_LOG_LINE("i2s_channel_enable (TX) failed");
          return false;
        }
        ECCLES_LOG_LINE("I2S TX channel enabled");
      }

      //ECCLES_INPUT: I2S standard RX from external mic
      if(config.mode & ECCLES_INPUT){
        //FIX: this used to request I2S_ROLE_MASTER, but TX above is also a master on these
        //exact same bclk/ws GPIOs -- two separate I2S peripherals both driving the same
        //clock/word-select lines is an electrical/logical conflict. ECCLES_INPUT is only ever
        //enabled together with ECCLES_EXTERNAL (see AUDIO_MODE::I2S_IN in Audio.cpp), so TX is
        //always the one active master whenever RX runs; RX only needs to listen as a slave.
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
        chanCfg.dma_desc_num  = dmaCount;
        chanCfg.dma_frame_num = dmaLen;

        res = i2s_new_channel(&chanCfg, nullptr, &audioHandles.rx);
        if(res != ESP_OK){ ECCLES_LOG_LINE("i2s_new_channel (RX) failed"); return false; }

        i2s_std_config_t stdCfg = {};
        stdCfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(config.rate);
        stdCfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bw, slotMode);
        stdCfg.gpio_cfg.bclk = (gpio_num_t)I2S_CLOCK_PIN;
        stdCfg.gpio_cfg.ws   = (gpio_num_t)I2S_WORD_PIN;
        stdCfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
        stdCfg.gpio_cfg.din  = (gpio_num_t)MIC_PIN;
        stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        stdCfg.gpio_cfg.invert_flags.mclk_inv = false;
        stdCfg.gpio_cfg.invert_flags.bclk_inv  = false;
        stdCfg.gpio_cfg.invert_flags.ws_inv    = false;

        res = i2s_channel_init_std_mode(audioHandles.rx, &stdCfg);
        if(res != ESP_OK){
          i2s_del_channel(audioHandles.rx);
          audioHandles.rx = nullptr;
          ECCLES_LOG_LINE("i2s_channel_init_std_mode (RX) failed");
          return false;
        }

        res = i2s_channel_enable(audioHandles.rx);
        if(res != ESP_OK){
          i2s_del_channel(audioHandles.rx);
          audioHandles.rx = nullptr;
          ECCLES_LOG_LINE("i2s_channel_enable (RX) failed");
          return false;
        }
        ECCLES_LOG_LINE("I2S RX channel enabled");
      }

      //ECCLES_INPUT_INTERNAL: ADC is handled entirely by esp_adc/adc_oneshot (already new API)
      //no i2s involvement needed — eccles_analogRead8() in EcclesTypes.cpp handles it directly
      if(config.mode & ECCLES_INPUT_INTERNAL){
        ECCLES_LOG_LINE("ADC input mode: handled by adc_oneshot, no I2S needed");
      }

      return true;
    }

    void initializeAll(){
      if(isInitialized) return;
      initializeIOPins();
      isInitialized = true;
    }

  };
};
