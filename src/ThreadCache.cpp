#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"
#include "../include/PageCache.h"

#include <cstdint>

namespace XmemoryPool {
ThreadCache::~ThreadCache() {
  for (size_t i = 0; i < FREE_LIST_SIZE; ++i) {
    if (!freeList[i]) {
      continue;
    }

    void *start = freeList[i];
    void *end = start;
    while (*reinterpret_cast<void **>(end)) {
      end = *reinterpret_cast<void **>(end);
    }
    CentralCache::getInstance().returnRange(start, end, i);
  }
}

void *ThreadCache::allocate(size_t size) {
  if (size == 0) {
    size = ALIGNMENT;
  }

  if (size > MAX_BYTES) {
    size_t numPages =
        (size + PageCache::PAGE_SIZE - 1) >> PageCache::PAGE_SHIFT;
    Span *span = PageCache::getInstance().allocSpan(numPages);
    if (!span) {
      return nullptr;
    }
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(span->pageId << PageCache::PAGE_SHIFT));
  }

  size_t index = SizeClass::getIndex(size);
  if (void *ptr = freeList[index]) {
    freeList[index] = *reinterpret_cast<void **>(ptr);
    --freeListSize[index];
    return ptr;
  }

  return fetchFromCentralCache(index);
}

void ThreadCache::deallocate(void *ptr, size_t size) {
  if (!ptr) {
    return;
  }

  if (size == 0) {
    size = ALIGNMENT;
  }

  if (size > MAX_BYTES) {
    Span *span = PageCache::getInstance().findSpan(ptr);
    PageCache::getInstance().deallocSpan(span);
    return;
  }

  size_t index = SizeClass::getIndex(size);
  *reinterpret_cast<void **>(ptr) = freeList[index];
  freeList[index] = ptr;
  ++freeListSize[index];

  if (freeListSize[index] > getPolicy(index).maxFreeListLength) {
    returnToCentralCache(index);
  }
}

void *ThreadCache::fetchFromCentralCache(size_t index) {
  const size_t batchNum = getPolicy(index).batchCount;
  FetchRangeResult fetchResult =
      CentralCache::getInstance().fetchRange(index, batchNum);
  if (!fetchResult.head || fetchResult.count == 0) {
    return nullptr;
  }

  void *result = fetchResult.head;

  freeList[index] = *reinterpret_cast<void **>(result);
  *reinterpret_cast<void **>(result) = nullptr;
  freeListSize[index] = fetchResult.count - 1;
  return result;
}

void ThreadCache::returnToCentralCache(size_t index) {
  size_t totalNum = freeListSize[index];
  if (totalNum <= 1) {
    return;
  }

  size_t keepNum = totalNum / 2;
  void *keepHead = freeList[index];
  void *splitNode = keepHead;
  for (size_t i = 1; i < keepNum && splitNode; ++i) {
    splitNode = *reinterpret_cast<void **>(splitNode);
  }

  if (!splitNode) {
    return;
  }

  void *returnHead = *reinterpret_cast<void **>(splitNode);
  if (!returnHead) {
    return;
  }

  *reinterpret_cast<void **>(splitNode) = nullptr;

  void *returnTail = returnHead;
  while (*reinterpret_cast<void **>(returnTail)) {
    returnTail = *reinterpret_cast<void **>(returnTail);
  }

  freeList[index] = keepHead;
  freeListSize[index] = keepNum;
  CentralCache::getInstance().returnRange(returnHead, returnTail, index);
}

FreeListPolicy ThreadCache::getPolicy(size_t index) const {
  return TransferPolicyTable::get(index);
}
} // namespace XmemoryPool
