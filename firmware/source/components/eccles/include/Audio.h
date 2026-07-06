/*
    this file reads and plays audio sections from file,NOTE: audio is played in
    sections and the file containing the audio must first be loaded through audio
    .load function, there are two playing modes,MODE::DAC and MODE::I2S each mode 
    selects the underlying audio driver to play the binary audio, the audio file to
    be played must be pure uncompressed PCM audio buffer as the file does'nt handle 
    decompressing the file, there are max 10 audio buffer clips to play per time and 
    they must be set before calling audio.play, NOTE: audioinput depends on Pins::MIC_PIN
    while audiooutput depends on i2s data pins

    this code and all of its syntax is written and optimised by nwobodo ecclesiastes
    A.K.A Igwe Starking
*/
#ifndef ECCLES_ESP_AUDIO
#define ECCLES_ESP_AUDIO

//dependencies
#include "FileSystem.h"

ECCLES_API {

    #define AUDIO_RECORD_MAX_BUFFER 4096
    #define AUDIO_OUTPUT_DEFAULT_DELAY 500

    //audio mode used for playing audio in different modes
    enum class AUDIO_MODE {I2S,I2S_DAC,I2S_ADC,I2S_IN,NONE};

    //audio input mode for mics
    enum class AUDIO_INPUT {I2S,I2S_ADC};

    //audio channel,for DAC we only support mono
    enum class AUDIO_CHANNEL {MONO,STEREO};

    //this defines the types of task sent to audio player
    enum class AUDIO_TYPE {CLIP,BUFFER,STREAM,SHUTDOWN /* use to safely shutdown the audio thread*/};

    //audio configuration file used to setup the audio system
    struct audioConfig {
        e_uint8 sampleSize = 16; //audio sample size 8bit,16bit etc
        AUDIO_CHANNEL channel = AUDIO_CHANNEL::MONO; //default for DAC
        e_uint16 sampleRate = 8000; //audio sample rate 44,100 etc
    };

    

    //this is just a single audio buffer to be played per time,NOTE the same
    //config is used for all clips so all the provided clip must match the configuration
    //supplied to the audio system otherwise distortion may occure
    struct audioClip {
        e_uint32 offset = 0; //offset to the loaded file where the binary audio clip is
        e_uint32 size = 0; //size of the audio PCM binary
        e_uint16 delay = 0; //delay for every clip
    };

    //this holds the audio clip and it's metadata to be sent to audio system
    struct audioTask {
        audioClip clip; //the main audio data
        AUDIO_TYPE type; //audio data type
        audioConfig config; //audio play config
        FileSystem* file; //the file that holds the clip
        AUDIO_MODE mode = AUDIO_MODE::NONE; //audio play mode
        e_uint8* buffer; //stores audio buffer in buffer mode
        e_uint32 size; //stores buffer size
    };

    //this class converts audio binary to a modulated audio signals and play them to speaker(s)
    //NOTE: amplifier is needed even for DAC audio, DAC audio is the poorest here but we support it
    //here incase we don't have I2S,NOTE: I2S is needed to play a2dp

    class AudioOutput {

        FileSystem audioFile;
        audioConfig config;
        AUDIO_MODE mode;
        QueueHandle_t msgQueue; //holds the audio task queue


        public:
        //this holds the amount of delay in every clip
        static e_uint16 delay;
        
        AudioOutput();
        ~AudioOutput();

        //loads the file containing the audio clips,return true if the audio file is truely loaded
        e_boolean load(e_string path);

        //starts the audio play engine must be called before calling play
        void start(audioConfig config);

        //return if the audio is currently playing,you can't use the audio buffer in this mode
        e_boolean isPlaying();

        //plays the clips written to audio buffer
        void play(audioClip& clip,AUDIO_MODE mode);

        //plays an audio from audioTask
        void play(audioTask& task);

        //returns the underlying audio config
        audioConfig getConfig();

        //reconfigure the audioSystem with the given config
        e_boolean reConfigure(audioConfig cfg,AUDIO_MODE m);

        //plays from buffer already loaded to RAM,this does't care about the audio buffer
        void playBuffer(e_uint8* buffer,e_uint32 size,AUDIO_MODE mode);

    };

    //callback for audio recording,once audio record engine read the amount of bytes specified in start
    //it calls this function to process it if this fuction returns false audio record is automatically exited
    //NOTE: audio input runs on a task different from the one that calls start

    struct AudioInputHandler {
        virtual e_boolean onData(e_uint8* buffer,e_uint16 len) const = 0; //must be overriden
    };

    //task to be sent to audio record engine
    struct AudioInputTask {
        AudioInputHandler* handler;
        audioConfig config;
        AUDIO_INPUT inputMode;
        e_uint16 buffSize; //buffer size we won't record above the size of 2 bytes
    };

    //this class records audio from the system and sends it to a client in conversation mode
    class AudioInput {

        //audio configuration file
        audioConfig config;
        AudioInputHandler* handler = nullptr;

        public:
        
        static volatile e_boolean run;
        

        AudioInput();
        ~AudioInput();

        //this starts the underlying audio record engine and start reading the audio binaries
        //once we read up to the buffSize we call the specified audio handler to process it
        //once this is called the mode can't be changed and moreover we can't use i2s internal DAC
        //and i2s internal ADC at the same time since both is tied to one i2s port,doing that will cause
        //silence undefined behaviour,if you are already using i2s internal DAC for audio,you must use
        //normal ADC or switch audio play mode

        e_boolean start(AudioInputHandler* h,audioConfig config,e_uint16 buffSize,AUDIO_INPUT mode);

        //this stops the audio input thread
        void stop();

    };

};
#endif
