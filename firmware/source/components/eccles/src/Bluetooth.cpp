//Base implementation of Bluetooth executor
//we are an A2DP sink, but act as AVRCP controller (CT) to send play/pause/volume to the phone.
//decoded PCM is copied into an e_malloc buffer and pushed through EcclesTTS's shared AudioOutput.

//PORT NOTE: this entire file already called straight into esp-idf's raw Bluedroid classic
//bluetooth stack (esp_bt.h/esp_bt_main.h/esp_a2dp_api.h/esp_avrc_api.h) even in the arduino
//build, since arduino-esp32 has no higher level a2dp-sink+avrcp-controller wrapper of its own.
//nothing in this file changes for the esp-idf port: every esp_bt_*/esp_a2d_*/esp_avrc_* call,
//every comment about the specific older IDF (~3.3 bundled with espressif32@3.5.0) this project
//targets, and every stubbed-out newer API is preserved exactly as-is

#include "Executors.h"
#include "RuntimeMemory.h"
#include "EcclesTTS.h"
#include <cstring>
#include <driver/i2s_std.h>
#include <driver/dac_continuous.h>

/*
    we don't need this but because we encountered issues with the bluetooth
    api which took us days to resolve and after heavy debugging we found out that
    arduino internal api releases the bluetooth memory if nothing includes its api 
    in the codebase thats why we pull in this even though we don't actually use it
*/

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

ECCLES_API {

    Bluetooth* Bluetooth::instance = nullptr;

    static constexpr e_uint8 AVRC_TL_PASSTHROUGH = 0;
    static constexpr e_uint8 AVRC_TL_ABS_VOLUME  = 1; //transaction label for absolute volume commands

    //a2dp profile callback: connection state, audio state, negotiated codec config
    static void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param){
        Bluetooth* bt = Bluetooth::instance;
        if(bt == nullptr) return;

        switch(event){
            case ESP_A2D_CONNECTION_STATE_EVT: {
                e_boolean nowConnected = (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED);
                bt->connected = nowConnected;
                if(!nowConnected){
                    bt->playing = false;
                    bt->avrcConnected = false;
                    //NOTE: we deliberately do not reset/hand back the shared audio output here.
                    //bluetooth stays armed for the next connection; only stop() (via execute())
                    //gives the output back to tts
                } else if(bt->audible){
                    EcclesTTS::speak("bluetooth connected successfully"); //we can safely do this since tts still owns audio pipeline
                }
                ECCLES_LOG_LINE(nowConnected ? "Bluetooth: a2dp connected" : "Bluetooth: a2dp disconnected");
                break;
            }
            case ESP_A2D_AUDIO_STATE_EVT: {
                e_boolean nowPlaying = (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
                bt->playing = nowPlaying;

                //only take over the shared output once music actually starts, not just on connect
                if(nowPlaying && !bt->audioStarted){
                    EcclesTTS::pause();
                    AudioOutput::delay = 0; //a2dp buffers stream continuously, the inter-clip delay would break playback
                    AudioOutput* out = EcclesTTS::getOutput();
                    if(out == nullptr){
                        ECCLES_LOG_LINE("Bluetooth: getOutput() returned null, cannot reconfigure for a2dp");
                        break;
                    }
                    out->reConfigure(bt->pendingCfg, EcclesTTS::getMode());
                    bt->audioStarted = true;
                    ECCLES_LOG_LINE("Bluetooth: shared audio output reconfigured for a2dp");
                }
                break;
            }
            case ESP_A2D_AUDIO_CFG_EVT: {
                //just remember the negotiated codec params here, applied later once playback starts
                bt->pendingCfg.sampleSize = 16; //SBC always decodes to 16-bit PCM

                bt->pendingCfg.channel = (param->audio_cfg.mcc.cie.sbc[0] & 0x0F) == 0x08
                                ? AUDIO_CHANNEL::MONO : AUDIO_CHANNEL::STEREO;

                e_uint8 freqBits = (param->audio_cfg.mcc.cie.sbc[0] >> 4) & 0x0F;
                switch(freqBits){
                    case 0x08: bt->pendingCfg.sampleRate = 16000; break;
                    case 0x04: bt->pendingCfg.sampleRate = 32000; break;
                    case 0x02: bt->pendingCfg.sampleRate = 44100; break;
                    case 0x01: bt->pendingCfg.sampleRate = 48000; break;
                    default:   bt->pendingCfg.sampleRate = 44100; break;
                }
                break;
            }
            default:
                break;
        }
    }

    //a2dp sink data callback: write decoded PCM directly to I2S or DAC
    //queuing through AudioOutput added too much latency, direct write is faster
    static void a2dpDataCallback(const e_uint8* buf, e_uint32 len){
        Bluetooth* bt = Bluetooth::instance;
        if((bt == nullptr) || (!bt->audioStarted)) return;
        size_t written = 0;
        AUDIO_MODE m = EcclesTTS::getMode();
        if(m == AUDIO_MODE::I2S_DAC){
            if(audioHandles.dac){
                dac_continuous_write(audioHandles.dac, (e_uint8*)buf, len, &written, pdMS_TO_TICKS(100));
            }
        } else {
            i2s_chan_handle_t ch = audioHandles.tx;
            if(ch){
                i2s_channel_write(ch, buf, len, &written, pdMS_TO_TICKS(100));
            }
        }
    }

    //avrcp controller callback: connection state + responses from the phone
    static void avrcCallback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t* param){
        Bluetooth* bt = Bluetooth::instance;
        if(bt == nullptr) return;

        switch(event){
            case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
                bt->avrcConnected = param->conn_stat.connected;
                ECCLES_LOG_LINE(bt->avrcConnected ? "Bluetooth: avrcp connected" : "Bluetooth: avrcp disconnected");
                break;
            }
            case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
                //phone confirmed the volume we sent, update our local tracking to the confirmed value
                bt->volume = param->set_volume_rsp.volume;
                break;
            }
            default:
                break;
        }
    }

    //execute bluetooth based instructions
    e_boolean Bluetooth::execute(Command& com){
        if(com.target != DeviceID::BLUETOOTH) return false;
        ECCLES_LOG_LINE("bluetooth instruction given");
        switch(com.action){
            case CommandAction::ON:          start();     return true;
            case CommandAction::OFF:         stop();      return true;
            case CommandAction::PLAY:        play();      return true;
            case CommandAction::PAUSE:       pause();     return true;
            case CommandAction::NEXT:        next();      return true;
            case CommandAction::PREV:        prev();      return true;
            case CommandAction::VOLUME_UP:   volumeUp();  return true;
            case CommandAction::VOLUME_DOWN: volumeDown();return true;
            case CommandAction::SILENCE: silence(false);  return true;
            case CommandAction::VOICE:   silence(true);   return true;
            case CommandAction::SET_VOLUME: {
                //volume value is carried as the first byte of the command's data payload
                if(com.data == nullptr || com.size < 1) return false;
                setVolume(com.data[0]);
                return true;
            }
            default: break;
        }

        CommandResult r;
        r.id = com.id;
        r.sender = com.sender;
        if(com.action == CommandAction::QUERY_ON){
            r.size = on ? 1 : 0;
        } else if(com.action == CommandAction::QUERY_OFF){
            r.size = on ? 0 : 1;
        } else if(com.action == CommandAction::GET_STATE){
            r.size = isConnected() ? 1 : 0;
        } else if(com.action == CommandAction::QUERY_STATE){
            r.size = isPlaying() ? 1 : 0;
        } else if(com.action == CommandAction::GET_DATA){
            r.size = getVolume();
        } else {
            return false;
        }
        sendResult(r);
        return true;
    }

    //check bluetooth related conditions
    e_boolean Bluetooth::checkCondition(Condition& con){
        if(con.target != DeviceID::BLUETOOTH) return false;
        switch(con.action){
            case CommandAction::QUERY_ON : return on;
            case CommandAction::QUERY_OFF : return !(on);
            case CommandAction::QUERY_STATE : return isPlaying();
            case CommandAction::GET_STATE : return isConnected();
            default: return false;
        }
    }

    //start bluetooth a2dp sink + avrcp controller
    void Bluetooth::start(){
        if(on) return;
        ECCLES_LOG_LINE("bluetooth start called");

        //conversation holds high priority than bluetooth here and we can't run bluetooth if conversation is already running
        Conversation* con = Conversation::instance;
        if(con != nullptr && con->active){
            ECCLES_LOG_LINE("Bluetooth: can't start bluetooth while conversation is running");
            return;
        }

        //BLE memory release is permanent, only ever do it the first time this object starts
        if(instance == nullptr){
            esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
            if(err != ESP_OK){
                ECCLES_LOG("failed to release the ble mem: ");
                ECCLES_LOG_LINE(esp_err_to_name(err));
                return;
            }
        }
        instance = this;

        //track exactly which stages actually came up so a failure can unwind only those, in reverse
        e_boolean controllerInited = false, controllerEnabled = false;
        e_boolean bluedroidInited = false, bluedroidEnabled = false;
        e_boolean avrcInited = false, a2dpInited = false;

        e_boolean status = true;

        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if(status){
            if(esp_bt_controller_init(&config) == ESP_OK) controllerInited = true; else {
                ECCLES_LOG_LINE("failed to initialize controller");
                status = false;
            }
        }
        if(status){
            //wait for bluetooth to really be in init state before enabling.
            //FIX: this used to be "while(status == IDLE){}", which is backwards -- we want to
            //wait *until* the controller reaches IDLE after the (asynchronous) init call, not
            //spin forever once it's already there. It also had no yield, so any timing where
            //the wrong condition held even briefly could hard-lock the core and trip the
            //watchdog. Added a yield and a bounded timeout so a genuine controller failure
            //can't hang the device forever.
            e_uint16 waited = 0;
            while(esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE){
                vTaskDelay(pdMS_TO_TICKS(10));
                if(++waited > 500){ //5 seconds
                    ECCLES_LOG_LINE("timed out waiting for bt controller to reach idle");
                    status = false;
                    break;
                }
            }
        }
        if(status){
            esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);

            //ESP_BT_MODE_BLE_MODE reports invalid arg here even after releasing ble memory, BTDM is what actually works
            if(err == ESP_OK) controllerEnabled = true; else {
                ECCLES_LOG("failed to enable controller: ");
                ECCLES_LOG_LINE(esp_err_to_name(err));
                status = false;
            }
        }
        if(status){
            if(esp_bluedroid_init() == ESP_OK) bluedroidInited = true; else {
                ECCLES_LOG_LINE("failed to enable bluedroid");
                status = false;
            }
        }
        if(status){
            if(esp_bluedroid_enable() == ESP_OK) bluedroidEnabled = true; else {
                ECCLES_LOG_LINE("failed to enable bluedroid");
                status = false;
            }
        }

        //AVRC must init before A2DP; we are CT so we send commands to the phone (TG)
        if(status){
            if(esp_avrc_ct_init() == ESP_OK) avrcInited = true; else {
                ECCLES_LOG_LINE("failed to enable avrc");
                status = false;
            }
        }
        if(status && esp_avrc_ct_register_callback(avrcCallback) != ESP_OK) status = false;

        if(status && esp_a2d_register_callback(a2dpCallback) != ESP_OK) status = false;
        if(status && esp_a2d_sink_register_data_callback(a2dpDataCallback) != ESP_OK) status = false;
        if(status){
            if(esp_a2d_sink_init() == ESP_OK) a2dpInited = true;
            else status = false;
        }

        if(!status){
            ECCLES_LOG_LINE("Bluetooth: start failed, rolling back");

            //unwind only the stages that actually came up, in reverse order
            if(a2dpInited)       esp_a2d_sink_deinit();
            if(avrcInited)       esp_avrc_ct_deinit();
            if(bluedroidEnabled) esp_bluedroid_disable();
            if(bluedroidInited)  esp_bluedroid_deinit();
            if(controllerEnabled) esp_bt_controller_disable();
            if(controllerInited)  esp_bt_controller_deinit();

            on = false;
            if(audible) EcclesTTS::speak("failed to turn on bluetooth");
            return;
        }

        e_stringa configured = Configuration::readString("BT_NAME");
        e_string btName = eccles_compareString(configured.c_str(), "") ? "Eccles Smart Bike" : configured.c_str();
        esp_bt_dev_set_device_name(btName);

        //espressif32@3.5.0 (IDF ~3.3) predates the two-argument esp_bt_gap_set_scan_mode()
        //overload (ESP_BT_CONNECTABLE / ESP_BT_GENERAL_DISCOVERABLE were added later, IDF 4.x).
        //the old single-enum form is fully equivalent for our purposes, so we use it instead.
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

        on = true;
        if(audible) EcclesTTS::speak("bluetooth turned on successfully");
    }

    //stop bluetooth a2dp sink + avrcp controller, mirrors start() in reverse order
    void Bluetooth::stop(){
        if(!on) return;

        //same old-IDF scan mode API as in start(); ESP_BT_SCAN_MODE_CONNECTABLE alone is both
        //non-discoverable and still connectable. ESP_BT_SCAN_MODE_NONE would be fully closed,
        //but we're about to tear the controller down entirely anyway, so this is just tidy.
        // Change from: esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE);
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

        esp_a2d_sink_deinit();
        esp_avrc_ct_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();

        if(audioStarted){
            EcclesTTS::reset(); //hand the shared output back to tts and resume it
            audioStarted = false;
        }

        if(audible) EcclesTTS::speak("bluetooth turned off successfully");

        connected = false;
        playing = false;
        avrcConnected = false;
        on = false;
    }

    //transport control - these are all AVRCP CONTROLLER commands sent to the phone
    void Bluetooth::play(){
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    void Bluetooth::pause(){
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    void Bluetooth::next(){
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    void Bluetooth::prev(){
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    //set the bluetooth audio volume, 0-127 AVRCP absolute volume scale
    void Bluetooth::setVolume(e_uint8 v){
        volume = (v > 127) ? 127 : v;
        if(!avrcConnected) return;
        esp_avrc_ct_send_set_absolute_volume_cmd(AVRC_TL_ABS_VOLUME, volume);
    }

    void Bluetooth::volumeUp(){
        e_uint16 next = (e_uint16)volume + VOLUME_STEP;
        volume = next > 127 ? 127 : (e_uint8)next;
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_VOL_UP, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_VOL_UP, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    void Bluetooth::volumeDown(){
        e_int next = (e_int)volume - VOLUME_STEP;
        volume = next < 0 ? 0 : (e_uint8)next;
        if(!avrcConnected) return;
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_VOL_DOWN, ESP_AVRC_PT_CMD_STATE_PRESSED);
        esp_avrc_ct_send_passthrough_cmd(AVRC_TL_PASSTHROUGH, ESP_AVRC_PT_CMD_VOL_DOWN, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }

    //returns the last volume confirmed by the phone via SET_ABSOLUTE_VOLUME_RSP_EVT
    e_uint8 Bluetooth::getVolume(){
        return volume;
    }

    e_boolean Bluetooth::isPlaying(){
        return playing;
    }

    e_boolean Bluetooth::isConnected(){
        return connected;
    }

    //FIX: parameter used to be named "silent" while actually meaning "audible" (silence(true)
    //turns audio ON) -- functionally correct at both call sites but confusing to read/maintain.
    void Bluetooth::silence(e_boolean audibleFlag){
        audible = audibleFlag;
    }

};
