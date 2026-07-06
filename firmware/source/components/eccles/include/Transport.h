/*
  manages Transport connections, transport is a means of communication with our client and we currently support three means
  our clients has the opportunity co connect to us using Wifi/Web/TCP or using serial NOTE: we currently support 
  serial text not serial binaries,we are also planning to include bluetooth support as a means of communication but considering
  that bluetooth is heavy and running it side by side with wifi/tcp add bandwitch to our network we are considering using
  bluetooth only for a2dp instead of for normal instructions

  @IGWE_STARKING all rights reserved 
*/

#ifndef ECCLES_WEB_HANDLER
#define ECCLES_WEB_HANDLER


//dependencies
//PORT NOTE: <WiFi.h> (arduino-esp32's wifi wrapper) is replaced with esp-idf's native
//esp_wifi.h + the netif/event glue it needs, <ESPAsyncWebServer.h> (a third party library with
//no esp-idf equivalent shipped in-tree) is replaced with esp-idf's own esp_http_server, which
//has built in websocket support, see Transport.cpp for how AsyncWebServer/AsyncWebSocket's
//role is rebuilt on top of httpd_handle_t/httpd_uri_t/httpd_ws_frame_t
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "Executor.h"

ECCLES_API {

  #define DEFAULT_SSID "Eccles Smart Bike"
  #define DEFAULT_PSWD "12345678"

  //we used namespace here instead of class to avoid too much object abstractions

  //this handles TCP and even UDP transport system
  namespace WebTransport {
    
    void prepare();
    void run();
    void cleanup();
    ResultHandler* getHandler();
    e_boolean isWifiConnected();   //true once IP_EVENT_STA_GOT_IP has fired
    e_boolean isClientConnected(); //true while at least one websocket client is open

  };

  //handles serial instructions and replies,NOTE:serial instruction processing is heavy since we are dealing with raw strings here
  namespace SerialTransport {
    void prepare(); //sets up the serial layer for instructions,reply and debugging
    void run(); //runs the serial instructions if any
    void cleanup(); //safely shuts down the serial system
    ResultHandler* getHandler(); //returns the serial handler for replies
  };

  //bluetooth is heavy on ESP so we avoid it for now and use it only for A2DP
  /*
  namespace BluetoothTransport {
    void prepare();
    void run();
    void cleanup();
    ResultHandler* getHandler();
  };*/

  /*
    Tranport Manager this manages all transport in transport namespaces and routes result back to the appropriete 
    transport for processing
  */
  namespace Transport {
    void prepare(); //prepares all available transports
    void run(); //execute all supported transports
    void cleanup(); //cleanup all supported transport
    ResultHandler* getHandler(); //this is the default main handler given to command executor
  };

  //handles HTML displaying for web controls
  namespace HTML {
    //displays html page to a browser client using the server provided in arg
    //PORT NOTE: AsyncWebServer& is replaced with esp-idf's httpd_handle_t (a plain opaque
    //handle, esp_http_server has no server "object" to pass by reference)
    void load(httpd_handle_t server); //note this loads a static hardcoded page we are working toward creating a dynamic html loader from file
  };


};

#endif
