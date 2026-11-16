// INPUT: Existing Mali counter device and explicit sample count/interval.
// OUTPUT: All supported counters as complete, numbered CSV sample sets.
// POS: Read-only profiling of all GPU clients; no inputs or clock writes.
#include <hwcpipe/counter_database.hpp>
#include <hwcpipe/gpu.hpp>
#include <hwcpipe/sampler.hpp>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <cerrno>
#include <iomanip>
#include <unistd.h>
static int number(const char *text,int low,int high) {
 char *end=nullptr;errno=0;long value=std::strtol(text,&end,10);
 if(errno||!text[0]||*end||value<low||value>high)return -1;
 return static_cast<int>(value);
}
int main(int argc,char **argv) {
 int count=argc>1?number(argv[1],1,120):30,ms=argc>2?number(argv[2],100,5000):1000;
 if(argc>3||count<0||ms<0)return 2;
 std::cout<<std::setprecision(17);
 auto gpu=hwcpipe::gpu(0);if(!gpu){std::cerr<<"gpu probe failed\n";return 3;}
 std::cout<<"gpu,cores,"<<gpu.num_shader_cores()<<",bus_width,"<<gpu.bus_width()<<"\n";
 hwcpipe::counter_database db;hwcpipe::sampler_config config(gpu);
 std::vector<hwcpipe_counter> counters;
 for(auto c:db.counters_for_gpu(gpu)) {
  auto ec=config.add_counter(c);if(!ec)counters.push_back(c);
 }
 hwcpipe::sampler<> sampler(config);auto ec=sampler.start_sampling();
 if(ec){std::cerr<<"sampling start: "<<ec.message()<<"\n";return 4;}
 std::cout<<"sample,counter_id,name,value\n";
 for(int i=0;i<count;i++) {
  usleep(ms*1000);ec=sampler.sample_now();
  if(ec){std::cerr<<"sample: "<<ec.message()<<"\n";return 5;}
  for(auto c:counters) {
   hwcpipe::counter_metadata meta;hwcpipe::counter_sample s;
   if(db.describe_counter(c,meta)||sampler.get_counter_value(c,s))continue;
   std::cout<<i<<","<<static_cast<unsigned>(c)<<",\""<<meta.name<<"\",";
   if(s.type==hwcpipe::counter_sample::type::uint64)std::cout<<s.value.uint64;
   else std::cout<<s.value.float64;
   std::cout<<"\n";
  }
  std::cout.flush();
 }
 ec=sampler.stop_sampling();return ec?6:0;
}
