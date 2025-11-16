/**
 * @file timeseries.h
 * @brief Time-series optimizations for BTOON
 * 
 * Provides specialized data structures and algorithms for efficient
 * storage, compression, and analysis of time-series data.
 */

#ifndef BTOON_TIMESERIES_H
#define BTOON_TIMESERIES_H

#include "btoon/btoon.h"
#include "btoon/compression.h"
#include "btoon/delta_codec.h"
#include <vector>
#include <deque>
#include <optional>
#include <chrono>
#include <algorithm>
#include <functional>

namespace btoon {
namespace timeseries {

/**
 * @brief Time-series data point
 */
template<typename T>
struct DataPoint {
    Timestamp timestamp;
    T value;
    std::optional<std::unordered_map<std::string, Value>> metadata;
    
    DataPoint() = default;
    DataPoint(const Timestamp& ts, const T& val) 
        : timestamp(ts), value(val) {}
    DataPoint(const Timestamp& ts, T&& val)
        : timestamp(ts), value(std::move(val)) {}
};

/**
 * @brief Aggregation functions for time-series
 */
enum class AggregationFunction {
    MEAN,
    SUM,
    MIN,
    MAX,
    COUNT,
    MEDIAN,
    STDDEV,
    VARIANCE,
    FIRST,
    LAST,
    PERCENTILE
};

/**
 * @brief Downsampling methods
 */
enum class DownsamplingMethod {
    AVERAGE,      // Take average of points in window
    LTTB,         // Largest Triangle Three Buckets
    MIN_MAX,      // Keep min and max in each window
    FIRST_LAST,   // Keep first and last points
    RANDOM,       // Random sampling
    M4            // Min, Max, Mean, Median
};

/**
 * @brief Time-series compression methods
 */
enum class TimeSeriesCompression {
    NONE,
    DELTA,           // Delta encoding
    DELTA_DELTA,     // Double delta encoding
    XOR,             // XOR compression for floats
    GORILLA,         // Facebook's Gorilla compression
    SIMPLE8B,        // Simple-8b integer compression
    RLE,             // Run-length encoding
    DICTIONARY       // Dictionary compression for repeated values
};

/**
 * @brief Optimized time-series storage
 */
template<typename T>
class TimeSeries {
public:
    using value_type = DataPoint<T>;
    using iterator = typename std::vector<value_type>::iterator;
    using const_iterator = typename std::vector<value_type>::const_iterator;
    
    TimeSeries() = default;
    
    // Data insertion
    void append(const Timestamp& timestamp, const T& value);
    void append(const DataPoint<T>& point);
    void append_batch(const std::vector<DataPoint<T>>& points);
    
    // Data access
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    const DataPoint<T>& operator[](size_t index) const { return data_[index]; }
    const DataPoint<T>& at(size_t index) const { return data_.at(index); }
    
    // Iterators
    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }
    
    // Time range queries
    std::vector<DataPoint<T>> range(const Timestamp& start, const Timestamp& end) const;
    std::optional<DataPoint<T>> at_time(const Timestamp& timestamp) const;
    DataPoint<T> interpolate_at(const Timestamp& timestamp) const;
    
    // Aggregation
    T aggregate(AggregationFunction func, 
                const Timestamp& start = Timestamp{},
                const Timestamp& end = Timestamp{}) const;
    
    // Resampling
    TimeSeries<T> resample(std::chrono::milliseconds interval,
                          AggregationFunction func = AggregationFunction::MEAN) const;
    
    // Downsampling
    TimeSeries<T> downsample(size_t target_points,
                             DownsamplingMethod method = DownsamplingMethod::LTTB) const;
    
    // Moving window operations
    TimeSeries<T> moving_average(size_t window_size) const;
    TimeSeries<T> moving_sum(size_t window_size) const;
    TimeSeries<T> moving_min(size_t window_size) const;
    TimeSeries<T> moving_max(size_t window_size) const;
    TimeSeries<double> moving_stddev(size_t window_size) const;
    
    // Transformations
    TimeSeries<T> diff(size_t lag = 1) const;  // Difference series
    TimeSeries<double> pct_change() const;     // Percentage change
    TimeSeries<double> cumsum() const;         // Cumulative sum
    TimeSeries<T> shift(int periods) const;    // Shift by periods
    
    // Statistics
    T mean() const;
    T median() const;
    double stddev() const;
    double variance() const;
    T min() const;
    T max() const;
    double correlation(const TimeSeries<T>& other) const;
    double autocorrelation(size_t lag) const;
    
    // Missing data handling
    void fill_missing(const T& value);                    // Fill with constant
    void fill_forward();                                  // Forward fill
    void fill_backward();                                 // Backward fill
    void fill_interpolate();                             // Linear interpolation
    
    // Outlier detection
    std::vector<size_t> detect_outliers_zscore(double threshold = 3.0) const;
    std::vector<size_t> detect_outliers_iqr(double factor = 1.5) const;
    std::vector<size_t> detect_outliers_isolation_forest() const;
    
    // Compression
    std::vector<uint8_t> compress(TimeSeriesCompression method = TimeSeriesCompression::GORILLA) const;
    static TimeSeries<T> decompress(const std::vector<uint8_t>& data, TimeSeriesCompression method);
    
    // Serialization
    Value to_btoon(bool use_compression = true) const;
    static TimeSeries<T> from_btoon(const Value& value);
    
private:
    std::vector<DataPoint<T>> data_;
    
    // Compression helpers
    std::vector<uint8_t> compress_delta() const;
    std::vector<uint8_t> compress_gorilla() const;
    std::vector<uint8_t> compress_xor() const;
    
    // Downsampling helpers
    TimeSeries<T> downsample_lttb(size_t target_points) const;
    TimeSeries<T> downsample_m4(size_t target_points) const;
};

/**
 * @brief Multi-variate time series
 */
template<typename... Types>
class MultivariateSeries {
public:
    using TupleType = std::tuple<Types...>;
    using DataPointType = DataPoint<TupleType>;
    
    void append(const Timestamp& timestamp, const Types&... values);
    std::vector<DataPointType> range(const Timestamp& start, const Timestamp& end) const;
    
    // Extract individual series
    template<size_t Index>
    TimeSeries<typename std::tuple_element<Index, TupleType>::type> get_series() const;
    
    // Cross-correlation
    template<size_t I1, size_t I2>
    double cross_correlation(size_t lag = 0) const;
    
    // Cointegration test
    bool is_cointegrated(double significance_level = 0.05) const;
    
private:
    std::vector<DataPointType> data_;
};

/**
 * @brief Circular buffer for streaming time-series
 */
template<typename T>
class CircularTimeSeries {
public:
    CircularTimeSeries(size_t max_size) : max_size_(max_size) {}
    
    void append(const Timestamp& timestamp, const T& value);
    size_t size() const { return data_.size(); }
    size_t capacity() const { return max_size_; }
    
    // Get latest N points
    std::vector<DataPoint<T>> latest(size_t n) const;
    
    // Real-time statistics
    T current_mean() const;
    double current_stddev() const;
    T current_min() const;
    T current_max() const;
    
    // Anomaly detection for streaming
    bool is_anomaly(const T& value, double threshold = 3.0) const;
    
private:
    std::deque<DataPoint<T>> data_;
    size_t max_size_;
    
    // Running statistics
    mutable std::optional<T> cached_mean_;
    mutable std::optional<double> cached_stddev_;
};

/**
 * @brief Time-series database interface
 */
class TimeSeriesDB {
public:
    struct QueryResult {
        std::vector<Timestamp> timestamps;
        std::vector<Value> values;
        std::unordered_map<std::string, std::vector<Value>> tags;
    };
    
    // Data ingestion
    void write(const std::string& metric,
              const Timestamp& timestamp,
              const Value& value,
              const std::unordered_map<std::string, Value>& tags = {});
    
    void write_batch(const std::string& metric,
                    const std::vector<std::pair<Timestamp, Value>>& points,
                    const std::unordered_map<std::string, Value>& tags = {});
    
    // Queries
    QueryResult query(const std::string& metric,
                     const Timestamp& start,
                     const Timestamp& end,
                     const std::unordered_map<std::string, Value>& tag_filter = {}) const;
    
    // Aggregation queries
    QueryResult query_aggregate(const std::string& metric,
                               const Timestamp& start,
                               const Timestamp& end,
                               std::chrono::milliseconds interval,
                               AggregationFunction func,
                               const std::unordered_map<std::string, Value>& tag_filter = {}) const;
    
    // Continuous queries
    using QueryCallback = std::function<void(const QueryResult&)>;
    std::string register_continuous_query(const std::string& metric,
                                         std::chrono::milliseconds interval,
                                         QueryCallback callback);
    void unregister_continuous_query(const std::string& query_id);
    
    // Retention policies
    void set_retention_policy(const std::string& metric,
                            std::chrono::hours retention_period);
    
    // Persistence
    void save(const std::string& filename) const;
    void load(const std::string& filename);
    
private:
    struct MetricData {
        std::map<Timestamp, Value> data;
        std::unordered_map<std::string, std::vector<Value>> tags;
        std::optional<std::chrono::hours> retention;
    };
    
    std::unordered_map<std::string, MetricData> metrics_;
    std::unordered_map<std::string, QueryCallback> continuous_queries_;
};

/**
 * @brief Forecasting models
 */
class TimeSeriesForecaster {
public:
    enum class Model {
        ARIMA,
        EXPONENTIAL_SMOOTHING,
        HOLT_WINTERS,
        LINEAR_REGRESSION,
        POLYNOMIAL_REGRESSION,
        NEURAL_NETWORK
    };
    
    template<typename T>
    static TimeSeries<T> forecast(const TimeSeries<T>& series,
                                 size_t periods,
                                 Model model = Model::ARIMA);
    
    // Seasonal decomposition
    template<typename T>
    struct Decomposition {
        TimeSeries<T> trend;
        TimeSeries<T> seasonal;
        TimeSeries<T> residual;
    };
    
    template<typename T>
    static Decomposition<T> decompose(const TimeSeries<T>& series,
                                     size_t period);
    
    // Change point detection
    template<typename T>
    static std::vector<size_t> detect_changepoints(const TimeSeries<T>& series,
                                                  double penalty = 1.0);
};

/**
 * @brief Technical indicators for financial time-series
 */
class TechnicalIndicators {
public:
    template<typename T>
    static TimeSeries<T> sma(const TimeSeries<T>& series, size_t period);  // Simple Moving Average
    
    template<typename T>
    static TimeSeries<T> ema(const TimeSeries<T>& series, size_t period);  // Exponential Moving Average
    
    template<typename T>
    static TimeSeries<T> bollinger_bands(const TimeSeries<T>& series,
                                        size_t period = 20,
                                        double stddev_factor = 2.0);
    
    template<typename T>
    static TimeSeries<double> rsi(const TimeSeries<T>& series, size_t period = 14);  // Relative Strength Index
    
    template<typename T>
    static TimeSeries<double> macd(const TimeSeries<T>& series,
                                  size_t fast_period = 12,
                                  size_t slow_period = 26,
                                  size_t signal_period = 9);
};

// ============= Utility Functions =============

/**
 * @brief Align multiple time-series to common timestamps
 */
template<typename T>
std::vector<TimeSeries<T>> align_series(const std::vector<TimeSeries<T>>& series,
                                       const std::string& method = "outer");

/**
 * @brief Merge multiple time-series
 */
template<typename T>
TimeSeries<T> merge_series(const std::vector<TimeSeries<T>>& series,
                         std::function<T(const std::vector<T>&)> merge_func);

/**
 * @brief Convert time-series to/from pandas DataFrame format
 */
template<typename T>
Value timeseries_to_dataframe(const TimeSeries<T>& series);

template<typename T>
TimeSeries<T> dataframe_to_timeseries(const Value& dataframe);

} // namespace timeseries
} // namespace btoon

#endif // BTOON_TIMESERIES_H
