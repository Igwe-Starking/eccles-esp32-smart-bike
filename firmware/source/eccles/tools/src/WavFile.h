//an interface to read and operate on a wav file

#include "EcclesTypes.h"
#include <fstream>

#ifndef ECCLES_WAV_FILE
#define ECCLES_WAV_FILE

ECCLES_API {

    //wav header container
    #pragma pack(push,1) //package this struct tightly

    typedef struct HeaderDefinition {
        e_char riff[4]; //Resource Interface File Format: RIFF is the first data in a wave file
        e_uint32 chunkSize; //total file size minus riff and size being 8
        e_char wave[4]; //WAVE: this RIFF container actually contain wav,riff can contain many things 
        e_char format[4]; //tells us how this wav file is encoded
        e_uint32 subChunkSize; //tells us the size of the format chunk if any
        e_uint16 audioFormat; //tells us wether this audio is compressed and the compression format
        e_uint16 channel; //channel mono / stereo
        e_uint32 sampleRate; //sample rate in hertz
        e_uint32 byteRate; //how many bytes per second is here
        e_uint16 blockAlign; //audio frame size
        e_uint16 bitDepth; //8bit 16 bit
        e_char data[4]; //offset to the real audio data
        e_uint32 dataSize; //size of real audio data

    } wav_header_t;

    #pragma pack(pop) //return to normal struct structuring

    //wav file mode operation

    enum class FileMode {
        UNKNOWN,READ,WRITE
    };

    //wav file impl

    class WavFile {

        std::fstream wavFile; //for reading
        e_uint32 size = 0;
        FileMode mode = FileMode::UNKNOWN;
        e_uint32 f_ptr = 0; //used to keep track of the file cursor position,this must not be corrupted

        public:

        WavFile();
        ~WavFile();

        //opens the wav file,must be called before any operation is made on the file
        //returns true on success false otherwise
        e_boolean open(e_string path,FileMode m);

        //returns the size of the loaded wav file
        e_uint32 getSize();

        //reads the header from the opened file and write it to the provided header
        void readHeader(wav_header_t& header);

        //write the given header to the opened file,the file must be opened on mode write
        void writeHeader(wav_header_t& header);

        //reads from the open file and fill the provided buffer returns how many bytes where
        //successfully read
        e_uint32 read(e_uint8* buffer,e_uint32 len);

        //write to the opened file from the provided buffer,must be opened on mode write
        //returns true if the write actually succeded
        e_boolean write(e_uint8* buffer,e_uint32 len);

        //compares two headers and returns true if the two headers matches
        static e_boolean compareHeader(wav_header_t& first,wav_header_t& second);

        //verify this header to ensure its actually a real wav file
        e_boolean verifyHeader(wav_header_t& header);

        //closes the underlying file if opened
        void close();
    };
};

#endif