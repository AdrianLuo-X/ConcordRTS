bool tlsBorrowCanBind(std::uint16_t port) {
    SOCKET candidate = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate == INVALID_SOCKET) return false;
    const BOOL exclusive = TRUE;
    const int option = setsockopt(candidate, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const bool bound = option == 0 && bind(candidate,
        reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    closesocket(candidate);
    return bound;
}

void runTlsCertificateBorrowSmoke(SmokeContext& smoke, TestCertificate& certificate) {
    int checks = 0;
    const auto expect = [&](bool condition, const std::string& message) {
        ++checks;
        smoke.expect(condition, "typed certificate borrow: " + message);
    };
    expect(RuntimeInternetTlsServerCertificate{}.empty(), "default certificate");
    expect(RuntimeInternetTlsPeerTrust{}.empty(), "default system trust");
    expect(!certificate.serverCertificate().empty(), "owned certificate view");
    expect(!certificate.peerTrust().empty(), "owned private trust view");
    expect(RuntimeInternetTlsCertificateInterop::nativeCertificate(
        certificate.serverCertificate()) == certificate.get(), "same native certificate");
    expect(RuntimeInternetTlsCertificateInterop::nativePeerTrust(
        certificate.peerTrust()) == certificate.chainEngine(), "same native trust");

    std::string error;
    RuntimeInternetTlsServer server;
    RuntimeInternetTlsServerConfig config;
    config.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    expect(!server.open(config, error), "empty identity rejects listener");
    expect(!server.isOpen() && server.port() == 0, "empty identity leaves closed listener");
    for (int iteration = 0; iteration != 3; ++iteration) {
        // The caller's duplicate and private binding both die before the
        // actual handshake. open() must have acquired its original own ref.
        {
            PCCERT_CONTEXT borrowed = CertDuplicateCertificateContext(certificate.get());
            expect(borrowed != nullptr, "caller duplicate created");
            RuntimeInternetTlsCertificateInterop binding(borrowed);
            config.serverCertificate = binding.serverCertificate();
            error.clear();
            expect(server.open(config, error), "open borrowed certificate: " + error);
            if (borrowed) CertFreeCertificateContext(borrowed);
            borrowed = nullptr;
        }
        if (!server.isOpen()) break;
        RuntimeInternetTlsClientConfig client;
        client.endpoint = {"127.0.0.1", server.port()};
        client.serverName = "localhost";
        client.peerTrust = certificate.peerTrust();
        client.leafCertificateSha256 = certificate.sha256(error);
        ConnectedPair pair;
        error.clear();
        expect(waitForConnection(server, client, true, pair, error),
            "handshake after caller reference/binding destruction: " + error);
        if (pair.client && pair.server) {
            const std::vector<std::uint8_t> bytes{0, 1, 0x7f, 0x80, 0xff};
            expect(transferBytes(*pair.client, *pair.server, bytes, error),
                "same authenticated stream: " + error);
            pair.client->close();
            pair.server->close();
        }
        server.close();
        server.close();
        expect(!server.isOpen() && server.connectionCount() == 0,
            "idempotent close and reentry");
    }

    // Keep another real WSA reference alive so final WSACleanup cannot hide a
    // leaked listen socket. These listeners never accept a connection, so a
    // failed post-control bind cannot be caused by TCP TIME_WAIT.
    WSADATA data{};
    const bool started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    expect(started, "independent socket subsystem lifetime");
    if (started) {
        config.serverCertificate = certificate.serverCertificate();
        std::uint16_t destroyedPort = 0;
        {
            RuntimeInternetTlsServer scoped;
            expect(scoped.open(config, error), "open destructor listener");
            destroyedPort = scoped.port();
            expect(destroyedPort && !tlsBorrowCanBind(destroyedPort),
                "destructor pre-control listener really owns port");
        }
        expect(destroyedPort && tlsBorrowCanBind(destroyedPort),
            "destructor releases listen socket before unrelated WSA cleanup");

        RuntimeInternetTlsServer destination, source;
        expect(destination.open(config, error), "open destination listener");
        expect(source.open(config, error), "open source listener");
        const auto priorDestinationPort = destination.port();
        const auto sourcePort = source.port();
        expect(priorDestinationPort && sourcePort != priorDestinationPort
            && !tlsBorrowCanBind(priorDestinationPort)
            && !tlsBorrowCanBind(sourcePort), "active move pre-controls");
        destination = std::move(source);
        expect(destination.isOpen() && destination.port() == sourcePort
            && !source.isOpen(), "active move preserves source listener");
        expect(tlsBorrowCanBind(priorDestinationPort),
            "move assignment releases previous destination listen socket");
        auto* same = &destination;
        destination = std::move(*same);
        expect(destination.isOpen() && destination.port() == sourcePort,
            "self move preserves active listener");
        source.close();
        RuntimeInternetTlsServer moved(std::move(destination));
        expect(moved.isOpen() && moved.port() == sourcePort
            && !destination.isOpen(), "move construction preserves active listener");
        destination.close();
        moved.close();
        expect(tlsBorrowCanBind(sourcePort), "explicit close releases moved listener");

        RuntimeInternetTlsServer reopened;
        expect(reopened.open(config, error), "open active-reentry listener");
        const auto firstPort = reopened.port();
        expect(firstPort && !tlsBorrowCanBind(firstPort), "active-reentry pre-control");
        expect(reopened.open(config, error), "active listener can reopen");
        const auto secondPort = reopened.port();
        expect(secondPort && secondPort != firstPort && tlsBorrowCanBind(firstPort),
            "active reopen releases previous listen socket");
        expect(!tlsBorrowCanBind(secondPort), "replacement listener really owns new port");
        RuntimeInternetTlsServerConfig invalid;
        invalid.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
        expect(!reopened.open(invalid, error) && !reopened.isOpen(),
            "failed reentry preserves original closed-on-failure behavior");
        expect(tlsBorrowCanBind(secondPort), "failed reopen also releases previous listener");
        WSACleanup();
    }
    std::cout << "TLS certificate/trust borrow and listener lifetime: "
        << checks << " checks executed\n";
}
