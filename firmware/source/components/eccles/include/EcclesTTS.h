//the base eccles-tts controller engine,this is the heart of the eccles tts system
//its the bridge between the eccles-tts-packager and the ESP firmware
//NOTE: this codebase depend on StaticModel.h emitted by eccles-tts-packager.exe
//make sure eccles-tts-packager.exe is configure in this build or you extact the
//corresponding StaticModel.h from the packager and place it in the the include directory 
//and make sure that StaticModel.eccles is uploaded to the ESP flash
//otherwise this codebase would be of no benefit

/*
    this codebase and the entire project is the framework of
    Nwobodo Ecclesiastes Chidera A.K.A Igwe Starking
*/

//the StaticModel.h is included in the cpp file because it contains a live function
//which will eventually cause multiple definition error

#ifndef ECCLES_ESP_TTS
#define ECCLES_ESP_TTS

#include "EcclesTypes.h"
#include "Audio.h"

ECCLES_API {

    //we are looking forward to designing a multi-lingual model system
    //where we have different staticModel.eccles for different languages
    //and a single StaticModel.h to address them all precisely,but this is 
    //is still a future plan so we leave its skeleton here

    enum class LANG {
        EN_UK, //english uk, this is the default currently supported
        EN_US, //english us
        EN_NG, //english my country nigeria, thats pidgeon english
        IGB_NG //igbo nigeria,my native language
    };

    //we needed to use class to abstract few members

    class EcclesTTS {
        //the core model audio player
        static AudioOutput mp;

        //flag to pause or play the tts engine
        static e_boolean run;
        static e_boolean valid;

        //mode for all play operations
        static AUDIO_MODE mode;

        //play numeric models
        static void playDigits(e_uint8 digits); //for now we only support play to hundred
        public:

        //initialize the tts-engine using the specified language
        //returns true if successful if this fails everyother call to
        //this api does nothing
        static e_boolean initEngine(LANG lang);

        //pause the tts-engine operation in case other form of audio is needed
        static void pause();

        //ask the tts-engine to continue after a pause
        static void play();

        //change the tts play mode
        static void setMode(AUDIO_MODE m);

        //get the current tts play mode
        static AUDIO_MODE getMode();

        //ask the tts-engine to speak the given words using the init language
        static void speak(e_string words);

        //hand over a voice buffer to the tts engine to speak if
        static void speakBuffer(e_uint8* buffer,e_uint32 size);

        //returns the underlying AudioOutput, here TTS manages the audio pipeline
        //and to avoid creating another audio system for any other system that needs audio
        //outside the tts environment we can pause the tts get its output and when done
        //hand over the output and continue the tts system
        //NOTE: must pause the TTS system before getting its output
        static AudioOutput* getOutput();

        //reset the tts system back to its default settings
        //must be called after another api reConfigures mp
        static void reset();
    };

};

#endif // !ECCLES_ESP_TTS
