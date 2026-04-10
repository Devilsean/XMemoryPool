#pragma once

#include "Common.h"

#include <array>
#include <mutex>

namespace XmemoryPool {
struct FetchRangeResult {
  void *head = nullptr;
  size_t count = 0;
};

class CentralCache {
public:
  static CentralCache &getInstance() {
    static CentralCache instance;
    return instance;
  }

  FetchRangeResult fetchRange(size_t index, size_t batchNum);
  void returnRange(void *start, void *end, size_t index);

private:
  CentralCache() = default;

  std::array<void *, FREE_LIST_SIZE> centralFreeList{};
  std::array<std::mutex, FREE_LIST_SIZE> mutexes{};
};
} // namespace XmemoryPool
