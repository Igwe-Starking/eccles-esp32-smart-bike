/*
  native-lib.cpp
  ----------------
  JNI surface for starking.eccles.smartbike.CommandBridge. Kept deliberately thin: every
  function here just marshals JNI types to/from Eccles:: types and calls straight into
  Engine (see eccles/Engine.h) - no logic lives in this file.
*/

#include <jni.h>

#include <string>
#include <thread>

#include "eccles/Eccles.h"

namespace {
std::thread gEngineThread;

std::string jstringToStd(JNIEnv* env, jstring value) {
  if (value == nullptr) return "";
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string result(chars ? chars : "");
  env->ReleaseStringUTFChars(value, chars);
  return result;
}
}  // namespace

extern "C" {

// void CommandBridge.initEngine(String firmwareIp)
// Launches eccles_main() on its own thread.
JNIEXPORT void JNICALL
Java_starking_eccles_smartbike_CommandBridge_initEngine(JNIEnv* env, jclass, jstring firmwareIp) {
  const std::string ip = jstringToStd(env, firmwareIp);
  if (gEngineThread.joinable()) gEngineThread.join(); // safety: clean up any previous session
  gEngineThread = std::thread([ip] { Eccles::eccles_main(ip); });
}

// void CommandBridge.send(int commandType, byte[] utf8Text)
// Pushes a Java-originated command (voice, typed, or button-synthesized text - all funnel
// through the same path) onto the native inbound queue.
JNIEXPORT void JNICALL
Java_starking_eccles_smartbike_CommandBridge_send(JNIEnv* env, jclass, jint commandType, jbyteArray utf8Text) {
  (void)commandType; // only CommandType.TEXT_BINARY exists today; reserved for future kinds
  if (utf8Text == nullptr) return;
  jsize len = env->GetArrayLength(utf8Text);
  std::string text(static_cast<size_t>(len), '\0');
  env->GetByteArrayRegion(utf8Text, 0, len, reinterpret_cast<jbyte*>(text.data()));
  Eccles::Engine::instance().submitText(text);
}

// NativeMessage CommandBridge.requestMessage()
// Blocks (on the calling Java thread - see MessageThread.java, which owns a dedicated
// background thread for exactly this reason) until Engine has something to report, then
// returns it as a small object Java can read straight off.
JNIEXPORT jobject JNICALL
Java_starking_eccles_smartbike_CommandBridge_requestMessage(JNIEnv* env, jclass) {
  Eccles::OutboundMessage msg;
  if (!Eccles::Engine::instance().nextOutboundMessage(msg)) return nullptr; // engine stopped

  jclass cls = env->FindClass("starking/eccles/smartbike/NativeMessage");
  jmethodID ctor = env->GetMethodID(cls, "<init>", "(IIILjava/lang/String;III)V");
  jstring text = env->NewStringUTF(msg.text.c_str());
  jobject obj = env->NewObject(cls, ctor,
                                static_cast<jint>(msg.type),
                                static_cast<jint>(msg.device),
                                static_cast<jint>(msg.value),
                                text,
                                static_cast<jint>(msg.extra1),
                                static_cast<jint>(msg.extra2),
                                static_cast<jint>(msg.extra3));
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(text);
  return obj;
}

// void CommandBridge.stopEngine()
JNIEXPORT void JNICALL
Java_starking_eccles_smartbike_CommandBridge_stopEngine(JNIEnv*, jclass) {
  Eccles::Engine::instance().stop();
  if (gEngineThread.joinable()) gEngineThread.join();
}

}  // extern "C"
