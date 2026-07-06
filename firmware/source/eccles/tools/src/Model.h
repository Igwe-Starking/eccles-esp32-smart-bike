/*
    Base class for eccles-tts-model objects,defines model operation
*/

#ifndef ECCLES_TTS_MODEL
#define ECCLES_TTS_MODEL

#include "EcclesTypes.h"

ECCLES_API {

    //struct representation of a packaged model file
    //offset/size are e_uint32: ESP32 flash is at most a few tens of MB,well within
    //a 32 bit range,e_uint64 here would just waste RAM/flash for no benefit
    typedef struct ModelDescription {
        e_string name = nullptr;
        e_uint32 offset = 0;
        e_uint32 size = 0;
    } model_t;

    class Model {
        public:
        Model() = default;
        virtual ~Model() = default;

        //pack and structure the model must be implemented return true on success
        virtual e_boolean pack() = 0;

        //extract the model in extraction mode
        virtual e_boolean extract() = 0;

    };
};

#endif // !ECCLES_TTS_MODEL