#pragma once

#include "Common.h"

namespace XmemoryPool
{
    // 作为应用程序直接调用的最上层缓存，每个线程拥有独立实例（通过Thread_local实例实现）
    // 维护不同大小类的自由链表数组，实现无锁内存分配与释放，最大化减少线程竞争
    // 当本地缓存不足时，调用 fetchFromCentralCache() 从中心缓存批量获取内存块
    // 当本地缓存过多时，调用 returnToCentralCache()将内存块归还给中心缓存
    // 管理每个自由链表的内存块数量统计（freeListSize_数组）
    class ThreadCache
    {
    public:
        static ThreadCache *getInstance() // 声明为静态函数，保证线程缓存的单例模式
        {
            static thread_local ThreadCache instance;
            return &instance;
        }

        void *allocate(size_t size);
        void deallocate(void *ptr, size_t size);

    private:
        ThreadCache() = default;
        // 从中心缓存获取内存
        void *fetchFromCentralCache(size_t index);
        // 将内存归还到中心缓存
        void returnToCentralCache(void *start, size_t size);
        // 获取批量数量
        size_t getBatchNum(size_t size);
        // 判断是否应该归还到中心缓存
        bool shouldReturnToCentralCache(size_t index);

        std::array<void *, FREE_LIST_SIZE> freeList{};     // 自由链表数组，每个元素是一个指针，指向一个内存块
        std::array<size_t, FREE_LIST_SIZE> freeListSize{}; // 每个自由链表中内存块的数量
    };
}