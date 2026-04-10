#pragma once

#include <array>
#include <cstddef>

namespace XmemoryPool
{
    // 内存对齐和大小相关常量
    constexpr size_t ALIGNMENT = 8;                          // 最小对齐数
    constexpr size_t MAX_BYTES = 256 * 1024;                  // 最大小对象 256KB
    constexpr size_t TRANSFER_TARGET_BYTES = 4 * 1024;        // 单次批量搬运目标字节数

    // Size Classes 数量（约100个而不是32768个）
    // 参考 TCMalloc 的非线性分级设计
    constexpr size_t kNumClasses = 100;

    // Size Classes 表：每个下标对应的内存块大小
    // 由静态初始化器自动生成
    class SizeClassTable
    {
    public:
        static constexpr size_t size(size_t index) 
        {
            return kSizeClasses[index];
        }

        static size_t getIndex(size_t size)
        {
            // 二分查找合适的 Size Class
            size_t left = 0, right = kNumClasses;
            while (left < right) {
                size_t mid = (left + right) / 2;
                if (size < kSizeClasses[mid]) {
                    right = mid;
                } else {
                    left = mid + 1;
                }
            }
            // 确保返回有效的索引
            return (left > 0) ? left - 1 : 0;
        }

        static size_t numClasses() { return kNumClasses; }

    private:
        // 预计算的 Size Classes 表
        // [0, 128): 8 字节步长 -> 16 个
        // [128, 256): 16 字节步长 -> 8 个
        // [256, 512): 32 字节步长 -> 8 个
        // [512, 1024): 64 字节步长 -> 8 个
        // [1024, 2048): 128 字节步长 -> 8 个
        // [2048, 4096): 256 字节步长 -> 8 个
        // [4096, 8192): 512 字节步长 -> 8 个
        // [8192, 16384): 1024 字节步长 -> 6 个
        // [16384, 32768): 2048 字节步长 -> 8 个
        // [32768, 65536): 4096 字节步长 -> 7 个
        // [65536, 131072): 8192 字节步长 -> 8 个
        // [131072, 262144]: 16384 字节步长 -> 9 个
        // 总计: 16 + 8 + 8 + 8 + 8 + 8 + 8 + 6 + 8 + 7 + 8 + 9 = 95 个
        // 精简为约 86 个
        static constexpr size_t kSizeClasses[kNumClasses] = {
            // [0, 128): 8 字节步长，16个
            8, 16, 24, 32, 40, 48, 56, 64,
            72, 80, 88, 96, 104, 112, 120, 128,
            // [128, 256): 16 字节步长，8个
            144, 160, 176, 192, 208, 224, 240, 256,
            // [256, 512): 32 字节步长，8个
            288, 320, 352, 384, 416, 448, 480, 512,
            // [512, 1024): 64 字节步长，8个
            576, 640, 704, 768, 832, 896, 960, 1024,
            // [1024, 2048): 128 字节步长，8个
            1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048,
            // [2048, 4096): 256 字节步长，8个
            2304, 2560, 2816, 3072, 3328, 3584, 3840, 4096,
            // [4096, 8192): 512 字节步长，8个
            4608, 5120, 5632, 6144, 6656, 7168, 7680, 8192,
            // [8192, 16384): 1024 字节步长，5个
            9216, 10240, 11264, 12288, 13312,
            // [16384, 32768): 2048 字节步长，8个
            18432, 20480, 22528, 24576, 26624, 28672, 30720, 32768,
            // [32768, 65536): 4096 字节步长，7个
            36864, 40960, 45056, 49152, 53248, 57344, 61440,
            // [65536, 131072): 8192 字节步长，8个
            73728, 81920, 90112, 98304, 106496, 114688, 122880, 131072,
            // [131072, 262144): 16384 字节步长，8个
            147456, 163840, 180224, 196608, 212992, 229376, 245760, 262144
        };
    };

    // 兼容旧的 FREE_LIST_SIZE（实际使用 SizeClassTable::numClasses()）
    constexpr size_t FREE_LIST_SIZE = kNumClasses;

    struct FreeListPolicy
    {
        size_t batchCount;
        size_t maxFreeListLength;
    };

    class TransferPolicyTable
    {
    public:
        static constexpr FreeListPolicy get(size_t index)
        {
            return buildPolicy(SizeClassTable::size(index));
        }

    private:
        static constexpr size_t clamp(size_t value, size_t low, size_t high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        static constexpr FreeListPolicy buildPolicy(size_t size)
        {
            const size_t batch = clamp(TRANSFER_TARGET_BYTES / size, 2, 32);
            return {batch, batch * 2};
        }

    };

    // 大小类（兼容旧接口）
    class SizeClass
    {
    public:
        static size_t roundUp(size_t bytes)
        {
            if (bytes == 0) return SizeClassTable::size(0);
            size_t index = SizeClassTable::getIndex(bytes);
            return SizeClassTable::size(index);
        }

        static size_t getIndex(size_t bytes)
        {
            if (bytes == 0) return 0;
            return SizeClassTable::getIndex(bytes);
        }

        static size_t getSizeByIndex(size_t index)
        {
            return SizeClassTable::size(index);
        }
    };
}
