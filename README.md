# XMemoryPool 

一个基于 **C++11** 实现的高性能、高并发内存分配器，采用经典的 **TCMalloc 三级缓存架构**，并在底层引入 **基数树 (Radix Tree)** 极速映射逻辑。

## 核心架构

本项目通过三级缓存机制，有效解决了多线程环境下原生 `malloc/free` 的全局锁竞争瓶颈：

1. **ThreadCache**: 线程本地缓存。每个线程独享，分配内存时 **无需加锁**，是极速分配的核心。
2. **CentralCache**: 中心缓存。通过 **桶锁 (Bucket Lock)** 机制减少竞争，负责向 ThreadCache 提供批量内存块。
3. **PageCache**: 页级别管理。负责与系统交互 (`mmap`)，并支持 **Span 内存合并** 以减少外部碎片。

---

## 技术亮点

### 1. 基数树 (Radix Tree) 极速映射
本项目在 `PageCache` 层引入了两级/三级 **基数树** 替代传统的 `std::map`：
* **O(1) 查询**: 实现 `PageID -> Span*` 的极速定位，查询开销不随内存规模增长。
* **无锁读取**: 在内存回收路径上，基数树支持天然的并发读取，彻底消灭了回收时的查询锁竞争。

### 2. 智能 Span 合并策略
* 利用基数树轻松获取相邻页面的 `Span` 状态。
* 在 `dealloc` 时自动触发向前、向后合并，将碎片化的页重新组合成大块连续内存，提升内存利用率。

### 3. 高并发性能优化
* **分段锁技术**: CentralCache 针对不同规格的内存桶独立加锁。
* **TLS (Thread Local Storage)**: 完美实现线程隔离，确保 ThreadCache 的零竞争访问。

---

## 性能基准 (Benchmark)

以下是在 16 线程高并发压力下的测试结果（数据源自 `perf_test.cpp`）：

![性能测试截图](perf_test.jpg)


| 测试项目 (16 线程/混合场景) | 原生 `new/delete` | **XMemoryPool** | 性能提升 |
| :--- | :--- | :--- | :--- |
| **单线程极致分配** (20w 次, 32B) | 9.443 ms | **6.376 ms** | **+32.5%** |
| **多线程高并发** (16 线程并发) | 151.726 ms | **96.061 ms** | **+36.7%** |
| **混合大小分配** (10w 次) | 36.239 ms | **24.911 ms** | **+31.3%** |


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