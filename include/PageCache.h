#pragma once
#include "RadixTree.h"
#include <mutex>

namespace XmemoryPool
{
    // Span 结构体建议挪到外部或保持在 PageCache 内，但需要包含双向链表指针
    struct Span
    {
        size_t pageId;    // 起始页号 (addr >> 12)
        size_t numPages;  // 页数
        
        Span* prev = nullptr; 
        Span* next = nullptr;
        
        bool inUse = false; // 是否正在被 CentralCache 使用
    };

    // SpanList 管理某一类页数的双向链表
    class SpanList {
    public:
        SpanList() {
            head = new Span;
            head->next = head;
            head->prev = head;
        }
        // 需要实现 PushFront, PopFront, Erase 等操作
        void PushFront(Span* span) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!span) return;
            Span* first = head->next;
            head->next = span;
            span->prev = head;
            span->next = first;
            first->prev = span;
        }
        
        Span* PopFront() {
            std::lock_guard<std::mutex> lock(mtx);
            if (head->next == head) {
                return nullptr;
            }
            Span* first = head->next;
            head->next = first->next;
            first->next->prev = head;
            first->prev = first->next = nullptr;
            return first;
        }
        
        void Erase(Span* span) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!span || !span->prev || !span->next) return;
            span->prev->next = span->next;
            span->next->prev = span->prev;
            span->prev = span->next = nullptr;
        }
        
        bool Empty() {
            std::lock_guard<std::mutex> lock(mtx);
            return head->next == head;
        }
    private:
        Span* head;
        std::mutex mtx; // 桶锁
    };

    class PageCache
    {
    public:
        static const size_t PAGE_SHIFT = 12; // 4KB
        static const size_t PAGE_SIZE = 1 << PAGE_SHIFT; // 4096 bytes
        static const size_t MAX_PAGES = 129; // 最大管理 128 页

        static PageCache& getInstance() {
            static PageCache instance;
            return instance;
        }

        // 核心接口
        Span* allocSpan(size_t numPages);
        void deallocSpan(Span* span);
        
        // 系统内存分配和释放
        void* systemAlloc(size_t numPages);
        void systemFree(void* ptr, size_t numPages);

    private:
        PageCache() = default;

    private:
        // 按页数分类的空闲链表数组。下标 1~128
        // 比如 spanLists[8] 挂的都是 8 页大小的空闲 Span
        SpanList spanLists[MAX_PAGES];

        // 基数树：页号 -> Span* 的映射
        // 用于回收内存时，通过页号快速找 Span，以及找相邻页号进行合并
        RadixTree<36> idSpanMap; 

        std::mutex pageMtx; // 全局锁（因为合并操作涉及多个 SpanList）
    };
}