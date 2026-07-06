/*
  MessageQueue.h
  ---------------
  The "dual std::queue between native and Java" called for in the integration spec: one
  instance carries Java -> native text commands, a second carries native -> Java UI/status
  updates. Blocking pop() is what lets Java's MessageThread just call requestMessage() in a
  tight loop without spinning - the JNI call parks on the native condition_variable until
  Engine actually has something to say.
*/

#ifndef ECCLES_MESSAGE_QUEUE_H
#define ECCLES_MESSAGE_QUEUE_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "EcclesTypes.h"

ECCLES_API {

template <typename T>
class MessageQueue {
 public:
  void push(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(item));
    }
    cv_.notify_one();
  }

  // Blocks until an item is available or `keepRunning` is cleared. Takes a *live* atomic
  // rather than a boolean snapshot so a stop() that flips it while this is already blocked
  // is actually noticed (checked whenever the wait wakes, at most every 250ms so shutdown
  // stays responsive even without a spurious wakeup). Returns false if it gave up empty.
  e_boolean pop(T& out, const std::atomic<bool>* keepRunning = nullptr) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (queue_.empty()) {
      if (keepRunning && !keepRunning->load()) return false;
      cv_.wait_for(lock, std::chrono::milliseconds(250));
      if (keepRunning && !keepRunning->load() && queue_.empty()) return false;
    }
    out = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  e_boolean tryPop(T& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void wakeAll() { cv_.notify_all(); }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<T> queue_;
};

};  // ECCLES_API

#endif  // ECCLES_MESSAGE_QUEUE_H
