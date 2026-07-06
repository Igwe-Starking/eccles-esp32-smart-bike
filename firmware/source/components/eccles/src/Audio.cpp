//base implementation of audio system using new IDF 5.x i2s_std and dac_continuous drivers

#include "Audio.h"
#include "RuntimeMemory.h"
#include "Pins.h"
#include "esp_task_wdt.h"
#include <driver/i2s_std.h>
#include <driver/dac_continuous.h>

ECCLES_API {

    namespace I2S {

        e_boolean port0Installed = false, port1Installed = false;
        i2sConfig iConfig = {};
        audioConfig config;

        void install(audioConfig cf, AUDIO_MODE md){
            config = cf;
            iConfig.rate   = config.sampleRate;
            iConfig.depth  = config.sampleSize;
            iConfig.stereo = config.channel == AUDIO_CHANNEL::STEREO;

            e_boolean* currentPort = &port0Installed;

            I2S_PLAY_MODE m = ECCLES_INTERNAL;
            switch(md){
                case AUDIO_MODE::I2S:     m = ECCLES_EXTERNAL;                                              currentPort = &port1Installed; break;
                case AUDIO_MODE::I2S_DAC: m = ECCLES_INTERNAL;                                              break;
                case AUDIO_MODE::I2S_ADC: m = (I2S_PLAY_MODE)(ECCLES_INPUT_INTERNAL | ECCLES_INTERNAL);     break;
                case AUDIO_MODE::I2S_IN:  m = (I2S_PLAY_MODE)(ECCLES_INPUT | ECCLES_EXTERNAL);              currentPort = &port1Installed; break;
                default: break;
            }
            iConfig.mode = m;

            *currentPort = Pins::initializeAudioPins(iConfig);
            if(!(*currentPort)) ECCLES_LOG_LINE("i2s driver operation failed");
            else                ECCLES_LOG_LINE("i2s driver operation successful");

            if(iConfig.exit){
                *currentPort = !(*currentPort);
                iConfig.exit = false;
            }
        }

        void uninstall(AUDIO_MODE m){
            if(!(port0Installed || port1Installed)) return;
            iConfig.exit = true;
            install(config, m);
        }

        void playBuffer(e_uint8* buffer, e_uint32 size, AUDIO_MODE md){
            if(md == AUDIO_MODE::NONE) return;
            if(!port0Installed && !port1Installed) return;

            size_t written = 0;
            ECCLES_LOG_LINE("writing to audio driver");

            if(md == AUDIO_MODE::I2S_DAC){
                //internal DAC output via dac_continuous
                if(audioHandles.dac){
                    dac_continuous_write(audioHandles.dac, buffer, size, &written, pdMS_TO_TICKS(100));
                }
            } else {
                //external I2S output via i2s_channel_write. FIX: this used to be a ternary
                //that evaluated to audioHandles.tx on both branches (dead conditional, likely
                //a leftover from an incomplete edit). Playback always goes out over tx --
                //audioHandles.rx is only ever used for recording, never for writing.
                i2s_chan_handle_t ch = audioHandles.tx;
                if(ch){
                    i2s_channel_write(ch, buffer, size, &written, pdMS_TO_TICKS(100));
                }
            }

            ECCLES_LOG("audio written: ");
            ECCLES_LOG_LINE((e_uint32)written);
            if(written != size) ECCLES_LOG_LINE("i2s written bytes not equal to requested");
        }

        void play(FileSystem* f, audioClip c, AUDIO_MODE md){
            if(f == nullptr){
                ECCLES_LOG_LINE("attempting to play an audio clip on null file exiting...");
                return;
            }
            e_uint8 buffer[FILE_MAX_CHUNK_BUFFER];
            e_uint32 rdi = 0;

            FILE* fl = f->getFile();
            fseek(fl, (long)c.offset, SEEK_SET);

            while(c.size >= (rdi + FILE_MAX_CHUNK_BUFFER)){
                e_uint16 rd = (e_uint16)fread(buffer, 1, FILE_MAX_CHUNK_BUFFER, fl);
                playBuffer(buffer, rd, md);
                rdi += rd;
            }
            if(c.size > rdi){
                e_uint16 rm = c.size - rdi;
                rm = (e_uint16)fread(buffer, 1, rm, fl);
                playBuffer(buffer, rm, md);
            }
        }

        e_boolean recordAndCall(AudioInputHandler* h, e_uint16 buffSize, AUDIO_MODE m){ //buffSize unused,fixed AUDIO_RECORD_MAX_BUFFER is read instead
            if(m == AUDIO_MODE::I2S && !port1Installed){
                ECCLES_LOG_LINE("record attempted but RX port not installed");
                return true;
            }
            e_uint8 data[AUDIO_RECORD_MAX_BUFFER];
            size_t rb = 0;

            ECCLES_LOG_LINE("reading from i2s RX buffer");

            if(audioHandles.rx){
                i2s_channel_read(audioHandles.rx, data, AUDIO_RECORD_MAX_BUFFER, &rb, pdMS_TO_TICKS(100));
            }

            ECCLES_LOG_LINE("calling the event handler");
            return h->onData(data, (e_uint16)rb);
        }

    };

    void playThread(void* arg){
        QueueHandle_t handle = (QueueHandle_t)arg;
        if(handle == nullptr){
            ECCLES_LOG_LINE("audio task created with null message handle");
            eccles_taskDelete();
            return;
        }
        audioTask t;
        AUDIO_MODE prevMode = AUDIO_MODE::NONE;

        while(1){
            eccles_readMsg(handle, &t);

            if(t.mode != prevMode){
                if(t.mode != AUDIO_MODE::NONE){
                    if(prevMode != AUDIO_MODE::NONE) I2S::uninstall(prevMode);
                    I2S::install(t.config, t.mode);
                }
            }
            prevMode = t.mode;

            if(t.type == AUDIO_TYPE::CLIP){
                I2S::play(t.file, t.clip, t.mode);
            } else if(t.type == AUDIO_TYPE::BUFFER){
                I2S::playBuffer(t.buffer, t.size, t.mode);
                e_free(t.buffer);
            } else if(t.type == AUDIO_TYPE::SHUTDOWN){
                break;
            }
            eccles_wait(AudioOutput::delay + t.clip.delay);
        }
        eccles_taskDelete();
        eccles_deleteMsgQueue(handle);
    }

    void recordThread(void* arg){
        eccles_wdtInclude(NULL);
        AudioInputTask* t = (AudioInputTask*)arg;
        if(!t){
            ECCLES_LOG_LINE("attempting to record audio on a null audio input task exiting...");
            eccles_taskDelete();
            return;
        }
        AUDIO_MODE m = t->inputMode == AUDIO_INPUT::I2S ? AUDIO_MODE::I2S : AUDIO_MODE::I2S_ADC;

        while(AudioInput::run){
            if(!I2S::recordAndCall(t->handler, t->buffSize, m)) break;
            eccles_wait(1);
            eccles_wdtReset();
        }
        eccles_deleteWDT(NULL);
        eccles_taskDelete();
    }

    e_uint16 AudioOutput::delay = AUDIO_OUTPUT_DEFAULT_DELAY;

    AudioOutput::AudioOutput(){}

    void AudioOutput::start(audioConfig config){
        this->config = config;
        msgQueue = eccles_createMsgQueue(10, sizeof(audioTask));
        eccles_taskInit(playThread, "audioThread", 6000, msgQueue, 5);
    }

    AudioOutput::~AudioOutput(){
        audioTask t;
        t.type = AUDIO_TYPE::SHUTDOWN;
        play(t);
    }

    e_boolean AudioOutput::load(e_string path){
        return (audioFile.load(path, "r") == FileStatus::SUCCESSFUL);
    }

    e_boolean AudioOutput::isPlaying(){
        return (eccles_hasMsg(msgQueue));
    }

    void AudioOutput::play(audioClip& clip, AUDIO_MODE mode){
        if(!audioFile){
            ECCLES_LOG_LINE("attempting to play an audio clip without loading any audio file");
            return;
        }
        audioTask t;
        t.clip = clip;
        t.type = AUDIO_TYPE::CLIP;
        t.mode = mode;
        play(t);
        this->mode = mode;
    }

    audioConfig AudioOutput::getConfig(){
        return config;
    }

    e_boolean AudioOutput::reConfigure(audioConfig cfg, AUDIO_MODE m){
        if(msgQueue == nullptr) return false;

        ECCLES_LOG("reconfiguring audio pipeline with bitrate:");
        ECCLES_LOG(cfg.sampleRate);
        ECCLES_LOG(" bitDepth:");
        ECCLES_LOG(cfg.sampleSize);
        ECCLES_LOG(" channels:");
        ECCLES_LOG_LINE(cfg.channel == AUDIO_CHANNEL::MONO ? "mono" : "stereo");

        I2S::uninstall(mode);
        I2S::install(cfg, m);
        mode = m;
        return true;
    }

    void AudioOutput::play(audioTask& task){
        //FIX: SHUTDOWN must always get through even when audio is muted, otherwise the
        //background playThread this message is meant to stop never receives it and is
        //leaked (along with its message queue) for the lifetime of the device whenever an
        //AudioOutput is destructed while AUDIBLE is off.
        if(globalState.AUDIBLE == OFF && task.type != AUDIO_TYPE::SHUTDOWN) return;
        task.config = config;
        if(task.type == AUDIO_TYPE::CLIP) task.file = &audioFile;
        eccles_sendMsg(msgQueue, &task);
    }

    void AudioOutput::playBuffer(e_uint8* buffer, e_uint32 size, AUDIO_MODE mode){
        audioTask t;
        t.type   = AUDIO_TYPE::BUFFER;
        t.buffer = buffer;
        t.size   = size;
        t.mode   = mode;
        play(t);
        this->mode = mode;
    }

    volatile e_boolean AudioInput::run = false;

    AudioInput::AudioInput(){}
    AudioInput::~AudioInput(){ stop(); }

    void AudioInput::stop(){ AudioInput::run = false; }

    e_boolean AudioInput::start(AudioInputHandler* h, audioConfig config, e_uint16 bSize, AUDIO_INPUT m){
        if(!h){
            ECCLES_LOG_LINE("attempt to start recording on a null handler exiting...");
            return false;
        }
        static AudioInputTask t = {
            .handler   = h,
            .config    = config,
            .inputMode = m,
            .buffSize  = bSize
        };
        run = true;
        eccles_taskInit(recordThread, "recordThread", 6000, &t, 6);
        return true;
    }
};
