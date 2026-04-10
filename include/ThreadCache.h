#pragma once

#include "Common.h"

#include <array>

namespace XmemoryPool {
class ThreadCache {
public:
  static ThreadCache *getInstance() {
    static thread_local ThreadCache instance;
    return &instance;
  }

  ~ThreadCache();

  void *allocate(size_t size);
  void deallocate(void *ptr, size_t size);

private:
  ThreadCache() = default;

  void *fetchFromCentralCache(size_t index);
  void returnToCentralCache(size_t index);
  FreeListPolicy getPolicy(size_t index) const;

  std::array<void *, FREE_LIST_SIZE> freeList{};
  std::array<size_t, FREE_LIST_SIZE> freeListSize{};
};
} // namespace XmemoryPool
