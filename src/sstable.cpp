#include "titankv/sstable.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace titankv {
namespace {
constexpr std::uint32_t kMagic = 0x544b5653;  // TKVS
constexpr std::size_t kHeader = 4 + 8 + 1 + 4 + 4;
void put32(std::ostream& out, std::uint32_t v) { for (int i=0;i<4;++i) out.put(static_cast<char>(v >> (i*8))); }
void put64(std::ostream& out, std::uint64_t v) { for (int i=0;i<8;++i) out.put(static_cast<char>(v >> (i*8))); }
std::uint32_t get32(const char* p) { std::uint32_t n=0; for(int i=0;i<4;++i)n|=static_cast<std::uint32_t>(static_cast<unsigned char>(p[i]))<<(i*8); return n; }
std::uint64_t get64(const char* p) { std::uint64_t n=0; for(int i=0;i<8;++i)n|=static_cast<std::uint64_t>(static_cast<unsigned char>(p[i]))<<(i*8); return n; }
bool read_exact(std::istream& in, char* p, std::size_t n) { in.read(p, static_cast<std::streamsize>(n)); return in.gcount() == static_cast<std::streamsize>(n); }
Record read_record(std::istream& in) {
  std::array<char,kHeader> h{}; if (!read_exact(in,h.data(),h.size()) || get32(h.data()) != kMagic) throw std::runtime_error("corrupt SSTable");
  const auto ks=get32(h.data()+13), vs=get32(h.data()+17); if (ks>(1U<<26)||vs>(1U<<28)) throw std::runtime_error("invalid SSTable record");
  Record r{get64(h.data()+4),h[12]!=0,std::string(ks,'\0'),std::string(vs,'\0')}; if(!read_exact(in,r.key.data(),ks)||!read_exact(in,r.value.data(),vs)) throw std::runtime_error("truncated SSTable"); return r;
}
void write_record(std::ostream& out, const Record& r) { put32(out,kMagic);put64(out,r.sequence);out.put(r.tombstone?1:0);put32(out,static_cast<std::uint32_t>(r.key.size()));put32(out,static_cast<std::uint32_t>(r.value.size()));out.write(r.key.data(),static_cast<std::streamsize>(r.key.size()));out.write(r.value.data(),static_cast<std::streamsize>(r.value.size())); }
}  // namespace

SSTable::SSTable(std::filesystem::path path) : path_(std::move(path)) {
  std::ifstream in(path_, std::ios::binary); if (!in) throw std::runtime_error("cannot open SSTable");
  while (in.peek() != std::char_traits<char>::eof()) { const auto offset=static_cast<std::uint64_t>(in.tellg()); const auto r=read_record(in); index_.push_back({r.key,offset}); const auto h1=std::hash<std::string>{}(r.key),h2=std::hash<std::string>{}("#"+r.key); bloom_[(h1%(bloom_.size()*64))/64]|=std::uint64_t{1}<<(h1%64); bloom_[(h2%(bloom_.size()*64))/64]|=std::uint64_t{1}<<(h2%64); max_sequence_=std::max(max_sequence_,r.sequence); }
  if (!std::is_sorted(index_.begin(),index_.end(),[](const auto&a,const auto&b){return a.key<b.key;})) throw std::runtime_error("SSTable is not sorted");
}
void SSTable::write(const std::filesystem::path& path, const std::vector<Record>& records) {
  std::ofstream out(path,std::ios::binary|std::ios::trunc); if(!out) throw std::runtime_error("cannot write SSTable"); for(const auto&r:records) write_record(out,r); out.flush(); if(!out) throw std::runtime_error("SSTable write failed");
}
bool SSTable::may_contain(const std::string& key) const { const auto h1=std::hash<std::string>{}(key),h2=std::hash<std::string>{}("#"+key); return (bloom_[(h1%(bloom_.size()*64))/64]&(std::uint64_t{1}<<(h1%64))) && (bloom_[(h2%(bloom_.size()*64))/64]&(std::uint64_t{1}<<(h2%64))); }
std::optional<Record> SSTable::get(const std::string& key) const {
  if (!may_contain(key)) return std::nullopt;
  const auto it = std::lower_bound(index_.begin(), index_.end(), key, [](const IndexEntry& a, const std::string& b) { return a.key < b; });
  if (it == index_.end() || it->key != key) return std::nullopt;
  std::ifstream in(path_,std::ios::binary); in.seekg(static_cast<std::streamoff>(it->offset)); return read_record(in);
}
std::vector<Record> SSTable::all() const { std::vector<Record> result; result.reserve(index_.size()); std::ifstream in(path_,std::ios::binary); while(in.peek()!=std::char_traits<char>::eof()) result.push_back(read_record(in)); return result; }
}  // namespace titankv
