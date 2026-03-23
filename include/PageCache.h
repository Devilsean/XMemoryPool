#pragma once
#include "RadixTree.h"
#include <mutex>

namespace XmemoryPool {
// Span 结构体建议挪到外部或保持在 PageCache 内，但需要包含双向链表指针
struct Span {
  size_t pageId;   // 起始页号 (addr >> 12)
  size_t numPages; // 页数

  Span *prev = nullptr;
  Span *next = nullptr;

  bool inUse = false; // 是否正在被 CentralCache 使用
};

// SpanList 管理某一类页数的双向循环链表
// 循环：消除边界条件判断，简化代码（还可以O(1)时间定位尾部）
class SpanList {
public:
  SpanList() {
    head = (Span *)std::malloc(sizeof(Span));
    head->next = head;
    head->prev = head;
  }

  ~SpanList() { std::free(head); }

  // 需要实现 PushFront, PopFront, Erase 等操作
  void PushFront(Span *span) {
    if (!span)
      return;
    Span *first = head->next;
    head->next = span;
    span->prev = head;
    span->next = first;
    first->prev = span;
  }

  Span *PopFront() {
    if (head->next == head) {
      return nullptr;
    }
    Span *first = head->next;
    head->next = first->next;
    first->next->prev = head;
    first->prev = first->next = nullptr;
    return first;
  }

  void Erase(Span *span) {
    if (!span || !span->prev || !span->next)
      return;
    span->prev->next = span->next;
    span->next->prev = span->prev;
    span->prev = span->next = nullptr;
  }

  bool Empty() { return head->next == head; }

private:
  Span *head; // 哨兵头结点
};

class PageCache {
public:
  static const size_t PAGE_SHIFT = 12; // 4KB，内存页的大小是2的多少次方
  static const size_t PAGE_SIZE = 1 << PAGE_SHIFT; // 4096 bytes，内存页的大小
  static const size_t MAX_PAGES = 256; // 最大管理 256 页，连续内存块的最大页数

  static PageCache &getInstance() {
    static PageCache instance;
    return instance;
  }

  // 核心接口
  Span *allocSpan(size_t numPages);
  void deallocSpan(Span *span);

  // 系统内存分配和释放
  void *systemAlloc(size_t numPages);
  void systemFree(void *ptr, size_t numPages);

private:
  PageCache() = default;
  ~PageCache() {
    // 清理还在桶里的物理内存（对于被CentralCache拿走物理内存的，随进程销毁即可）
    for (size_t i = 0; i < MAX_PAGES; ++i) {
      Span *cur = spanLists[i].PopFront();
      while (cur) {
        // 将未使用的物理内存还给系统
        void *ptr = (void *)(cur->pageId << PAGE_SHIFT);
        systemFree(ptr, cur->numPages);

        // 这里绝对不要free(cur)，把它留给第二步统一清理！ 否则会导致 Double
        // Free 崩溃
        cur = spanLists[i].PopFront();
      }
    }

    // 终极清扫，通过基数树(RadixTree)找出所有遗落在外的 Span 并释放
    uintptr_t lastFreedSpan = 0;

    // 遍历整个 20 位页号空间 (1 << 20)
    for (size_t i = 0; i < (1 << 20); ++i) {
      // 从户口本查出当前页对应的 Span
      uintptr_t currentSpan = reinterpret_cast<uintptr_t>(idSpanMap.get(i));

      // 如果查到了 Span，并且它不是我们上一次刚刚释放的那个（防重复释放）
      if (currentSpan != 0 && currentSpan != lastFreedSpan) {
        lastFreedSpan = currentSpan; // 记录下来，因为一个 Span 会占据连续几页

        Span *spanToFree = reinterpret_cast<Span *>(currentSpan);
        spanToFree->~Span();   // 显式析构
        std::free(spanToFree); // 彻底回收
      }
    }
  }

private:
  // 按页数分类的空闲链表数组。下标 1~128
  // 比如 spanLists[8] 挂的都是 8 页大小的空闲 Span
  SpanList spanLists[MAX_PAGES];

  // 基数树：页号 -> Span* 的映射
  // 用于回收内存时，通过页号快速找 Span，以及找相邻页号进行合并
  RadixTree<20> idSpanMap;

  std::mutex pageMtx; // 全局锁（因为合并操作涉及多个 SpanList）
};
} // namespace XmemoryPool
