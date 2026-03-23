#include "../include/CentralCache.h"
#include "../include/PageCache.h"

#include <thread>

namespace XmemoryPool {
static const size_t SPAN_PAGES = 8;
CentralCache::CentralCache() {
  for (auto &ptr : centralFreeList) {
    ptr.store(nullptr, std::memory_order_relaxed);
  }
  // 互斥锁无需额外初始化
}
// 线程缓存从中心缓存获取内存块
void *CentralCache::fetchRange(size_t index, size_t batchNum) {
  if (batchNum == 0)
    return nullptr;
  // 获取对应的桶锁
  std::unique_lock<std::mutex> lock(getMutexForIndex(index));

  // 桶里有现成的内存块
  void *result = centralFreeList[index].load(std::memory_order_relaxed);
  if (result) {
    void *current = result;
    void *prev = nullptr;
    size_t count = 0;

    // 尝试取走 batchNum 个块
    while (current && count < batchNum) {
      prev = current;
      current = *(reinterpret_cast<void **>(current));
      count++;
    }

    // 断开取走的这部分链表，并更新桶头
    if (prev)
      *(reinterpret_cast<void **>(prev)) = nullptr;
    centralFreeList[index].store(current, std::memory_order_release);
    return result;
  }

  // 桶里没内存了，去 PageCache 批发
  // 释放桶锁，允许其他线程在此时归还内存到这个桶，不阻塞并发
  lock.unlock();

  size_t size = (index + 1) * ALIGNMENT;
  size_t totalSize = size * batchNum;
  size_t numPages =
      (totalSize + PageCache::PAGE_SIZE - 1) >> PageCache::PAGE_SHIFT;

  // 如果算出来不足 8 页，我们按 8 页批（小对象多批点，提高效率）
  if (numPages < 8)
    numPages = 8;
  // fetchFromPageCache 内部会加 PageCache 的全局大锁
  void *spanStart = fetchFromPageCache(numPages * PageCache::PAGE_SIZE);
  if (!spanStart)
    return nullptr;

  // 在锁外执行耗时的切分逻辑
  size_t totalBlocks = (numPages * PageCache::PAGE_SIZE) / size;
  if (totalBlocks == 0)
    return nullptr;

  // 在局部变量里预先连好整条链表（不占锁，多线程并行切分）
  char *cur = static_cast<char *>(spanStart);
  // 锁外切分：一定要保证哪怕只有 1 个块，逻辑也闭环
  char *next = nullptr;
  for (size_t i = 0; i < totalBlocks - 1; ++i) {
    next = cur + size;
    *(reinterpret_cast<void **>(cur)) = next;
    cur = next;
  }
  *(reinterpret_cast<void **>(cur)) = nullptr; // 最后一个块封口

  lock.lock();

  // 分配逻辑简化
  void *actualResult = spanStart;
  void *remainStart = nullptr;
  // 实际分配的块数：取 min，防止 PageCache 给的内存太多
  size_t actualBatch = std::min(batchNum, totalBlocks);

  if (actualBatch >= totalBlocks) {
    // PageCache 给的还没 ThreadCache 想要的多，全给它
    remainStart = nullptr;
  } else {
    // 多余的，需要切分
    void *actualReturnEnd = spanStart;
    for (size_t i = 0; i < actualBatch - 1; ++i) {
      actualReturnEnd = *(reinterpret_cast<void **>(actualReturnEnd));
    }
    remainStart = *(reinterpret_cast<void **>(actualReturnEnd));
    *(reinterpret_cast<void **>(actualReturnEnd)) = nullptr; // 断开
  }

  // 挂回剩余部分：直接连在当前桶的最前面
  if (remainStart) {
    void *oldHead = centralFreeList[index].load(std::memory_order_relaxed);
    // 这里要注意：remainStart 链表的末尾是之前的 cur，它已经封口了
    // 如果要把 remainStart 这一串挂上去，应该是：
    // 把 cur 指向 oldHead，把 remainStart 设为新 head
    *(reinterpret_cast<void **>(cur)) = oldHead;
    centralFreeList[index].store(remainStart, std::memory_order_release);
  }

  return actualResult;
}

// 将内存块归还到中心缓存
void CentralCache::returnRange(void *start, void *end, size_t index) {
  if (!start || !end)
    return;

  // 获取桶锁（使用alignas(64) 解决虚假竞争）
  std::lock_guard<std::mutex> lock(getMutexForIndex(index));

  // O(1) 挂载逻辑
  // 先取出桶里原来的头节点
  void *oldHead = centralFreeList[index].load(std::memory_order_relaxed);

  // 把归还链表的最后一个节点的 next 指向原来的头
  *(reinterpret_cast<void **>(end)) = oldHead;

  // 更新桶的头节点为归还链表的起始点
  // 使用 release 语义确保之前的写入对其他线程可见
  centralFreeList[index].store(start, std::memory_order_release);
}

void *CentralCache::fetchFromPageCache(size_t size) {
  // 使用位运算代替除法，效率更高
  size_t numPages = (size + PageCache::PAGE_SIZE - 1) >> PageCache::PAGE_SHIFT;

  // 预先确定申请页数，逻辑更清晰
  size_t applyPages = (numPages > SPAN_PAGES) ? numPages : SPAN_PAGES;

  // 统一调用接口
  Span *span = PageCache::getInstance().allocSpan(applyPages);

  if (span) {
    return (void *)(span->pageId << PageCache::PAGE_SHIFT);
  }

  return nullptr;
}
} // namespace XmemoryPool