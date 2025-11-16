#ifndef BTOON_DICTIONARY_CODEC_H
#define BTOON_DICTIONARY_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class DictionaryCodec {
public:
    static std::vector<uint8_t> encode(const Array& data);
    static Array decode(const std::vector<uint8_t>& data);
};

} // namespace btoon

#endif // BTOON_DICTIONARY_CODEC_H
