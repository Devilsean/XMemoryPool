#include "../include/CentralCache.h"
#include "../include/PageCache.h"

#include <algorithm>
#include <cstdint>

namespace XmemoryPool {
namespace {
constexpr size_t kDefaultSpanPages = 8;
}

FetchRangeResult CentralCache::fetchRange(size_t index, size_t batchNum) {
  if (index >= FREE_LIST_SIZE || batchNum == 0) {
    return {};
  }

  {
    std::lock_guard<std::mutex> lock(mutexes[index]);
    void *head = centralFreeList[index];
    if (head) {
      void *current = head;
      void *tail = nullptr;
      size_t count = 0;

      while (current && count < batchNum) {
        tail = current;
        current = *reinterpret_cast<void **>(current);
        ++count;
      }

      if (tail) {
        *reinterpret_cast<void **>(tail) = nullptr;
      }
      centralFreeList[index] = current;
      return {head, count};
    }
  }

  const size_t blockSize = SizeClass::getSizeByIndex(index);
  const size_t totalBytes = blockSize * batchNum;
  size_t numPages =
      (totalBytes + PageCache::PAGE_SIZE - 1) >> PageCache::PAGE_SHIFT;
  numPages = std::max(numPages, kDefaultSpanPages);

  Span *span = PageCache::getInstance().allocSpan(numPages);
  if (!span) {
    return {};
  }

  char *spanStart = reinterpret_cast<char *>(
      static_cast<uintptr_t>(span->pageId << PageCache::PAGE_SHIFT));
  const size_t totalBlocks = (numPages * PageCache::PAGE_SIZE) / blockSize;
  if (totalBlocks == 0) {
    return {};
  }

  char *current = spanStart;
  for (size_t i = 0; i + 1 < totalBlocks; ++i) {
    char *next = current + blockSize;
    *reinterpret_cast<void **>(current) = next;
    current = next;
  }
  *reinterpret_cast<void **>(current) = nullptr;

  const size_t actualBatch = std::min(batchNum, totalBlocks);
  void *result = spanStart;
  void *remainStart = nullptr;

  if (actualBatch < totalBlocks) {
    void *tail = result;
    for (size_t i = 1; i < actualBatch; ++i) {
      tail = *reinterpret_cast<void **>(tail);
    }
    remainStart = *reinterpret_cast<void **>(tail);
    *reinterpret_cast<void **>(tail) = nullptr;
  }

  if (remainStart) {
    std::lock_guard<std::mutex> lock(mutexes[index]);
    void *remainTail = remainStart;
    while (*reinterpret_cast<void **>(remainTail)) {
      remainTail = *reinterpret_cast<void **>(remainTail);
    }
    *reinterpret_cast<void **>(remainTail) = centralFreeList[index];
    centralFreeList[index] = remainStart;
  }

  return {result, actualBatch};
}

void CentralCache::returnRange(void *start, void *end, size_t index) {
  if (!start || !end || index >= FREE_LIST_SIZE) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutexes[index]);
  *reinterpret_cast<void **>(end) = centralFreeList[index];
  centralFreeList[index] = start;
}
} // namespace XmemoryPool
