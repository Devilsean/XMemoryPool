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
    void* CentralCache::fetchRange(size_t index, size_t batchNum)
    {
        // 1. 获取对应的桶锁
        std::unique_lock<std::mutex> lock(getMutexForIndex(index));

        // 路径 A：桶里有现成的内存块
        void* result = centralFreeList[index].load(std::memory_order_relaxed);
        if (result) 
        {
            void* current = result;
            void* prev = nullptr;
            size_t count = 0;
            
            // 尝试取走 batchNum 个块
            while (current && count < batchNum) {
                prev = current;
                current = *(reinterpret_cast<void**>(current));
                count++;
            }
            
            // 断开取走的这部分链表，并更新桶头
            if (prev) *(reinterpret_cast<void**>(prev)) = nullptr;
            centralFreeList[index].store(current, std::memory_order_release);
            return result;
        }

        // 路径 B：桶里没内存了，去 PageCache 批发
        // 释放桶锁，允许其他线程在此时归还内存到这个桶，不阻塞并发
        lock.unlock(); 

        size_t size = (index + 1) * ALIGNMENT;
        // fetchFromPageCache 内部会加 PageCache 的全局大锁
        void* spanStart = fetchFromPageCache(size); 
        if (!spanStart) return nullptr;

        // 在锁外执行耗时的切分逻辑
        // 假设每个 Span 固定给 8 页内存
        static const size_t SPAN_PAGES = 8;
        size_t totalBlocks = (SPAN_PAGES * PageCache::PAGE_SIZE) / size;
        
        // 在局部变量里预先连好整条链表（不占锁，多线程并行切分）
        char* cur = static_cast<char*>(spanStart);
        for (size_t i = 0; i < totalBlocks - 1; ++i) {
            void* next = cur + size;
            *(reinterpret_cast<void**>(cur)) = next;
            cur = static_cast<char*>(next);
        }
        *(reinterpret_cast<void**>(cur)) = nullptr; // 尾部封口

        // 重新拿锁，进行 O(1) 挂载
        lock.lock();

        // 确定返回给 ThreadCache 的部分
        void* actualResult = spanStart;
        void* actualReturnEnd = spanStart;
        size_t actualBatch = std::min(batchNum, totalBlocks);

        // 找到 batchNum 对应的末尾点
        for (size_t i = 0; i < actualBatch - 1; ++i) {
            actualReturnEnd = *(reinterpret_cast<void**>(actualReturnEnd));
        }

        // 剩余的部分（如果有）
        void* remainStart = *(reinterpret_cast<void**>(actualReturnEnd));
        
        // 断开返回部分与剩余部分的连接
        *(reinterpret_cast<void**>(actualReturnEnd)) = nullptr;

        // 如果还有多余的块，一并挂回 CentralCache 桶头
        if (remainStart) {
            void* oldHead = centralFreeList[index].load(std::memory_order_relaxed);
            // 将刚才连好的链表尾部（cur）指向旧的头
            *(reinterpret_cast<void**>(cur)) = oldHead; 
            centralFreeList[index].store(remainStart, std::memory_order_release);
        }

        return actualResult;
    }

    // 将内存块归还到中心缓存
    void CentralCache::returnRange(void *start, void *end, size_t index)
    {
        if (!start || !end) return;

        // 获取桶锁（使用alignas(64) 解决虚假竞争）
        std::lock_guard<std::mutex> lock(getMutexForIndex(index));
        
        // O(1) 挂载逻辑
        // 先取出桶里原来的头节点
        void *oldHead = centralFreeList[index].load(std::memory_order_relaxed);
        
        // 把归还链表的最后一个节点的 next 指向原来的头
        *(reinterpret_cast<void **>(end)) = oldHead;
        
        // 更新桶的头节点为归还链表的起始点
        // 使用 release 语义确保之前的写入对其他线程可见
        centralFreeList[index].store(start, std::memory_order_release);
    }

    void *CentralCache::fetchFromPageCache(size_t size)
    {
        // 计算实际需要的页数
        size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

        // 根据内存大小确定申请策略
        Span* span = nullptr;
        if (size > SPAN_PAGES * PageCache::PAGE_SIZE)
        {
            span = PageCache::getInstance().allocSpan(numPages);
        }
        else
        {
            span = PageCache::getInstance().allocSpan(SPAN_PAGES);
        }
        
        // 将Span转换为实际的内存地址
        if (span)
        {
            return (void*)(span->pageId << PageCache::PAGE_SHIFT);
        }
        return nullptr;
    };
}