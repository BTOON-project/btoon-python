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
 * @file stream_decoder.h
 * @brief Header file for the BTOON StreamDecoder class.
 */
#ifndef BTOON_STREAM_DECODER_H
#define BTOON_STREAM_DECODER_H

#include "btoon.h"
#include <iostream>
#include <memory>
#include <optional>

namespace btoon {

class StreamDecoderImpl; // Pimpl idiom

class StreamDecoder {
public:
    StreamDecoder(std::istream& stream, const DecodeOptions& options = {});
    ~StreamDecoder();

    std::optional<Value> read();
    bool has_next();

private:
    std::unique_ptr<StreamDecoderImpl> pimpl_;
};

} // namespace btoon

#endif // BTOON_STREAM_DECODER_H
