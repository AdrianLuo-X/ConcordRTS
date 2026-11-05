void runServiceCredentialCertificateSmoke(SmokeContext& smoke, TestCertificate& certificate) {
    int checks = 0;
    const auto expect = [&](bool condition, const std::string& message) {
        ++checks;
        smoke.expect(condition, "credential/certificate owner: " + message);
    };
    const auto digestBytes = [](std::string_view text) {
        std::array<std::uint8_t, 32U> result{};
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (std::size_t i = 0; i != result.size(); ++i)
            result[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
        return result;
    };
    const std::array<std::pair<std::string_view, std::string_view>, 3> vectors{{
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"0123456789abcdef", "9f9f5111f7b27a781f1f1ddde5ebc2dd2b796bfc7365c9c28b548e564176929f"}
    }};
    for (const auto& [text, hex] : vectors) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        const auto original = bytes;
        std::array<std::uint8_t, 32U> observed{};
        expect(RuntimeInternetServiceCredentialPrimitives::hash(bytes, observed)
            && observed == digestBytes(hex), "same native SHA256 known vector");
        expect(bytes == original, "hash borrows input without mutation");
    }
    auto bytes = digestBytes(vectors.back().second);
    expect(RuntimeInternetServiceCredentialPrimitives::equal(bytes, bytes), "equal fixed digest");
    for (std::size_t i = 0; i != bytes.size(); ++i) {
        auto changed = bytes;
        changed[i] ^= 1U;
        expect(!RuntimeInternetServiceCredentialPrimitives::equal(bytes, changed),
            "digest mismatch at position " + std::to_string(i));
    }
    expect(!RuntimeInternetServiceCredentialPrimitives::equal(bytes,
        std::span<const std::uint8_t>(bytes).first(31)), "digest length mismatch");
    expect(RuntimeInternetServiceCredentialPrimitives::equal({}, {}), "equal empty spans");
    RuntimeInternetServiceCredentialPrimitives::clear(bytes);
    expect(std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0; }),
        "actual volatile array wipe covers every byte");
    RuntimeInternetServiceCredentialPrimitives::clear(bytes);
    expect(std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0; }),
        "repeated array wipe");
    std::vector<std::uint8_t> temporary(43, 0xa5);
    const auto capacity = temporary.capacity();
    RuntimeInternetServiceCredentialPrimitives::clear(temporary);
    expect(temporary.empty() && temporary.capacity() == capacity, "original vector wipe and clear lifecycle");
    RuntimeInternetServiceCredentialPrimitives::clear(temporary);
    expect(temporary.empty(), "empty vector clear");

    RuntimeInternetServiceCredentialVerifier verify;
    std::string error;
    {
        const std::string ini =
            "[Service]\nServiceId=credential-native\nListenPort=47070\nListenScope=LoopbackIpv4\n"
            "PublicRelayAddress=relay.example.invalid\nCertificateStoreLocation=CurrentUser\n"
            "CertificateStoreName=MY\nCertificateSha1=00112233445566778899AABBCCDDEEFF00112233\n"
            "[Credentials]\npeer-a=9F9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F\n";
        RuntimeInternetServiceHostDeploymentConfig config;
        expect(parseRuntimeInternetServiceHostConfigText(ini, config, error), "original strict config: " + error);
        auto copied = config.credentials;
        expect(copied.size() == 1 && config.credentials.size() == 1, "shared readonly store copy");
        auto moved = std::move(copied);
        expect(moved.size() == 1 && copied.size() == 0, "store move preserves ownership");
        verify = moved.makeVerifier(config.serviceId);
    }
    const std::string raw = "0123456789abcdef";
    const std::vector<std::uint8_t> credential(raw.begin(), raw.end());
    RuntimeInternetServiceCredentialRequest request;
    request.claimedServiceId = "untrusted-wire-claim";
    request.claimedPeerId = "peer-a";
    request.credential = credential;
    RuntimeInternetServiceVerifiedIdentity identity;
    expect(verify(request, identity, error) && identity.serviceId == "credential-native"
        && identity.peerId == "peer-a", "verifier retains store after every config/copy/move owner dies");
    for (std::size_t i = 0; i != credential.size(); ++i) {
        auto wrong = credential;
        wrong[i] ^= 1U;
        request.credential = wrong;
        identity = {"must", "clear"};
        expect(!verify(request, identity, error) && identity.serviceId.empty()
            && identity.peerId.empty(), "wrong credential position clears identity " + std::to_string(i));
    }
    for (const std::size_t length : {0U, 15U, 4097U}) {
        const std::vector<std::uint8_t> invalid(length, 0x41);
        request.credential = invalid;
        identity = {"must", "clear"};
        expect(!verify(request, identity, error) && identity.serviceId.empty()
            && identity.peerId.empty(), "invalid credential length clears identity");
    }
    request.credential = credential;
    request.claimedPeerId = "unknown";
    identity = {"must", "clear"};
    expect(!verify(request, identity, error) && identity.serviceId.empty()
        && identity.peerId.empty(), "unknown peer cannot inherit authority");
    request.claimedPeerId = "peer-a";
    expect(verify(request, identity, error) && identity.peerId == "peer-a",
        "rejected requests do not mutate shared verifier state");

    std::array<std::uint8_t, 20U> thumbprint{};
    DWORD thumbprintBytes = static_cast<DWORD>(thumbprint.size());
    expect(CertGetCertificateContextProperty(certificate.get(), CERT_SHA1_HASH_PROP_ID,
        thumbprint.data(), &thumbprintBytes) && thumbprintBytes == thumbprint.size(),
        "actual certificate thumbprint");
    const auto memoryStore = [&](int copies) {
        RuntimeCertificateStoreWin32 store(CertOpenStore(CERT_STORE_PROV_MEMORY,
            0U, 0U, CERT_STORE_CREATE_NEW_FLAG, nullptr));
        expect(static_cast<bool>(store), "real process-memory store opens");
        for (int i = 0; i != copies && store; ++i) {
            expect(CertAddCertificateContextToStore(store.get(), certificate.get(),
                CERT_STORE_ADD_ALWAYS, nullptr) != 0, "actual certificate added to memory store");
        }
        return store;
    };
    RuntimeInternetServiceCertificateContext empty;
    expect(!RuntimeInternetServiceCertificateFactory::selectFromOwnedStore(
        memoryStore(0), thumbprint, empty, error)
        && error == "server certificate thumbprint was not found"
        && !empty.valid() && empty.serverCertificate().empty(), "zero matches reject without owner");
    RuntimeInternetServiceCertificateContext loaded;
    expect(RuntimeInternetServiceCertificateFactory::selectFromOwnedStore(
        memoryStore(1), thumbprint, loaded, error) && loaded.valid()
        && !loaded.serverCertificate().empty(), "one match owns native duplicate after store closes: " + error);
    const auto borrowed = loaded.serverCertificate();
    if (loaded.valid()) {
        std::array<std::uint8_t, 20U> retainedThumbprint{};
        DWORD count = static_cast<DWORD>(retainedThumbprint.size());
        expect(CertGetCertificateContextProperty(
            RuntimeInternetTlsCertificateInterop::nativeCertificate(borrowed),
            CERT_SHA1_HASH_PROP_ID, retainedThumbprint.data(), &count)
            && retainedThumbprint == thumbprint, "owned duplicate valid after consumed memory store closed");
    }
    RuntimeInternetServiceCertificateContext moved(std::move(loaded));
    expect(!loaded.valid() && loaded.serverCertificate().empty()
        && moved.valid() && moved.serverCertificate() == borrowed, "PImpl move keeps stable certificate borrow");
    RuntimeInternetServiceCertificateContext assigned;
    expect(RuntimeInternetServiceCertificateFactory::selectFromOwnedStore(
        memoryStore(1), thumbprint, assigned, error), "destination owns previous native reference");
    assigned = std::move(moved);
    expect(!moved.valid() && assigned.valid() && assigned.serverCertificate() == borrowed,
        "move assignment releases previous owner and preserves original borrow");
    RuntimeInternetTlsServer tls;
    RuntimeInternetTlsServerConfig config;
    config.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    config.serverCertificate = assigned.serverCertificate();
    expect(tls.open(config, error), "selected certificate retains actual accessible private key: " + error);
    assigned.reset();
    expect(!assigned.valid() && assigned.serverCertificate().empty(), "owner reset clears standard view");
    if (tls.isOpen()) {
        RuntimeInternetTlsClientConfig client;
        client.endpoint = {"127.0.0.1", tls.port()};
        client.serverName = "localhost";
        client.peerTrust = certificate.peerTrust();
        client.leafCertificateSha256 = certificate.sha256(error);
        ConnectedPair pair;
        expect(waitForConnection(tls, client, true, pair, error),
            "actual TLS after store and selected context owners released: " + error);
        if (pair.client) pair.client->close();
        if (pair.server) pair.server->close();
    }
    tls.close();
    RuntimeInternetServiceCertificateContext duplicated;
    expect(!RuntimeInternetServiceCertificateFactory::selectFromOwnedStore(
        memoryStore(2), thumbprint, duplicated, error)
        && error == "server certificate thumbprint is not unique"
        && !duplicated.valid(), "duplicate thumbprint rejects exact original unique-match policy");

    RuntimeInternetServiceCertificateContext resetOnFailure;
    expect(RuntimeInternetServiceCertificateFactory::selectFromOwnedStore(
        memoryStore(1), thumbprint, resetOnFailure, error), "load before invalid selection");
    RuntimeInternetServiceCertificateSelection invalid;
    expect(!loadRuntimeInternetServiceCertificate(invalid, resetOnFailure, error)
        && error == "server certificate selection is invalid"
        && !resetOnFailure.valid(), "original public invalid selection resets previous owner before failure");
    RuntimeInternetServiceCertificateSelection unavailable;
    unavailable.location = RuntimeInternetCertificateStoreLocation::CurrentUser;
    unavailable.storeName = "ConcordP09Native" + std::to_string(nowMilliseconds());
    unavailable.sha1Thumbprint = thumbprint;
    expect(!loadRuntimeInternetServiceCertificate(unavailable, resetOnFailure, error)
        && error == "server certificate store could not be opened"
        && !resetOnFailure.valid(), "actual readonly existing-store unavailable path fails closed");
    std::cout << "Original credential primitives/shared verifier/memory certificate owner: "
        << checks << " checks executed\n";
}
