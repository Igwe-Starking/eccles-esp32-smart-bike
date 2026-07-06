/*
  Eccles bike control software used to power and run smart bike system,created by nwobodo Ecclesiastes
  on 19/03/2026,all right reserved to starking cooperation
*/

#include "Eccles.h"

ECCLES_SYSTEM

ExecutorManager manager;
PowerManager    power;

bool btInUse(){return true;}

void eccles_thread();

ECCLES_API_ENTRY void eccles_main() {

  eccles_startLog(115200);
  ECCLES_LOG_LINE("eccles bike control starting...");

  eccles_installWDT(100);
  eccles_wdtInclude(NULL);

  initRuntimeMemory();

  //prepare() opens NVS and the executor mutex — must happen before TTS and Transport
  manager.prepare();
  eccles_wdtReset();

  EcclesTTS::initEngine(LANG::EN_UK);
  eccles_wdtReset();

  Transport::prepare();
  eccles_wdtReset();

  manager.setResultHandler(Transport::getHandler());

  //init power manager after Transport so WiFi state is queryable from the first tick
  power.init();

  ECCLES_LOG_LINE("eccles bike control ready");

  while(1){
    eccles_thread();
    //power.tick() handles all yielding and sleep for this loop tick;
    //it also resets the idle timer whenever a connection is active
    e_uint32 slept = power.tick();
    //account for time spent sleeping so the watchdog doesn't expire during light/deep sleep
    if(slept > 0) eccles_wdtReset();
  }
}

void eccles_thread() {
  Transport::run();
  manager.run();
  eccles_wdtReset();
}
