#include "raknet_bitstream_view.h"

#include <algorithm>
#include <limits>

namespace {

BitStream* CreateBitStream(unsigned char* data, unsigned int lengthInBits, bool copyData) {
    const unsigned int safeLengthInBits = std::min(
        lengthInBits,
        static_cast<unsigned int>(std::numeric_limits<int>::max()));
    auto* bitStream = new BitStream(data, BITS_TO_BYTES(safeLengthInBits), copyData);
    bitStream->SetWriteOffset(static_cast<int>(safeLengthInBits));
    return bitStream;
}

} // namespace

RakNetBitStreamView::RakNetBitStreamView(BitStream* bitStream)
    : bitStream_(bitStream) {
}

RakNetBitStreamView::RakNetBitStreamView(unsigned char* data, unsigned int lengthInBits, bool copyData)
    : bitStream_(CreateBitStream(data, lengthInBits, copyData))
    , ownsBitStream_(true) {
}

RakNetBitStreamView::~RakNetBitStreamView() {
    if (ownsBitStream_) {
        delete bitStream_;
    }
}

int RakNetBitStreamView::GetNumberOfBitsUsed() const {
    return bitStream_ ? bitStream_->GetNumberOfBitsUsed() : 0;
}

int RakNetBitStreamView::GetNumberOfBytesUsed() const {
    return bitStream_ ? bitStream_->GetNumberOfBytesUsed() : 0;
}

int RakNetBitStreamView::GetNumberOfUnreadBits() const {
    return bitStream_ ? bitStream_->GetNumberOfUnreadBits() : 0;
}

int RakNetBitStreamView::GetNumberOfUnreadBytes() const {
    return BITS_TO_BYTES(GetNumberOfUnreadBits());
}

int RakNetBitStreamView::GetReadOffset() const {
    return bitStream_ ? bitStream_->GetReadOffset() : 0;
}

int RakNetBitStreamView::GetWriteOffset() const {
    return bitStream_ ? bitStream_->GetWriteOffset() : 0;
}

void RakNetBitStreamView::Reset() {
    if (bitStream_) {
        bitStream_->Reset();
    }
}

void RakNetBitStreamView::ResetReadPointer() {
    if (bitStream_) {
        bitStream_->ResetReadPointer();
    }
}

void RakNetBitStreamView::ResetWritePointer() {
    if (bitStream_) {
        bitStream_->ResetWritePointer();
    }
}

void RakNetBitStreamView::SetReadOffset(int offset) {
    if (bitStream_) {
        bitStream_->SetReadOffset(offset);
    }
}

void RakNetBitStreamView::SetWriteOffset(int offset) {
    if (bitStream_) {
        bitStream_->SetWriteOffset(offset);
    }
}

void RakNetBitStreamView::IgnoreBits(int count) {
    if (bitStream_) {
        bitStream_->IgnoreBits(count);
    }
}

void RakNetBitStreamView::IgnoreBytes(int count) {
    IgnoreBits(BYTES_TO_BITS(count));
}

bool RakNetBitStreamView::ReadBool() {
    bool value = false;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::int8_t RakNetBitStreamView::ReadInt8() {
    std::int8_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::int16_t RakNetBitStreamView::ReadInt16() {
    std::int16_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::int32_t RakNetBitStreamView::ReadInt32() {
    std::int32_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::uint8_t RakNetBitStreamView::ReadUInt8() {
    std::uint8_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::uint16_t RakNetBitStreamView::ReadUInt16() {
    std::uint16_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::uint32_t RakNetBitStreamView::ReadUInt32() {
    std::uint32_t value = 0;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

float RakNetBitStreamView::ReadFloat() {
    float value = 0.0f;
    if (bitStream_) {
        bitStream_->Read(value);
    }
    return value;
}

std::string RakNetBitStreamView::ReadString(int length) {
    if (!bitStream_ || length <= 0) {
        return {};
    }

    std::string value(static_cast<std::size_t>(length), '\0');
    if (!bitStream_->Read(value.data(), length)) {
        return {};
    }
    return value;
}

bool RakNetBitStreamView::ReadBits(unsigned char* output, int numberOfBits) {
    return bitStream_ && output && numberOfBits >= 0
        ? bitStream_->ReadBits(output, numberOfBits, false)
        : false;
}

bool RakNetBitStreamView::ReadCompressedUInt32(std::uint32_t& value) {
    return bitStream_ ? bitStream_->ReadCompressed(value) : false;
}

void RakNetBitStreamView::WriteBool(bool value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteInt8(std::int8_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteInt16(std::int16_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteInt32(std::int32_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteUInt8(std::uint8_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteUInt16(std::uint16_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteUInt32(std::uint32_t value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteFloat(float value) {
    if (bitStream_) {
        bitStream_->Write(value);
    }
}

void RakNetBitStreamView::WriteString(std::string_view value) {
    if (bitStream_ && !value.empty()) {
        bitStream_->Write(value.data(), static_cast<int>(value.size()));
    }
}

void RakNetBitStreamView::WriteBits(const unsigned char* input, int numberOfBits) {
    if (bitStream_ && input && numberOfBits > 0) {
        bitStream_->WriteBits(input, numberOfBits, false);
    }
}

void RakNetBitStreamView::WriteCompressedUInt32(std::uint32_t value) {
    if (bitStream_) {
        bitStream_->WriteCompressed(value);
    }
}

unsigned char* RakNetBitStreamView::Data() const {
    return bitStream_ ? bitStream_->GetData() : nullptr;
}

BitStream* RakNetBitStreamView::raw() const {
    return bitStream_;
}
