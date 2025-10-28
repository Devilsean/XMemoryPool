#include "../include/CentralCache.h"
#include "../include/PageCache.h"

#include <thread>

namespace XmemoryPool
{
    static const size_t SPAN_PAGES = 8;
    CentralCache::CentralCache()
    {
        for (auto &ptr : centralFreeList)
        {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        // 互斥锁无需额外初始化
    }
    // 从中心缓存获取内存块
    void *CentralCache::fetchRange(size_t index, size_t batchNum)
    {
        // 检查索引是否有效
        if (index >= FREE_LIST_SIZE || batchNum == 0)
            return nullptr;

        // 使用分段互斥锁保证线程安全
        std::lock_guard<std::mutex> lock(getMutexForIndex(index));

        void *result = nullptr;
        try
        {
            // 尝试从中心缓存获取内存块
            result = centralFreeList[index].load(std::memory_order_relaxed);
            if (!result)
            {
                // 如果没有空闲内存块，从PageCache获取
                size_t size = (index + 1) * ALIGNMENT;
                result = fetchFromPageCache(size);

                if (!result)
                {
                    return nullptr;
                }

                // 将从PageCache获取的内存块切分层小块
                char *start = static_cast<char *>(result);
                static const size_t SPAN_PAGES = 8;
                size_t totalblocks = (SPAN_PAGES * PageCache::PAGE_SIZE) / size;
                size_t allocateblocks = std::min(batchNum, totalblocks);

                if (allocateblocks > 1)
                {
                    // 构建链表
                    for (size_t i = 1; i < allocateblocks; ++i)
                    {
                        void *current = start + (i - 1) * size;
                        void *next = start + i * size;
                        *reinterpret_cast<void **>(current) = next;
                    }
                    // 设置最后一个节点的next为nullptr
                    *(reinterpret_cast<void **>(start + (allocateblocks - 1) * size)) = nullptr;
                }
                // 构建留在CentralCache的内存链表
                if (totalblocks > allocateblocks)
                {
                    void *remainStart = start + allocateblocks * size;
                    for (size_t i = allocateblocks + 1; i < totalblocks; ++i)
                    {
                        void *current = start + (i - 1) * size;
                        void *next = start + i * size;
                        *reinterpret_cast<void **>(current) = next;
                    }
                    *(reinterpret_cast<void **>(start + (totalblocks - 1) * size)) = nullptr;

                    centralFreeList[index].store(remainStart, std::memory_order_release);
                }
            }
            else // 如果中心缓存有index对应的内存块
            {
                void *current = result;
                void *prev = nullptr;
                size_t count = 0;

                // 遍历链表，找到batchNum个节点
                while (current && count < batchNum)
                {
                    prev = current;
                    current = *(reinterpret_cast<void **>(current));
                    count++;
                }

                // 断开链表
                if (prev)
                {
                    *(reinterpret_cast<void **>(prev)) = nullptr;
                }

                // 更新中心缓存的链表头
                centralFreeList[index].store(current, std::memory_order_release);
            }
        }
        catch (...)
        {
            throw;
        }
        return result;
    };

    // 将内存块归还到中心缓存
    void CentralCache::returnRange(void *start, size_t size, size_t index)
    {
        if (!start || index >= FREE_LIST_SIZE)
        {
            return;
        }

        // 首先在无锁环境下计算链表长度和尾节点
        void *end = start;
        size_t count = 1;
        while (*reinterpret_cast<void **>(end) != nullptr && count < size)
        {
            end = *(reinterpret_cast<void **>(end));
            count++;
        }

        // 然后再获取锁进行插入操作
        std::lock_guard<std::mutex> lock(getMutexForIndex(index));
        try
        {
            // 将归还的链表连接到中心缓存的链表头部
            void *current = centralFreeList[index].load(std::memory_order_relaxed);
            *(reinterpret_cast<void **>(end)) = current;
            centralFreeList[index].store(start, std::memory_order_release);
        }
        catch (...)
        {
            throw;
        }
    }
    void *CentralCache::fetchFromPageCache(size_t size)
    {
        // 计算实际需要的页数
        size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

        // 根据内存大小确定申请策略
        if (size > SPAN_PAGES * PageCache::PAGE_SIZE)
        {
            return PageCache::getInstance().allocspan(numPages);
        }
        else
        {
            return PageCache::getInstance().allocspan(SPAN_PAGES);
        }
    };
}