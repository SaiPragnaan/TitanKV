#include "titankv/engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef TITANKV_BENCH_MODE
#define TITANKV_BENCH_MODE "write_test"
#endif
using Clock=std::chrono::steady_clock;
struct Result { double seconds{}; std::vector<double> us; };
static void report(const Result& r) { auto v=r.us;std::sort(v.begin(),v.end());auto q=[&](double p){return v[static_cast<std::size_t>((v.size()-1)*p)];};std::cout<<std::fixed<<std::setprecision(2)<<"ops="<<v.size()<<" ops/sec="<<v.size()/r.seconds<<" p50_us="<<q(.50)<<" p95_us="<<q(.95)<<" p99_us="<<q(.99)<<"\n"; }
int main() {
  constexpr int n=20000; const std::string mode=TITANKV_BENCH_MODE; auto dir=std::filesystem::temp_directory_path()/"titankv-benchmark";std::filesystem::remove_all(dir);
  titankv::Engine db({.data_dir=dir,.memtable_max_records=512,.compaction_trigger=4,.sync_writes=false,.wal_sync_interval=128});for(int i=0;i<n;++i)db.set("key"+std::to_string(i),"value"+std::to_string(i));
  if (mode == "recovery_test") {
    db.flush();
    db.compact();
    const auto start = Clock::now();
    titankv::Engine recovered({.data_dir=dir,.memtable_max_records=512,.sync_writes=false});
    for (int i = 0; i < n; ++i) [[maybe_unused]] auto value = recovered.get("key" + std::to_string(i));
    const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    std::cout << mode << " recovery_us=" << std::fixed << std::setprecision(2) << elapsed << " recovered_keys=" << n << "\n";
    std::filesystem::remove_all(dir);
    return 0;
  }
  Result r;auto begin=Clock::now();std::mutex lat_m;unsigned threads=mode=="concurrency_test"?8:1;std::vector<std::thread> workers;
  for(unsigned t=0;t<threads;++t)workers.emplace_back([&,t]{std::vector<double> local;for(int i=t;i<n;i+=threads){auto s=Clock::now();if(mode=="read_test") [[maybe_unused]] auto x=db.get("key"+std::to_string(i));else if(mode=="mixed_workload"){if(i%4==0)db.set("key"+std::to_string(i),"updated");else [[maybe_unused]] auto x=db.get("key"+std::to_string(i));}else db.set("new"+std::to_string(i),"payload");local.push_back(std::chrono::duration<double,std::micro>(Clock::now()-s).count());}std::lock_guard l(lat_m);r.us.insert(r.us.end(),local.begin(),local.end());});
  for (auto& w : workers) w.join();
  r.seconds = std::chrono::duration<double>(Clock::now() - begin).count();
  std::cout << mode << " ";
  report(r);
  db.flush();
  std::filesystem::remove_all(dir);
  return 0;
}
