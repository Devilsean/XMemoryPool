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
        size_t size = (index + 1) * ALIGNMENT;
        size_t threshold;

        if (size <= 64)       // 小对象
            threshold = 512;  // 从128大幅提高到512
        else if (size <= 256) // 中等对象
            threshold = 256;  // 从64提高到256
        else                  // 较大对象
            threshold = 128;  // 从32提高到128

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
        size_t keepRatio = 4; // 保留1/4的内存块在ThreadCache中
        if (alignedSize <= 64)
        {
            keepRatio = 2; // 小对象保留更多
        }
        else if (alignedSize >= 1024)
        {
            keepRatio = 8; // 大对象保留更少
        }

        // 计算要归还内存块数量
        size_t batchNum = freeListSize[index];
        if (batchNum <= 1)
            return; // 如果只有一个块，则不归还

        // 保留一部分在ThreadCache中（比如保留1/4）
        size_t keepNum = std::max(batchNum / keepRatio, size_t(1));
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
    // 修改ThreadCache::getBatchNum函数
    size_t ThreadCache::getBatchNum(size_t size)
    {
        constexpr size_t MAX_BATCH_SIZE = 16 * 1024; // 增加到16KB
        size_t batchNum;

        // 大幅增加小对象的批量获取数量
        if (size <= 32)
            batchNum = 256; // 从64增加到256
        else if (size <= 64)
            batchNum = 128; // 从32增加到128
        else if (size <= 128)
            batchNum = 64; // 从16增加到64
        else if (size <= 256)
            batchNum = 32; // 从8增加到32
        else if (size <= 512)
            batchNum = 16; // 从4增加到16
        else if (size <= 1024)
            batchNum = 8; // 从2增加到8
        else
            batchNum = 4; // 从1增加到4

        size_t maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size);
        return std::max(size_t(1), std::min(batchNum, maxNum));
    }
}