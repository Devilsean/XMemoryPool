#include "../include/MemoryPool.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

using namespace XmemoryPool;
using namespace std::chrono;

// 计时器类（增强型）
class Timer {
  high_resolution_clock::time_point start;

public:
  Timer() : start(high_resolution_clock::now()) {}
  void reset() { start = high_resolution_clock::now(); }
  double elapsed_ms() const {
    return duration_cast<microseconds>(high_resolution_clock::now() - start)
               .count() /
           1000.0;
  }
};

class PerformanceTest {
public:
  // 格式化输出表头
  static void printHeader(const std::string &testName) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << " Test: " << testName << "\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Allocator" << std::setw(15)
              << "Time(ms)" << std::setw(20) << "Throughput(ops/s)"
              << "\n";
  }

  // 系统预热
  static void warmup() {
    std::cout << "Warming up systems... ";
    std::vector<std::pair<void *, size_t>> ptrs;
    ptrs.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
      size_t s = (i % 128) + 8;
      ptrs.push_back({MemoryPool::allocate(s), s});
    }
    for (auto &p : ptrs)
      MemoryPool::deallocate(p.first, p.second);
    std::cout << "Done.\n";
  }

  // 单线程小对象压力测试
  static void testSmallAllocation() {
    constexpr size_t NUM_ALLOCS = 500000; // 增加样本量
    constexpr size_t SMALL_SIZE = 32;
    printHeader("Small Allocation (Fixed 32B)");

    auto runTest = [&](auto allocFunc, auto deallocFunc,
                       const std::string &name) {
      std::vector<void *> ptrs;
      ptrs.reserve(NUM_ALLOCS);
      Timer t;
      for (size_t i = 0; i < NUM_ALLOCS; ++i) {
        ptrs.push_back(allocFunc(SMALL_SIZE));
        if (i % 4 == 0) {
          deallocFunc(ptrs.back(), SMALL_SIZE);
          ptrs.pop_back();
        }
      }
      for (void *p : ptrs)
        deallocFunc(p, SMALL_SIZE);

      double ms = t.elapsed_ms();
      double ops = (NUM_ALLOCS / ms) * 1000.0;
      std::cout << std::left << std::setw(20) << name << std::setw(15)
                << std::fixed << std::setprecision(2) << ms << std::setw(20)
                << std::scientific << ops << "\n";
    };

    runTest(MemoryPool::allocate, MemoryPool::deallocate, "Memory Pool");
    runTest([](size_t s) { return ::operator new(s); },
            [](void *p, size_t) { ::operator delete(p); }, "System Malloc");
  }

  // 多线程高并发测试（修正了随机数瓶颈）
  static void testMultiThreaded() {
    constexpr size_t NUM_THREADS = 32;
    constexpr size_t ALLOCS_PER_THREAD = 100000;
    printHeader("Multi-threaded (Mixed 8-512B)");

    auto threadTask = [](bool usePool) {
      // 每个线程独立的随机数引擎，避免全局锁
      std::mt19937 gen(
          std::hash<std::thread::id>{}(std::this_thread::get_id()));
      std::uniform_int_distribution<size_t> sizeDist(8, 512);
      std::uniform_int_distribution<int> actionDist(0, 100);

      std::vector<std::pair<void *, size_t>> ptrs;
      ptrs.reserve(ALLOCS_PER_THREAD);

      for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i) {
        size_t s = sizeDist(gen);
        void *p = usePool ? MemoryPool::allocate(s) : ::operator new(s);
        ptrs.push_back({p, s});

        if (actionDist(gen) < 70) { // 70% 几率即时释放
          size_t idx = gen() % ptrs.size();
          if (usePool)
            MemoryPool::deallocate(ptrs[idx].first, ptrs[idx].second);
          else
            ::operator delete(ptrs[idx].first);
          ptrs[idx] = ptrs.back();
          ptrs.pop_back();
        }
      }
      for (auto &p : ptrs) {
        if (usePool)
          MemoryPool::deallocate(p.first, p.second);
        else
          ::operator delete(p.first);
      }
    };

    auto runMulti = [&](bool usePool, const std::string &name) {
      std::vector<std::thread> workers;
      Timer t;
      for (size_t i = 0; i < NUM_THREADS; ++i)
        workers.emplace_back(threadTask, usePool);
      for (auto &w : workers)
        w.join();

      double ms = t.elapsed_ms();
      double totalOps = (NUM_THREADS * ALLOCS_PER_THREAD / ms) * 1000.0;
      std::cout << std::left << std::setw(20) << name << std::setw(15)
                << std::fixed << std::setprecision(2) << ms << std::scientific
                << totalOps << "\n";
    };

    runMulti(true, "Memory Pool");
    runMulti(false, "System Malloc");
  }

  // 4. 单线程混合大小测试 (补全)
  static void testMixedSizes() {
    constexpr size_t NUM_ALLOCS = 300000;
    const size_t SIZES[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    printHeader("Single-threaded Mixed Sizes (16B-2KB)");

    auto runMixed = [&](auto allocFunc, auto deallocFunc,
                        const std::string &name) {
      std::vector<std::pair<void *, size_t>> ptrs;
      ptrs.reserve(NUM_ALLOCS);
      std::mt19937 gen(42);
      Timer t;

      for (size_t i = 0; i < NUM_ALLOCS; ++i) {
        size_t s = SIZES[gen() % 8];
        ptrs.push_back({allocFunc(s), s});
        if (i % 10 == 0 && !ptrs.empty()) {
          deallocFunc(ptrs.back().first, ptrs.back().second);
          ptrs.pop_back();
        }
      }
      for (auto &p : ptrs)
        deallocFunc(p.first, p.second);

      double ms = t.elapsed_ms();
      double ops = (NUM_ALLOCS / ms) * 1000.0;
      std::cout << std::left << std::setw(20) << name << std::setw(15)
                << std::fixed << std::setprecision(2) << ms << std::scientific
                << ops << "\n";
    };

    runMixed(MemoryPool::allocate, MemoryPool::deallocate, "Memory Pool");
    runMixed([](size_t s) { return ::operator new(s); },
             [](void *p, size_t) { ::operator delete(p); }, "System Malloc");
  }
};

int main() {
  std::cout << "--- XmemoryPool Benchmark Suite ---" << std::endl;
  PerformanceTest::warmup();
  PerformanceTest::testSmallAllocation();
  PerformanceTest::testMultiThreaded();
  PerformanceTest::testMixedSizes();
  std::cout << "\n" << std::string(60, '=') << "\n";
  return 0;
}