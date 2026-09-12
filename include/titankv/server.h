#pragma once

#include "titankv/engine.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace titankv {

class Server {
 public:
  Server(std::shared_ptr<Engine> engine, std::uint16_t port, unsigned workers);
  ~Server();
  void run();
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace titankv
