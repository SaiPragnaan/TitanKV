#include "titankv/engine.h"
#include "titankv/server.h"
#include <csignal>
#include <iostream>

int main(int argc, char** argv) {
  const std::filesystem::path dir=argc>1?argv[1]:"./titankv-data"; const auto port=static_cast<std::uint16_t>(argc>2?std::stoi(argv[2]):7379);
  try { auto engine=std::make_shared<titankv::Engine>(titankv::Options{.data_dir=dir}); titankv::Server server(engine,port,std::thread::hardware_concurrency()); std::cout<<"TitanKV listening on "<<port<<"; data="<<dir<<'\n'; server.run(); } catch(const std::exception& e) { std::cerr<<"fatal: "<<e.what()<<'\n';return 1; } return 0;
}
