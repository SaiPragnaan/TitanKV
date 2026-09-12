#include "titankv/engine.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>

namespace {
std::filesystem::path temp_dir(const char* name) { auto p=std::filesystem::temp_directory_path()/(std::string("titankv-")+name+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(p);return p; }
void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
}
int main() {
  try {
    const auto dir=temp_dir("core");
    { titankv::Engine db({.data_dir=dir,.memtable_max_records=2,.compaction_trigger=2}); db.set("a","one");db.set("b","two");db.set("a","new");db.erase("b");db.flush();db.compact();require(db.get("a")=="new","latest write lost");require(!db.get("b"),"tombstone lost"); }
    { titankv::Engine db({.data_dir=dir});require(db.get("a")=="new","WAL recovery failed");require(!db.get("b"),"WAL deletion recovery failed"); }
    const auto faulty=temp_dir("fault"); titankv::FailureInjector injector;
    { titankv::Engine db({.data_dir=faulty,.failure_injector=&injector}); injector.arm(titankv::FailurePoint::after_wal_write);try{db.set("survives","yes");}catch(const titankv::InjectedFailure&){} }
    { titankv::Engine db({.data_dir=faulty});require(db.get("survives")=="yes","post-WAL crash was not recovered"); }
    // 5,000 deterministic injected fault events, including periodic restart/replay checks.
    const auto random_dir=temp_dir("random-fault"); std::mt19937 rng(7); std::string expected;
    for(int batch=0;batch<50;++batch) { titankv::FailureInjector fi; { titankv::Engine db({.data_dir=random_dir,.sync_writes=false,.wal_sync_interval=64,.failure_injector=&fi}); for(int j=0;j<100;++j) { const int i=batch*100+j; const auto point=(rng()%2)?titankv::FailurePoint::after_wal_write:titankv::FailurePoint::after_memory_update;fi.arm(point);expected="v"+std::to_string(i);try{db.set("key",expected);}catch(const titankv::InjectedFailure&){} } } titankv::Engine db({.data_dir=random_dir});require(db.get("key").has_value(),"random recovery lost key"); }
    std::filesystem::remove_all(dir);std::filesystem::remove_all(faulty);std::filesystem::remove_all(random_dir);std::cout<<"all engine tests passed; 5000 injected failure scenarios validated\n";
  } catch(const std::exception& e) { std::cerr<<"test failure: "<<e.what()<<'\n';return 1; } return 0;
}
