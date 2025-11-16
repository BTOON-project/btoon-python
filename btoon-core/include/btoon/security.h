//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net & https://github.com/BTOON-project/btoon-coren// https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT


/**
 * @file security.h
 * @brief Header file for the BTOON Security class.
 * 
 * This file defines the Security class, which provides mechanisms for ensuring
 * data integrity and safety during serialization and deserialization in the BTOON
 * library. It includes HMAC signing for integrity and type restriction for secure
 * deserialization.
 */
#ifndef BTOON_SECURITY_H
#define BTOON_SECURITY_H

#include "btoon.h"
#include <string>
#include <vector>
#include <set>

namespace btoon {

/**
 * @brief Class responsible for security features in BTOON.
 * 
 * The Security class offers methods to protect BTOON data integrity using HMAC
 * signatures and to prevent deserialization attacks by restricting allowed data
 * types. It is designed to enhance the safety of handling untrusted data.
 */
class Security {
public:
    /**
     * @brief Default constructor for Security.
     */
    Security() = default;
    
    /**
     * @brief Default destructor for Security.
     */
    ~Security() = default;

    /**
     * @brief Sets a secret key for HMAC signing.
     * 
     * This method sets the secret key used for generating and verifying HMAC
     * signatures to ensure data integrity.
     * 
     * @param key The secret key as a string.
     */
    void setSecretKey(const std::string& key);

    /**
     * @brief Signs data with an HMAC for integrity protection.
     * 
     * This method generates an HMAC-SHA256 signature for the provided data using
     * the set secret key.
     * 
     * @param data The binary data to sign.
     * @return A vector of bytes representing the HMAC signature.
     * @throws BtoonException if no secret key is set.
     */
    std::vector<uint8_t> sign(std::span<const uint8_t> data) const;

    /**
     * @brief Verifies an HMAC signature for data integrity.
     * 
     * This method checks if the provided signature matches the computed HMAC-SHA256
     * signature for the data using the set secret key.
     * 
     * @param data The binary data to verify.
     * @param signature The signature to check against.
     * @return True if the signature is valid, false otherwise.
     * @throws BtoonException if no secret key is set.
     */
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature) const;

    /**
     * @brief Sets the allowed data types for deserialization.
     * 
     * This method defines a whitelist of data types that are permitted during
     * deserialization. Types are identified by their index in the btoon::Value variant.
     * 
     * @param types A set of allowed type indices (size_t).
     */
    void setAllowedTypes(const std::set<size_t>& types);

    /**
     * @brief Checks if a data type is allowed during deserialization.
     * 
     * This method determines whether a given data type index is in the whitelist of
     * allowed types. If no restrictions are set, all types are allowed.
     * 
     * @param type_index The index of the type in the btoon::Value variant.
     * @return True if the type is allowed, false otherwise.
     */
    bool isTypeAllowed(size_t type_index) const;

private:
    std::string secretKey_;              /**< Secret key for HMAC operations. */
    std::set<size_t> allowedTypes_;      /**< Set of allowed type indices for deserialization. */
    bool restrictTypes_ = false;         /**< Flag indicating if type restriction is active. */
};

} // namespace btoon

#endif // BTOON_SECURITY_H
