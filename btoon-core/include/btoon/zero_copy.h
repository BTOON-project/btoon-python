/**
 * @file zero_copy.h
 * @brief Zero-copy APIs for efficient language bindings
 * 
 * Provides memory-mapped and view-based access to BTOON data
 * to minimize copying between language runtimes.
 */

#ifndef BTOON_ZERO_COPY_H
#define BTOON_ZERO_COPY_H

#include "btoon.h"
#include <span>
#include <string_view>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "btoon/capi.h"

#ifndef BTOON_API
#ifdef _WIN32
#  ifdef BTOON_BUILD_SHARED
#    define BTOON_API __declspec(dllexport)
#  else
#    define BTOON_API
#  endif
#else
#  define BTOON_API __attribute__((visibility("default")))
#endif
#endif

namespace btoon {

struct ValueView;
using ArrayView = std::vector<ValueView>;
using MapView = std::unordered_map<std::string_view, ValueView>;

struct ValueView : std::variant<
    Nil,
    Bool,
    Int,
    Uint,
    Float,
    std::string_view,
    std::span<const uint8_t>,
    ArrayView,
    MapView,
    Timestamp,
    Extension
> {
    using variant::variant;
};

/**
 * @brief Memory view for zero-copy access to BTOON data
 * 
 * Provides a non-owning view into BTOON encoded data that can be
 * shared across language boundaries without copying.
 */
class MemoryView {
public:
    using Deleter = std::function<void(const uint8_t*)>;
    
    /**
     * @brief Create a view from existing memory
     * @param data Pointer to data
     * @param size Size of data
     * @param deleter Optional custom deleter for memory management
     */
    MemoryView(const uint8_t* data, size_t size, Deleter deleter = nullptr)
        : data_(data), size_(size), deleter_(deleter) {}
    
    /**
     * @brief Create a view from a span
     */
    explicit MemoryView(std::span<const uint8_t> span)
        : data_(span.data()), size_(span.size()), deleter_(nullptr) {}
    
    ~MemoryView() {
        if (deleter_ && data_) {
            deleter_(data_);
        }
    }
    
    // Move-only semantics
    MemoryView(MemoryView&& other) noexcept
        : data_(other.data_), size_(other.size_), deleter_(std::move(other.deleter_)) {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    
    MemoryView& operator=(MemoryView&& other) noexcept {
        if (this != &other) {
            if (deleter_ && data_) {
                deleter_(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            deleter_ = std::move(other.deleter_);
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    // Disable copy
    MemoryView(const MemoryView&) = delete;
    MemoryView& operator=(const MemoryView&) = delete;
    
    /**
     * @brief Get data pointer
     */
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get data size
     */
    size_t size() const { return size_; }
    
    /**
     * @brief Get as span
     */
    std::span<const uint8_t> span() const {
        return std::span<const uint8_t>(data_, size_);
    }
    
    /**
     * @brief Check if view is valid
     */
    bool valid() const { return data_ != nullptr && size_ > 0; }
    
    /**
     * @brief Release ownership without calling deleter
     */
    const uint8_t* release() {
        const uint8_t* ptr = data_;
        data_ = nullptr;
        size_ = 0;
        deleter_ = nullptr;
        return ptr;
    }
    
private:
    const uint8_t* data_;
    size_t size_;
    Deleter deleter_;
};

/**
 * @brief Zero-copy decoder that provides views into original data
 * 
 * Instead of copying strings and binary data, provides string_view
 * and span references into the original encoded buffer.
 */
class ZeroCopyDecoder {
public:
    struct Options {
        bool validate_utf8;
        bool check_bounds;
        size_t max_depth;

        Options()
            : validate_utf8(true),
              check_bounds(true),
              max_depth(128) {}
    };
    
    explicit ZeroCopyDecoder(const Options& opts = Options())
        : options_(opts) {}
    
    /**
     * @brief Decode with zero-copy for strings and binary
     * 
     * Returns a value tree where strings and binary data are
     * views into the original buffer rather than copies.
     */
    ValueView decode(const MemoryView& view);
    
    /**
     * @brief Decode from raw pointer (caller manages lifetime)
     */
    ValueView decode(const uint8_t* data, size_t size);
    
private:
    ValueView decodeValue(const uint8_t* data, size_t size, size_t& pos, size_t depth);
    Options options_;
};

/**
 * @brief Memory-mapped file for zero-copy file access
 */
class MemoryMappedFile {
public:
    /**
     * @brief Open a file for memory-mapped reading
     */
    static std::unique_ptr<MemoryMappedFile> open(const std::string& path);
    
    /**
     * @brief Get memory view of the file
     */
    MemoryView view() const {
        return MemoryView(data_, size_);
    }
    
    /**
     * @brief Get data pointer
     */
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get file size
     */
    size_t size() const { return size_; }
    
    ~MemoryMappedFile();
    
private:
    MemoryMappedFile(const uint8_t* data, size_t size, void* handle)
        : data_(data), size_(size), handle_(handle) {}
    
    const uint8_t* data_;
    size_t size_;
    void* handle_;  // Platform-specific handle
};

/**
 * @brief Shared memory buffer for inter-process communication
 */
class SharedMemoryBuffer {
public:
    /**
     * @brief Create or open a shared memory segment
     */
    static std::unique_ptr<SharedMemoryBuffer> create(
        const std::string& name, 
        size_t size,
        bool create_new = true
    );
    
    /**
     * @brief Get writable data pointer
     */
    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get buffer size
     */
    size_t size() const { return size_; }
    
    /**
     * @brief Get memory view for zero-copy access
     */
    MemoryView view() const {
        return MemoryView(data_, size_);
    }
    
    ~SharedMemoryBuffer();
    
private:
    SharedMemoryBuffer(uint8_t* data, size_t size, void* handle)
        : data_(data), size_(size), handle_(handle) {}
    
    uint8_t* data_;
    size_t size_;
    void* handle_;  // Platform-specific handle
};

/**
 * @brief Buffer pool for recycling allocations
 */
class BufferPool {
public:
    /**
     * @brief Buffer handle with automatic return to pool
     */
    class Buffer {
    public:
        Buffer(std::vector<uint8_t>&& data, BufferPool* pool)
            : data_(std::move(data)), pool_(pool) {}
        
        ~Buffer() {
            if (pool_ && !data_.empty()) {
                pool_->return_buffer(std::move(data_));
            }
        }
        
        // Move-only
        Buffer(Buffer&& other) noexcept
            : data_(std::move(other.data_)), pool_(other.pool_) {
            other.pool_ = nullptr;
        }
        
        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                if (pool_ && !data_.empty()) {
                    pool_->return_buffer(std::move(data_));
                }
                data_ = std::move(other.data_);
                pool_ = other.pool_;
                other.pool_ = nullptr;
            }
            return *this;
        }
        
        // Disable copy
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        
        uint8_t* data() { return data_.data(); }
        const uint8_t* data() const { return data_.data(); }
        size_t size() const { return data_.size(); }
        size_t capacity() const { return data_.capacity(); }
        
        void resize(size_t size) { data_.resize(size); }
        void clear() { data_.clear(); }
        
        std::vector<uint8_t>& vector() { return data_; }
        const std::vector<uint8_t>& vector() const { return data_; }
        
    private:
        std::vector<uint8_t> data_;
        BufferPool* pool_;
    };
    
    /**
     * @brief Create a buffer pool with specified parameters
     */
    explicit BufferPool(size_t max_buffers = 100, size_t default_size = 4096)
        : max_buffers_(max_buffers), default_size_(default_size) {}
    
    /**
     * @brief Get a buffer from the pool
     */
    Buffer get_buffer(size_t min_size = 0);
    
    /**
     * @brief Clear all cached buffers
     */
    void clear();
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        size_t buffers_in_use;
        size_t buffers_cached;
        size_t total_allocated;
        size_t cache_hits;
        size_t cache_misses;
    };
    
    Stats stats() const;
    
private:
    void return_buffer(std::vector<uint8_t>&& buffer);
    
    mutable std::mutex mutex_;
    std::vector<std::vector<uint8_t>> buffers_;
    size_t max_buffers_;
    size_t default_size_;
    
    // Statistics
    mutable size_t buffers_in_use_ = 0;
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    
    friend class Buffer;
};

/**
 * @brief Zero-copy encoder that writes directly to provided buffer
 */
class ZeroCopyEncoder {
public:
    /**
     * @brief Encode directly into provided buffer
     * @return Number of bytes written
     */
    size_t encode_into(const Value& value, uint8_t* buffer, size_t buffer_size);
    
    /**
     * @brief Encode into a buffer pool buffer
     */
    BufferPool::Buffer encode_pooled(const Value& value, BufferPool& pool);
    
    /**
     * @brief Encode into shared memory
     */
    size_t encode_into_shared(const Value& value, SharedMemoryBuffer& buffer);
};

/**
 * @brief C API for zero-copy operations (for FFI)
 */
extern "C" {

/**
 * @brief Opaque handle for memory view
 */
typedef struct btoon_memory_view* btoon_memory_view_t;

/**
 * @brief Create memory view from data
 */
BTOON_API btoon_memory_view_t btoon_memory_view_create(
    const uint8_t* data,
    size_t size,
    void (*deleter)(const uint8_t*)
);

/**
 * @brief Get data pointer from memory view
 */
BTOON_API const uint8_t* btoon_memory_view_data(btoon_memory_view_t view);

/**
 * @brief Get size from memory view
 */
BTOON_API size_t btoon_memory_view_size(btoon_memory_view_t view);

/**
 * @brief Release memory view
 */
BTOON_API void btoon_memory_view_free(btoon_memory_view_t view);

/**
 * @brief Memory map a file
 */
BTOON_API btoon_memory_view_t btoon_mmap_file(const char* path);

/**
 * @brief Unmap a memory-mapped file
 */
BTOON_API void btoon_munmap_file(btoon_memory_view_t view);

/**
 * @brief Create/open shared memory
 */
BTOON_API btoon_memory_view_t btoon_shm_create(
    const char* name,
    size_t size,
    int create_new
);

/**
 * @brief Close shared memory
 */
BTOON_API void btoon_shm_close(btoon_memory_view_t view);

/**
 * @brief Zero-copy decode
 */
BTOON_API int btoon_decode_zero_copy(
    const uint8_t* data,
    size_t size,
    btoon_value_t* out_value
);

} // extern "C"

} // namespace btoon

#endif // BTOON_ZERO_COPY_H
