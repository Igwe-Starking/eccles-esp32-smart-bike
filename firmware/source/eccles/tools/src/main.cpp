/*
    eccles-tts-packager entry point
    scans a folder of .wav files and packs them into a StaticModel.eccles binary
    and a StaticModel.h firmware index header

    usage:
      eccles-tts-packager -platformIO
          runs from eccles/tools/bin/, resolves all paths relative to the IDF project root

      eccles-tts-packager <folder> [config.txt]
          packs wav files in <folder> using optional config, writes StaticModel.eccles
          and StaticModel.h into the current working directory
*/

#include "Eccles.h"
#include <iostream>
#include <cstring>

ECCLES_SYSTEM

using namespace std;

constexpr e_string TAG = "eccles-tts-packager:";

ECCLES_API_ENTRY e_int eccles_main(e_int count,char* params[]){
    //FIX: count is the real argc, which always includes argv[0] (the program's own path), so
    //it can never actually be 0 -- this check never fired for a genuine no-args invocation.
    //The loop below also used to start at i=0 and treat argv[0] itself as a real argument,
    //which (for the "no args" case) silently assigned the program's own exe path to
    //globalState.folderName instead of reporting "no wav folder specified".
    if(count <= 1){
        cerr<<TAG<<" no argument specified"<<endl;
        cerr<<TAG<<" usage: eccles-tts-packager -platformIO"<<endl;
        cerr<<TAG<<" usage: eccles-tts-packager <wav-folder> [config.txt]"<<endl;
        return 1;
    }

    e_boolean dynamic = false;

    for(e_uint8 i = 1; i < count; i++){ //start at 1: skip argv[0], the program's own path
        if(strcmp("-platformIO",params[i]) == 0){
            globalState.platformIO = true;
        } else if(strcmp("-type",params[i]) == 0){
            if((e_uint)(i + 1) < count && strcmp("dynamic",params[i + 1]) == 0){
                dynamic = true;
            }
        } else if(string(params[i]).find(".txt") != string::npos){
            globalState.configPath = params[i];
        } else {
            globalState.folderName = params[i];
        }
    }

    //FIX: "-type dynamic" used to be parsed into a variable that nothing ever read; a
    //staticModel was instantiated and packed unconditionally regardless of this flag, so
    //asking for a dynamic model silently produced a static one instead. There is no
    //DynamicModel implementation anywhere in this tool (see staticModel.h's own comment
    //distinguishing the two), so we now report that clearly instead of doing the wrong thing
    //quietly.
    if(dynamic){
        cerr<<TAG<<" dynamic model packaging is not implemented in this build, only static models are supported"<<endl;
        return 1;
    }

    staticModel sm;

    if(globalState.platformIO){
        //platformIO mode: paths are resolved relative to eccles/tools/bin/ inside the IDF project
        return sm.pack() ? 0 : 1;
    }

    if(globalState.folderName == nullptr){
        cerr<<TAG<<" no wav folder specified"<<endl;
        cerr<<TAG<<" usage: eccles-tts-packager <wav-folder> [config.txt]"<<endl;
        return 1;
    }

    //CLI mode: pack the specified folder and write outputs to cwd
    return sm.pack() ? 0 : 1;
}
