//Base implementation of Conversation executor

#include "Executors.h"
#include "EcclesTTS.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <sys/ioctl.h>
#include <driver/i2s_std.h>
#include <driver/dac_continuous.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

ECCLES_API {

    Conversation* Conversation::instance = nullptr;
    constexpr e_uint16 PORT = 4210; //our remote port
    constexpr e_string IP = "255.255.255.255"; //our remote ip
    constexpr e_uint8 RAW_REPLY = 36; //dataType meaning speak com.data as-is, skip the AI reply lookup

    static int cnSocket = -1;
    static struct sockaddr_in cnDest = {};

    static void cnUdpBegin(e_uint16 port){
        if(cnSocket != -1) return;
        cnSocket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(cnSocket == -1) return;
        int enableBroadcast = 1;
        setsockopt(cnSocket,SOL_SOCKET,SO_BROADCAST,&enableBroadcast,sizeof(enableBroadcast));
        int nonBlock = 1;
        ioctl(cnSocket,FIONBIO,&nonBlock);

        struct sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        bind(cnSocket,(struct sockaddr*)&local,sizeof(local));

        cnDest.sin_family = AF_INET;
        cnDest.sin_port = htons(PORT);
        cnDest.sin_addr.s_addr = inet_addr(IP);
    }

    static void cnUdpStop(){
        if(cnSocket == -1) return;
        close(cnSocket);
        cnSocket = -1;
    }

    static void cnUdpSend(const e_uint8* data,e_uint16 len){
        if(cnSocket == -1) return;
        sendto(cnSocket,data,len,0,(struct sockaddr*)&cnDest,sizeof(cnDest));
    }

    static e_int cnUdpRecv(e_uint8* buf,e_uint16 len){
        if(cnSocket == -1) return 0;
        e_int r = recv(cnSocket,buf,len,0);
        return r > 0 ? r : 0;
    }

    //AI reply word bank: file scope constexpr arrays have internal linkage by default,
    //unlike in-class static constexpr arrays, which need an out of class definition to be
    //odr-used through a decayed pointer and were causing the reported linker errors
    namespace {

        constexpr e_uint8 REPLY_BUF_SIZE = 128; //must match EcclesTTS::speak()'s internal buffer size

        constexpr e_string GREETINGS[] = {
            "hello there how can i help you today",
            "hey good to hear your voice",
            "hi there im listening",
            "hello im here whats on your mind",
            "hey there ready when you are"
        };
        constexpr e_string MORNING[] = {
            "good morning hope you slept well",
            "morning lets have a good ride today",
            "good morning the bike is ready when you are"
        };
        constexpr e_string EVENING[] = {
            "good evening how was your day",
            "evening lets get you home safe",
            "good evening im here if you need anything"
        };
        constexpr e_string FAREWELLS[] = {
            "goodbye ride safe out there",
            "see you later take care",
            "bye for now ill be here",
            "later have a smooth ride",
            "goodbye stay safe on the road"
        };
        constexpr e_string THANKS[] = {
            "youre very welcome",
            "anytime happy to help",
            "no problem at all",
            "glad i could help"
        };
        constexpr e_string IDENTITY[] = {
            "im eccles your bikes assistant",
            "im the eccles conversation system built into your bike",
            "eccles here built to keep you company on the road"
        };
        constexpr e_string HELP_REPLIES[] = {
            "i can check fuel temp battery engine and bluetooth",
            "ask about fuel temp battery engine or bluetooth",
            "i can report on bike systems or just chat with you"
        };
        constexpr e_string JOKES[] = {
            "why did the bike fall over it was two tired",
            "i would tell you a road joke but it might not go anywhere",
            "engines dont tell jokes they just rev about it"
        };
        constexpr e_string APOLOGY_ACK[] = {
            "no worries at all",
            "its fine dont worry about it",
            "all good no need to apologize"
        };
        constexpr e_string POSITIVE_ACK[] = {
            "thats great to hear",
            "glad things are going well",
            "good to know lets keep going",
            "love to hear that"
        };
        constexpr e_string NEGATIVE_ACK[] = {
            "sorry to hear that hope it gets better soon",
            "thats rough take it easy out there",
            "i hear you take your time"
        };
        constexpr e_string LOVE_ACK[] = {
            "thats sweet of you to say",
            "love this ride too"
        };
        constexpr e_string HATE_ACK[] = {
            "sorry to hear that lets see if i can help",
            "thats no good lets sort it out"
        };
        constexpr e_string RIDE_HYPE[] = {
            "lets go im ready when you are",
            "ready when you are lets ride",
            "lets hit the road"
        };
        constexpr e_string SPEED_TALK[] = {
            "lets keep it safe out there",
            "speed feels good just stay safe",
            "i trust you just watch the road"
        };
        constexpr e_string WEATHER_REPLIES[] = {
            "i cant check the weather yet but ride safe either way",
            "no weather sensor on board yet just watch the road conditions"
        };
        constexpr e_string TIME_REPLIES[] = {
            "i dont have a clock yet but ill let you know once i do",
            "no clock on board yet check your phone for now"
        };
        constexpr e_string POLITE_ACK[] = {
            "of course go ahead",
            "sure tell me what you need"
        };
        constexpr e_string BT_ON[] = {
            "bluetooth is connected and ready",
            "bluetooth is on and linked up"
        };
        constexpr e_string BT_OFF[] = {
            "bluetooth is currently off",
            "bluetooth is not connected right now"
        };
        constexpr e_string ENGINE_ON[] = {
            "the engine is running smoothly",
            "engine is on and running fine"
        };
        constexpr e_string ENGINE_OFF[] = {
            "the engine is currently off",
            "engine is off right now"
        };
        constexpr e_string IGN_ON[] = { "ignition is on" };
        constexpr e_string IGN_OFF[] = { "ignition is off" };
        constexpr e_string LOCK_ON[] = { "the engine is locked right now" };
        constexpr e_string LOCK_OFF[] = { "the engine is unlocked" };
        constexpr e_string FUEL_PREFIX[] = { "fuel level is at " };
        constexpr e_string TEMP_PREFIX[] = { "engine temperature is at " };
        constexpr e_string BATTERY_PREFIX[] = { "battery voltage is around " };
        constexpr e_string HORN_REPLIES[] = {
            "say the word and ill sound the horn",
            "just ask and ill honk for you"
        };
        constexpr e_string LIGHT_REPLIES[] = {
            "the headlamp can be switched on whenever you need it",
            "just say turn on the lights and ill handle it"
        };
        constexpr e_string SIGNAL_REPLIES[] = {
            "let me know left or right and ill signal for you",
            "just tell me which way youre turning"
        };
        constexpr e_string SHOCK_REPLIES[] = {
            "the shock sensor is keeping an eye on the road for you",
            "ill let you know if i feel anything unusual"
        };
        constexpr e_string STATUS_REPLIES[] = {
            "ask about fuel temp battery engine or bluetooth",
            "i can check any system just ask"
        };
        constexpr e_string WELLBEING_REPLIES[] = {
            "im doing great thanks for asking how about you",
            "running smoothly how are you doing",
            "all systems good how about yourself"
        };
        constexpr e_string GENERIC_REPLIES[] = {
            "im listening go ahead",
            "tell me more im here",
            "got it whats next",
            "interesting tell me more about that",
            "im here whenever you need me",
            "okay im following along"
        };
        constexpr e_string GREET_FALLBACK[] = { "im here say something whenever youre ready" };

        //word hashes used to route a heard word to a reply bank
        constexpr e_uint32 H_HELLO = eccles_hashCT("hello");
        constexpr e_uint32 H_HI = eccles_hashCT("hi");
        constexpr e_uint32 H_HEY = eccles_hashCT("hey");
        constexpr e_uint32 H_MORNING = eccles_hashCT("morning");
        constexpr e_uint32 H_EVENING = eccles_hashCT("evening");
        constexpr e_uint32 H_NIGHT = eccles_hashCT("night");
        constexpr e_uint32 H_BYE = eccles_hashCT("bye");
        constexpr e_uint32 H_GOODBYE = eccles_hashCT("goodbye");
        constexpr e_uint32 H_LATER = eccles_hashCT("later");
        constexpr e_uint32 H_THANKS = eccles_hashCT("thanks");
        constexpr e_uint32 H_THANK = eccles_hashCT("thank");
        constexpr e_uint32 H_APPRECIATE = eccles_hashCT("appreciate");
        constexpr e_uint32 H_HOW = eccles_hashCT("how");
        constexpr e_uint32 H_ARE = eccles_hashCT("are");
        constexpr e_uint32 H_YOU = eccles_hashCT("you");
        constexpr e_uint32 H_DOING = eccles_hashCT("doing");
        constexpr e_uint32 H_FEELING = eccles_hashCT("feeling");
        constexpr e_uint32 H_FUEL = eccles_hashCT("fuel");
        constexpr e_uint32 H_GAS = eccles_hashCT("gas");
        constexpr e_uint32 H_TEMP = eccles_hashCT("temp");
        constexpr e_uint32 H_TEMPERATURE = eccles_hashCT("temperature");
        constexpr e_uint32 H_HOT = eccles_hashCT("hot");
        constexpr e_uint32 H_ENGINE = eccles_hashCT("engine");
        constexpr e_uint32 H_MOTOR = eccles_hashCT("motor");
        constexpr e_uint32 H_BLUETOOTH = eccles_hashCT("bluetooth");
        constexpr e_uint32 H_CONNECTED = eccles_hashCT("connected");
        constexpr e_uint32 H_BATTERY = eccles_hashCT("battery");
        constexpr e_uint32 H_VOLTAGE = eccles_hashCT("voltage");
        constexpr e_uint32 H_POWER = eccles_hashCT("power");
        constexpr e_uint32 H_NAME = eccles_hashCT("name");
        constexpr e_uint32 H_WHO = eccles_hashCT("who");
        constexpr e_uint32 H_IGNITION = eccles_hashCT("ignition");
        constexpr e_uint32 H_LOCK = eccles_hashCT("lock");
        constexpr e_uint32 H_LOCKED = eccles_hashCT("locked");
        constexpr e_uint32 H_HORN = eccles_hashCT("horn");
        constexpr e_uint32 H_HEADLAMP = eccles_hashCT("headlamp");
        constexpr e_uint32 H_LIGHT = eccles_hashCT("light");
        constexpr e_uint32 H_LIGHTS = eccles_hashCT("lights");
        constexpr e_uint32 H_TURN = eccles_hashCT("turn");
        constexpr e_uint32 H_SIGNAL = eccles_hashCT("signal");
        constexpr e_uint32 H_SHOCK = eccles_hashCT("shock");
        constexpr e_uint32 H_BUMP = eccles_hashCT("bump");
        constexpr e_uint32 H_HELP = eccles_hashCT("help");
        constexpr e_uint32 H_JOKE = eccles_hashCT("joke");
        constexpr e_uint32 H_FUNNY = eccles_hashCT("funny");
        constexpr e_uint32 H_GOOD = eccles_hashCT("good");
        constexpr e_uint32 H_GREAT = eccles_hashCT("great");
        constexpr e_uint32 H_FINE = eccles_hashCT("fine");
        constexpr e_uint32 H_AWESOME = eccles_hashCT("awesome");
        constexpr e_uint32 H_BAD = eccles_hashCT("bad");
        constexpr e_uint32 H_SAD = eccles_hashCT("sad");
        constexpr e_uint32 H_TIRED = eccles_hashCT("tired");
        constexpr e_uint32 H_WORRIED = eccles_hashCT("worried");
        constexpr e_uint32 H_SCARED = eccles_hashCT("scared");
        constexpr e_uint32 H_LOVE = eccles_hashCT("love");
        constexpr e_uint32 H_HATE = eccles_hashCT("hate");
        constexpr e_uint32 H_READY = eccles_hashCT("ready");
        constexpr e_uint32 H_RIDE = eccles_hashCT("ride");
        constexpr e_uint32 H_GO = eccles_hashCT("go");
        constexpr e_uint32 H_SPEED = eccles_hashCT("speed");
        constexpr e_uint32 H_FAST = eccles_hashCT("fast");
        constexpr e_uint32 H_WEATHER = eccles_hashCT("weather");
        constexpr e_uint32 H_TIME = eccles_hashCT("time");
        constexpr e_uint32 H_DATE = eccles_hashCT("date");
        constexpr e_uint32 H_OK = eccles_hashCT("ok");
        constexpr e_uint32 H_OKAY = eccles_hashCT("okay");
        constexpr e_uint32 H_YES = eccles_hashCT("yes");
        constexpr e_uint32 H_NO = eccles_hashCT("no");
        constexpr e_uint32 H_PLEASE = eccles_hashCT("please");
        constexpr e_uint32 H_SORRY = eccles_hashCT("sorry");
        constexpr e_uint32 H_WHATSUP = eccles_hashCT("sup");
        constexpr e_uint32 H_STATUS = eccles_hashCT("status");
        constexpr e_uint32 H_CHECK = eccles_hashCT("check");
        constexpr e_uint32 H_REPORT = eccles_hashCT("report");

        //xorshift32,no rand(),deterministic and allocation free
        e_uint32 nextRand(){
            static e_uint32 state = 0x9E3779B9u;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return state;
        }

        //picks a varied reply,salts the seed with the heard phrase so back to back
        //identical inputs don't always land on the same index
        e_string pick(const e_string* list,e_uint8 count,e_string heard){
            if(count == 0) return "";
            e_uint32 salt = (heard != nullptr) ? eccles_hashRT(heard) : 0;
            return list[(nextRand() + salt) % count];
        }

        void copyReply(e_char* out,e_string text){
            e_uint8 i = 0;
            for(; i < REPLY_BUF_SIZE - 1 && text[i] != '\0'; ++i) out[i] = text[i];
            out[i] = '\0';
        }

        //appends a 0-100 number as plain digits without overrunning REPLY_BUF_SIZE
        void appendPercent(e_char* out,e_uint8 value){
            e_uint8 v = value > 100 ? 100 : value;
            e_uint8 len = 0;
            while(out[len] != '\0' && len < REPLY_BUF_SIZE - 1) ++len;
            if(len >= REPLY_BUF_SIZE - 4) return;

            if(v >= 100){ out[len]='1'; out[len+1]='0'; out[len+2]='0'; out[len+3]='\0'; }
            else if(v >= 10){ out[len]=(e_char)('0'+(v/10)); out[len+1]=(e_char)('0'+(v%10)); out[len+2]='\0'; }
            else { out[len]=(e_char)('0'+v); out[len+1]='\0'; }
        }

        #define PICK(bank) pick(bank,sizeof(bank)/sizeof(bank[0]),heard)

        //builds an AI reply from a heard phrase into out,word by word hashing,
        //same single pass strtok pattern BinaryCommand::parse uses in Executor.cpp
        void aiReply(e_string heard,e_char* out){
            out[0] = '\0';
            if(heard == nullptr || heard[0] == '\0'){
                copyReply(out,PICK(GREET_FALLBACK));
                return;
            }

            e_char words[96];
            e_uint8 i = 0;
            for(; i < sizeof(words) - 1 && heard[i] != '\0'; ++i){
                words[i] = (e_char) tolower((e_uint8) heard[i]);
            }
            words[i] = '\0';

            e_boolean sawHow = false, sawAre = false, sawYou = false, sawDoing = false, sawFeeling = false, sawStatus = false, sawSignal = false;

            e_char* word = strtok(words," ");
            while(word != nullptr){
                const e_uint32 h = eccles_hashRT(word);

                switch(h){
                    case H_HELLO: case H_HI: case H_HEY: case H_WHATSUP:
                        copyReply(out,PICK(GREETINGS)); return;

                    case H_MORNING:
                        copyReply(out,PICK(MORNING)); return;

                    case H_EVENING: case H_NIGHT:
                        copyReply(out,PICK(EVENING)); return;

                    case H_BYE: case H_GOODBYE: case H_LATER:
                        copyReply(out,PICK(FAREWELLS)); return;

                    case H_THANK: case H_THANKS: case H_APPRECIATE:
                        copyReply(out,PICK(THANKS)); return;

                    case H_NAME: case H_WHO:
                        copyReply(out,PICK(IDENTITY)); return;

                    case H_HELP:
                        copyReply(out,PICK(HELP_REPLIES)); return;

                    case H_JOKE: case H_FUNNY:
                        copyReply(out,PICK(JOKES)); return;

                    case H_SORRY:
                        copyReply(out,PICK(APOLOGY_ACK)); return;

                    case H_GOOD: case H_GREAT: case H_FINE: case H_AWESOME: case H_OK: case H_OKAY: case H_YES:
                        copyReply(out,PICK(POSITIVE_ACK)); return;

                    case H_BAD: case H_SAD: case H_TIRED: case H_WORRIED: case H_SCARED: case H_NO:
                        copyReply(out,PICK(NEGATIVE_ACK)); return;

                    case H_LOVE:
                        copyReply(out,PICK(LOVE_ACK)); return;

                    case H_HATE:
                        copyReply(out,PICK(HATE_ACK)); return;

                    case H_READY: case H_RIDE: case H_GO:
                        copyReply(out,PICK(RIDE_HYPE)); return;

                    case H_SPEED: case H_FAST:
                        copyReply(out,PICK(SPEED_TALK)); return;

                    case H_WEATHER:
                        copyReply(out,PICK(WEATHER_REPLIES)); return;

                    case H_TIME: case H_DATE:
                        copyReply(out,PICK(TIME_REPLIES)); return;

                    case H_PLEASE:
                        copyReply(out,PICK(POLITE_ACK)); return;

                    case H_BLUETOOTH: case H_CONNECTED: {
                        Bluetooth* bt = Bluetooth::instance;
                        e_boolean on = (bt != nullptr) && bt->on;
                        copyReply(out,on ? PICK(BT_ON) : PICK(BT_OFF));
                        return;
                    }

                    case H_ENGINE: case H_MOTOR: {
                        e_boolean running = (globalState.ENGINE_RUNNING == ON);
                        copyReply(out,running ? PICK(ENGINE_ON) : PICK(ENGINE_OFF));
                        return;
                    }

                    case H_IGNITION: {
                        e_boolean on = (globalState.IGNITION_STATE == ON);
                        copyReply(out,on ? PICK(IGN_ON) : PICK(IGN_OFF));
                        return;
                    }

                    case H_LOCK: case H_LOCKED: {
                        e_boolean locked = (globalState.ENGINE_LOCK == ON);
                        copyReply(out,locked ? PICK(LOCK_ON) : PICK(LOCK_OFF));
                        return;
                    }

                    case H_FUEL: case H_GAS:
                        copyReply(out,PICK(FUEL_PREFIX));
                        appendPercent(out,globalState.FUEL_DATA);
                        return;

                    case H_TEMP: case H_TEMPERATURE: case H_HOT:
                        copyReply(out,PICK(TEMP_PREFIX));
                        appendPercent(out,globalState.TEMP_DATA);
                        return;

                    case H_BATTERY: case H_VOLTAGE: case H_POWER:
                        copyReply(out,PICK(BATTERY_PREFIX));
                        appendPercent(out,(e_uint8) globalState.VOLTAGE_LEVEL);
                        return;

                    case H_HORN:
                        copyReply(out,PICK(HORN_REPLIES)); return;

                    case H_HEADLAMP: case H_LIGHT: case H_LIGHTS:
                        copyReply(out,PICK(LIGHT_REPLIES)); return;

                    case H_TURN: case H_SIGNAL: sawSignal = true; break;

                    case H_SHOCK: case H_BUMP:
                        copyReply(out,PICK(SHOCK_REPLIES)); return;

                    case H_HOW: sawHow = true; break;
                    case H_ARE: sawAre = true; break;
                    case H_YOU: sawYou = true; break;
                    case H_DOING: sawDoing = true; break;
                    case H_FEELING: sawFeeling = true; break;
                    case H_STATUS: case H_CHECK: case H_REPORT: sawStatus = true; break;
                    default: break;
                }

                word = strtok(nullptr," ");
            }

            //order independent "how are you / how you doing / how you feeling"
            if(sawHow && sawYou && (sawAre || sawDoing || sawFeeling)){
                copyReply(out,PICK(WELLBEING_REPLIES));
                return;
            }

            if(sawStatus){
                copyReply(out,PICK(STATUS_REPLIES));
                return;
            }

            //turn/signal only matters if no other device word already matched
            if(sawSignal){
                copyReply(out,PICK(SIGNAL_REPLIES));
                return;
            }

            copyReply(out,PICK(GENERIC_REPLIES));
        }

        #undef PICK

    };

    //pushes recorded mic audio out over udp and plays back whatever arrives,
    //runs on the AudioInput record thread, not the main loop
    class ConversationHandler : public AudioInputHandler {
        e_boolean onData(e_uint8* data,e_uint16 len) const override {
            Conversation* cnv = Conversation::instance;
            if(cnv == nullptr || !cnv->active) return false;

            cnUdpSend(data,len);

            e_uint8 pkt[2048];
            e_int ps = cnUdpRecv(pkt,sizeof(pkt));
            if(ps > 0){
                size_t written = 0;
                esp_err_t err = ESP_OK;
                if(audioHandles.dac){
                    err = dac_continuous_write(audioHandles.dac,pkt,(size_t)ps,&written,pdMS_TO_TICKS(100));
                } else if(audioHandles.tx){
                    err = i2s_channel_write(audioHandles.tx,pkt,(size_t)ps,&written,pdMS_TO_TICKS(100));
                }
                if(err != ESP_OK){
                    cnv->stop();
                    return false;
                }
            }
            return true;
        }
    };

    ConversationHandler cnh;

    //execute conversation related instructions
    e_boolean Conversation::execute(Command& com){
        if(com.target != DeviceID::CONVERSATION) return false;
        switch (com.action){
            case CommandAction::START_REAL : start(ConversationMode::REAL,com); return true;
            case CommandAction::START_AI : start(ConversationMode::AI,com); return true;
            case CommandAction::OFF : stop(); return true;
            case CommandAction::PLAY : reply(com); return true;
            default: break;
        }

        //probably a query command we store the result in result.size here
        CommandResult r;
        r.id = com.id;
        r.sender = com.sender;

        if(com.action == CommandAction::QUERY_ON){
            r.size = active ? 1 : 0;
        } else if(com.action == CommandAction::QUERY_OFF){
            r.size = active ? 0 : 1;
        }
        sendResult(r);
        return true;
    }

    e_boolean Conversation::checkCondition(Condition& con){
        if(con.target != DeviceID::CONVERSATION) return false;
        switch(con.action){
            case CommandAction::QUERY_ON : return active;
            case CommandAction::QUERY_OFF : return !(active);
            default: return false;
        }
    }

    //starts the conversation engine, AI mode is handled entirely through reply() so this
    //function only does real work for REAL mode
    void Conversation::start(ConversationMode m,Command& com){
        if(active || com.size < 4) return;

        Bluetooth* bt = Bluetooth::instance;
        if(bt != nullptr && bt->on) bt->stop();

        if(m != ConversationMode::REAL) return;

        //command data[0,1] = requested sample rate, data[2] = bit depth, data[3] = channel count
        audioConfig cfg;
        cfg.sampleRate = (e_uint16) ((com.data[0] << 8) | (com.data[1] & 0xff));
        cfg.sampleSize = com.data[2];
        cfg.channel = com.data[3] == 1 ? AUDIO_CHANNEL::MONO : AUDIO_CHANNEL::STEREO;

        EcclesTTS::pause();
        EcclesTTS::getOutput()->reConfigure(cfg,AUDIO_MODE::I2S_ADC);

        //recording uses the same config the caller requested, mirrored back in the result
        //below so the caller always knows exactly what rate/depth/channel we're sending
        CommandResult r;
        r.id = com.id;
        r.sender = com.sender;
        r.size = 4;
        e_char dt[4];
        dt[0] = (e_uint8) ((cfg.sampleRate >> 8) & 0xff);
        dt[1] = (e_uint8) (cfg.sampleRate & 0xff);
        dt[2] = cfg.sampleSize;
        dt[3] = cfg.channel == AUDIO_CHANNEL::MONO ? 1 : 2;
        r.data = dt;
        sendResult(r);

        if(!recorder.start(&cnh,cfg,2048,AUDIO_INPUT::I2S_ADC)){
            ECCLES_LOG_LINE("failed to start conversation");
            EcclesTTS::reset();
            EcclesTTS::speak("conversation could not start please try again");
            return;
        }

        active = true;
        Conversation::instance = this;
        cnUdpBegin(PORT); //real mode streams mic/speaker audio over udp
    }

    //AI mode replies through bound command objects rather than udp, since this only ever
    //carries short text, not a continuous audio stream. com.data is the heard phrase, not
    //guaranteed null terminated by the sender. RAW_REPLY speaks it as-is, otherwise an AI
    //reply is built from it
    void Conversation::reply(Command& com){
        if(com.data == nullptr || com.size == 0) return;

        e_char heard[96];
        e_uint16 n = com.size < (sizeof(heard) - 1) ? com.size : (sizeof(heard) - 1);
        memcpy(heard,com.data,n);
        heard[n] = '\0';

        if(com.dataType == RAW_REPLY){
            EcclesTTS::speak(heard);
            return;
        }

        e_char said[REPLY_BUF_SIZE];
        aiReply(heard,said);
        EcclesTTS::speak(said);
    }

    //stop the conversation engine
    void Conversation::stop(){
        if(!active) return;

        recorder.stop();
        EcclesTTS::reset();
        active = false;
        cnUdpStop();

        EcclesTTS::speak("conversation has ended take care");
    }

    //check if conversation is active
    e_boolean Conversation::isActive(){
        return active;
    }
};
