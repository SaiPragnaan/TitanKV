#pragma once

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace titankv {
class IDisk { public: virtual ~IDisk() = default; virtual void before_write() = 0; };
class INetwork { public: virtual ~INetwork() = default; virtual void before_send() = 0; };
class IClock { public: virtual ~IClock() = default; virtual std::chrono::steady_clock::time_point now() = 0; };
class SystemClock final : public IClock { public: std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); } };

enum class FailurePoint { none, before_wal_write, after_wal_write, after_memory_update, disk_error, delayed_write, corrupt_wal };
class InjectedFailure : public std::runtime_error { public: explicit InjectedFailure(const std::string& what) : std::runtime_error(what) {} };
class FailureInjector final : public IDisk {
 public:
  void arm(FailurePoint point) { point_.store(point); }
  void before_write() override { trigger(FailurePoint::disk_error, "injected disk failure"); }
  void trigger(FailurePoint point, const char* message) {
    auto expected = point;
    if (point_.compare_exchange_strong(expected, FailurePoint::none)) {
      if (point == FailurePoint::delayed_write) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      else throw InjectedFailure(message);
    }
  }
 private: std::atomic<FailurePoint> point_{FailurePoint::none};
};
}  // namespace titankv
