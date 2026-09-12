#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>

// Phase-0 reference server: intentionally RAM-only and single-threaded.
int main(int argc, char** argv) {
  const auto port = static_cast<std::uint16_t>(argc > 1 ? std::stoi(argv[1]) : 7380);
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0); int yes = 1; ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(port);
  if (listener < 0 || ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(listener, 32) < 0) return 1;
  std::unordered_map<std::string, std::string> data; std::cout << "TitanKV baseline listening on " << port << '\n';
  for (;;) { const int client = ::accept(listener, nullptr, nullptr); if (client < 0) continue; std::string buffer; char chunk[1024]; while (const auto n = ::recv(client, chunk, sizeof(chunk), 0)) { if (n < 0) break; buffer.append(chunk, static_cast<std::size_t>(n)); std::size_t pos; while ((pos = buffer.find('\n')) != std::string::npos) { std::istringstream in(buffer.substr(0, pos)); buffer.erase(0, pos + 1); std::string op, key, value, reply; in >> op >> key; if (op == "SET") { std::getline(in, value); if (!value.empty() && value.front() == ' ') value.erase(0, 1); data[key] = value; reply = "+OK\n"; } else if (op == "GET") { const auto it = data.find(key); reply = it == data.end() ? "$-1\n" : "+" + it->second + "\n"; } else if (op == "DELETE") { data.erase(key); reply = "+OK\n"; } else reply = "-ERR unknown command\n"; ::send(client, reply.data(), reply.size(), MSG_NOSIGNAL); } } ::close(client); }
}
