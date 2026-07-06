//base implementation of configuration executor
//PORT NOTE: the original wrapped arduino-esp32's Preferences class (itself a thin wrapper
//over nvs_get_*/nvs_set_* anyway). esp-idf has no Preferences class so PR is now a raw
//nvs_handle_t and every PR.getX/putX call below is its nvs_get_x/nvs_set_x equivalent.
//every method keeps its original name, signature, and the exact same binary protocol
//handling in execute() (CONF_MGC_I/F/S/B dataTypes, key/value packing, etc)

#include "Executors.h"
#include <nvs_flash.h>
#include <cstring>
#include <cstdlib>

ECCLES_API {

    //initializing members
    nvs_handle_t Configuration::PR = 0;
    e_boolean Configuration::opened = false;

    //open the configuration file the name is Eccles and we open in read/write mode
    void Configuration::open(){
        if(opened) return; //we can't open twice

        //flash the nvs system first
        esp_err_t err = nvs_flash_init();
        if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if(err != ESP_OK){
            ECCLES_LOG_LINE("failed to flash the nvs system");
            return;
        }
        //PORT NOTE: replaces PR.begin("Eccles",false), nvs_open with NVS_READWRITE gives the
        //same implicit-create, read/write semantics arduino's Preferences::begin had
        opened = (nvs_open("Eccles",NVS_READWRITE,&PR) == ESP_OK);
    }

    //read a string value, type = Arduino String
    //returns the stored string or an empty FixedStr<64> if the key doesn't exist
    e_stringa Configuration::readString(e_string k){
        if(opened){
            size_t len = 0;
            if(nvs_get_str(PR,k,nullptr,&len) != ESP_OK || len == 0) return "";
            e_char buf[64]; //every persisted string in this project is short, matches Configuration's own 56-byte key/value cap
            if(len > sizeof(buf)) len = sizeof(buf);
            if(nvs_get_str(PR,k,buf,&len) != ESP_OK) return "";
            return e_stringa(buf);
        }
        return "";
    }

    //read an int value
    e_uint32 Configuration::readInt(e_string k){
        if(opened){
            e_uint32 v = 0;
            nvs_get_u32(PR,k,&v); //leaves v at its default (0) if the key doesn't exist yet, same as PR.getInt(k,0)
            return v;
        }
        return 0;
    }

    //read a float
    //PORT NOTE: nvs has no native float type, we store/retrieve the raw bits through nvs_get_u32/
    //nvs_set_u32 via a union, so the float value itself is preserved exactly
    e_float Configuration::readFloat(e_string k){
        if(opened){
            union { e_uint32 i; e_float f; } u;
            u.i = 0;
            nvs_get_u32(PR,k,&u.i);
            return u.f;
        }
        return 0.0f;
    }

    //reads a boolean
    e_boolean Configuration::readBoolean(e_string k){
        if(opened){
            e_uint8 v = 0;
            nvs_get_u8(PR,k,&v);
            return v != 0;
        }
        return false;
    }

    //write a string
    void Configuration::writeString(e_string k,e_string v){
        if(opened){
            nvs_set_str(PR,k,v);
            nvs_commit(PR);
        }
    }

    //write an int
    void Configuration::writeInt(e_string k,e_uint32 v){
        if(opened){
            nvs_set_u32(PR,k,v);
            nvs_commit(PR);
        }
    }

    //write a float
    void Configuration::writeFloat(e_string k,e_float v){
        if(opened){
            union { e_uint32 i; e_float f; } u;
            u.f = v;
            nvs_set_u32(PR,k,u.i);
            nvs_commit(PR);
        }
    }

    //write a boolean
    void Configuration::writeBoolean(e_string k,e_boolean v){
        if(opened){
            nvs_set_u8(PR,k,v ? 1 : 0);
            nvs_commit(PR);
        }
    }

    //void close the configuration file
    void Configuration::close(){
        if(opened) nvs_close(PR);
        opened = false;
    }

    //execute configuration specific commands
    e_boolean Configuration::execute(Command& com){
        //check if this command is really ment for us
        if(com.target != DeviceID::CONFIG) return false; //try other executors
        /*
            here Command.dataType holds the type of configuration data requested
            if string is requested Command.size we hold the size of the string key and value
            the string key and value is stored in the command data buffer as key/value
            we have to split '/' this to distinguist between the key and value

            for other non string types com.size holds the size of key name + data,we have to read 4 bytes after
            the key name to decode the value requested

            because condition and configuration use the same dataType we cant execute a configuration command on condition
            what's the essence of executing a setup instruction on condition though
        */
        //get the config key
        e_char key[57]; //we can't have a key with name larger than 56 if we do we raise alarm and quit
        if(com.data == nullptr){
            ECCLES_LOG_LINE("configuration command has no data attached");
            return true; //we failed but the command is still ours so we tell the list to quit searching
        }
        if(com.size >= (sizeof(key)-1)){
            ECCLES_LOG_LINE("too long key or key/value at most 56");
            return true; //we failed but the command is still ours so we tell the list to quit searching
        }
        //FIX: for non-string types we read the key length as (com.size - 4), but com.size is
        //attacker/network controlled. Without this lower bound, a com.size of 0-3 underflows
        //this unsigned subtraction to a huge value and both memcpy calls below (and the write
        //path further down) overrun the fixed-size "key" stack buffer / read wildly out of bounds.
        if(com.dataType != CONF_MGC_S && com.size < 4){
            ECCLES_LOG_LINE("configuration command too small for its data type");
            return true;
        }

        if(com.dataType == CONF_MGC_S){
            memcpy(key,com.data,com.size);
            key[com.size] = '\0'; //add NULL
        } else {
            memcpy(key,com.data,com.size - 4);
            key[com.size - 4] = '\0';
        }

        if(com.action == CommandAction::WRITE){
            //requested to update configuration value

            if(com.dataType == CONF_MGC_I){ //write int
                e_uint32 vl;
                memcpy(&vl,com.data + (com.size - 4),sizeof(vl));
                writeInt(key,vl);
                return true;
            }

            if(com.dataType == CONF_MGC_F){ //write float
                e_float vl;
                memcpy(&vl,com.data + (com.size - 4),sizeof(vl));
                writeFloat(key,vl);
                return true;
            }

            if(com.dataType == CONF_MGC_B){
                //here we store the boolean as in,>=1 for true 0 for false
                e_uint32 vl;
                memcpy(&vl,com.data + (com.size - 4),sizeof(vl));
                writeBoolean(key,(vl >= 1));
                return true;
            }

            if(com.dataType == CONF_MGC_S){//write string
                //here the key already contain the value we split it
                e_string ky = strtok(key,"/");
                e_string vl = strtok(nullptr,"/");

                if(!ky || !vl){
                    ECCLES_LOG_LINE("invalid key/value");
                    return true;
                }

                writeString(ky,vl);
                return true;
            }

        } else if(com.action == CommandAction::READ){
            //here result is requested we must send it
            CommandResult r;
            r.id = com.id;
            r.sender = com.sender;
            /*
                the requested result is appended to Command result data buffer
                which is structured like this
                CommandResult.size contains the size of everything in the buffer
                the buffer is organized like this:
                buffer[0] = result type e.g int,float,boolean,string
                buffer[size-1] contain the result data

                for string results
                the string start at buffer[1] and its size is result.size - 1, 1 that holds the type
                for string the result string contain the key and value as k/v to decode it you must split / in the string

                for other type the key starts at buffer[1] and its size is result.size -5 1 for type 4 for value
                the value starts from result.size-4 and its size is 4 bytes
            */
            e_char rd[128]; //type byte + up to 56 byte key + 4 byte value, matches key[]'s own bound

            if((com.dataType == CONF_MGC_I) || (com.dataType == CONF_MGC_F) || (com.dataType == CONF_MGC_B)){ //read int

                rd[0] = com.dataType; //tell the caller this is the result type we are giving

                e_uint32 vl = 0;
                e_float vf = 0.0f;

                switch(com.dataType){
                    case CONF_MGC_I : vl = readInt(key); break;
                    case CONF_MGC_F : vf = readFloat(key); break;
                    case CONF_MGC_B : vl = readBoolean(key) ? 1 : 0; break;
                    default: vl = 0; break;
                }

                e_uint klen = strlen(key);

                memcpy(rd + 1,key,klen);

                memcpy(
                    rd + 1 + klen,
                    com.dataType == CONF_MGC_F ? (void*)&vf : (void*)&vl,
                    sizeof(vl)
                );

                r.size = 1 + klen + sizeof(vl);

                //this result must not be stored except the data variable is copied before this function returns
                r.data = rd;
                sendResult(r);
                return true;
            }

            if(com.dataType == CONF_MGC_S){ //string data

                e_stringa sv = readString(key);
                e_string sd = sv.c_str();

                rd[0] = com.dataType;
                snprintf(rd + 1,sizeof(rd) - 2,"%s/%s",key,sd);

                r.size = strlen(rd + 1) + 2;

                r.data = rd;
                sendResult(r);
                return true;
            }
        }

        return true; //return true anyway because we checked earlier and the DeviceID is CONFIG so we tell the list to stop searching
    }

    //is it necessary to use settings condition and moreover the config keys uses string but the
    //condition object has no way to store string so for now this is not supported
    e_boolean Configuration::checkCondition(Condition& con) {
        return false; //what else can we do and why would someone run a condition with this target
    }
};
