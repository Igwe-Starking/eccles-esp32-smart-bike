//base implemenation of a transport
//PORT NOTE: this file is the heaviest architectural change in the whole port. arduino's
//WiFi.h + the third party ESPAsyncWebServer/AsyncWebSocket library + WiFiUDP + ESPmDNS + Serial
//are replaced with esp-idf's native equivalents: esp_wifi (station mode connect), esp_http_server
//(which has built in websocket support, used here in place of AsyncWebServer/AsyncWebSocket),
//raw lwIP/BSD sockets for the UDP broadcast, the "mdns" component, and the uart driver for
//serial. every namespace, function name, comment and piece of business logic is kept exactly
//where it was; only the underlying networking calls changed.

#include "Transport.h"
#include "Executors.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <cstring>

ECCLES_API {


  //defining the main globals,we do this here to prevent exposing it globally to others and to avoid linker errors
  //which we experienced earlier.

  //PORT NOTE: AsyncWebServer server(80) / AsyncWebSocket socket("/ws") are replaced with a
  //plain httpd_handle_t (esp-idf's server handle) plus our own tiny list of connected
  //websocket client file descriptors, since esp_http_server addresses ws clients by their
  //underlying socket fd rather than handing out an AsyncWebSocketClient object
  static httpd_handle_t server = nullptr; //we used static here because we want only this file to own it and avoid all extern linkage

  #define MAX_WS_CLIENTS 4
  static int wsClientFds[MAX_WS_CLIENTS]; //connected websocket client file descriptors, -1 = empty slot

  static void wsClientsReset(){
    for(int& fd : wsClientFds) fd = -1;
  }

  static void wsClientAdd(int fd){
    for(int& slot : wsClientFds){
      if(slot == -1){ slot = fd; return; }
    }
  }

  static void wsClientRemove(int fd){
    for(int& slot : wsClientFds){
      if(slot == fd){ slot = -1; return; }
    }
  }

  static e_boolean anyWsClientConnected(){
    for(int fd : wsClientFds){ if(fd != -1) return true; }
    return false;
  }

  extern e_boolean connected; //defined further down this file, forward-declared for wsCloseHandler below

  //FIX: wsClientRemove() used to be defined but never called from anywhere, so client fd
  //slots (and the "connected" flag below) never got freed on disconnect. esp_http_server
  //notifies us of a socket closing via httpd_config_t::close_fn, which we now wire up in
  //start() below. Note: once you supply a custom close_fn you take over responsibility for
  //actually closing the socket (the default one esp-idf installs does this for you).
  static void wsCloseHandler(httpd_handle_t hd,int sockfd){
    wsClientRemove(sockfd);
    if(!anyWsClientConnected()) connected = false; //allow IP re-broadcast / new connections
    ECCLES_LOG_LINE("ws client disconnected");
    close(sockfd);
  }

  //finds the client file descriptor matching the given senderID (AsyncWebSocketClient::id()
  //used to just be the client's connection index, here we use its raw socket fd as the id,
  //assigned in the websocket open handler below)
  static int wsClientFind(e_uint32 senderID){
    for(int fd : wsClientFds){
      if(fd == (int) senderID) return fd;
    }
    return -1;
  }


  //handlers,this process and send result to client
  //web handler
  struct WebHandler : public ResultHandler {
    void sendResult(e_uint8* res,e_uint16 size,e_uint32 senderID) const override {
      //sending result to a web client
      //PORT NOTE: AsyncWebSocketClient* cl = socket.client(senderID); cl->binary(res,size);
      //is replaced with esp_http_server's httpd_ws_send_frame_async, addressed by the raw
      //socket fd we tracked as this client's senderID
      int fd = wsClientFind(senderID);
      if(fd != -1 && server != nullptr){
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_BINARY;
        frame.payload = res;
        frame.len = size;
        httpd_ws_send_frame_async(server,fd,&frame);
      }
    }
  };

  //Serial Handler
  struct SerialHandler : public ResultHandler {
    void sendResult(e_uint8* res,e_uint16 size,e_uint32 senderID) const override {
      //for now we don't send result data to serial since the result data is binary and must be converted to text to make
      //sense on serial log,we only send device state and sensor data

      e_uint16 v = (res[1] << 8) | (res[2] & 0xFF); //we get the Result.size which is where state and sensor data are stored
      e_string msg = nullptr;
      if(v == 0) msg = "OFF"; else if(v == 1) msg= "ON";
      if(msg != nullptr){
        //print state to serial
        //PORT NOTE: Serial.print/println -> ECCLES_LOG/ECCLES_LOG_LINE (uart driver backed,
        //see EcclesTypes.cpp), same two-call shape kept so the printed lines look identical
        ECCLES_LOG("Device is ");
        ECCLES_LOG_LINE(msg);
        return;
      }
      //if we got here we may be dealing with sensor values
      ECCLES_LOG("Device value is ");
      ECCLES_LOG_LINE(v);
    }
  };

  WebHandler wh;
  SerialHandler sh; //web and serial handlers

  //Transport Handler,this is the main transport handle given to executor it detects which transport layer sends the 
  //command and route the result back to the layer, senderID 1 indicates Serial the rest is web

  struct TransportHandler : public ResultHandler {
    void sendResult(e_uint8* res,e_uint16 size,e_uint32 senderID) const override {
      if(senderID == SRL_RT_MGC){
        //hand the result to serial,this is ambiguos because we need to make sure that WebTransport does't give sender id == SRL_RT_MGC but it is unlikely
        SerialTransport::getHandler()->sendResult(res,size,senderID);
      } else {
        //for every other sender id hand the result to web since we currently implement this two
        WebTransport::getHandler()->sendResult(res,size,senderID);
      }
    }
  };

  constexpr e_uint16 PORT = 4210; //our udp port
  constexpr e_string IP = "255.255.255.255"; //not to be  confused with the name this is just our subnet ip

  //PORT NOTE: WiFiUDP udp; is replaced with a raw lwIP/BSD UDP socket, created lazily the
  //first time broadcastIP() runs (mirrors WiFiUDP's own implicit lazy socket creation)
  static int udpSocket = -1;
  e_boolean connected = false;

  e_uint32 delay = 1000; //time interval used to broadcast our IP address,default 1sec

  TransportHandler rh; //processes result and sent to clients

  #define SERIAL_BAUD 115200 //our serial baud rate
  #define SERIAL_BUFFER_MAX 64 //reserved bytes for our serial text
  #define SERIAL_MAX_READ 16 //max serial byte we can read per loop to avoid huging other task and to improve responsiveness


  //this functions runs web socket events
  //PORT NOTE: replaces AsyncWebSocket's onEvent(socket,client,type,arg,data,len) callback,
  //esp_http_server's websocket support instead gives each connection its own httpd_uri_t
  //handler invoked per HTTP request/frame, the three event types (DATA/CONNECT/DISCONNECT)
  //are reconstructed in wsHandler() below using httpd's own request lifecycle: a GET request
  //with the websocket handshake header is the "connect" moment, a WS frame is "data", and the
  //fd closing (httpd's close callback) is "disconnect"
  static void onEvent(httpd_req_t* req,httpd_ws_frame_t* frame,e_uint8* data,size_t len){
    if(len < 8) return; //corrupted or partial data
    Command* com = BinaryCommand::parse(data,len);
    if(com){
      com->sender = httpd_req_to_sockfd(req);
      Executor::send(*com);
    }
  }

  //esp_http_server calls this once per ws connection lifecycle event via its uri handler,
  //we dispatch into the same three cases the original onEvent() switched on
  static esp_err_t wsHandler(httpd_req_t *req){
    if(req->method == HTTP_GET){
      //handshake just completed, this is esp_http_server's equivalent of WS_EVT_CONNECT
      int fd = httpd_req_to_sockfd(req);
      wsClientAdd(fd);
      connected = true;
      ECCLES_LOG_LINE("ws client connected");
      return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    //first call with a zero buffer to learn the frame length, same two-step pattern esp-idf's
    //own websocket examples use, since httpd doesn't hand us a length up front like AsyncWebSocket did
    esp_err_t ret = httpd_ws_recv_frame(req,&frame,0);
    if(ret != ESP_OK) return ret;

    if(frame.len == 0) return ESP_OK; //nothing to read, e.g a ping/control frame

    e_uint8 buf[256]; //our binary command protocol never needs more than this, mirrors BINARY_CMD_SIZE + a small data payload
    if(frame.len > sizeof(buf)) return ESP_FAIL; //oversized/corrupted frame, reject it

    frame.payload = buf;
    ret = httpd_ws_recv_frame(req,&frame,frame.len);
    if(ret != ESP_OK) return ret;

    onEvent(req,&frame,buf,frame.len); //WS_EVT_DATA equivalent
    return ESP_OK;
  }

  //broacasts the IP address to the client incase the Domain name system failed
  void broadcastIP(){
    
    //we add ECCLES_IP because we use it in the client code to detect that we are the one talking on the udp network

    //String txt = "ECCLES_IP:"+WiFi.localIP().toString(); we use snprintf instead of this to minimize heap usage
    //PORT NOTE: WiFi.localIP() is replaced with esp_netif_get_ip_info() against the default
    //station netif, IPAddress::toString() with esp-idf's own IP4ADDR_STRFMT/esp_ip4addr_ntoa
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ipInfo = {};
    if(netif == nullptr || esp_netif_get_ip_info(netif,&ipInfo) != ESP_OK) return;

    e_char ipStr[16];
    esp_ip4addr_ntoa(&ipInfo.ip,ipStr,sizeof(ipStr));

    char txt[64];
    snprintf(txt,sizeof(txt),"ECCLES_IP:%s",ipStr);

    //PORT NOTE: udp.beginPacket(IP,PORT); udp.write(...); udp.endPacket(); is replaced with a
    //single sendto() against a lazily created broadcast-enabled UDP socket
    if(udpSocket == -1){
      udpSocket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
      if(udpSocket == -1){
        ECCLES_LOG_LINE("broadcastIP: failed to create udp socket");
        return;
      }
      int enableBroadcast = 1;
      setsockopt(udpSocket,SOL_SOCKET,SO_BROADCAST,&enableBroadcast,sizeof(enableBroadcast));
    }

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr.s_addr = inet_addr(IP);

    sendto(udpSocket,txt,strlen(txt),0,(struct sockaddr*)&dest,sizeof(dest));
  }

  //wifi event handler: tracks station connect/disconnect/got-ip, replacing the simple
  //WiFi.status() == WL_CONNECTED polling arduino's WiFi.h exposed
  static e_boolean wifiGotIp = false;

  static void wifiEventHandler(void* arg,esp_event_base_t base,int32_t event_id,void* data){
    if(base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
      wifiGotIp = false;
      esp_wifi_connect(); //keep retrying, mirrors WiFi.begin()'s own automatic reconnect behaviour
    } else if(base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
      wifiGotIp = true;
    }
  }

  void WebTransport::prepare(){
    //configuring the ip address, we do this to avoid dynamically assigned IPs not recommended here
    /*

    we tried this on most android and it's not consistent so we default to normal DHCP server
    but the issue is that DHCP is dynamic we don't control which ip is assigned but we are working on a way
    to detect and supply the ip at runtime to our client

    
    IPAddress local(192,168,43,50); //this is our constant IP address on the network
    IPAddress gate(192,168,43,1); //this is our router address,NOTE: our android is the router which is why we use 43 here
    IPAddress sub(255,255,255,0);

    WiFi.config(local,gate,sub);

    */

    wsClientsReset();

    //PORT NOTE: arduino's WiFi.h quietly initializes netif/event loop/wifi driver the first
    //time you touch it, esp-idf needs each of those set up explicitly: nvs (already done by
    //Configuration::open() before this runs), the tcp/ip netif layer, the default event loop,
    //a default station netif, and the wifi driver itself in station mode
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t initCfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&initCfg);

    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifiEventHandler,nullptr);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifiEventHandler,nullptr);

    //starting wifi driver
    e_stringa ssidStored = Configuration::readString("wifi_ssid");
    e_stringa pswStored = Configuration::readString("wifi_pswd");
    e_string ssid = ssidStored.c_str();
    e_string psw = pswStored.c_str();

    if(eccles_compareString(ssid,"")) ssid = DEFAULT_SSID;
    if(eccles_compareString(psw,"")) psw = DEFAULT_PSWD;

    wifi_config_t wifiCfg = {};
    strncpy((char*)wifiCfg.sta.ssid,ssid,sizeof(wifiCfg.sta.ssid)-1);
    strncpy((char*)wifiCfg.sta.password,psw,sizeof(wifiCfg.sta.password)-1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA,&wifiCfg);
    esp_wifi_start();
    esp_wifi_connect();
  }

  //runs the web transport layer,NOTE: this function runs repeatedly in the loop to avoid blocking the thread waiting for wifi to connect
  void WebTransport::run(){
    static e_boolean initialized = false;
    //PORT NOTE: WiFi.status() != WL_CONNECTED is replaced with the wifiGotIp flag maintained
    //by wifiEventHandler() above, set true only once IP_EVENT_STA_GOT_IP actually fires
    if(!wifiGotIp){
      if(initialized){ //we where once connected so we cleanup the connection
        cleanup();
      }
       initialized = false;
       return; //we are already connected exit the function
    } 

    if(!connected){ //we support max one connection for now
    
      //we broadcast our IP here,NOTE: if domanin name fail only app can reliable connect to us,web browsing will need to guess our IP
      e_uint32 t = eccles_millis();
      if(t - delay >= 1000){
        //we broadcast our IP every seconds so client has chance of reading it and connecting to us
        broadcastIP();
        delay = t + 1000; //non blocking delay
      }
     
    }

   
    if(initialized) return; //init guard to prevent our setup from repeating once connected
      //we are connected to AP but not to a client setup mDNS here
    
    //displaying our ip once in log
    //PORT NOTE: WiFi.localIP() is replaced with the same esp_netif_get_ip_info lookup
    //broadcastIP() uses, ECCLES_LOG_LINE only accepts a single c-string/numeric argument so we
    //format it ourselves here instead of relying on an implicit IPAddress->String conversion
    {
      esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      esp_netif_ip_info_t ipInfo = {};
      if(netif != nullptr && esp_netif_get_ip_info(netif,&ipInfo) == ESP_OK){
        e_char ipStr[16];
        esp_ip4addr_ntoa(&ipInfo.ip,ipStr,sizeof(ipStr));
        ECCLES_LOG_LINE(ipStr);
      }
    }

    //PORT NOTE: socket.onEvent(onEvent); server.addHandler(&socket); is replaced with starting
    //an httpd server and registering a uri handler for "/ws" that has .is_websocket = true,
    //esp_http_server then drives our wsHandler() per connection/frame instead of a single
    //persistent callback object
    httpd_config_t httpCfg = HTTPD_DEFAULT_CONFIG();
    httpCfg.close_fn = wsCloseHandler; //FIX: previously unset, so disconnects were never detected
    if(httpd_start(&server,&httpCfg) == ESP_OK){
      httpd_uri_t wsUri = {};
      wsUri.uri = "/ws";
      wsUri.method = HTTP_GET;
      wsUri.handler = wsHandler;
      wsUri.is_websocket = true;
      httpd_register_uri_handler(server,&wsUri);

      //loading our html page here
      HTML::load(server);
    }

    //starting mDNS so the device is reachable as eccles.local, replaces ESPmDNS's implicit
    //setup, kept here rather than as a separate include-time global to match how httpd itself
    //is only started once we're actually connected
    mdns_init();
    mdns_hostname_set("eccles");

    initialized = true;
    ECCLES_LOG_LINE("connected succussfully to AP");
  }

  void WebTransport::cleanup(){
    //closing all sockets
    //PORT NOTE: socket.closeAll(); socket.cleanupClients(); is replaced with stopping the
    //whole httpd server (which closes every open connection, websocket included) and
    //resetting our tracked client fd list
    if(server != nullptr){
      httpd_stop(server);
      server = nullptr;
    }
    wsClientsReset();

    if(udpSocket != -1){
      close(udpSocket);
      udpSocket = -1;
    }
  }

  //returning the result handler

  ResultHandler* WebTransport::getHandler(){
    return &wh;
  }

  e_boolean WebTransport::isWifiConnected(){
    return wifiGotIp;
  }

  e_boolean WebTransport::isClientConnected(){
    return connected;
  }

  //Serial transport,controls communication over the serial bus

  void SerialTransport::prepare(){
    //prepairing the serial bus
    //PORT NOTE: Serial.begin(SERIAL_BAUD) is replaced with eccles_startLog (uart driver on
    //UART_NUM_0, see EcclesTypes.cpp), the same physical port arduino's Serial object used
    eccles_startLog(SERIAL_BAUD);
  }

  //runs the serial communication bus
  void SerialTransport::run(){
    static e_char buffer[SERIAL_BUFFER_MAX]; //holds our serial data
    static e_uint8 index = 0;

    e_uint8 rb = SERIAL_MAX_READ;

    //PORT NOTE: Serial.available()/Serial.read() (which block on nothing and return -1/0 when
    //empty) are replaced with uart_get_buffered_data_len + a zero-timeout uart_read_bytes, so
    //this loop keeps its original non-blocking, "drain up to SERIAL_MAX_READ bytes" shape
    size_t avail = 0;
    uart_get_buffered_data_len(UART_NUM_0,&avail);

    while(avail > 0 && rb--){
      e_uint8 c = 0;
      if(uart_read_bytes(UART_NUM_0,&c,1,0) != 1) break;
      avail--;

      if(c == '\n'){ //we got full instruction
        buffer[index] = '\0'; //we terminate string whenever we got a newline

        //processing of the instruction
        
        e_uint16 len = 0;
        Command* com = StringCommand::parse(buffer,&len);
        if(com != nullptr){
          com->sender = SRL_RT_MGC; //serial sender id
          Executor::send(*com);
        }
        //reseting buffer
        index = 0;
        //FIX: this used to "return;" here, silently discarding any further buffered bytes
        //for this call instead of continuing to drain up to SERIAL_MAX_READ bytes as the
        //loop is meant to. Falling through lets any additional queued commands in the same
        //call be processed instead of waiting for the next poll.
        continue;
      }
      if(c == '\r') continue; //we don't need carriage return here
      if(index < SERIAL_BUFFER_MAX - 1){
        //we convert to lower case here
        if(c > 64 && c < 91) c+=32; //lower case conversion
        buffer[index++] = c;
      } else { //overflow we reset buffer index here
        index = 0;
      }
    }
  }


  //cleaning and exiting the serial bus
  void SerialTransport::cleanup(){
    //PORT NOTE: Serial.end() is replaced with uninstalling the uart driver we installed in
    //eccles_startLog/eccles_uartInit
    uart_driver_delete(UART_NUM_0);
  }

  //returning the result handler
  ResultHandler* SerialTransport::getHandler(){
    return &sh;
  }

  //prepairing all transports
  void Transport::prepare(){
    //we prepares serial first since its also used for debuging that might debug webtransport
    SerialTransport::prepare();
    WebTransport::prepare();
  }

  //running the transport system
  void Transport::run(){
    //here Web transport first and most important
    WebTransport::run();
    SerialTransport::run();
  }

  //cleaning the transport layers
  void Transport::cleanup(){
    WebTransport::cleanup();
    SerialTransport::cleanup();
  }

  //returning the result processing object
  ResultHandler* Transport::getHandler(){
    return &rh;
  }

  //serves a file from LittleFS flash storage, streaming it in FILE_MAX_CHUNK_BUFFER chunks
  //so large files (app.js) never need to be fully buffered in RAM
  static esp_err_t serveFile(httpd_req_t* r, e_string vfsPath, e_string contentType){
    FILE* f = fopen(vfsPath, "rb");
    if(!f){
      ECCLES_LOG("serveFile: not found: ");
      ECCLES_LOG_LINE(vfsPath);
      httpd_resp_send_404(r);
      return ESP_FAIL;
    }

    httpd_resp_set_type(r, contentType);

    e_uint8 buf[128]; //FILE_MAX_CHUNK_BUFFER causes stack overflow error in the wifi stack so we replace it with this
    size_t rd;
    while((rd = fread(buf, 1, sizeof(buf), f)) > 0){
      if(httpd_resp_send_chunk(r, (const char*)buf, (ssize_t)rd) != ESP_OK){
        fclose(f);
        httpd_resp_send_chunk(r, nullptr, 0);
        return ESP_FAIL;
      }
    }
    fclose(f);
    httpd_resp_send_chunk(r, nullptr, 0); //signal end of chunked response
    return ESP_OK;
  }

  static esp_err_t indexHandler(httpd_req_t* r){
    return serveFile(r, "/littlefs/index.html", "text/html");
  }

  static esp_err_t cssHandler(httpd_req_t* r){
    return serveFile(r, "/littlefs/style.css", "text/css");
  }

  static esp_err_t jsHandler(httpd_req_t* r){
    return serveFile(r, "/littlefs/app.js", "application/javascript");
  }

  void HTML::load(httpd_handle_t s){
    //serve index.html, style.css and app.js from LittleFS flash storage
    //files are streamed in chunks so large JS files never need to be fully buffered in RAM
    auto reg = [&](e_string uri, httpd_method_t m, esp_err_t(*fn)(httpd_req_t*)){
      httpd_uri_t u = {};
      u.uri = uri; u.method = m; u.handler = fn;
      httpd_register_uri_handler(s, &u);
    };
    reg("/",          HTTP_GET, indexHandler);
    reg("/index.html",HTTP_GET, indexHandler);
    reg("/style.css", HTTP_GET, cssHandler);
    reg("/app.js",    HTTP_GET, jsHandler);
  }
};
