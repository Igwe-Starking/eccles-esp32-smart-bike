/*
    this class processes and re_configure audio wav files based on the ecclesConfig.txt
    supplied, this file is part of the eccles-tts-packaging library written by Nwobodo Ecclesiastes
*/

#include "EcclesTypes.h"
#include "WavFile.h"
#include <iostream>
#include <vector>

#ifndef ECCLES_AUDIO_CONFIG
#define ECCLES_AUDIO_CONFIG

ECCLES_API {


    //eccles configuration file struct, eccles configuration is used to modify the audio 
    //and change it to desired settings, with configuration we can turn 8 bit to 16 or vice versa

     //holds the channel definition
    enum class Channel {
        MONO,STEREO
    };

    //holds the types of model
    enum class ModelType {
        STATIC,DYNAMIC
    };

    //holds the content of eccles configuration file
    typedef struct Config {
        e_uint8 raw = 0; //this is used to specify if the file should be left untouched instead of applying default config if raw = 1,audio content is preserved as is else defualt config is auto applied
        e_uint16 rate = 8000  ; //bit rate
        e_uint8 depth= 16; //bit depth
        Channel channel = Channel::MONO; //channel
        e_boolean TSR = true; //trailing silent removal
        ModelType type = ModelType::STATIC;
    } eccles_config_t;

    //the class that applies and processes the configuration
    class AudioConfig {
        eccles_config_t config= {}; //the default config object

        public:
        AudioConfig();
        ~AudioConfig();

        //open and read the config file
        e_boolean open(e_string path);

        //change the specified buffer to match the underlying configurations
        //NOTE: this applies all the settings in eccles_config_t into the in buffer and
        //write it back into the out buffer,the caller must make sure that the in buffer
        //and the out buffer are of the same size, the buffer's lifecycle belongs to the caller
        std::vector<e_uint8> configure(e_uint8* in,e_uint32 len,wav_header_t& header);

        //change the wav file sample rate to the specified one,changing rate changes the 
        //input buffer size
        std::vector<e_uint8> configureRate(std::vector<e_uint8> in,wav_header_t& header);

        //change the depth of the wav file to the specified one
        std::vector<e_uint8> configureDepth(std::vector<e_uint8> in,wav_header_t& header);

        //change the channel of the wav file to the specified one
        std::vector<e_uint8> configureChannel(std::vector<e_uint8> in,wav_header_t& header);

        //remove trailing silence from the wav file,removing silence will change the actual 
        //buffer size so we create the buffer ourself,the caller is responsible to free this buffer
        std::vector<e_uint8> configureTSR(std::vector<e_uint8> in,wav_header_t& header);

    };

};

#endif
