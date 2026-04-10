#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <mutex>

namespace XmemoryPool {
struct Span {
  size_t pageId = 0;
  size_t numPages = 0;
  bool isFree = false;

  Span *prev = nullptr;
  Span *next = nullptr;
};

class SpanList {
public:
  SpanList() {
    head = static_cast<Span *>(std::malloc(sizeof(Span)));
    head->next = head;
    head->prev = head;
  }

  ~SpanList() { std::free(head); }

  bool Empty() const { return head->next == head; }

  void PushFront(Span *span) {
    if (!span) {
      return;
    }
    Span *first = head->next;
    head->next = span;
    span->prev = head;
    span->next = first;
    first->prev = span;
  }

  Span *PopFront() {
    if (Empty()) {
      return nullptr;
    }
    Span *first = head->next;
    Erase(first);
    return first;
  }

  void Erase(Span *span) {
    if (!span || !span->prev || !span->next) {
      return;
    }
    span->prev->next = span->next;
    span->next->prev = span->prev;
    span->prev = nullptr;
    span->next = nullptr;
  }

private:
  Span *head = nullptr;
};

class PageCache {
public:
  static constexpr size_t PAGE_SHIFT = 12;
  static constexpr size_t PAGE_SIZE = 1 << PAGE_SHIFT;
  static constexpr size_t MAX_PAGES = 256;

  static PageCache &getInstance() {
    static PageCache instance;
    return instance;
  }

  Span *allocSpan(size_t numPages);
  void deallocSpan(Span *span);
  Span *findSpan(void *ptr);

  void releaseAllSpans();

private:
  PageCache() = default;
  ~PageCache() { releaseAllSpans(); }

  void *systemAlloc(size_t numPages);
  void systemFree(void *ptr, size_t numPages);

  void insertFreeSpan(Span *span);
  void removeFreeSpan(Span *span);

  std::array<SpanList, MAX_PAGES> spanLists;
  std::map<size_t, Span *> spansByPageId;
  std::mutex pageMtx;
};
} // namespace XmemoryPool
