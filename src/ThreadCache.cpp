#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"

namespace XmemoryPool
{
    void *ThreadCache::allocate(size_t size)
    {
        // 处理0字节的分配情况
        if (size == 0)
        {
            size = ALIGNMENT;
        }

        // 如果大于最大值，直接调用malloc
        if (size > MAX_BYTES)
        {
            return malloc(size);
        }

        // 获取索引
        size_t index = SizeClass::getIndex(size);
        // 更新自由链表大小
        freeListSize[index]--;

        // 如果有空闲内存块，直接返回头指针
        if (void *ptr = freeList[index])
        {
            freeList[index] = *(reinterpret_cast<void **>(ptr));
            return ptr;
        }
        // 如果没有空闲内存块，则从中心缓存中获取
        return fetchFromCentralCache(index);
    }

    void ThreadCache::deallocate(void *ptr, size_t size)
    {
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }

        size_t index = SizeClass::getIndex(size);

        // 插入到线程本地自由链表
        *(reinterpret_cast<void **>(ptr)) = freeList[index];
        freeList[index] = ptr;

        // 更新自由链表大小
        freeListSize[index]++;

        if (shouldReturnToCentralCache(index))
        {
            returnToCentralCache(freeList[index], size);
        }
    }

    bool ThreadCache::shouldReturnToCentralCache(size_t index)
    {
        // 设定阈值，当链表超过一定长度时归还给中心缓存
        size_t threshold = 64;
        return freeListSize[index] > threshold;
    }

    void *ThreadCache::fetchFromCentralCache(size_t index)
    {
        // 计算字节数 - 修复：使用SizeClass::roundUp计算实际大小
        size_t size = (index + 1) * ALIGNMENT;
        // 计算批量数量
        size_t batchNum = getBatchNum(size);
        // 从中心缓存中获取内存块
        void *start = CentralCache::getInstance().fetchRange(index, batchNum);
        if (!start)
        {
            return nullptr;
        }
        // 更新自由链表大小
        freeListSize[index] += batchNum;
        // 取一个返回，其余放入线程本地自由链表
        void *result = start;
        if (batchNum > 1)
        {
            freeList[index] = *(reinterpret_cast<void **>(start));
        }
        return result;
    }

    void ThreadCache::returnToCentralCache(void *start, size_t size)
    {
        // 根据大小计算对应的索引
        size_t index = SizeClass::getIndex(size);

        // 获取对齐后的实际块大小
        size_t alignedSize = SizeClass::roundUp(size);

        // 计算要归还内存块数量
        size_t batchNum = freeListSize[index];
        if (batchNum <= 1)
            return; // 如果只有一个块，则不归还

        // 保留一部分在ThreadCache中（比如保留1/4）
        size_t keepNum = std::max(batchNum / 4, size_t(1));
        size_t returnNum = batchNum - keepNum;

        // 将内存块串成链表
        char *current = static_cast<char *>(start);
        // 使用对齐后的大小计算分割点
        char *splitNode = current;
        for (size_t i = 0; i < keepNum - 1; ++i)
        {
            splitNode = reinterpret_cast<char *>(*reinterpret_cast<void **>(splitNode));
            if (splitNode == nullptr)
            {
                // 如果链表提前结束，更新实际的返回数量
                returnNum = batchNum - (i + 1);
                break;
            }
        }

        if (splitNode != nullptr)
        {
            // 将要返回的部分和要保留的部分断开
            void *nextNode = *reinterpret_cast<void **>(splitNode);
            *reinterpret_cast<void **>(splitNode) = nullptr; // 断开连接

            // 更新ThreadCache的空闲链表
            freeList[index] = start;

            // 更新自由链表大小
            freeListSize[index] = keepNum;

            // 将剩余部分返回给CentralCache
            if (returnNum > 0 && nextNode != nullptr)
            {
                CentralCache::getInstance().returnRange(nextNode, returnNum * alignedSize, index);
            }
        }
    }
    // 计算获取批量数
    size_t ThreadCache::getBatchNum(size_t size)
    {
        constexpr size_t MAX_BATCH_SIZE = 4 * 1024; // 最大批量大小为1MB
        // 使用预定义的常量
        size_t batchNum;
        // 计算需要规划的内存块
        // 对于小对象，批量获取数量更多，减少从中心缓存的获取次数
        if (size <= 32)
            batchNum = 64; // 增加批量获取数量
        else if (size <= 64)
            batchNum = 32; // 增加批量获取数量
        else if (size <= 128)
            batchNum = 16;
        else if (size <= 256)
            batchNum = 8;
        else if (size <= 512)
            batchNum = 4;
        else if (size <= 1024)
            batchNum = 2;
        else
            batchNum = 1;

        size_t maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size);

        return std::max(size_t(1), std::min(batchNum, maxNum));
    }
}