//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

#ifndef BTOON_MEMORY_POOL_H
#define BTOON_MEMORY_POOL_H

#include <cstddef>
#include <vector>
#include <memory>
#include <stack>
#include <cstdint>

namespace btoon {

/**
 * @brief Memory pool allocator for efficient memory management.
 * 
 * Features:
 * - SIMD-aligned allocations (32-byte alignment for AVX2)
 * - Block reuse through free list
 * - Configurable growth strategy
 * - Thread-safe option (disabled by default)
 */
class MemoryPool {
public:
    static constexpr size_t SIMD_ALIGNMENT = 32;  // 32-byte alignment for AVX2
    static constexpr size_t DEFAULT_BLOCK_SIZE = 65536;  // 64KB default block

    explicit MemoryPool(size_t initial_size = DEFAULT_BLOCK_SIZE);
    ~MemoryPool();

    // Non-copyable, non-movable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    /**
     * @brief Allocate aligned memory from the pool
     * @param size Number of bytes to allocate
     * @return Pointer to allocated memory (aligned to SIMD_ALIGNMENT)
     */
    void* allocate(size_t size);
    
    /**
     * @brief Return memory to the pool for reuse
     * @param ptr Pointer to memory to deallocate
     * @param size Size of the allocation
     */
    void deallocate(void* ptr, size_t size);
    
    /**
     * @brief Get total memory allocated by the pool
     */
    size_t total_allocated() const { return total_allocated_; }
    
    /**
     * @brief Get current memory in use
     */
    size_t current_usage() const { return current_usage_; }
    
    /**
     * @brief Reset the pool (keeps allocated blocks but marks them as free)
     */
    void reset();

private:
    struct Block {
        std::unique_ptr<uint8_t[], decltype(&std::free)> data;
        size_t size;
        
        Block(size_t sz);
    };
    
    struct FreeChunk {
        void* ptr;
        size_t size;
        
        FreeChunk(void* p, size_t s) : ptr(p), size(s) {}
    };
    
    void* allocate_from_block(size_t size);
    void new_block(size_t min_size);
    void* align_ptr(void* ptr, size_t alignment);
    
    std::vector<Block> blocks_;
    std::stack<FreeChunk> free_list_;
    
    uint8_t* current_pos_;
    size_t remaining_;
    size_t block_size_;
    size_t total_allocated_;
    size_t current_usage_;
};

} // namespace btoon

#endif // BTOON_MEMORY_POOL_H