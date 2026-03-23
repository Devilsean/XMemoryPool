#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"

namespace XmemoryPool {
void *ThreadCache::allocate(size_t size) {
  // 快速路径：内联常见情况，减少分支
  if (size <= MAX_BYTES && size > 0) {
    // 获取索引 - 内联计算
    size_t index = ((size + ALIGNMENT - 1) >> ALIGN_SHIFT) - 1;

    // 快速路径：直接从本地缓存分配
    void *ptr = freeList[index];
    if (ptr) {
      freeList[index] = *(reinterpret_cast<void **>(ptr));
      freeListSize[index]--;
      return ptr;
    }
    // 慢速路径：从中心缓存获取
    return fetchFromCentralCache(index);
  }

  // 边界情况处理
  if (size == 0) {
    size = ALIGNMENT;
    void *ptr = freeList[0];
    if (ptr) {
      freeList[0] = *(reinterpret_cast<void **>(ptr));
      freeListSize[0]--;
      return ptr;
    }
    return fetchFromCentralCache(0);
  }

  // 大对象直接使用系统分配
  return malloc(size);
}

void ThreadCache::deallocate(void *ptr, size_t size) {
  // 大对象直接释放
  if (size > MAX_BYTES) {
    free(ptr);
    return;
  }

  // 统一获取索引
  size_t index = SizeClass::getIndex(size);

  // 链表插入逻辑
  *(reinterpret_cast<void **>(ptr)) = freeList[index];
  freeList[index] = ptr;

  size_t newSize = ++freeListSize[index];

  // 归还检查
  if (newSize >= maxSize[index] * 2) {
    returnToCentralCache(freeList[index], size);
  }
}

// bool ThreadCache::shouldReturnToCentralCache(size_t index) {
//   size_t size = (index + 1) * ALIGNMENT;
//   size_t threshold;

//   if (size <= 64)       // 小对象
//     threshold = 512;    // 从128大幅提高到512
//   else if (size <= 256) // 中等对象
//     threshold = 256;    // 从64提高到256
//   else                  // 较大对象
//     threshold = 128;    // 从32提高到128

//   return freeListSize[index] > threshold;
// }

void *ThreadCache::fetchFromCentralCache(size_t index) {
  // 计算字节数
  size_t size = (index + 1) * ALIGNMENT;
  // 计算批量数量
  size_t batchNum = maxSize[index];
  // 慢启动：每次Miss，下一次申请的数量就多一点
  if (maxSize[index] < getBatchNum(size)) {
    maxSize[index] += 2;
  }

  // 从中心缓存中获取内存块
  void *start = CentralCache::getInstance().fetchRange(index, batchNum);
  if (!start) {
    return nullptr;
  }

  // 取第一个返回，其余放入自由链表
  void *result = start;
  void *next = *(reinterpret_cast<void **>(start));

  if (next) {
    freeList[index] = next;
    freeListSize[index] = batchNum - 1;
  } else {
    freeListSize[index] = 0;
  }

  return result;
}

void ThreadCache::returnToCentralCache(void *start, size_t size) {
  if (!start)
    return;

  // 1. 基本参数计算
  size_t index = SizeClass::getIndex(size);
  size_t totalNum = freeListSize[index];

  // 如果数量太少（比如只有1个），没必要归还，直接挂回 ThreadCache 即可
  if (totalNum <= 1)
    return;

  size_t keepNum = totalNum / 2;
  if (keepNum == 0)
    keepNum = 1;
  size_t returnNum = totalNum - keepNum;

  // 2. 寻找 ThreadCache 留下的最后一个节点 (splitNode)
  // 这里的 start 就是当前 ThreadCache 链表的头
  void *splitNode = start;
  for (size_t i = 0; i < keepNum - 1 && splitNode != nullptr; ++i) {
    splitNode = *(reinterpret_cast<void **>(splitNode));
  }

  if (splitNode != nullptr) {
    // 3. 确定要归还给 CentralCache 的起始点 (actualReturnStart)
    void *actualReturnStart = *(reinterpret_cast<void **>(splitNode));

    if (actualReturnStart != nullptr) {
      // 4. 寻找归还链表的终点 (actualReturnEnd)
      // 这一步是为了让 CentralCache 能够 O(1) 挂载，不用再遍历一遍
      void *actualReturnEnd = actualReturnStart;
      for (size_t i = 0; i < returnNum - 1 && actualReturnEnd != nullptr; ++i) {
        actualReturnEnd = *(reinterpret_cast<void **>(actualReturnEnd));
      }

      // 5. 物理断开链表
      // splitNode 之后的部分被切断，归还给 CentralCache
      *(reinterpret_cast<void **>(splitNode)) = nullptr;

      // 6. 更新 ThreadCache 状态
      freeList[index] = start; // 剩下的前半部分
      freeListSize[index] = keepNum;

      // 7. 移交给 CentralCache
      // 加上 actualReturnEnd 提速
      CentralCache::getInstance().returnRange(actualReturnStart,
                                              actualReturnEnd, index);
    }
  }
}

// 计算获取批量数
// 修改ThreadCache::getBatchNum函数
size_t ThreadCache::getBatchNum(size_t size) {
  // 优化批量获取策略 - 减少从CentralCache获取的频率
  if (size <= 32)
    return 128;
  else if (size <= 64)
    return 64;
  else if (size <= 128)
    return 32;
  else if (size <= 256)
    return 16;
  else if (size <= 512)
    return 8;
  else if (size <= 1024)
    return 4;
  else
    return 2;
}
} // namespace XmemoryPool