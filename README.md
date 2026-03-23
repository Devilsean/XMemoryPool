# XMemoryPool 

一个基于 **C++11** 实现的高性能、高并发内存分配器，采用经典的 **TCMalloc 三级缓存架构**，并在底层引入 **基数树 (Radix Tree)** 极速映射逻辑。

## 核心架构

本项目通过三级缓存机制，有效解决了多线程环境下原生 `malloc/free` 的全局锁竞争瓶颈：

1. **ThreadCache**: 线程本地缓存。每个线程独享，分配内存时 **无需加锁**，是极速分配的核心。
2. **CentralCache**: 中心缓存。通过 **分段桶锁 (Bucket Lock)** 机制减少竞争，负责向 ThreadCache 提供批量内存块。
3. **PageCache**: 页级别管理。负责与系统交互 (`mmap`)，并支持 **Span 内存合并** 以减少外部碎片。

---

## 技术亮点

### 1. 基数树 (Radix Tree) 极速映射
本项目在 `PageCache` 层引入了两级/三级 **基数树** 替代传统的 `std::map`：
* **O(1) 查询**: 实现 `PageID -> Span*` 的极速定位，查询开销不随内存规模增长。

* **无锁读取**: 在内存回收路径上，基数树支持天然的并发读取，彻底消灭了回收时的查询锁竞争。

### 2. 锁脱钩与高并发优化
* **分段锁与对齐**: CentralCache 采用分段锁，并使用 `alignas(64)` 消除 伪共享 (False Sharing) 导致的缓存一致性风暴。

* **锁脱钩技术**: 优化了 CentralCache 与 PageCache 的交互，在申请底层资源前释放桶锁，确保高并发下的极致稳定性，杜绝了阻塞蔓延。

* **TLS (Thread Local Storage)**: 完美实现线程隔离，确保 ThreadCache 的零竞争访问。

### 3. 自适应慢启动（Slow Start）
* **动态策略**: 引入类似 TCP 的慢启动机制，ThreadCache 根据申请频率动态调整批量获取的大小（`maxSize`）。

* **负载均衡**: 自动适应混合场景下的分配模式，有效降低了内存频繁在二三级缓存间搬运（Thrashing）带来的抖动。

### 4. 智能 Span 合并策略
* 利用基数树轻松获取相邻页面的 `Span` 状态。

* 在 `dealloc` 时自动触发向前、向后合并，将碎片化的页重新组合成大块连续内存，提升内存利用率。

---

## 性能基准 (Benchmark)

以下是在 16 线程高并发压力下运行6次的平均测试结果（数据源自 `perf_test.cpp`）：

最后一次测试截图：
![测试截图](perf_test.jpg)

平均测试数据：
|测试项目|Memory Pool (平均耗时)|New/Delete (平均耗时)|性能提升 (%)|倍速 (X)|
|---|---|---|---|---|
|小对象分配 (32B)|**6.08 ms**|11.21 ms|**45.76%**|
|多线程并发 (16 线程)|**110.12 ms**|137.19 ms|**19.73%**|
|混合尺寸分配 (10W 次)|**16.06 ms**|28.26 ms|**43.17%**|

---

## 构建与运行

### 环境要求
* CMake 3.10+
* C++11 编译器 (GCC/Clang)
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

本项目在开发过程中深受以下项目的启发，并在其基础上进行了重构与性能优化：

* **[youngyangyang04/memory-pool](https://github.com/youngyangyang04/memory-pool)**：本项目逻辑架构的基石。在学习其三级缓存思路的基础上，本项目引入了 **基数树 (Radix Tree)** 替代原版的 `std::map` 映射，并优化了 `PageCache` 的合并算法，使高并发下的查找效率从 $O(\log N)$ 进化为 $O(1)$。
* **Google TCMalloc**: 整个三级缓存设计思想的源泉。

在此向相关项目的开发者表示由衷的感谢！