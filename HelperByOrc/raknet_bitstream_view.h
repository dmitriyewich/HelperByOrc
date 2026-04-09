#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "external/raknet/BitStream.h"

class RakNetBitStreamView {
public:
    explicit RakNetBitStreamView(BitStream* bitStream);
    RakNetBitStreamView(unsigned char* data, unsigned int lengthInBits, bool copyData);
    ~RakNetBitStreamView();

    int GetNumberOfBitsUsed() const;
    int GetNumberOfBytesUsed() const;
    int GetNumberOfUnreadBits() const;
    int GetNumberOfUnreadBytes() const;
    int GetReadOffset() const;
    int GetWriteOffset() const;

    void Reset();
    void ResetReadPointer();
    void ResetWritePointer();
    void SetReadOffset(int offset);
    void SetWriteOffset(int offset);
    void IgnoreBits(int count);
    void IgnoreBytes(int count);

    bool ReadBool();
    std::int8_t ReadInt8();
    std::int16_t ReadInt16();
    std::int32_t ReadInt32();
    std::uint8_t ReadUInt8();
    std::uint16_t ReadUInt16();
    std::uint32_t ReadUInt32();
    float ReadFloat();
    std::string ReadString(int length);
    bool ReadBits(unsigned char* output, int numberOfBits);
    bool ReadCompressedUInt32(std::uint32_t& value);

    void WriteBool(bool value);
    void WriteInt8(std::int8_t value);
    void WriteInt16(std::int16_t value);
    void WriteInt32(std::int32_t value);
    void WriteUInt8(std::uint8_t value);
    void WriteUInt16(std::uint16_t value);
    void WriteUInt32(std::uint32_t value);
    void WriteFloat(float value);
    void WriteString(std::string_view value);
    void WriteBits(const unsigned char* input, int numberOfBits);
    void WriteCompressedUInt32(std::uint32_t value);

    unsigned char* Data() const;
    BitStream* raw() const;

private:
    BitStream* bitStream_ = nullptr;
    bool ownsBitStream_ = false;
};
