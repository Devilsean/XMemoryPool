#pragma once

#include "Common.h"
#include <mutex>

namespace XmemoryPool
{
    // 作为全局单例，是中间层协调者，负责管理线程缓存中的内存块
    // 维护以原子指针实现的自由链表数组
    // 使用细粒度自旋锁保证线程安全，减少锁竞争
    // 针对小对象使用更优化的策略
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
        void returnRange(void *start, size_t size, size_t index);

    private:
        // 设置为私有函数防止显式调用，保证中心缓存的单例模式
        CentralCache()
        {
            for (auto &ptr : centralFreeList)
            {                                                  // 初始化所有自由列表指针
                ptr.store(nullptr, std::memory_order_relaxed); // 提高性能
            }
            for (auto &lock : locks)
            { // 初始化所有自旋锁
                lock.clear();
            }
        }
        void *fetchFromPageCache(size_t size);

    private:
        // 中心缓存的自由列表
        std::array<std::atomic<void *>, FREE_LIST_SIZE> centralFreeList;
        // 用于同步的自旋锁
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks;
        // 用于批量操作的自旋锁，减少锁竞争
    };
}