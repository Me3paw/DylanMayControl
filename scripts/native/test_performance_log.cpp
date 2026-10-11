#include "../../src/native/performance_log.h"
#include <cassert>
#include <thread>
#include <vector>
int main() {
    performance_log::Metric sampled;
    for (unsigned i=0;i<256;++i) if (sampled.admit(128)) sampled.record(i+1);
    assert(sampled.calls==256 && sampled.samples==2 && sampled.ticks==130 && sampled.maximum==129);
    performance_log::Metric concurrent;
    std::vector<std::thread> workers;
    for (unsigned i=1;i<=4;++i) workers.emplace_back([&,i](){for(unsigned j=0;j<10000;++j) if(concurrent.admit(1)) concurrent.record(i);});
    for (auto& worker:workers) worker.join();
    assert(concurrent.calls==40000 && concurrent.samples==40000 && concurrent.ticks==100000 && concurrent.maximum==4);
    assert(performance_log::stride(performance_log::Phase::GuardedRead)==128);
    assert(performance_log::stride(performance_log::Phase::Flush)==1);
}
