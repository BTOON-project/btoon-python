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
 * @file decoder.h
 * @brief Header file for the BTOON Decoder class.
 * 
 * This file defines the Decoder class, which provides methods to deserialize data
 * from the BTOON binary format into the unified btoon::Value type.
 */
#ifndef BTOON_DECODER_H
#define BTOON_DECODER_H

#include "btoon.h"
#include "security.h"
#include "memory_pool.h"
#include <vector>
#include <span>

namespace btoon {

class MemoryPool;

/**
 * @brief Class responsible for decoding data from BTOON binary format.
 * 
 * The Decoder class provides methods to deserialize binary data encoded in the
 * BTOON format back into a btoon::Value. It handles type detection and
 * conversion, ensuring data integrity with bounds checking and error handling.
 */
class Decoder {
public:
    /**
     * @brief Default constructor for Decoder.
     */
    Decoder();

    /**
     * @brief Constructor with a memory pool.
     * @param pool Pointer to a MemoryPool to use for allocations.
     */
    explicit Decoder(MemoryPool* pool);

    /**
     * @brief Constructor with security settings for HMAC verification.
     * @param security Reference to a Security object for verifying signatures.
     */
    explicit Decoder(const Security& security);

    /**
     * @brief Constructor with security settings and a memory pool.
     * @param security Reference to a Security object for verifying signatures.
     * @param pool Pointer to a MemoryPool to use for allocations.
     */
    Decoder(const Security& security, MemoryPool* pool);
    
    /**
     * @brief Default destructor for Decoder.
     */
    ~Decoder();

    /**
     * @brief Enables or disables security verification for decoded data.
     * @param enable Boolean to enable (true) or disable (false) security verification.
     */
    void setSecurityEnabled(bool enable) { useSecurity_ = enable; }

    /**
     * @brief Decodes a BTOON binary buffer into a btoon::Value.
     * 
     * This method deserializes the provided binary buffer and returns the data as
     * a btoon::Value that can hold any of the supported BTOON data types. If security 
     * is enabled, it verifies the HMAC signature before decoding.
     * 
     * @param buffer The binary data buffer to decode.
     * @return A btoon::Value containing the decoded value.
     * @throws BtoonException if the buffer is invalid, decoding fails, or signature verification fails.
     */
    Value decode(std::span<const uint8_t> buffer) const;
    std::pair<Value, size_t> decode_and_get_pos(std::span<const uint8_t> buffer) const;

private:
    Value decode(std::span<const uint8_t> buffer, size_t& pos) const;
    Nil decodeNil(size_t& pos) const;
    Bool decodeBool(std::span<const uint8_t> buffer, size_t& pos) const;
    Int decodeInt(std::span<const uint8_t> buffer, size_t& pos) const;
    Uint decodeUint(std::span<const uint8_t> buffer, size_t& pos) const;
    Float decodeFloat(std::span<const uint8_t> buffer, size_t& pos) const;
    String decodeString(std::span<const uint8_t> buffer, size_t& pos) const;
    Binary decodeBinary(std::span<const uint8_t> buffer, size_t& pos) const;
    Array decodeArray(std::span<const uint8_t> buffer, size_t& pos) const;
    Map decodeMap(std::span<const uint8_t> buffer, size_t& pos) const;

    Value decodeExtension(std::span<const uint8_t> buffer, size_t& pos) const;
    Date decodeDate(std::span<const uint8_t> buffer, size_t& pos, size_t len) const;
    DateTime decodeDateTime(std::span<const uint8_t> buffer, size_t& pos, size_t len) const;
    BigInt decodeBigInt(std::span<const uint8_t> buffer, size_t& pos, size_t len) const;
    VectorFloat decodeVectorFloat(std::span<const uint8_t> buffer, size_t& pos, size_t len) const;
    VectorDouble decodeVectorDouble(std::span<const uint8_t> buffer, size_t& pos, size_t len) const;

    uint64_t readVarInt(std::span<const uint8_t> buffer, size_t& pos, uint8_t bits) const;
    
    std::span<const uint8_t> verifyAndExtractData(std::span<const uint8_t> buffer) const;

    const Security* security_ = nullptr; /**< Pointer to Security object for HMAC verification. */
    bool useSecurity_ = false;           /**< Flag to enable/disable security verification. */
    MemoryPool* pool_;                   /**< Pointer to MemoryPool for allocations. */
    bool owns_pool_ = false;             /**< Flag to indicate if the Decoder owns the MemoryPool. */
};

} // namespace btoon

#endif // BTOON_DECODER_H
