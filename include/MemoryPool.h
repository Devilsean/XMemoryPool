#pragma once

#include "ThreadCache.h"

namespace XmemoryPool
{
    // 作为对外的统一接口层，隐藏了内部复杂的三级缓存架构
    // 提供allocate()方法和deallocate()静态方法，内部调用ThreadCache实例完成实际操作
    class MemoryPool
    {
    public:
        static void *allocate(size_t size) // 分配内存
        {
            return ThreadCache::getInstance()->allocate(size);
        }

        static void deallocate(void *ptr, size_t size) // 释放内存
        {
            ThreadCache::getInstance()->deallocate(ptr, size);
        }
    };
}