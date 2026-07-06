/*
    this file holds the decralation of all system executors we 
    do this to reduce include file size
*/

#ifndef ECCLES_ESP_EXECUTORS
#define ECCLES_ESP_EXECUTORS

//dependencies
#include "Executor.h"
#include "DeviceManager.h" //used by device executor
#include "Audio.h" //used by bluetooth to play decoded a2dp PCM through AudioOutput
//PORT NOTE: <Preferences.h> was arduino-esp32's thin wrapper class over the esp-idf nvs_flash
//component, esp-idf has no such class so Configuration now talks to nvs_flash/nvs directly
#include "nvs_flash.h"
#include "nvs.h"

ECCLES_API {

    //we planned to use namespaces for some functions but to be an executor
    //you must extend Executor which means only a class can do that

    //DeviceExecutor: this executor handles device related commands
    class DeviceExecutor : public Executor {
        public:

        LiveData ld; //and extension of DeviceManager that handles device instructions
        e_boolean execute(Command& com) override;
        e_boolean checkCondition(Condition& con) override;
        void runMonitor();
        DeviceExecutor();
        //DeviceManager and LiveData does the rest
    };

    //Configuration: this class manages settings in key-value pairs
    //we would have used a namespace for this because it's a global object
    //but since it's an executor use class with static members for global access
    class Configuration : public Executor {
        public:
        e_boolean execute(Command& com) override;
        e_boolean checkCondition(Condition& con) override;

        //PORT NOTE: replaces the original "static Preferences PR;" handle, holds the open
        //nvs handle for our namespace instead of an arduino Preferences object
        static nvs_handle_t PR;
        static e_boolean opened; //keeps record of validity

        //open the configuration for operation,must be called before any configuration operation
        static void open();
        
        //get a String value from the configuration
        static e_stringa readString(e_string k);

        //get an int value from the configuration
        static e_uint32 readInt(e_string k);

        //get a float value from the configuration
        static e_float readFloat(e_string k);

        //get a boolean value
        static e_boolean readBoolean(e_string k);

        //write a String value to the configuration
        static void writeString(e_string k,e_string v);

        //write an int value to the configuration
        static void writeInt(e_string k,e_uint32 v);

        //write a float value to the configuration
        static void writeFloat(e_string k,e_float v);

        //write a boolean value to the configuration
        static void writeBoolean(e_string k,e_boolean v);

        //close the configuration preference
        static void close();

    };

    //Bluetooth: this class manages bluetooth  a2dp NOTE: bluetooth is only use here for a2dp playback
    //we are an A2DP SINK (we receive audio from the phone), so transport control (play/pause/next/prev/volume)
    //is done by acting as an AVRCP CONTROLLER (CT) and sending those commands TO the phone, which is the AVRCP TARGET (TG).
    //decoded PCM audio coming from the phone is pushed into shared AudioOutput with the TTS system
    class Bluetooth : public Executor {
        public:
        e_boolean on = false; //true once the bt stack + a2dp sink + avrc ct are all initialized
        e_boolean audible = true; //audible by default
        e_boolean execute(Command& com) override;
        e_boolean checkCondition(Condition& con) override;
        audioConfig pendingCfg = {};

        //start the bluetooth a2dp engine
        void start();

        //stop the bluetooth a2dp engine
        void stop();

        //next the bluetooth audio
        void next();

        //prev the bluetooth audio
        void prev();

        //pause the bluetooth audio
        void pause();

        //play the bluetooth audio
        void play();

        //set the bluetooth audio volume, 0-127 absolute AVRCP scale
        void setVolume(e_uint8 volume);

        //raise the bluetooth audio volume by one step
        void volumeUp();

        //lower the bluetooth audio volume by one step
        void volumeDown();

        //get the bluetooth audio volume, 0-127 absolute AVRCP scale
        e_uint8 getVolume();

        //check if bluetooth a2dp is playing
        e_boolean isPlaying();

        //check if bluetooth a2dp is currently connected
        e_boolean isConnected();

        //silents the bluetooth domain
        void silence(e_boolean silent);

        //internal state, public because the IDF callbacks must be plain free functions
        //(not member functions) and reach our state through the single instance pointer ---

        static Bluetooth* instance; //the one Bluetooth executor instance, set in the constructor

        e_boolean audioStarted = false;  //true once audioOut.start() has been called with a negotiated config

        e_boolean connected = false;     //a2dp link to a phone is up
        e_boolean playing = false;       //audio stream is currently flowing
        e_boolean avrcConnected = false; //avrcp control channel to the phone is up
        e_uint8 volume = 64;             //last known absolute volume, 0-127 scale, ~50%

        static constexpr e_uint8 VOLUME_STEP = 8; //step size used by volumeUp/volumeDown

    };

    //Conversation: this class manages communication,conversation works like phone call with your bike
    //in real mode as long as your phone is connected to the bike you can talk from wherever you are and
    //your voice will be played in real time through the bike speakers and whoever is close to the bike can 
    //speak and your phone will play their voice in real time, you are only limited by the WiFi range 
    //in AI mode you can say simple things and the Communication AI will pick the context and reply to you
    //NOTE: AI mode is highly limited and only few replies are actually used

    enum class ConversationMode {
        AI,REAL
    };

    class Conversation : public Executor {
        public:
        e_boolean active = false;

        e_boolean execute(Command& com) override;
        e_boolean checkCondition(Condition& con);

        static Conversation* instance;

        AudioInput recorder;

        //start the conversation engine
        void start(ConversationMode m,Command& com);

        //stop the conversation engine
        void stop();

        //check if conversation is actually active
        e_boolean isActive();

        //reply, used in AI mode
        void reply(Command& com);
    };

    //ExecutorManager: this class though not an executor but manages and handle all present executors
    class ExecutorManager {

        //create all executors in the global sense

        /*
            we previously used a loop search pattern where all executors where loops 
            and the command given to all any that returns true owns the command and stop
            the loop but we find out that this design pattern is inefficient so we migrate 
            to index table,but the price we have to pay for this is that the order of decralation
            here must match its corresponding order of DeviceID
        */

        DeviceExecutor dv; //we put this first because most of the instruction are device instruction so device specific instruction are evaluated first
        Configuration cnf;
        Bluetooth bt;
        Conversation cnv;

        public:

        //we wanted to use this functions as static but we needed an object of this class
        //for the executors to be created so we force initialization

        void prepare(); //prepare any executor that needs preparation
        void setResultHandler(ResultHandler* h); //give this result handler to executors to use for results

        //run the executors command pool we wanted this to be extremely fast so we ask the compiler to inline it
        void run() __attribute__((hot));
        void cleanup(); //unused for now,kept for api symmetry with prepare()
    };

};

#endif // !ECCLES_ESP_EXECUTORS
