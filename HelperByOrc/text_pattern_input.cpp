#include "text_pattern_input.h"

namespace text_pattern_input {
namespace {

bool IsDigit(char value) {
    return value >= '0' && value <= '9';
}

int TwoDigits(char tens, char ones) {
    return (tens - '0') * 10 + (ones - '0');
}

} // namespace

ChatlogSample ExtractChatlogPayload(std::string_view value) {
    constexpr std::size_t kTimestampLength = 10;
    if (value.size() < kTimestampLength
        || value[0] != '['
        || value[3] != ':'
        || value[6] != ':'
        || value[9] != ']'
        || !IsDigit(value[1])
        || !IsDigit(value[2])
        || !IsDigit(value[4])
        || !IsDigit(value[5])
        || !IsDigit(value[7])
        || !IsDigit(value[8])) {
        return {value, false};
    }

    const int hour = TwoDigits(value[1], value[2]);
    const int minute = TwoDigits(value[4], value[5]);
    const int second = TwoDigits(value[7], value[8]);
    if (hour > 23 || minute > 59 || second > 59) {
        return {value, false};
    }

    std::size_t payloadOffset = kTimestampLength;
    if (payloadOffset < value.size() && value[payloadOffset] == ' ') {
        ++payloadOffset;
    }
    return {value.substr(payloadOffset), true};
}

} // namespace text_pattern_input
