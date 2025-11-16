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
 * @file stream_encoder.h
 * @brief Header file for the BTOON StreamEncoder class.
 */
#ifndef BTOON_STREAM_ENCODER_H
#define BTOON_STREAM_ENCODER_H

#include "btoon.h"
#include <iostream>
#include <memory>

namespace btoon {

class StreamEncoderImpl; // Pimpl idiom

class StreamEncoder {
public:
    StreamEncoder(std::ostream& stream, const EncodeOptions& options = {});
    ~StreamEncoder();

    void write(const Value& value);
    void close();

private:
    std::unique_ptr<StreamEncoderImpl> pimpl_;
};

} // namespace btoon

#endif // BTOON_STREAM_ENCODER_H
