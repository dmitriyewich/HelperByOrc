#pragma once

#include <string_view>

namespace text_pattern_input {

struct ChatlogSample {
    std::string_view payload;
    bool timestampRemoved = false;
};

ChatlogSample ExtractChatlogPayload(std::string_view value);

} // namespace text_pattern_input
