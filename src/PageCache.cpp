#include "../include/PageCache.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <new>
#include <sys/mman.h>

namespace XmemoryPool {
void PageCache::insertFreeSpan(Span *span) {
  if (!span || span->numPages >= MAX_PAGES) {
    return;
  }
  span->isFree = true;
  spanLists[span->numPages].PushFront(span);
}

void PageCache::removeFreeSpan(Span *span) {
  if (!span || span->numPages >= MAX_PAGES) {
    return;
  }
  spanLists[span->numPages].Erase(span);
  span->isFree = false;
}

Span *PageCache::allocSpan(size_t numPages) {
  std::lock_guard<std::mutex> lock(pageMtx);
  const size_t needPages = std::max<size_t>(1, numPages);

  if (needPages >= MAX_PAGES) {
    void *ptr = systemAlloc(needPages);
    if (!ptr) {
      return nullptr;
    }

    Span *span = new Span();
    span->pageId = reinterpret_cast<size_t>(ptr) >> PAGE_SHIFT;
    span->numPages = needPages;
    spansByPageId[span->pageId] = span;
    return span;
  }

  while (true) {
    for (size_t pages = needPages; pages < MAX_PAGES; ++pages) {
      if (spanLists[pages].Empty()) {
        continue;
      }

      Span *span = spanLists[pages].PopFront();
      span->isFree = false;

      if (span->numPages > needPages) {
        Span *remainder = new Span();
        remainder->pageId = span->pageId + needPages;
        remainder->numPages = span->numPages - needPages;
        spansByPageId[remainder->pageId] = remainder;
        insertFreeSpan(remainder);
        span->numPages = needPages;
      }

      return span;
    }

    void *ptr = systemAlloc(MAX_PAGES - 1);
    if (!ptr) {
      return nullptr;
    }

    Span *span = new Span();
    span->pageId = reinterpret_cast<size_t>(ptr) >> PAGE_SHIFT;
    span->numPages = MAX_PAGES - 1;
    spansByPageId[span->pageId] = span;
    insertFreeSpan(span);
  }
}

void PageCache::deallocSpan(Span *span) {
  if (!span) {
    return;
  }

  std::lock_guard<std::mutex> lock(pageMtx);
  auto it = spansByPageId.find(span->pageId);
  if (it == spansByPageId.end()) {
    return;
  }

  if (span->numPages >= MAX_PAGES) {
    spansByPageId.erase(it);
    void *ptr =
        reinterpret_cast<void *>(static_cast<uintptr_t>(span->pageId << PAGE_SHIFT));
    systemFree(ptr, span->numPages);
    delete span;
    return;
  }

  span->isFree = true;

  if (it != spansByPageId.begin()) {
    auto prevIt = std::prev(it);
    Span *prev = prevIt->second;
    if (prev->isFree && prev->pageId + prev->numPages == span->pageId &&
        prev->numPages + span->numPages < MAX_PAGES) {
      removeFreeSpan(prev);
      spansByPageId.erase(it);
      prev->numPages += span->numPages;
      delete span;
      span = prev;
      it = prevIt;
    }
  }

  auto nextIt = std::next(it);
  if (nextIt != spansByPageId.end()) {
    Span *next = nextIt->second;
    if (next->isFree && span->pageId + span->numPages == next->pageId &&
        span->numPages + next->numPages < MAX_PAGES) {
      removeFreeSpan(next);
      span->numPages += next->numPages;
      spansByPageId.erase(nextIt);
      delete next;
    }
  }

  insertFreeSpan(span);
}

Span *PageCache::findSpan(void *ptr) {
  if (!ptr) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(pageMtx);
  size_t pageId = reinterpret_cast<size_t>(ptr) >> PAGE_SHIFT;
  auto it = spansByPageId.upper_bound(pageId);
  if (it == spansByPageId.begin()) {
    return nullptr;
  }

  --it;
  Span *span = it->second;
  if (pageId >= span->pageId && pageId < span->pageId + span->numPages) {
    return span;
  }
  return nullptr;
}

void *PageCache::systemAlloc(size_t numPages) {
  const size_t size = numPages * PAGE_SIZE;
  void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return ptr == MAP_FAILED ? nullptr : ptr;
}

void PageCache::systemFree(void *ptr, size_t numPages) {
  if (!ptr) {
    return;
  }
  munmap(ptr, numPages * PAGE_SIZE);
}

void PageCache::releaseAllSpans() {
  std::lock_guard<std::mutex> lock(pageMtx);
  for (auto &[pageId, span] : spansByPageId) {
    void *ptr =
        reinterpret_cast<void *>(static_cast<uintptr_t>(pageId << PAGE_SHIFT));
    systemFree(ptr, span->numPages);
    delete span;
  }
  spansByPageId.clear();
}
} // namespace XmemoryPool
