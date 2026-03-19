#pragma once
#include <cstring>

template <int BITS>
class RadixTree {
private:
    // 假设 BITS = 20 (对于 32 位系统，4KB 页)
    // 一级用 10 位，二级用 10 位
    static const int INTER_BITS = 10; 
    static const int INTER_SIZE = 1 << INTER_BITS; // 1024

    static const int LEAF_BITS = BITS - INTER_BITS;
    static const int LEAF_SIZE = 1 << LEAF_BITS; // 1024

    struct LeafNode {
        void* values[LEAF_SIZE]; // 存储 Span* 指针
    };

    LeafNode* root[INTER_SIZE]; // 一级数组，存放指向二级节点的指针

public:
    RadixTree() {
        memset(root, 0, sizeof(root));
    }

    // 获取页号对应的 Span
    inline void* get(size_t pageId) const {
        size_t i1 = pageId >> LEAF_BITS;        // 高 10 位
        size_t i2 = pageId & (LEAF_SIZE - 1);  // 低 10 位

        if (i1 >= INTER_SIZE || root[i1] == nullptr) return nullptr;
        return root[i1]->values[i2];
    }

    // 建立页号到 Span 的映射
    inline void set(size_t pageId, void* spanPtr) {
        size_t i1 = pageId >> LEAF_BITS;
        size_t i2 = pageId & (LEAF_SIZE - 1);

        if (root[i1] == nullptr) {
            // 只有用到这块内存时，才申请二级的空间（Lazy Allocation）
            root[i1] = new LeafNode;
            memset(root[i1], 0, sizeof(LeafNode));
        }
        root[i1]->values[i2] = spanPtr;
    }

    // 删除页号对应的映射   
    void erase(size_t pageId) {
        size_t i1 = pageId >> LEAF_BITS;
        size_t i2 = pageId & (LEAF_SIZE - 1);
        if (i1 < INTER_SIZE && root[i1] != nullptr) {
            root[i1]->values[i2] = nullptr;
        }
    }
};