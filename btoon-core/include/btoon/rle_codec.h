#ifndef BTOON_RLE_CODEC_H
#define BTOON_RLE_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class RleCodec {
public:
    static std::vector<uint8_t> encode(const Array& data);
    static Array decode(const std::vector<uint8_t>& data);
};

} // namespace btoon

#endif // BTOON_RLE_CODEC_H
