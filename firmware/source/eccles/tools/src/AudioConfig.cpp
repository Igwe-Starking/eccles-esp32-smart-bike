/*
    base implementation of audio config file
*/

#include "AudioConfig.h"
#include <fstream>
#include <algorithm>
#include <random>

ECCLES_API {

    using namespace std;

    //helper class for sample processing
    class Resampler {

        public:
        //convert a sample to double neccesary for some operations
        //sampleSize is the bit depth of the sample e.g 8,16,24,32
        //8 bit PCM is unsigned so we shift it to a signed range before normalizing
        //all other depths are already signed so we divide directly
        static e_double scaleUp(e_int t, e_uint8 sampleSize){
            //we support different bit depths here we need to convert them to float 
            //in order to correctly process them in some operations

            //get its bitmask max based on the given sample size
            e_uint64 bm = ((1ULL << sampleSize) - 1);

            e_uint64 bmd = (bm / 2) + 1; //255 / 2 is cliped to 127 so we add 1 to make it 128 the same applies to other types
            //and more over since others are signed this is actually used in the division

            //8 bit PCM are unsigned while the rest is signed so we differenciate it here
            if(sampleSize == 8){ //8 bit
                //in that case we divede the bit max by two
                return (((double)t - (double)bmd) / (double)bmd);
            } else { //not 8 bit
                return ((double)t / (double)bmd);
            }
        }

        //retrieve an audio sample from a given buffer sample can be 8bit,16,24,32 etc
        //returns the sample as a universal e_int carrier,the caller knows the actual depth
        static e_int getSample(vector<e_uint8>& buffer, e_uint8 sampleSize, e_uint32 sampleIndex){
            e_uint8 byteCount = sampleSize / 8;
            if(sampleIndex + byteCount > buffer.size()) return 0;

            //we check the sample size and retrieve the corresponding sample by it
            //wav pcm data is little endian on x86,the lowest addressed byte is the least significant byte
            switch (byteCount){
                case 1 : return (e_uint8)  buffer[sampleIndex];
                case 2 : return (e_int16) ((buffer[sampleIndex + 1] << 8) | (buffer[sampleIndex]));
                case 3 : {
                    //there is no cpp integer type to hold 24 bit sample so we store it anyway to 32 bit
                    return (e_int) ((buffer[sampleIndex + 2] << 16) | (buffer[sampleIndex + 1] << 8) | (buffer[sampleIndex]));
                }
                case 4 : return (e_int) ((buffer[sampleIndex + 3] << 24) | (buffer[sampleIndex + 2] << 16) | (buffer[sampleIndex + 1] << 8) | (buffer[sampleIndex]));
                default : return 0;
            }
        }

        //write a given sample to a buffer
        //s is the sample as a universal e_int carrier,sampleSize tells us how many bytes to write
        static void writeSample(vector<e_uint8>& buffer, e_uint8 sampleSize, e_int s, e_uint32 index){
            //check if the args are valid 
            e_uint8 byteCount = sampleSize / 8;
            if((buffer.size() == 0) || (index + byteCount > buffer.size())) return;

            //get the binary equivalence (wav pcm data is little endian on x86)
            switch (byteCount) {
                case 1 : buffer[index] = (e_uint8)s; return;
                case 2 : buffer[index] = (s & 0xff); buffer[index + 1] = ((s >> 8) & 0xff); return;
                case 3 : buffer[index] = (s & 0xff); buffer[index + 1] = ((s >> 8) & 0xff); buffer[index + 2] = ((s >> 16) & 0xff); return;
                case 4 : buffer[index] = (s & 0xff); buffer[index + 1] = ((s >> 8) & 0xff); buffer[index + 2] = ((s >> 16) & 0xff); buffer[index + 3] = ((s >> 24) & 0xff); return;
                default : cerr<< "invalid audio bit depth given on writeSample exiting..." <<endl; return;
            }
        }

        //convert a sample from float to its original sample size
        //returns the result as a universal e_int carrier,sampleSize tells us the target depth
        static e_int scaleDown(e_double sample, e_uint8 sampleSize, e_boolean ditter = false){
            //get the sample bit depth
            e_uint64 s_bit = (1ULL << sampleSize) - 1;

            //random generation used for dittering
            static thread_local minstd_rand rand(random_device{}());
            static thread_local uniform_real_distribution<e_float> dist(-0.5f, 0.5f);

            //calculate noise
            e_float noise = dist(rand) + dist(rand);

            if(sampleSize == 8){ //8 bit sample
                e_double s = (sample * 128.0) + 128.0 + (ditter ? noise : 0);
                return static_cast<e_int>(clamp(s, 0.0, 255.0));
            }

            //calculate for non 8 bit samples
            e_double half = (double)(s_bit / 2);
            e_double s = (sample * half) + (ditter ? noise : 0);
            return (e_int) clamp(s, -(half + 1.0), half);
        }
    };

    /*
        AudioConfig conversion helpers,We previously created a universal fuctions that
        can convert the sample to any uses a function named get sample to retrieve the 
        sample but this created a distorted audio which led to complete rewrite of this file
        this led us to discard the 32 bitd depth support and only support 8 and 16,
        NOTE: 32 bit audios will be ignore for conversion for now untill we find a way to solve the 
        previous mess
    */

    

    AudioConfig::AudioConfig(){}
    AudioConfig::~AudioConfig(){}

    //open the specified configuration file and write its content to the underlying
    //eccles_config_t object
    e_boolean AudioConfig::open(e_string path){
        //we open the ecclesConfig.txt and read the contents
        ifstream st(path);
        
        //check if the file is actually opened
        if(!st){
            //this will automatically use the default eccles_config_t
            //FIX: path can be nullptr (no config file supplied, the normal default case).
            //Printing a null e_string used to corrupt cout's failbit, silently swallowing
            //every subsequent cout message -- including the final success summary -- for
            //the rest of the run.
            cout<<"failed to open ecclesConfig.txt from the following dir:"<<(path ? path : "<none>")<<endl;
            return false;
        }

        //read the whole file line by line
        string l;
        while(getline(st,l)){
            //get the separator position
            e_uint32 p = l.find(":");
            if(p == string::npos) continue; //invalid line

            //extract the key and the value from the string
            string k = l.substr(0,p); //the key
            string v = l.substr(p + 1); //the value
            
            if(k == "raw"){
                //if this is set all other configurations are ignored and the file is 
                //packaged as is unmodified
                config.raw = atoi(v.c_str());
            } else if(k == "rate"){
                //the frame rate
                e_uint16 r = atoi(v.c_str());
                config.rate = r > 0 ? r : config.rate;
            } else if(k == "depth"){
                //bit depdth 8,16 etc
                e_uint8 d = atoi(v.c_str());
                config.depth = d > 0 ? d : config.depth;
            } else if(k == "channel"){
                //audio channel mono,stereo
                Channel c = Channel::MONO;
                if(v == "stereo") c = Channel::STEREO;
                config.channel = c; 
            } else if(k == "tsr"){
                e_boolean tsr = false;
                if(v == "true") tsr = true;
                config.TSR = tsr;
            } else if(k == "type"){
                //audio pack type static,dynamic
                if(v == "dynamic") config.type = ModelType::DYNAMIC;
            }
        }
        return true;
        //here we go ifstream will auto close on exit
    }

    //configures this buffer with the underlying opened config
    //converts the raw in buffer to a vector then pipes it through each configuration
    //function in order,the output of one is fed into the next,the final result is returned
    //if raw mode is set in config the buffer is returned as is without any processing
    vector<e_uint8> AudioConfig::configure(e_uint8* in,e_uint32 len,wav_header_t& header){
        //check the validity of args
        if(in == nullptr || len == 0) return vector<e_uint8>();

        //wrap the raw buffer in a vector so all config functions can work on it uniformly
        vector<e_uint8> buf(in, in + len);

        //if raw is set we skip all processing and return the buffer as is
        if(config.raw == 1) return buf;

        //apply each configuration in order,if a config function returns an empty vector
        //it means no change was needed for that step so we keep the current buffer as is

        //step 1: rate conversion
        vector<e_uint8> rateOut = configureRate(buf, header);
        if(!rateOut.empty()){
            buf = rateOut;
            header.sampleRate = config.rate; //update the header to reflect the new rate
            header.byteRate   = config.rate * header.channel * (header.bitDepth / 8);
        }

        //step 2: bit depth conversion
        vector<e_uint8> depthOut = configureDepth(buf, header);
        if(!depthOut.empty()){
            buf = depthOut;
            header.bitDepth  = config.depth; //update the header to reflect the new depth
            header.byteRate  = header.sampleRate * header.channel * (config.depth / 8);
            header.blockAlign = header.channel * (config.depth / 8);
        }

        //step 3: channel conversion
        vector<e_uint8> channelOut = configureChannel(buf, header);
        if(!channelOut.empty()){
            buf = channelOut;
            //update the header channel count to reflect the new channel layout
            header.channel    = (config.channel == Channel::STEREO) ? 2 : 1;
            header.byteRate   = header.sampleRate * header.channel * (header.bitDepth / 8);
            header.blockAlign = header.channel * (header.bitDepth / 8);
        }

        //step 4: trailing silence removal
        vector<e_uint8> tsrOut = configureTSR(buf, header);
        if(!tsrOut.empty()){
            buf = tsrOut;
        }

        return buf;
    }

    //configures the audio rate to the config rate
    vector<e_uint8> AudioConfig::configureRate(vector<e_uint8> in,wav_header_t& header){
        //here we change the timeline of the audio data
        //we first check if the rate in config is equal to the rate in the header we don't have any problem
        //if the rate is greater we upsample and if lesser we down sample

        //we first check the validity of the supplied arguments
        if(in.size() ==0 || header.sampleRate == 0 || config.rate == 0) return vector<e_uint8>(); //no op

        //check if header rate and config rate is same
        if(config.rate == header.sampleRate){
            return vector<e_uint8>(); //no op
        }


        //since all supported rate are divisible to each we divid the config rate by the header rate
        e_uint64 len = ((e_uint64) in.size() * config.rate) / header.sampleRate;
        if(len == 0) return vector<e_uint8>();

        e_double r = static_cast<e_double>(header.sampleRate) / static_cast<e_double>(config.rate);

        //create the output buffer
        vector<e_uint8> out(len);

        e_uint32 sz = in.size();
        //FIX: this used to be config.depth/8 (the pipeline's *target* bit depth). At this
        //point in the pipeline (rate -> depth -> channel -> TSR) the buffer is still in the
        //wav's *native* header.bitDepth -- depth conversion only happens in the next stage,
        //configureDepth(). Using config.depth here misaligned every sample read/write by the
        //wrong byte stride whenever a clip's native depth differed from the configured target
        //depth, corrupting the resampled audio. Both the input and output buffers here are in
        //header.bitDepth, since this stage only changes the sample rate, not the bit depth.
        e_uint8 bz = header.bitDepth/8;
        if(bz == 0) return vector<e_uint8>(); //no op, invalid/corrupt header
        //start the processing loop
        for(e_uint64 i = 0;i < len;i+= bz){
            //i is a byte offset into the output buffer,but getSample/writeSample expect sample
            //offsets to also be expressed as byte offsets,so we convert i to a sample index first,
            //do the position math in sample units,then convert back to a byte offset for indexing
            e_uint64 sampleI = i / bz; //the output byte offset expressed as a sample index
            e_double pos = static_cast<e_double>(sampleI) * r; //the gap between the position of two rates in time
            e_uint32 sampleIndex = static_cast<e_uint32>(pos); //convert this gap to integer index
            e_float frac = static_cast<e_float>(pos - static_cast<e_double>(sampleIndex)); //get the fractional remainder of this gap
            e_uint32 index = sampleIndex * bz; //convert the sample index back to a byte offset

            e_double int_p = 0.0; //stores the interpolation value
            //bound check
            if(index >= sz - bz){
                int_p = Resampler::scaleUp(Resampler::getSample(in, header.bitDepth, sz - bz), header.bitDepth);
            } else {
                e_double sv  = Resampler::scaleUp(Resampler::getSample(in, header.bitDepth, index),      header.bitDepth); //get the sample at current index
                e_double sv1 = Resampler::scaleUp(Resampler::getSample(in, header.bitDepth, index + bz), header.bitDepth); //get the neighbouring sample

                //interpolate the two
                int_p = sv * (1.0 - frac) + sv1 * frac;
            }

            e_int ot = Resampler::scaleDown(int_p, header.bitDepth);
            Resampler::writeSample(out, header.bitDepth, ot, (e_uint32)i);
        }
        return out;
    }

    //change the input audio bit depth to the one specified in config
    vector<e_uint8> AudioConfig::configureDepth(vector<e_uint8> in,wav_header_t& header){
        //check the validity of args
        if(in.size() == 0 || config.depth == 0 || header.bitDepth == 0) return vector<e_uint8>();

        //the output size is the size of the desired depth divided by the size of the actual depth
        //but if the actual depth is greater we divide vice versa since thats the only way to get a positive value

        //we first check if the depths are same in that case we don't have to worry
        if(config.depth == header.bitDepth){ 
            return vector<e_uint8>();
        }

        e_uint8 bm = header.bitDepth / 8; //get the actual bytes count
        e_uint32 numSamples = in.size() / bm; //total number of samples in the input
        e_uint32 size = numSamples * (config.depth / 8); //output size scaled to new depth
        vector<e_uint8> out(size);

        //start the processing loop
        for(e_uint32 i = 0;i < numSamples; i++){
            e_uint32 inIndex  = i * bm; //byte offset in the input buffer
            e_uint32 outIndex = i * (config.depth / 8); //byte offset in the output buffer
            //here we blow it up,convert it to the desired bit depth and blow it down again
            e_int ot = Resampler::scaleDown(Resampler::scaleUp(Resampler::getSample(in, header.bitDepth, inIndex), header.bitDepth), config.depth, true); //ditter is needed here in bit depth conversion  
            Resampler::writeSample(out, config.depth, ot, outIndex);        
        }
        return out;
    }

    //change the audio channel to the desired one
    //mono to stereo: each sample is duplicated into a left and right channel
    //stereo to mono: each pair of left/right samples is averaged into a single sample
    vector<e_uint8> AudioConfig::configureChannel(vector<e_uint8> in,wav_header_t& header){
        //check validity of args
        if(in.size() == 0 || header.channel == 0 || header.bitDepth == 0) return vector<e_uint8>();

        //check if the channel is already matching,no work needed
        e_uint16 desiredChannels = (config.channel == Channel::STEREO) ? 2 : 1;
        if(header.channel == desiredChannels) return vector<e_uint8>(); //no op

        e_uint8 bz = header.bitDepth / 8; //bytes per single sample

        if(header.channel == 1 && desiredChannels == 2){
            //mono to stereo: output is twice the size since we duplicate every sample
            vector<e_uint8> out(in.size() * 2);
            e_uint32 numSamples = in.size() / bz;

            for(e_uint32 i = 0; i < numSamples; i++){
                e_uint32 inIndex  = i * bz; //source sample byte offset
                e_uint32 outIndex = i * bz * 2; //destination byte offset,two samples per frame

                e_int sample = Resampler::getSample(in, header.bitDepth, inIndex);

                //write the same sample to both the left and right channel
                Resampler::writeSample(out, header.bitDepth, sample, outIndex);
                Resampler::writeSample(out, header.bitDepth, sample, outIndex + bz);
            }
            return out;

        } else if(header.channel == 2 && desiredChannels == 1){
            //stereo to mono: output is half the size since we merge two channels into one
            vector<e_uint8> out(in.size() / 2);
            e_uint32 numFrames = in.size() / (bz * 2); //a stereo frame is two samples

            for(e_uint32 i = 0; i < numFrames; i++){
                e_uint32 inIndex  = i * bz * 2; //source frame byte offset
                e_uint32 outIndex = i * bz; //destination sample byte offset

                e_int left  = Resampler::getSample(in, header.bitDepth, inIndex); //left channel sample
                e_int right = Resampler::getSample(in, header.bitDepth, inIndex + bz); //right channel sample

                //average the two channels to produce the mono sample
                e_int mono = (left + right) / 2;
                Resampler::writeSample(out, header.bitDepth, mono, outIndex);
            }
            return out;
        }

        return vector<e_uint8>(); //no op for any other combination
    }

    //remove trailing silence from the wave file
    //silence is defined as samples at or near the zero level,for 8 bit this is 128,for signed its 0
    //we scan from the end of the buffer backwards and trim everything that is silence
    vector<e_uint8> AudioConfig::configureTSR(vector<e_uint8> in,wav_header_t& header){
        //check validity of args
        if(in.size() == 0 || header.bitDepth == 0) return vector<e_uint8>();

        //check if TSR is actually requested
        if(!config.TSR) return vector<e_uint8>(); //no op

        e_uint8 bz = header.bitDepth / 8; //bytes per sample
        e_uint32 numSamples = in.size() / bz;

        //the silence threshold,small values close to zero are considered silent
        //we allow a small margin to account for noise floor in real recordings
        constexpr e_double silenceThreshold = 0.01;

        //scan backwards from the end to find the last non-silent sample
        e_uint32 lastActive = 0;
        for(e_int i = (e_int)numSamples - 1; i >= 0; i--){
            e_uint32 inIndex = (e_uint32)i * bz;
            e_int sample = Resampler::getSample(in, header.bitDepth, inIndex);
            e_double normalized = Resampler::scaleUp(sample, header.bitDepth);

            //if the absolute value of the normalized sample is above the threshold
            //this is the las active sample,stop here
            if(normalized < -silenceThreshold || normalized > silenceThreshold){
                lastActive = (e_uint32)i + 1; //keep everything up to and including this sample
                break;
            }
        }

        //if the whole buffer is silent return a minimal single-frame buffer
        if(lastActive == 0) return vector<e_uint8>(bz, (header.bitDepth == 8) ? 128 : 0);

        //if nothing was trimmed return empty to signal no change
        if(lastActive == numSamples) return vector<e_uint8>(); //no op

        //return the trimmed buffer up to the last active sample
        return vector<e_uint8>(in.begin(), in.begin() + (lastActive * bz));
    }

};
