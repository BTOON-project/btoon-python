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
 * @file encoder.h
 * @brief Header file for the BTOON Encoder class.
 * 
 * This file defines the Encoder class, which provides methods to serialize data
 * into the BTOON binary format. It supports a variety of data types including
 * primitive types, arrays, maps, timestamps, and custom extensions.
 */
#ifndef BTOON_ENCODER_H
#define BTOON_ENCODER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "btoon.h"
#include "memory_pool.h"
#include "memory_pool.h"
#include "security.h"

namespace btoon {

/**
 * @brief Class responsible for encoding data into BTOON binary format.
 * 
 * The Encoder class provides a set of methods to convert various data types into
 * a compact binary representation following the BTOON specification. It is designed
 * for efficiency and minimal memory footprint.
 */
class Encoder {
public:
    /**
     * @brief Default constructor for Encoder.
     */
    Encoder();
    
    /**
     * @brief Constructor with a memory pool.
     * @param pool Pointer to a MemoryPool to use for allocations.
     */
    explicit Encoder(MemoryPool* pool);

    /**
     * @brief Constructor with security settings for HMAC signing.
     * @param security Reference to a Security object for signing encoded data.
     */
    explicit Encoder(const Security& security);
    
    /**
     * @brief Constructor with security settings and a memory pool.
     * @param security Reference to a Security object for signing encoded data.
     * @param pool Pointer to a MemoryPool to use for allocations.
     */
    Encoder(const Security& security, MemoryPool* pool);
    
    /**
     * @brief Default destructor for Encoder.
     */
    ~Encoder();

    /**
     * @brief Enables or disables security signing for encoded data.
     * @param enable Boolean to enable (true) or disable (false) security.
     */
    void setSecurityEnabled(bool enable);

    /**
     * @brief Gets the encoded data from the buffer.
     * @return A span of the encoded data.
     */
    std::span<const uint8_t> getBuffer();

    // Encode basic types
    /**
     * @brief Encodes a null value.
     */
    void encodeNil();
    
    /**
     * @brief Encodes a boolean value.
     * @param value The boolean value to encode.
     */
    void encodeBool(bool value);
    
    /**
     * @brief Encodes a signed integer value.
     * @param value The 64-bit signed integer to encode.
     */
    void encodeInt(int64_t value);
    
    /**
     * @brief Encodes an unsigned integer value.
     * @param value The 64-bit unsigned integer to encode.
     */
    void encodeUint(uint64_t value);
    
    /**
     * @brief Encodes a floating-point value.
     * @param value The double-precision floating-point number to encode.
     */
    void encodeFloat(double value);
    
    /**
     * @brief Encodes a string value.
     * @param value The string to encode.
     */
    void encodeString(const std::string& value);
    
    /**
     * @brief Encodes binary data.
     * @param value The vector of bytes to encode as binary data.
     */
    void encodeBinary(std::span<const uint8_t> value);

    // Encode compound types
    /**
     * @brief Encodes an array of elements.
     * @param elements A vector of encoded elements to be serialized as an array.
     */
    void encodeArray(const std::vector<std::vector<uint8_t>>& elements);
    
    /**
     * @brief Encodes a map of key-value pairs.
     * @param pairs A map of string keys to encoded values to be serialized as a map.
     */
    void encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs);

    // Encode timestamp
    /**
     * @brief Encodes a timestamp value.
     * @param timestamp A 64-bit integer representing a timestamp.
     */
    void encodeTimestamp(const Timestamp& timestamp);

    // Custom extension types
    void encodeDate(int64_t milliseconds);
    void encodeDateTime(int64_t nanoseconds);
    void encodeBigInt(std::span<const uint8_t> bytes);
    void encodeVectorFloat(const VectorFloat& value);
    void encodeVectorDouble(const VectorDouble& value);

    // Generic extension type
    void encodeExtension(int8_t type, std::span<const uint8_t> data);

    // Columnar encoding
    void encodeColumnar(const Array& data);

    // Encode a Value
    void encode(const Value& value);
    void setOptions(const EncodeOptions& opts) { options_ = opts; }

private:
    // Helper methods for encoding variable-length integers
    void encodeVarInt(uint64_t value, uint8_t prefix, uint8_t bits);
    
    /**
     * @brief Adds an HMAC signature to the encoded data if security is enabled.
     */
    void addSignatureIfEnabled();

    // SIMD-accelerated memory copy
    void simd_copy(uint8_t* dst, const uint8_t* src, size_t size) const;

    void grow_buffer(size_t needed);

    const Security* security_ = nullptr; /**< Pointer to Security object for HMAC signing. */
    bool useSecurity_ = false;           /**< Flag to enable/disable security signing. */
    MemoryPool* pool_ = nullptr;                   /**< Pointer to MemoryPool for allocations. */
    bool owns_pool_ = false;             /**< Flag to indicate if the Encoder owns the MemoryPool. */
    EncodeOptions options_;              /**< Encoding options for the encoder. */
    uint8_t* buffer_ = nullptr;          /**< Buffer for encoded data. */
    size_t capacity_ = 0;                /**< Capacity of the buffer. */
    size_t size_ = 0;                    /**< Current size of the encoded data. */
};

} // namespace btoon

#endif // BTOON_ENCODER_H
