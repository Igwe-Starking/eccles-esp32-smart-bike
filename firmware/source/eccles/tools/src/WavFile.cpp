//base implementation of wav file structure

#include "WavFile.h"
#include <cstring>

using namespace std; //FIX: moved here from WavFile.h -- this file needs it (ios::in, ios::binary,
                      //etc used unqualified below) but a header has no business imposing a
                      //using-directive on every translation unit that includes it.

ECCLES_API {

    WavFile::WavFile(){}

    WavFile::~WavFile(){
        //make sure to close the underlying file handle before we go
        close();
    }

    //opens the underlying wav file for processing, this must be done first
    e_boolean WavFile::open(e_string path,FileMode m){
        if(m == FileMode::UNKNOWN){
            cout << "file requested to open with an unknown mode"<<endl;
            return false;
        }
        
        //if this WavFile is already open from a previous call,close that handle first so
        //this open() always represents the path just given to it,not whatever was open before
        if(wavFile.is_open()) close();

        wavFile.open(path, (m == FileMode::READ ? ios::in : ios::out) | ios::binary); //we only process binary here
            
        //check if the file is actually opened
        if(!wavFile){
            cout<<"failed to open the given file:"<<path<<" in "<<(m == FileMode::READ ? "read mode" : " write mode")<<endl;
            return false;
        }

        //get the file size
        wavFile.seekg(0,ios::end);
        size = static_cast<e_uint32>(wavFile.tellg());

        //seek back to the begining of the file
        wavFile.seekg(0,ios::beg);

        //we are actually good to go update the mode
        mode = m;
        return true;
        
    }

    //get the size of the opened file
    e_uint32 WavFile::getSize(){
        return size;
    }

    //reads the underlying wav header into the provided header struct
    void WavFile::readHeader(wav_header_t& header){
        //check if we are actually on read mode
        if(mode != FileMode::READ){
            cout<<"readHeader requested on a file not opened on read mode"<<endl;
            return; //we can't do any other thing
        }
        wavFile.read((e_char*)&header,sizeof(header)); //this works on x86 but for arm or any big endian architecture this is unreliable but we will still fix this
        //wav is little endian and if the system is big endian it corrupts we are also very sure the struct is not padded
    }

    //writes the given header to a file thats opened in write mode
    void WavFile::writeHeader(wav_header_t& header){
        //check if the file is opened in write mode
        if(mode != FileMode::WRITE){
            cout<<"writeHeader requested on a file not opened on write mode "<<endl;
            return;
        }
        wavFile.write((e_char*) &header,sizeof(header)); //must be little endian packed struct
    }

    //read from the opened wav file and fill the specified buffer returns the size of bytes actually read
    e_uint32 WavFile::read(e_uint8* buffer,e_uint32 len){
        //check if the file is actually opened in read mode
        if(mode != FileMode::READ){
            cout<<"read requested on a file not opened on read mode"<<endl;
            return 0;
        }
        wavFile.read((e_char*)buffer,len);
        return (e_uint32)wavFile.gcount(); //how many bytes we read
    }

    //write to an opened wav file or file returns true if everything went normal
    e_boolean WavFile::write(e_uint8* buffer,e_uint32 len){
        //check if the file is opened in write mode
        if(mode != FileMode::WRITE){
            cout<<"write requested on a file not open in mode write"<<endl;
            return false;
        }
        wavFile.write((e_char*)buffer,len);
        return (e_boolean) wavFile;
    }

    //compares two wav headers and return true if they are same
    //NOTE: we only compare the fields that describe the audio FORMAT (audioFormat,channel,
    //sampleRate,bitDepth) since those are what must be shared across all clips in a bundle.
    //chunkSize and dataSize legitimately differ between files of different lengths,a raw
    //byte-for-byte compare of the whole struct would always report a mismatch
    e_boolean WavFile::compareHeader(wav_header_t& first,wav_header_t& second){
        return first.audioFormat == second.audioFormat
            && first.channel    == second.channel
            && first.sampleRate == second.sampleRate
            && first.bitDepth   == second.bitDepth;
    }

    //verify a wav header to ensure its actually real wav header
    e_boolean WavFile::verifyHeader(wav_header_t& header){
        //read riff,the real wav header riff does'nt include \0 so we add it to avoid breaking the rules
        e_char M_RIFF[5]; //size of riff plus null
        memcpy(M_RIFF,header.riff,4);
        M_RIFF[4] = '\0';

        //read wave and add null
        e_char M_WAVE[5]; //size of wave plus null
        memcpy(M_WAVE,header.wave,4);
        M_WAVE[4] = '\0';

        //compare and return
        if((strcmp(M_RIFF,"RIFF") != 0) || (strcmp(M_WAVE,"WAVE") != 0)) return false; //not matched

        //also reject any wav whose bit depth isn't a sane multiple of 8 (e.g 0,4,12),
        //AudioConfig divides by (bitDepth/8) in several places so a bitDepth that doesn't
        //divide evenly into bytes would cause a division by zero later on
        if(header.bitDepth == 0 || header.bitDepth % 8 != 0 || header.bitDepth > 32) return false;

        return true;

    }

    //close the opened file
    void WavFile::close(){
        if(wavFile) wavFile.close();
    }
};