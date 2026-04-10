# XMemoryPool

一个基于 **C++11** 实现的内存分配器，采用 **三级缓存架构**：`ThreadCache -> CentralCache -> PageCache`。

## 核心架构

1. **ThreadCache**：线程本地缓存。使用 `thread_local` 实现每线程独享，小对象分配无需锁；超过 256KB 的对象直接走 `PageCache`。
2. **CentralCache**：中心缓存。按大小类维护共享自由链表，负责与 `ThreadCache` 做批量搬运。
3. **PageCache**：页级别管理。使用 `std::map<size_t, Span*>` 按页号维护 Span 索引，负责与系统交互 (`mmap/munmap`)，并在页级做相邻 Span 合并。

---
## 技术亮点

### 1. 优化策略

- **TLS (Thread Local Storage)**：使用 `thread_local` 关键字实现每线程独立的 ThreadCache 实例，小对象命中时完全无锁。
- **批量搬运**：ThreadCache 和 CentralCache 之间按大小类批量拉取、批量归还，减少共享层交互次数。
- **非线性 Size Class**：小对象粒度更细，大对象粒度更粗，避免 8B 线性分桶造成过多桶位和管理开销。
- **页对齐系统分配**：PageCache 统一使用 `mmap/munmap` 管理页级内存，避免分配与释放接口不匹配。

### 2. Span 合并策略

- PageCache 维护按起始页号排序的 Span 索引（`std::map<size_t, Span*>`）。
- 在 `dealloc` 时检查前后相邻空闲 Span，在页级完成合并，减少外部碎片。

---
## 性能基准 

以下是在 16 线程高并发压力下运行12次的平均测试结果（数据源自 perf_test.cpp）：

最后一次测试截图：

![perf_test.jpg](./perf_test.jpg)

平均测试数据：

| 测试项目           | Memory Pool (平均耗时) | 系统 Malloc (平均耗时) | 性能提升 (%) |
| -------------- | ------------------ | ---------------- | -------- |
| 小对象分配 (8B)    | 14.36 ms           | 15.53 ms         | +7.51%   |
| 多线程并发 (16 线程)  | 76.17 ms           | 157.12 ms        | +51.52%  |
| 混合尺寸分配 (10W 次) | 124.75 ms          | 195.89 ms        | +36.31%  |

> 注：多线程测试结果受 CPU 核心数、锁竞争影响较大。实际性能取决于硬件环境和工作负载模式。

---
## 构建与运行

### 环境要求

* CMake 3.10+

* C++17 编译器 (GCC/Clang)

* Linux 系统 (支持 pthread & mmap)
### 编译步骤

```bash

mkdir build && cd build

cmake ..

make

```
### 使用

```c++

#include "MemoryPool.h"

using namespace XmemoryPool;

  

// 像使用 malloc/free 一样简单

void* ptr = MemoryPool::allocate(64);

MemoryPool::deallocate(ptr,64);

```

---

## 致谢与参考

* **[youngyangyang04/memory-pool](https://github.com/youngyangyang04/memory-pool)**

* **[Google TCMalloc](https://github.com/google/tcmalloc)**