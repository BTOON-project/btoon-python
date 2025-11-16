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
/**
 * @file compression.h
 * @brief Header file for BTOON data compression.
 */
#ifndef BTOON_COMPRESSION_H
#define BTOON_COMPRESSION_H

#include <cstdint>
#include <vector>
#include <span>

namespace btoon {

/**
 * @brief Enumeration of supported compression algorithms.
 */
enum class CompressionAlgorithm : uint8_t {
    NONE = 255,
    ZLIB = 0,
    LZ4 = 1,
    ZSTD = 2,
    BROTLI = 3,
    SNAPPY = 4,
    AUTO = 254  // Automatically select best algorithm
};

/**
 * @brief Compression level presets for easy configuration
 */
enum class CompressionLevel : int8_t {
    // Predefined levels
    FASTEST = 1,      // Maximum speed, minimal compression
    FAST = 2,         // Fast compression with reasonable ratio
    BALANCED = 3,     // Balance between speed and compression
    HIGH = 4,         // Higher compression, slower speed
    MAXIMUM = 5,      // Maximum compression, slowest speed
    
    // Special values
    DEFAULT = BALANCED,
    CUSTOM = -1       // Use custom numeric level
};

/**
 * @brief Compression profile for different use cases
 */
struct CompressionProfile {
    CompressionAlgorithm algorithm;
    int numeric_level;  // Algorithm-specific level
    size_t min_size;    // Minimum size to compress (bytes)
    bool adaptive;      // Enable adaptive compression
    
    CompressionProfile(CompressionAlgorithm algo = CompressionAlgorithm::ZLIB,
                      int level = 6,
                      size_t min = 256,
                      bool adapt = false)
        : algorithm(algo), numeric_level(level), min_size(min), adaptive(adapt) {}
    
    // Predefined profiles
    static CompressionProfile realtime();
    static CompressionProfile network();
    static CompressionProfile storage();
    static CompressionProfile streaming();
};

/**
 * @brief Compresses data using the specified algorithm.
 *
 * @param algorithm The compression algorithm to use.
 * @param data The binary data to compress.
 * @param level The compression level (algorithm-specific).
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, int level = 0);

/**
 * @brief Compresses data using a compression level preset.
 *
 * @param algorithm The compression algorithm to use.
 * @param data The binary data to compress.
 * @param level The compression level preset.
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, CompressionLevel level);

/**
 * @brief Compresses data using a compression profile.
 *
 * @param profile The compression profile to use.
 * @param data The binary data to compress.
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(const CompressionProfile& profile, std::span<const uint8_t> data);

/**
 * @brief Decompresses data using the specified algorithm.
 *
 * @param algorithm The compression algorithm to use.
 * @param compressed_data The compressed binary data.
 * @return A vector of bytes representing the decompressed data.
 */
std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data);

// --- Algorithm-specific implementations ---

std::vector<uint8_t> compress_zlib(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_zlib(std::span<const uint8_t> compressed_data);

// --- Helper functions ---

/**
 * @brief Converts compression level preset to algorithm-specific numeric level.
 */
int get_numeric_level(CompressionAlgorithm algo, CompressionLevel level);

/**
 * @brief Selects the best compression algorithm based on data characteristics.
 */
CompressionAlgorithm select_best_algorithm(std::span<const uint8_t> data, bool prefer_speed = false);

/**
 * @brief Estimates compression ratio for given data and algorithm.
 */
float estimate_compression_ratio(CompressionAlgorithm algo, std::span<const uint8_t> data);

#ifdef BTOON_WITH_LZ4
std::vector<uint8_t> compress_lz4(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_lz4(std::span<const uint8_t> compressed_data);
#endif

#ifdef BTOON_WITH_ZSTD
std::vector<uint8_t> compress_zstd(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_zstd(std::span<const uint8_t> compressed_data);
#endif

#ifdef BTOON_WITH_BROTLI
std::vector<uint8_t> compress_brotli(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_brotli(std::span<const uint8_t> compressed_data);
#endif

#ifdef BTOON_WITH_SNAPPY
std::vector<uint8_t> compress_snappy(std::span<const uint8_t> data);
std::vector<uint8_t> decompress_snappy(std::span<const uint8_t> compressed_data);
#endif

} // namespace btoon

#endif // BTOON_COMPRESSION_H
