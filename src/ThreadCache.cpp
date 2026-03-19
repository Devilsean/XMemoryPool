#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"

namespace XmemoryPool
{
    void *ThreadCache::allocate(size_t size)
    {
        // 快速路径：内联常见情况，减少分支
        if (size <= MAX_BYTES && size > 0)
        {
            // 获取索引 - 内联计算
            size_t index = ((size + ALIGNMENT - 1) >> ALIGN_SHIFT) - 1;

            // 快速路径：直接从本地缓存分配
            void *ptr = freeList[index];
            if (ptr)
            {
                freeList[index] = *(reinterpret_cast<void **>(ptr));
                freeListSize[index]--;
                return ptr;
            }
            // 慢速路径：从中心缓存获取
            return fetchFromCentralCache(index);
        }

        // 边界情况处理
        if (size == 0)
        {
            size = ALIGNMENT;
            void *ptr = freeList[0];
            if (ptr)
            {
                freeList[0] = *(reinterpret_cast<void **>(ptr));
                freeListSize[0]--;
                return ptr;
            }
            return fetchFromCentralCache(0);
        }

        // 大对象直接使用系统分配
        return malloc(size);
    }

    void ThreadCache::deallocate(void *ptr, size_t size)
    {
        // 大对象直接释放
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }

        // 快速路径：内联常见情况
        if (size > 0)
        {
            // 内联索引计算
            size_t index = ((size + ALIGNMENT - 1) >> ALIGN_SHIFT) - 1;

            // 快速插入到自由链表头部
            *(reinterpret_cast<void **>(ptr)) = freeList[index];
            freeList[index] = ptr;
            
            size_t newSize = ++freeListSize[index];

            // 延迟归还检查 - 使用更高的阈值减少归还频率
            if (newSize > 512)  // 统一使用更高的阈值
            {
                returnToCentralCache(freeList[index], size);
            }
            return;
        }

        // size == 0的情况，按ALIGNMENT处理
        size_t index = 0;
        *(reinterpret_cast<void **>(ptr)) = freeList[index];
        freeList[index] = ptr;
        freeListSize[index]++;
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
        // 计算字节数
        size_t size = (index + 1) * ALIGNMENT;
        // 计算批量数量
        size_t batchNum = getBatchNum(size);
        // 从中心缓存中获取内存块
        void *start = CentralCache::getInstance().fetchRange(index, batchNum);
        if (!start)
        {
            return nullptr;
        }
        
        // 取第一个返回，其余放入自由链表
        void *result = start;
        void *next = *(reinterpret_cast<void **>(start));
        
        if (next)
        {
            freeList[index] = next;
            freeListSize[index] = batchNum - 1;
        }
        else
        {
            freeListSize[index] = 0;
        }
        
        return result;
    }

    void ThreadCache::returnToCentralCache(void *start, size_t size)
    {
        if (!start) return;
        
        // 根据大小计算对应的索引
        size_t index = SizeClass::getIndex(size);

        // 获取对齐后的实际块大小
        size_t alignedSize = SizeClass::roundUp(size);
        
        // 计算要归还内存块数量
        size_t totalNum = freeListSize[index];
        if (totalNum <= 1)
            return; // 如果只有一个块，则不归还

        // 保留一半在ThreadCache中
        size_t keepNum = totalNum / 2;
        if (keepNum == 0) keepNum = 1;
        size_t returnNum = totalNum - keepNum;

        if (returnNum == 0) return;

        // 遍历链表找到分割点
        void *current = start;
        void *splitNode = nullptr;
        
        for (size_t i = 0; i < keepNum && current != nullptr; ++i)
        {
            splitNode = current;
            current = *reinterpret_cast<void **>(current);
        }

        if (splitNode != nullptr && current != nullptr)
        {
            // 断开链表
            *reinterpret_cast<void **>(splitNode) = nullptr;

            // 更新ThreadCache的空闲链表（保留前半部分）
            freeList[index] = start;
            freeListSize[index] = keepNum;

            // 将后半部分返回给CentralCache
            CentralCache::getInstance().returnRange(current, returnNum * alignedSize, index);
        }
    }
    // 计算获取批量数
    // 修改ThreadCache::getBatchNum函数
    size_t ThreadCache::getBatchNum(size_t size)
    {
        // 优化批量获取策略 - 减少从CentralCache获取的频率
        if (size <= 32)
            return 128;
        else if (size <= 64)
            return 64;
        else if (size <= 128)
            return 32;
        else if (size <= 256)
            return 16;
        else if (size <= 512)
            return 8;
        else if (size <= 1024)
            return 4;
        else
            return 2;
    }
}