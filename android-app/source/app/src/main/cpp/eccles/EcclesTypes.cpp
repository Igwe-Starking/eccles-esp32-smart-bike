//implements the logging primitives declared in EcclesTypes.h.
//on the ESP32 these write to UART; here they go to logcat under the "Eccles" tag so
//on-device and phone-side logs read the same way during a live debugging session.

#include "EcclesTypes.h"
#include <android/log.h>

#define ECCLES_LOG_TAG "Eccles"

ECCLES_API {

  void eccles_printRaw(e_string s) {
    __android_log_print(ANDROID_LOG_DEBUG, ECCLES_LOG_TAG, "%s", s ? s : "");
  }

  void eccles_printLine(e_string s) {
    __android_log_print(ANDROID_LOG_DEBUG, ECCLES_LOG_TAG, "%s", s ? s : "");
  }

};
