#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>

static const int RADIX_LEVELS = 4;
static const int BITS_PER_LEVEL = 13;
static const int RADIX_SIZE = 1 << BITS_PER_LEVEL;
static const int RADIX_MASK = RADIX_SIZE - 1;

template <int MAX_BITS = 52> class RadixTree {
private:
  struct Node {
    void *children[RADIX_SIZE];
  };

  Node *root;

  inline Node* allocNode() {
    Node *node = (Node *)std::malloc(sizeof(Node));
    if (node) {
      memset(node, 0, sizeof(Node));
    }
    return node;
  }

public:
  RadixTree() { root = allocNode(); }

  ~RadixTree() {
    freeRecursive(root, 0);
  }

  void freeRecursive(Node *node, int level) {
    if (!node) return;
    if (level < RADIX_LEVELS - 1) {
      for (int i = 0; i < RADIX_SIZE; ++i) {
        if (node->children[i]) {
          freeRecursive((Node *)node->children[i], level + 1);
        }
      }
    }
    std::free(node);
  }

  inline void *get(size_t pageId) const {
    Node *cur = root;
    if (!cur) return nullptr;

    for (int level = 0; level < RADIX_LEVELS; ++level) {
      size_t idx = (pageId >> (BITS_PER_LEVEL * (RADIX_LEVELS - 1 - level))) & RADIX_MASK;
      if (level == RADIX_LEVELS - 1) {
        return cur->children[idx];
      }
      cur = (Node *)cur->children[idx];
      if (!cur) return nullptr;
    }

    return nullptr;
  }

  inline void set(size_t pageId, void *spanPtr) {
    Node *cur = root;

    for (int level = 0; level < RADIX_LEVELS - 1; ++level) {
      size_t idx = (pageId >> (BITS_PER_LEVEL * (RADIX_LEVELS - 1 - level))) & RADIX_MASK;

      if (cur->children[idx] == nullptr) {
        cur->children[idx] = allocNode();
        if (!cur->children[idx]) return;
      }
      cur = (Node *)cur->children[idx];
    }

    size_t idx = pageId & RADIX_MASK;
    cur->children[idx] = spanPtr;
  }

  inline void erase(size_t pageId) {
    Node *cur = root;
    if (!cur) return;

    for (int level = 0; level < RADIX_LEVELS; ++level) {
      size_t idx = (pageId >> (BITS_PER_LEVEL * (RADIX_LEVELS - 1 - level))) & RADIX_MASK;
      if (level == RADIX_LEVELS - 1) {
        cur->children[idx] = nullptr;
        return;
      }
      cur = (Node *)cur->children[idx];
      if (!cur) return;
    }
  }
};