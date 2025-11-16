#ifndef BTOON_DELTA_CODEC_H
#define BTOON_DELTA_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class DeltaCodec {
public:
    static std::vector<uint8_t> encode(const Array& data);
    static Array decode(const std::vector<uint8_t>& data);
};

} // namespace btoon

#endif // BTOON_DELTA_CODEC_H
