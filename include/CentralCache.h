#pragma once

#include "Common.h"
#include <mutex>
#include <atomic>

namespace XmemoryPool
{
    // 定义一个带填充的锁结构，确保跨缓存行
    struct alignas(64) PaddedMutex {
    std::mutex mutex;
    };

    // 作为全局单例，是中间层协调者，负责管理线程缓存中的内存块
    class CentralCache
    {
    public:
        static CentralCache &getInstance()
        {
            static CentralCache instance;
            return instance;
        }

        // 从中心缓存获取内存块
        void *fetchRange(size_t index, size_t batchNum);
        // 将内存块归还到中心缓存
        void returnRange(void *start, void *end, size_t index);

    private:
        // 设置为私有函数防止显式调用，保证中心缓存的单例模式
        CentralCache();
        void *fetchFromPageCache(size_t size);

        static constexpr size_t MUTEX_COUNT = 64; // 分段锁数量
        // 中心缓存的自由列表
        std::array<std::atomic<void *>, FREE_LIST_SIZE> centralFreeList;
        // 使用分段互斥锁替代自旋锁
        std::array<PaddedMutex, MUTEX_COUNT> mutexes;

        // 获取对应index的锁
        std::mutex &getMutexForIndex(size_t index)
        {
            return mutexes[index % MUTEX_COUNT].mutex;
        }

        struct alignas(64) PaddedListHead {
          std::atomic<void*>head;
        };
    };
}