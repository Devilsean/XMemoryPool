#pragma once

#include <cstddef> //提供了定义大小类型的类型
#include <atomic>
#include <array>

namespace XmemoryPool
{
    // 内存对齐和大小相关常量
    constexpr size_t ALIGNMENT = 8;                          // 对齐数大小
    constexpr size_t ALIGN_SHIFT = 3;       // 对齐数大小的位移量
    constexpr size_t MAX_BYTES = 256 * 1024;                 // 内存块最大值
    constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // 自由链表长度

    // 大小类
    // 实现内存向上对齐，获取内存块索引
    class SizeClass
    {
    public:
        static size_t roundUp(size_t bytes) // 对齐内存块大小，返回内存块的最小整数倍数
        {
            return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        }

        static size_t getIndex(size_t bytes) // 返回内存块的索引，即内存块在自由链表中的位置
        {
            if (bytes == 0) return 0;
            return ((bytes + ALIGNMENT - 1) >> ALIGN_SHIFT) - 1;
        }
    };
}