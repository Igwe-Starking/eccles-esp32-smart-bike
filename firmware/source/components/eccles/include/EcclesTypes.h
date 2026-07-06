/*
  Eccles ESP 32 Library holding custom typenames and common functions,this file holds every 
  Eccles abstraction library for esp-idf

  PORT NOTE: this header wraps pure ESP-IDF (driver/gpio.h, driver/adc.h, esp_timer,
  vfs/uart driver, raw FreeRTOS) in place of the original Arduino core calls.
  std::string and heap allocation have been removed entirely — all string work uses
  e_char[] stack buffers or the RuntimeMemory pool allocator.
*/

#ifndef ECCLES_TYPES
#define ECCLES_TYPES

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "driver/uart.h"

#include "EcclesConfig.h"

#define ECCLES_SYSTEM using namespace Eccles;
#define ECCLES_API namespace Eccles
//esp-idf's startup code looks up app_main with C linkage
#define ECCLES_API_ENTRY extern "C"
#define eccles_main app_main

#ifdef ECCLES_DEBUG
#define ECCLES_LOG(arg) (Eccles::eccles_printRaw((arg)))
#define ECCLES_LOG_LINE(arg) (Eccles::eccles_printLine((arg)))
#else
#define ECCLES_LOG(arg)
#define ECCLES_LOG_LINE(arg)
#endif

//architecture abstractions — every macro below calls straight into esp-idf drivers
#define eccles_enable(pin)              gpio_set_level((gpio_num_t)(pin),1)
#define eccles_disable(pin)             gpio_set_level((gpio_num_t)(pin),0)
#define eccles_isOn(pin)                (gpio_get_level((gpio_num_t)(pin)) == 1)
#define eccles_isOff(pin)               (gpio_get_level((gpio_num_t)(pin)) == 0)
#define eccles_pinState(pin)            (gpio_get_level((gpio_num_t)(pin)))
#define eccles_read(pin)                (Eccles::eccles_analogRead8((pin)))
#define eccles_write(pin,value)         (Eccles::eccles_analogWrite((pin),(value)))
#define eccles_wait(time)               (vTaskDelay((time) / portTICK_PERIOD_MS))
#define eccles_taskInit(func,name,st,arg,pri) (xTaskCreate(func,name,st,arg,pri,NULL))
#define eccles_taskDelete()             (vTaskDelete(NULL))
#define eccles_createMsgQueue(num,size) (xQueueCreate(num,size))
#define eccles_deleteMsgQueue(queue)    (vQueueDelete(queue))
#define eccles_sendMsg(queue,msg)       (xQueueSend(queue,msg,portMAX_DELAY))
#define eccles_readMsg(queue,msg)       (xQueueReceive(queue,msg,portMAX_DELAY))
#define eccles_clearMsg(msg)            (xQueueReset(msg))
#define eccles_waitMicro(value)         (esp_rom_delay_us(value))
#define eccles_hasMsg(queue)            (uxQueueMessagesWaiting(queue))
#define eccles_installWDT(time)         (Eccles::eccles_wdtInit((time)))
#define eccles_wdtReset()               (esp_task_wdt_reset())
#define eccles_wdtInclude(task)         (esp_task_wdt_add((task) == NULL ? xTaskGetCurrentTaskHandle() : (TaskHandle_t)(task)))
#define eccles_deleteWDT(task)          (esp_task_wdt_delete((task) == NULL ? xTaskGetCurrentTaskHandle() : (TaskHandle_t)(task)))
#define eccles_lock(mux,t)              (xSemaphoreTake(mux,t))
#define eccles_unlock(mux)              (xSemaphoreGive(mux))
#define eccles_createLock()             (xSemaphoreCreateMutex())
#define eccles_startLog(br)             (Eccles::eccles_uartInit((br)))
#define eccles_mutex                    SemaphoreHandle_t

//arduino's HIGH/LOW kept for the one call site in Devices.cpp that uses them directly
#define HIGH 1
#define LOW  0

ECCLES_API {

using e_uint   = unsigned int;
using e_int    = int;
using e_uint8  = uint8_t;
using e_int8   = int8_t;
using e_uint16 = uint16_t;
using e_uint32 = uint32_t;
using e_float  = float;
using e_boolean= bool;
using e_uint64 = uint64_t;
using e_string = const char*;
using e_char   = char;

#define e_true  true
#define e_false false

enum class State { OFF,ON,UNKNOWN };
constexpr State ON      = State::ON;
constexpr State OFF     = State::OFF;
constexpr State UNKNOWN = State::UNKNOWN;

struct GLOBAL_STATE {
  State    IGNITION_STATE = OFF;
  State    AUDIBLE        = ON;
  e_uint8  TEMP_DATA      = 0;
  e_uint8  FUEL_DATA      = 0;
  State    ENGINE_LOCK    = OFF;
  State    ENGINE_RUNNING = OFF;
  e_float  VOLTAGE_LEVEL  = 0.0f;
  e_uint8  SHOCK_DATA     = 0;
  e_boolean EXECUTION_MODE= true;
};

extern GLOBAL_STATE globalState;

e_boolean eccles_compareString(e_string a,e_string b);

constexpr e_uint32 eccles_hashCT(e_string s,e_uint32 h = 5381){
  return (*s == 0) ? h : eccles_hashCT(s+1,((h<<5)+h)+(e_uint8)(*s));
}
const e_uint32 eccles_hashRT(e_string s);

//--- analog input ---
e_uint16 eccles_analogReadRaw(e_uint8 pin);
inline e_uint8 eccles_analogRead8(e_uint8 pin){
  return (e_uint8)(((e_uint32)eccles_analogReadRaw(pin)*255U)/4095U);
}

//--- pwm / watchdog ---
void eccles_analogWrite(e_uint8 pin,e_uint8 value);
void eccles_wdtInit(e_uint32 timeoutSeconds);

//--- uart logging ---
//ECCLES_LOG(arg) accepts e_string, numeric types via the overloads below — no heap involved
void eccles_uartInit(e_uint32 baud);
void eccles_printRaw(e_string s);
void eccles_printLine(e_string s);
inline void eccles_printRaw(e_uint32 v){ e_char b[16]; snprintf(b,sizeof(b),"%lu",(unsigned long)v); eccles_printRaw(b); }
inline void eccles_printLine(e_uint32 v){ e_char b[16]; snprintf(b,sizeof(b),"%lu",(unsigned long)v); eccles_printLine(b); }
inline void eccles_printRaw(e_int v)   { e_char b[16]; snprintf(b,sizeof(b),"%d",v); eccles_printRaw(b); }
inline void eccles_printLine(e_int v)  { e_char b[16]; snprintf(b,sizeof(b),"%d",v); eccles_printLine(b); }
inline void eccles_printRaw(e_float v) { e_char b[32]; snprintf(b,sizeof(b),"%f",v); eccles_printRaw(b); }
inline void eccles_printLine(e_float v){ e_char b[32]; snprintf(b,sizeof(b),"%f",v); eccles_printLine(b); }

//millis() replacement — esp_timer_get_time() returns microseconds, divide to milliseconds
inline e_uint32 eccles_millis(){ return (e_uint32)(esp_timer_get_time()/1000); }

//--- fixed-size stack string — replaces Eccles::String / e_stringa / std::string ---
//zero heap, backed by a char[N] array on the stack or in BSS, N includes the null terminator
//only the operations actually used across this codebase are implemented
template<e_uint8 N = 64>
class FixedStr {
  e_char buf[N];
  e_uint8 len = 0;
public:
  FixedStr(){ buf[0] = '\0'; }
  FixedStr(e_string s){ assign(s); }

  void assign(e_string s){
    if(!s){ buf[0]='\0'; len=0; return; }
    len = 0;
    while(len < (e_uint8)(N-1) && s[len]) { buf[len]=s[len]; len++; }
    buf[len] = '\0';
  }

  FixedStr& operator+=(e_string s){
    if(!s) return *this;
    e_uint8 i = 0;
    while(len < (e_uint8)(N-1) && s[i]) buf[len++] = s[i++];
    buf[len] = '\0';
    return *this;
  }

  e_string  c_str()   const { return buf; }
  e_uint8   length()  const { return len; }
  e_boolean empty()   const { return len == 0; }
  e_boolean operator==(e_string s) const { return eccles_compareString(buf, s ? s : ""); }
  e_boolean operator!=(e_string s) const { return !(*this == s); }
};

//e_stringa: fixed 64 byte string for all call sites that used arduino's String or std::string
//using an alias avoids changing any of those call sites
using e_stringa = FixedStr<64>;

};

#endif
