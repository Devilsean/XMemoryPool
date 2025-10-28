#include "../include/PageCache.h"

#include <sys/mman.h> //引入mmap
#include <cstring>

namespace XmemoryPool
{

    // 从页面缓存中分配指定页数的内存空间
    void *PageCache::allocspan(size_t numPages)
    {
        std::lock_guard<std::mutex> lock(mtx);

        // 查找合适的空闲span
        // lower_bound函数返回第一个大于等于numPages的元素的迭代器
        auto it = freeSpans.lower_bound(numPages);
        if (it != freeSpans.end())
        {
            Span *span = it->second;

            // 将取出的span从原有的空闲链表freeSpans[it->first]中移除
            if (span->next)
            {
                freeSpans[it->first] = span->next;
            }
            else
            {
                freeSpans.erase(it);
            }

            // 如果span大于需要的numPages则进行分割
            if (span->numPages > numPages)
            {
                Span *newSpan = new Span;
                newSpan->pageAddr = static_cast<char *>(span->pageAddr) +
                                    numPages * PAGE_SIZE;
                newSpan->numPages = span->numPages - numPages;
                newSpan->next = nullptr;

                // 将超出部分放回空闲Span*列表头部
                auto &list = freeSpans[newSpan->numPages];
                newSpan->next = list;
                list = newSpan;

                span->numPages = numPages;
            }

            // 记录span信息用于回收
            spanMap[span->pageAddr] = span;
            return span->pageAddr;
        }

        // 没有合适的span，向系统申请
        void *memory = systemAlloc(numPages);
        if (!memory)
            return nullptr;

        // 创建新的span
        Span *span = new Span;
        span->pageAddr = memory;
        span->numPages = numPages;
        span->next = nullptr;

        // 记录span信息用于回收
        spanMap[memory] = span;
        return memory;
    }

    void PageCache::deallocSpan(void *ptr, size_t numPages)
    {
        // 对于小内存块（<=4页），直接free并清理spanMap
        if (numPages <= 4)
        {
            // 从spanMap中删除对应的条目
            auto it = spanMap.find(ptr);
            if (it != spanMap.end())
            {
                delete it->second; // 释放Span对象
                spanMap.erase(it);
            }
            free(ptr);
            return;
        }

        // 大内存块的原有处理逻辑
        std::lock_guard<std::mutex> lock(mtx);

        // 查找对应的span
        auto it = spanMap.find(ptr);
        if (it == spanMap.end())
            return;

        Span *span = it->second;

        // 1. 尝试合并前一个相邻的span
        void *prevAddr = static_cast<char *>(ptr) - PAGE_SIZE; // 前一个页的地址
        auto prevIt = spanMap.find(prevAddr);
        if (prevIt != spanMap.end())
        {
            Span *prevSpan = prevIt->second;
            size_t prevNumPages = prevSpan->numPages;
            void *prevStartAddr = prevSpan->pageAddr;

            // 检查prevSpan是否是完整包含prevAddr的span
            if (static_cast<char *>(prevStartAddr) + prevNumPages * PAGE_SIZE == prevAddr)
            {
                // 检查prevSpan是否在空闲链表中
                auto &prevList = freeSpans[prevNumPages];
                bool found = false;

                if (prevList == prevSpan)
                {
                    prevList = prevSpan->next;
                    found = true;
                }
                else if (prevList)
                {
                    Span *current = prevList;
                    while (current->next)
                    {
                        if (current->next == prevSpan)
                        {
                            current->next = prevSpan->next;
                            found = true;
                            break;
                        }
                        current = current->next;
                    }
                }

                if (found)
                {
                    // 合并前一个span和当前span
                    prevSpan->numPages += span->numPages;
                    spanMap.erase(ptr);
                    // 更新span到当前处理的是prevSpan
                    ptr = prevSpan->pageAddr;
                    delete span;
                    span = prevSpan;
                }
            }
        }

        // 2. 尝试合并下一个相邻的span
        void *nextAddr = static_cast<char *>(ptr) + span->numPages * PAGE_SIZE;
        auto nextIt = spanMap.find(nextAddr);
        if (nextIt != spanMap.end())
        {
            Span *nextSpan = nextIt->second;

            // 首先检查nextSpan是否在空闲链表中
            bool found = false;
            auto &nextList = freeSpans[nextSpan->numPages];

            // 检查是否是头节点
            if (nextList == nextSpan)
            {
                nextList = nextSpan->next;
                found = true;
            }
            else if (nextList)
            {
                // 遍历链表查找nextSpan
                Span *prev = nextList;
                while (prev->next)
                {
                    if (prev->next == nextSpan)
                    {
                        prev->next = nextSpan->next;
                        found = true;
                        break;
                    }
                    prev = prev->next;
                }
            }
            if (found)
            {
                // 合并span
                span->numPages += nextSpan->numPages;
                spanMap.erase(nextAddr);
                delete nextSpan;
            }
        }

        // 3. 更新spanMap，确保映射正确
        spanMap[ptr] = span;

        // 4. 将合并后的span添加到对应的空闲链表
        auto &list = freeSpans[span->numPages];
        span->next = list;
        list = span;
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
        memset(ptr, 0, size);
        return ptr;
    }
}