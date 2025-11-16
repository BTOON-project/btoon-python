/**
 * @file validator.h
 * @brief Strict input validation for BTOON data
 * 
 * Provides comprehensive validation to ensure data integrity
 * and protect against malformed or malicious inputs.
 */

#ifndef BTOON_VALIDATOR_H
#define BTOON_VALIDATOR_H

// Prevent Windows from defining min/max macros that conflict with std::numeric_limits
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "btoon.h"
#include <span>
#include <string>
#include <vector>
#include <limits>
#include <optional>
#include <memory>

namespace btoon {

/**
 * @brief Validation options for controlling strictness
 */
struct ValidationOptions {
    // Size limits
    size_t max_depth = 128;              // Maximum nesting depth for containers
    size_t max_string_length = 1024 * 1024 * 10;  // 10MB max string
    size_t max_binary_length = 1024 * 1024 * 100; // 100MB max binary
    size_t max_array_size = 1000000;     // Maximum array elements
    size_t max_map_size = 100000;        // Maximum map entries
    size_t max_total_size = 1024 * 1024 * 1024;   // 1GB max total size
    
    // Type validation
    bool allow_nan_float = false;        // Allow NaN values in floats
    bool allow_inf_float = false;        // Allow infinity values in floats
    bool require_utf8_strings = true;    // Validate UTF-8 encoding
    bool allow_duplicate_map_keys = false; // Allow duplicate keys in maps
    
    // Security options
    bool check_circular_references = true; // Check for circular refs (if using references)
    bool validate_extension_types = true;  // Validate known extension types
    bool limit_recursion = true;          // Limit recursion to prevent stack overflow
    
    // Performance options
    bool fast_mode = false;               // Skip some expensive checks
    bool collect_stats = false;           // Collect validation statistics
};

/**
 * @brief Validation result with detailed error information
 */
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // Optional statistics
    struct Stats {
        size_t total_size = 0;
        size_t max_depth_reached = 0;
        size_t string_count = 0;
        size_t binary_count = 0;
        size_t array_count = 0;
        size_t map_count = 0;
        size_t largest_string = 0;
        size_t largest_binary = 0;
        size_t largest_array = 0;
        size_t largest_map = 0;
    };
    
    std::optional<Stats> stats;
    
    void addError(const std::string& error) {
        valid = false;
        errors.push_back(error);
    }
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
};

/**
 * @brief BTOON data validator
 */
class Validator {
public:
    explicit Validator(const ValidationOptions& options = ValidationOptions{});
    ~Validator();
    
    /**
     * @brief Validate encoded BTOON data
     * @param data Raw BTOON data to validate
     * @return Validation result with errors/warnings
     */
    ValidationResult validate(std::span<const uint8_t> data) const;
    
    /**
     * @brief Validate a decoded Value
     * @param value Decoded value to validate
     * @return Validation result with errors/warnings
     */
    ValidationResult validate(const Value& value) const;
    
    /**
     * @brief Quick validation check (less thorough but faster)
     * @param data Raw BTOON data to validate
     * @return True if data appears valid
     */
    bool quickCheck(std::span<const uint8_t> data) const;
    
    /**
     * @brief Sanitize untrusted input
     * @param data Raw data to sanitize
     * @return Sanitized data (may be modified or empty if invalid)
     */
    std::vector<uint8_t> sanitize(std::span<const uint8_t> data) const;
    
private:
    class ValidatorImpl;
    std::unique_ptr<ValidatorImpl> pimpl_;
};

/**
 * @brief Input bounds checker for safe parsing
 */
class BoundsChecker {
public:
    explicit BoundsChecker(std::span<const uint8_t> data);
    
    /**
     * @brief Check if reading n bytes at position is safe
     */
    bool canRead(size_t position, size_t n) const;
    
    /**
     * @brief Safely read a byte
     */
    std::optional<uint8_t> readByte(size_t& position) const;
    
    /**
     * @brief Safely read multiple bytes
     */
    std::optional<std::span<const uint8_t>> readBytes(size_t& position, size_t n) const;
    
    /**
     * @brief Get remaining bytes from position
     */
    size_t remaining(size_t position) const;
    
    /**
     * @brief Check if position is within bounds
     */
    bool inBounds(size_t position) const;
    
private:
    std::span<const uint8_t> data_;
};

/**
 * @brief UTF-8 validator
 */
class UTF8Validator {
public:
    /**
     * @brief Check if string is valid UTF-8
     * @param str String to validate
     * @return True if valid UTF-8
     */
    static bool isValid(std::string_view str);
    
    /**
     * @brief Sanitize string to valid UTF-8
     * @param str String to sanitize
     * @return Sanitized string with invalid sequences replaced
     */
    static std::string sanitize(std::string_view str);
    
private:
    static bool isValidCodePoint(uint32_t cp);
    static int getUTF8SequenceLength(uint8_t lead);
};

/**
 * @brief Type validator for specific BTOON types
 */
class TypeValidator {
public:
    /**
     * @brief Validate integer range
     */
    static bool validateInt(int64_t value, int64_t min = std::numeric_limits<int64_t>::min(),
                           int64_t max = std::numeric_limits<int64_t>::max());
    
    /**
     * @brief Validate unsigned integer range
     */
    static bool validateUint(uint64_t value, uint64_t min = 0,
                            uint64_t max = std::numeric_limits<uint64_t>::max());
    
    /**
     * @brief Validate float value
     */
    static bool validateFloat(double value, bool allowNaN = false, bool allowInf = false);
    
    /**
     * @brief Validate timestamp
     */
    static bool validateTimestamp(const Timestamp& ts);
    
    /**
     * @brief Validate extension type
     */
    static bool validateExtension(const Extension& ext);
};

/**
 * @brief Security-focused validator
 */
class SecurityValidator {
public:
    /**
     * @brief Check for potential security issues
     * @param data Raw BTOON data
     * @return List of security concerns found
     */
    static std::vector<std::string> checkSecurity(std::span<const uint8_t> data);
    
    /**
     * @brief Check for zip bomb patterns
     */
    static bool checkForZipBomb(std::span<const uint8_t> data);
    
    /**
     * @brief Check for excessive nesting (stack overflow risk)
     */
    static bool checkNestingDepth(std::span<const uint8_t> data, size_t maxDepth = 128);
    
    /**
     * @brief Check for unreasonable size claims
     */
    static bool checkSizeClaims(std::span<const uint8_t> data);
};

/**
 * Convenience functions
 */

/**
 * @brief Validate BTOON data with default options
 */
inline ValidationResult validate(std::span<const uint8_t> data) {
    Validator validator;
    return validator.validate(data);
}

/**
 * @brief Quick validation check
 */
inline bool isValid(std::span<const uint8_t> data) {
    Validator validator;
    return validator.quickCheck(data);
}

/**
 * @brief Strict validation with security checks
 */
inline ValidationResult validateStrict(std::span<const uint8_t> data) {
    ValidationOptions opts;
    opts.max_depth = 32;  // More restrictive
    opts.max_string_length = 1024 * 1024;  // 1MB
    opts.max_total_size = 100 * 1024 * 1024;  // 100MB
    opts.allow_duplicate_map_keys = false;
    opts.require_utf8_strings = true;
    
    Validator validator(opts);
    return validator.validate(data);
}

} // namespace btoon

#endif // BTOON_VALIDATOR_H
