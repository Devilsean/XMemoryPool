#include "../include/PageCache.h"
#include "../include/RadixTree.h"

#include <sys/mman.h> //引入mmap
#include <cstring>

namespace XmemoryPool
{

    // 从页面缓存中分配指定页数的内存空间
    Span* PageCache::allocSpan(size_t numPages) {
        std::lock_guard<std::mutex> lock(pageMtx); // 锁只在入口处拿一次
        size_t k = numPages;

        while (true) {
            // 1. 如果申请页数超过限制，直接走系统分配
            if (k >= MAX_PAGES) {
                void* ptr = systemAlloc(k);
                Span* span = (Span*)std::malloc(sizeof(Span)); // 使用 malloc 避开重写的 new
                new(span) Span();
                span->pageId = (size_t)ptr >> PAGE_SHIFT;
                span->numPages = k;
                span->inUse = true;
                idSpanMap.set(span->pageId, span); 
                return span;
            }

            // 2. 检查对应页数的桶里有没有现成的
            if (!spanLists[k].Empty()) {
                Span* span = spanLists[k].PopFront();
                span->inUse = true;
                for (size_t i = 0; i < span->numPages; ++i)
                    idSpanMap.set(span->pageId + i, span);
                return span;
            }

            // 3. 往后找更大的块进行切分
            for (size_t i = k + 1; i < MAX_PAGES; ++i) {
                if (!spanLists[i].Empty()) {
                    Span* bigSpan = spanLists[i].PopFront();
                    Span* kSpan = (Span*)std::malloc(sizeof(Span)); // 避开 new
                    new(kSpan) Span();

                    kSpan->pageId = bigSpan->pageId;
                    kSpan->numPages = k;
                    kSpan->inUse = true;

                    bigSpan->pageId += k;
                    bigSpan->numPages -= k;
                    spanLists[bigSpan->numPages].PushFront(bigSpan);
                    
                    idSpanMap.set(bigSpan->pageId, bigSpan);
                    idSpanMap.set(bigSpan->pageId + bigSpan->numPages - 1, bigSpan);

                    for (size_t j = 0; j < kSpan->numPages; ++j)
                        idSpanMap.set(kSpan->pageId + j, kSpan);

                    return kSpan;
                }
            }

            // 4. 全都找不到，向系统要个 128 页的巨头
            void* ptr = systemAlloc(MAX_PAGES - 1);
            Span* bigSpan = (Span*)std::malloc(sizeof(Span)); // 避开 new
            new(bigSpan) Span();
            bigSpan->pageId = (size_t)ptr >> PAGE_SHIFT;
            bigSpan->numPages = MAX_PAGES - 1;
            bigSpan->inUse = false;
            
            // 挂载到 128 页的桶里
            spanLists[bigSpan->numPages].PushFront(bigSpan);
            
            // 更新基数树映射，方便后续合并
            idSpanMap.set(bigSpan->pageId, bigSpan);
            idSpanMap.set(bigSpan->pageId + bigSpan->numPages - 1, bigSpan);

            // 不再递归调用 allocSpan(k)，而是依靠 while(true) 进入下一轮循环
            // 下一轮循环中，Step 2 必然能从 spanLists[128] 中拿到刚才申请的块
        }
    }

    void PageCache::deallocSpan(Span *span) {
        std::lock_guard<std::mutex> lock(pageMtx);
        // 限制：PageCache 只合并管理 128 页以内的
        if (span->numPages >= MAX_PAGES) {
            void* ptr = (void*)(span->pageId << PAGE_SHIFT);
            systemFree(ptr, span->numPages); // 这里的 systemFree 对应 mmap 的 munmap
            delete span;
            return;
        }

        // 1. 尝试向前合并（合并前不清理映射，因为需要用来查找邻居）
        while (true) {
            size_t prevId = span->pageId - 1;
            Span* prevSpan = (Span*)idSpanMap.get(prevId);
            
            // 没找到邻居，或者邻居正在被 CentralCache 使用，或者邻居页数对不上
            if (prevSpan == nullptr || prevSpan->inUse == true || 
                (prevSpan->pageId + prevSpan->numPages) != span->pageId) 
                break;

            // 合并后可能会超过 MAX_PAGES，停止合并
            if (span->numPages + prevSpan->numPages >= MAX_PAGES) break;

            // 把邻居从它原来的桶里拔出来 (O(1) 操作，因为是双向链表)
            spanLists[prevSpan->numPages].Erase(prevSpan);
            
            // 清理被合并span的映射
            idSpanMap.erase(prevSpan->pageId);
            idSpanMap.erase(prevSpan->pageId + prevSpan->numPages - 1);
            
            span->pageId = prevSpan->pageId;
            span->numPages += prevSpan->numPages;
            delete prevSpan;
        }

        // 2. 尝试向后合并
        while (true) {
            size_t nextId = span->pageId + span->numPages;
            Span* nextSpan = (Span*)idSpanMap.get(nextId);

            if (nextSpan == nullptr || nextSpan->inUse == true || nextSpan->pageId != nextId)
                break;

            if (span->numPages + nextSpan->numPages >= MAX_PAGES) break;

            spanLists[nextSpan->numPages].Erase(nextSpan);
            
            // 清理被合并span的映射
            idSpanMap.erase(nextSpan->pageId);
            idSpanMap.erase(nextSpan->pageId + nextSpan->numPages - 1);
            
            span->numPages += nextSpan->numPages;
            delete nextSpan;
        }

        // 3. 将最终合并成的大块挂回对应的桶，并标记为未使用
        span->inUse = false;
        spanLists[span->numPages].PushFront(span);

        // 更新基数树：只需要更新首尾两页即可，因为找邻居只看边界
        idSpanMap.set(span->pageId, span);
        idSpanMap.set(span->pageId + span->numPages - 1, span);
    }
    
    void *PageCache::systemAlloc(size_t numPages)
    {
        size_t size = numPages * PAGE_SIZE;

        // 对于小内存块（<=4页），使用malloc而不是mmap
        if (numPages <= 4)
        {
            return malloc(size);
        }

        // 大内存块仍然使用mmap
        void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED)
        {
            // mmap失败时回退到malloc
            return malloc(size);
        }

        // 消除冗余的 memset：在 systemAlloc 中，
        // mmap 拿到的内存不需要手动清零，这在频繁申请大块内存时开销极大。
        // memset(ptr, 0, size);
        
        return ptr;
    }
    
    void PageCache::systemFree(void* ptr, size_t numPages)
    {
        size_t size = numPages * PAGE_SIZE;
        
        // 对于小内存块（<=4页），使用free而不是munmap
        if (numPages <= 4)
        {
            free(ptr);
            return;
        }
        
        // 大内存块使用munmap
        if (munmap(ptr, size) == -1)
        {
            // munmap失败时回退到free
            free(ptr);
        }
    }
}