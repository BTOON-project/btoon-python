/**
 * @file batch_processor.h
 * @brief Batch processing optimizations for BTOON
 * 
 * Provides high-performance batch processing capabilities for large datasets,
 * including parallel processing, streaming, and memory-efficient operations.
 */

#ifndef BTOON_BATCH_PROCESSOR_H
#define BTOON_BATCH_PROCESSOR_H

#include "btoon/btoon.h"
#include "btoon/memory_pool.h"
#include <functional>
#include <vector>
#include <thread>
#include <future>
#include <queue>
#include <atomic>
#include <memory>

namespace btoon {
namespace batch {

/**
 * @brief Batch processing statistics
 */
struct BatchStatistics {
    size_t total_items = 0;
    size_t processed_items = 0;
    size_t failed_items = 0;
    size_t bytes_processed = 0;
    double processing_time_ms = 0.0;
    double items_per_second = 0.0;
    double bytes_per_second = 0.0;
    size_t peak_memory_usage = 0;
};

/**
 * @brief Batch processing options
 */
struct BatchOptions {
    size_t batch_size = 1000;           // Items per batch
    size_t max_memory_mb = 1024;        // Maximum memory usage in MB
    size_t worker_threads = 0;          // 0 = auto-detect
    bool use_memory_pool = true;        // Use memory pooling
    bool enable_statistics = true;      // Collect statistics
    bool preserve_order = false;        // Maintain input order in output
    size_t queue_size = 100;            // Maximum queued batches
    CompressionAlgorithm compression = CompressionAlgorithm::NONE;
};

/**
 * @brief Batch item with metadata
 */
template<typename T>
struct BatchItem {
    size_t index;
    T data;
    std::optional<std::string> key;
    std::chrono::steady_clock::time_point timestamp;
    
    BatchItem(size_t idx, T&& d) 
        : index(idx), data(std::move(d)), 
          timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Batch processing pipeline stage
 */
template<typename Input, typename Output>
class PipelineStage {
public:
    using TransformFunc = std::function<Output(const Input&)>;
    using BatchTransformFunc = std::function<std::vector<Output>(const std::vector<Input>&)>;
    
    PipelineStage(const std::string& name, TransformFunc func)
        : name_(name), transform_(func), is_batch_(false) {}
    
    PipelineStage(const std::string& name, BatchTransformFunc func)
        : name_(name), batch_transform_(func), is_batch_(true) {}
    
    std::vector<Output> process(const std::vector<Input>& input) {
        if (is_batch_) {
            return batch_transform_(input);
        } else {
            std::vector<Output> output;
            output.reserve(input.size());
            for (const auto& item : input) {
                output.push_back(transform_(item));
            }
            return output;
        }
    }
    
    const std::string& name() const { return name_; }
    
private:
    std::string name_;
    TransformFunc transform_;
    BatchTransformFunc batch_transform_;
    bool is_batch_;
};

/**
 * @brief Multi-stage processing pipeline
 */
template<typename T>
class ProcessingPipeline {
public:
    ProcessingPipeline(const BatchOptions& options = BatchOptions{})
        : options_(options) {}
    
    /**
     * @brief Add a processing stage
     */
    template<typename Func>
    ProcessingPipeline& add_stage(const std::string& name, Func func) {
        stages_.emplace_back(name, func);
        return *this;
    }
    
    /**
     * @brief Process items through the pipeline
     */
    std::vector<T> process(std::vector<T> input) {
        for (const auto& stage : stages_) {
            input = stage.process(input);
        }
        return input;
    }
    
    /**
     * @brief Process with progress callback
     */
    std::vector<T> process_with_progress(
        std::vector<T> input,
        std::function<void(size_t current, size_t total)> progress_cb) {
        
        size_t total_stages = stages_.size();
        for (size_t i = 0; i < total_stages; ++i) {
            input = stages_[i].process(input);
            progress_cb(i + 1, total_stages);
        }
        return input;
    }
    
private:
    BatchOptions options_;
    std::vector<PipelineStage<T, T>> stages_;
};

/**
 * @brief Parallel batch processor
 */
template<typename Input, typename Output>
class ParallelBatchProcessor {
public:
    using ProcessFunc = std::function<Output(const Input&)>;
    using ErrorHandler = std::function<void(const Input&, const std::exception&)>;
    
    ParallelBatchProcessor(ProcessFunc func, const BatchOptions& options = BatchOptions{})
        : process_func_(func), options_(options) {
        
        num_workers_ = options_.worker_threads;
        if (num_workers_ == 0) {
            num_workers_ = std::thread::hardware_concurrency();
        }
    }
    
    /**
     * @brief Process items in parallel batches
     */
    std::vector<Output> process(const std::vector<Input>& items) {
        if (items.empty()) return {};
        
        auto start_time = std::chrono::steady_clock::now();
        
        // Initialize workers
        std::vector<std::thread> workers;
        std::queue<std::vector<Input>> work_queue;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::atomic<bool> done{false};
        
        // Results storage
        std::vector<std::vector<Output>> results(num_workers_);
        std::vector<size_t> result_indices(num_workers_);
        
        // Split input into batches
        for (size_t i = 0; i < items.size(); i += options_.batch_size) {
            size_t end = std::min(i + options_.batch_size, items.size());
            work_queue.push(std::vector<Input>(items.begin() + i, items.begin() + end));
        }
        
        // Worker function
        auto worker = [&](size_t worker_id) {
            while (true) {
                std::vector<Input> batch;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    queue_cv.wait(lock, [&] { return !work_queue.empty() || done; });
                    
                    if (done && work_queue.empty()) break;
                    
                    batch = std::move(work_queue.front());
                    work_queue.pop();
                }
                
                // Process batch
                for (const auto& item : batch) {
                    try {
                        results[worker_id].push_back(process_func_(item));
                        stats_.processed_items++;
                    } catch (const std::exception& e) {
                        if (error_handler_) {
                            error_handler_(item, e);
                        }
                        stats_.failed_items++;
                    }
                }
            }
        };
        
        // Start workers
        for (size_t i = 0; i < num_workers_; ++i) {
            workers.emplace_back(worker, i);
        }
        
        // Wait for completion
        done = true;
        queue_cv.notify_all();
        for (auto& w : workers) {
            w.join();
        }
        
        // Merge results
        std::vector<Output> final_results;
        for (const auto& worker_results : results) {
            final_results.insert(final_results.end(), 
                               worker_results.begin(), 
                               worker_results.end());
        }
        
        // Update statistics
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        stats_.processing_time_ms = duration.count();
        stats_.items_per_second = stats_.processed_items * 1000.0 / stats_.processing_time_ms;
        
        return final_results;
    }
    
    /**
     * @brief Set error handler
     */
    void set_error_handler(ErrorHandler handler) {
        error_handler_ = handler;
    }
    
    /**
     * @brief Get processing statistics
     */
    const BatchStatistics& statistics() const { return stats_; }
    
private:
    ProcessFunc process_func_;
    ErrorHandler error_handler_;
    BatchOptions options_;
    size_t num_workers_;
    BatchStatistics stats_;
};

/**
 * @brief Streaming batch processor for large files
 */
class StreamingBatchProcessor {
public:
    using ProcessFunc = std::function<void(const Value&, size_t index)>;
    
    StreamingBatchProcessor(const BatchOptions& options = BatchOptions{})
        : options_(options), pool_(options.batch_size * 1024) {}
    
    /**
     * @brief Process BTOON file in streaming batches
     */
    void process_file(const std::string& filename, ProcessFunc func);
    
    /**
     * @brief Process stream in batches
     */
    void process_stream(std::istream& stream, ProcessFunc func);
    
    /**
     * @brief Process with transformation and output
     */
    template<typename Transform>
    void transform_file(const std::string& input_file,
                       const std::string& output_file,
                       Transform transform_func) {
        std::ifstream input(input_file, std::ios::binary);
        std::ofstream output(output_file, std::ios::binary);
        
        process_stream(input, [&](const Value& value, size_t index) {
            Value transformed = transform_func(value, index);
            // Write transformed value
            auto encoded = encode(transformed, {options_.compression});
            output.write(reinterpret_cast<const char*>(encoded.data()), 
                        encoded.size());
        });
    }
    
    const BatchStatistics& statistics() const { return stats_; }
    
private:
    BatchOptions options_;
    MemoryPool pool_;
    BatchStatistics stats_;
};

/**
 * @brief Map-Reduce processor for aggregation operations
 */
template<typename Key, typename Value, typename Result>
class MapReduceProcessor {
public:
    using MapFunc = std::function<std::pair<Key, Value>(const btoon::Value&)>;
    using ReduceFunc = std::function<Result(const Key&, const std::vector<Value>&)>;
    
    MapReduceProcessor(MapFunc mapper, ReduceFunc reducer, 
                      const BatchOptions& options = BatchOptions{})
        : mapper_(mapper), reducer_(reducer), options_(options) {}
    
    /**
     * @brief Process data with map-reduce
     */
    std::unordered_map<Key, Result> process(const std::vector<btoon::Value>& data) {
        // Map phase
        std::unordered_map<Key, std::vector<Value>> mapped;
        
        ParallelBatchProcessor<btoon::Value, std::pair<Key, Value>> map_processor(
            mapper_, options_);
        
        auto map_results = map_processor.process(data);
        
        // Group by key
        for (const auto& [key, value] : map_results) {
            mapped[key].push_back(value);
        }
        
        // Reduce phase
        std::unordered_map<Key, Result> results;
        for (const auto& [key, values] : mapped) {
            results[key] = reducer_(key, values);
        }
        
        return results;
    }
    
private:
    MapFunc mapper_;
    ReduceFunc reducer_;
    BatchOptions options_;
};

/**
 * @brief Window-based processor for time series and streaming data
 */
template<typename T>
class WindowProcessor {
public:
    enum WindowType {
        TUMBLING,   // Non-overlapping fixed windows
        SLIDING,    // Overlapping fixed windows
        SESSION     // Dynamic windows based on gaps
    };
    
    WindowProcessor(WindowType type, size_t window_size, 
                   size_t slide_interval = 0)
        : type_(type), window_size_(window_size), 
          slide_interval_(slide_interval ? slide_interval : window_size) {}
    
    /**
     * @brief Process items in windows
     */
    template<typename ProcessFunc>
    void process(const std::vector<T>& items, ProcessFunc func) {
        if (type_ == TUMBLING) {
            process_tumbling(items, func);
        } else if (type_ == SLIDING) {
            process_sliding(items, func);
        } else if (type_ == SESSION) {
            process_session(items, func);
        }
    }
    
private:
    WindowType type_;
    size_t window_size_;
    size_t slide_interval_;
    
    template<typename ProcessFunc>
    void process_tumbling(const std::vector<T>& items, ProcessFunc func) {
        for (size_t i = 0; i < items.size(); i += window_size_) {
            size_t end = std::min(i + window_size_, items.size());
            std::vector<T> window(items.begin() + i, items.begin() + end);
            func(window, i / window_size_);
        }
    }
    
    template<typename ProcessFunc>
    void process_sliding(const std::vector<T>& items, ProcessFunc func) {
        size_t window_id = 0;
        for (size_t i = 0; i + window_size_ <= items.size(); i += slide_interval_) {
            std::vector<T> window(items.begin() + i, items.begin() + i + window_size_);
            func(window, window_id++);
        }
    }
    
    template<typename ProcessFunc>
    void process_session(const std::vector<T>& items, ProcessFunc func) {
        // Session windows based on time gaps
        // Simplified implementation - would need timestamp comparison
        process_tumbling(items, func);
    }
};

// ============= Utility Functions =============

/**
 * @brief Split large file into chunks for parallel processing
 */
std::vector<std::pair<size_t, size_t>> split_file_chunks(
    const std::string& filename, 
    size_t num_chunks);

/**
 * @brief Merge sorted batches efficiently
 */
template<typename T, typename Compare = std::less<T>>
std::vector<T> merge_sorted_batches(
    const std::vector<std::vector<T>>& batches,
    Compare comp = Compare{});

/**
 * @brief Memory-mapped batch processing
 */
void process_mmap_file(const std::string& filename,
                      std::function<void(const uint8_t*, size_t)> processor,
                      const BatchOptions& options = BatchOptions{});

} // namespace batch
} // namespace btoon

#endif // BTOON_BATCH_PROCESSOR_H
