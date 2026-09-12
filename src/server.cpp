#include "titankv/server.h"

#include <algorithm>
#include <arpa/inet.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace titankv {
namespace {
class Pool {
 public:
  explicit Pool(unsigned workers) { for (unsigned i = 0; i < std::max(1U, workers); ++i) workers_.emplace_back([this] { loop(); }); }
  ~Pool() { { std::lock_guard lock(mutex_); stopping_ = true; } ready_.notify_all(); for (auto& worker : workers_) worker.join(); }
  void submit(std::function<void()> task) { { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); } ready_.notify_one(); }
 private:
  void loop() { for (;;) { std::function<void()> task; { std::unique_lock lock(mutex_); ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); }); if (stopping_ && tasks_.empty()) return; task = std::move(tasks_.front()); tasks_.pop_front(); } task(); } }
  std::mutex mutex_; std::condition_variable ready_; std::deque<std::function<void()>> tasks_; bool stopping_{}; std::vector<std::thread> workers_;
};

std::string execute(Engine& engine, const std::string& line) {
  std::istringstream input(line); std::string operation, key; input >> operation;
  if (operation == "PING") return "+PONG\n";
  if (operation == "GET") { if (!(input >> key)) return "-ERR usage: GET key\n"; const auto value = engine.get(key); return value ? "+" + *value + "\n" : "$-1\n"; }
  if (operation == "SET") { if (!(input >> key)) return "-ERR usage: SET key value\n"; std::string value; std::getline(input, value); if (!value.empty() && value.front() == ' ') value.erase(0, 1); engine.set(key, value); return "+OK\n"; }
  if (operation == "DELETE") { if (!(input >> key)) return "-ERR usage: DELETE key\n"; engine.erase(key); return "+OK\n"; }
  if (operation == "STATS") return "+sequence=" + std::to_string(engine.sequence()) + " tables=" + std::to_string(engine.table_count()) + "\n";
  return "-ERR unknown command\n";
}
}  // namespace

struct Server::Impl {
  struct Connection {
    explicit Connection(int socket) : fd(socket) {}
    int fd;
    std::mutex mutex;
    std::deque<std::string> pending;
    bool processing{};
    bool closed{};
    std::string input;
  };
  Impl(std::shared_ptr<Engine> storage, std::uint16_t listen_port, unsigned workers) : engine(std::move(storage)), port(listen_port), pool(workers) {}
  std::shared_ptr<Engine> engine; std::uint16_t port; Pool pool; std::atomic<bool> stopping{}; int listener{-1}; int epoll{-1};
  std::unordered_map<int, std::shared_ptr<Connection>> connections;
};

Server::Server(std::shared_ptr<Engine> engine, std::uint16_t port, unsigned workers) : impl_(std::make_unique<Impl>(std::move(engine), port, workers)) {}
Server::~Server() { stop(); }
void Server::stop() { impl_->stopping = true; if (impl_->listener >= 0) ::shutdown(impl_->listener, SHUT_RDWR); }

void Server::run() {
  impl_->listener = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); if (impl_->listener < 0) throw std::runtime_error("socket failed");
  int yes = 1; ::setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)); sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(impl_->port);
  if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(impl_->listener, 256) < 0) throw std::runtime_error("bind/listen failed");
  impl_->epoll = ::epoll_create1(0); epoll_event listener_event{}; listener_event.events = EPOLLIN; listener_event.data.fd = impl_->listener; ::epoll_ctl(impl_->epoll, EPOLL_CTL_ADD, impl_->listener, &listener_event);
  auto process_connection = [state = impl_.get()](const std::shared_ptr<Impl::Connection>& connection) {
    for (;;) {
      std::string command;
      { std::lock_guard lock(connection->mutex); if (connection->closed || connection->pending.empty()) { connection->processing = false; return; } command = connection->pending.front(); connection->pending.pop_front(); }
      std::string reply; try { reply = execute(*state->engine, command); } catch (const std::exception& error) { reply = std::string("-ERR ") + error.what() + "\n"; }
      std::lock_guard lock(connection->mutex);
      if (!connection->closed) ::send(connection->fd, reply.data(), reply.size(), MSG_NOSIGNAL);
    }
  };
  std::vector<epoll_event> events(64);
  while (!impl_->stopping) {
    const auto event_count = ::epoll_wait(impl_->epoll, events.data(), static_cast<int>(events.size()), 250); if (event_count < 0) continue;
    for (int i = 0; i < event_count; ++i) {
      const int fd = events[i].data.fd;
      if (fd == impl_->listener) { for (;;) { const int client = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK); if (client < 0) break; impl_->connections.emplace(client, std::make_shared<Impl::Connection>(client)); epoll_event event{}; event.events = EPOLLIN | EPOLLRDHUP; event.data.fd = client; ::epoll_ctl(impl_->epoll, EPOLL_CTL_ADD, client, &event); } continue; }
      const auto found = impl_->connections.find(fd); if (found == impl_->connections.end()) continue; const auto connection = found->second;
      if (events[i].events & (EPOLLHUP | EPOLLRDHUP)) { { std::lock_guard lock(connection->mutex); connection->closed = true; connection->pending.clear(); } ::close(fd); impl_->connections.erase(found); continue; }
      char bytes[4096]; const auto received = ::recv(fd, bytes, sizeof(bytes), 0); if (received <= 0) { { std::lock_guard lock(connection->mutex); connection->closed = true; connection->pending.clear(); } ::close(fd); impl_->connections.erase(found); continue; }
      bool schedule = false;
      { std::lock_guard lock(connection->mutex); connection->input.append(bytes, static_cast<std::size_t>(received)); for (std::size_t newline; (newline = connection->input.find('\n')) != std::string::npos;) { connection->pending.push_back(connection->input.substr(0, newline)); connection->input.erase(0, newline + 1); } if (!connection->pending.empty() && !connection->processing) { connection->processing = true; schedule = true; } }
      if (schedule) impl_->pool.submit([connection, process_connection] { process_connection(connection); });
    }
  }
  for (auto& [fd, connection] : impl_->connections) { std::lock_guard lock(connection->mutex); connection->closed = true; ::close(fd); }
  impl_->connections.clear(); if (impl_->epoll >= 0) ::close(impl_->epoll); if (impl_->listener >= 0) ::close(impl_->listener); impl_->epoll = impl_->listener = -1;
}
}  // namespace titankv
