RakClientInterface* SampApi::getRakClientInterface() {
    if (!isSAMPInitilizeLua()) {
        return nullptr;
    }

    std::uint32_t cNetGame = 0;
    if (!ResolveSampInfo(cNetGame) || cNetGame == 0) {
        SetError("CNetGame pointer is not available");
        return nullptr;
    }

    std::uint32_t rakClient = 0;
    if (!SafeRead(cNetGame + main_offsets.rakclient_interface.Get(currentVersion_), rakClient) || rakClient == 0) {
        SetError("RakClientInterface pointer is not available");
        return nullptr;
    }

    ClearError();
    return reinterpret_cast<RakClientInterface*>(rakClient);
}

bool SampApi::sendRpc(int id, BitStream& bitStream) {
    auto* rakClient = getRakClientInterface();
    if (!rakClient) {
        return false;
    }

    int rpcId = id;
    bool ok = false;
    if (!CallRakRpc(rakClient, &rpcId, &bitStream, ok) || !ok) {
        SetError("RakClientInterface::RPC failed");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::sendPacket(BitStream& bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel) {
    auto* rakClient = getRakClientInterface();
    if (!rakClient) {
        return false;
    }

    bool ok = false;
    if (!CallRakSend(rakClient, &bitStream, priority, reliability, orderingChannel, ok) || !ok) {
        SetError("RakClientInterface::Send failed");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::sendDialogResponse(
    int dialogId,
    int button,
    int listItem,
    std::string_view inputText,
    bool alreadyDecoded) {
    std::string gameText = PrepareOutgoingText(inputText, alreadyDecoded, false);
    if (gameText.size() > 255) {
        SetError("Dialog input text is too long");
        return false;
    }

    BitStream bitStream;
    bitStream.Write(static_cast<std::int16_t>(dialogId));
    bitStream.Write(static_cast<std::uint8_t>(button == 1 ? 1 : 0));
    bitStream.Write(static_cast<std::int16_t>(listItem));
    bitStream.Write(static_cast<std::uint8_t>(gameText.size()));
    if (!gameText.empty()) {
        bitStream.Write(gameText.data(), static_cast<int>(gameText.size()));
    }

    return sendRpc(62, bitStream);
}

