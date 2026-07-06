/*
  main.cpp
  ---------
  eccles_main() is the engine's entry point, "a.k.a. start" the
  single function CommandBridge.initEngine() launches on its own thread once Java has a
  firmware IP in hand (from UdpDiscovery.java reading the "ECCLES_IP:x.x.x.x" broadcast).
  Everything else - the websocket connection, the receive loop, CommandFactory, the two
  queues - is owned by Engine and reached through native-lib.cpp's JNI functions.
*/

#include "Eccles.h"

ECCLES_API {

void eccles_main(const std::string& firmwareIp) {
  ECCLES_LOG_LINE("eccles_main starting");
  Engine::instance().run(firmwareIp); // blocks until Engine::stop() is called
  ECCLES_LOG_LINE("eccles_main exiting");
}

};  // ECCLES_API
