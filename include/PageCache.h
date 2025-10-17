#pragma once

#include "Common.h"

#include <mutex>
#include <map>

namespace XmemoryPool
{
    // 作为全局单例，是最底层缓存，直接与操作系统交互，负责申请和释放物理页
    // 以页为单位管理内存，维护按页数分类的空闲span链表
    // 维护两个关键映射：
    // 1. freeSpans：按页数分类管理空闲span链表，每个页数对应一个span链表
    // 2. spanMap：页号到span的映射，用于快速定位span，实现快速回收
    // 优化点：
    // 1. 使用细粒度锁减少竞争
    // 2. 优化内存合并操作
    // 3. 使用哈希表加速查找
    class PageCache
    {
    public:
        static const size_t PAGE_SIZE = 4096;
        static PageCache &getInstance() // 声明为静态函数，保证单例模式
        {
            static PageCache instance;
            return instance;
        }

        void *allocspan(size_t numPages);             // 申请span
        void deallocSpan(void *ptr, size_t numPages); // 释放span

    private:
        PageCache() = default;

        // 向系统申请内存
        void *systemAlloc(size_t numPages);

    private:
        struct Span
        {
            void *pageAddr;  // 页起始地址
            size_t numPages; // 页数
            Span *next;      // 指向下一页
        };

        // 使用哈希表加速查找
        std::map<size_t, Span *> freeSpans; // 按页数管理空闲span，不同页数对应不同span链表
        std::map<void *, Span *> spanMap;   // 页号到span的映射，用于回收

        // 系统分配的内存记录
        std::mutex mtx;
    };
}