// Faults wrap real Schannel channels; authentication and relay routing still
// run through the production transport and service host.
struct ReconnectFaultControl {
    unsigned streamGeneration = 0;
    bool silentRelayLoss = false;
    bool dropCoordinator = false;
    bool waitForFlowReauthentication = false;
    std::uint64_t offlineUntil = 0;
    unsigned reauthentications = 0;
    std::vector<std::uint64_t> openedRoutes;
};

class ReconnectFaultStream final : public RuntimeInternetAuthenticatedTcpStream {
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream_;
    std::shared_ptr<ReconnectFaultControl> fault_;
    unsigned generation_;
public:
    ReconnectFaultStream(std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream,
        std::shared_ptr<ReconnectFaultControl> fault)
        : stream_(std::move(stream)), fault_(std::move(fault)), generation_(fault_->streamGeneration) {}
    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity() const override { return stream_->identity(); }
    RuntimeInternetTcpStreamIoResult read(std::span<std::uint8_t> bytes) override {
        if (generation_ != fault_->streamGeneration) {
            if (fault_->silentRelayLoss) return {};
            stream_->close();return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        return stream_->read(bytes);
    }
    RuntimeInternetTcpStreamIoResult write(std::span<const std::uint8_t> bytes) override {
        if (generation_ != fault_->streamGeneration) {
            if (fault_->silentRelayLoss) return {};
            stream_->close();return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        return stream_->write(bytes);
    }
    void close() noexcept override { stream_->close(); }
};

std::map<std::string, std::shared_ptr<ReconnectFaultControl>> reconnectFaults;

class ReconnectFaultAdapter final : public RuntimeInternetServiceAdapter {
    std::unique_ptr<RuntimeInternetServiceTransport> transport_;
    std::shared_ptr<ReconnectFaultControl> fault_;
public:
    ReconnectFaultAdapter(std::unique_ptr<RuntimeInternetServiceTransport> transport,
        std::shared_ptr<ReconnectFaultControl> fault)
        : transport_(std::move(transport)), fault_(std::move(fault)) {}
    std::string_view authenticatedServiceId() const noexcept override { return transport_->authenticatedServiceId(); }
    std::string_view authenticatedPeerId() const noexcept override { return transport_->authenticatedPeerId(); }
    bool sendCoordinator(const RuntimeNetworkPacket& packet, std::string& error) override {
        return transport_->sendCoordinator(packet, error);
    }
    bool sendCoordinatorWithAdmissionProof(const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> proof, std::string& error) override {
        return transport_->sendCoordinatorWithAdmissionProof(packet, proof, error);
    }
    bool openAuthenticatedTcpRelay(const RuntimeInternetServiceTcpOpenRequest& request, std::string& error) override {
        return transport_->openAuthenticatedTcpRelay(request, error);
    }
    bool reauthenticateCoordinator(std::string& error) override {
        if (nowMilliseconds() < fault_->offlineUntil) { error = "injected outage";return false; }
        ++fault_->reauthentications;
        fault_->waitForFlowReauthentication = false;
        return transport_->reauthenticateCoordinator(error);
    }
    std::vector<RuntimeInternetServiceEvent> poll(int timeoutMilliseconds) override {
        if (fault_->dropCoordinator) {
            fault_->dropCoordinator = false;
            fault_->waitForFlowReauthentication = true;
            std::string ignored;
            // Tear down the old real TLS channel. The flow must retry its own
            // authentication after the injected offline interval.
            (void)transport_->reauthenticateCoordinator(ignored);
            RuntimeInternetServiceEvent event;
            event.kind = RuntimeInternetServiceEventKind::Failure;
            event.authenticatedServiceId = std::string(authenticatedServiceId());
            event.authenticatedPeerId = std::string(authenticatedPeerId());
            event.reason = "injected coordinator disconnect";
            std::vector<RuntimeInternetServiceEvent> events;
            events.push_back(std::move(event));return events;
        }
        if (fault_->waitForFlowReauthentication || nowMilliseconds() < fault_->offlineUntil) return {};
        auto events = transport_->poll(timeoutMilliseconds);
        for (auto& event : events) {
            if (event.tcpStream) {
                fault_->openedRoutes.push_back(event.connectionId);
                event.tcpStream = std::make_unique<ReconnectFaultStream>(std::move(event.tcpStream), fault_);
            }
        }
        return events;
    }
    void close() noexcept override { transport_->close(); }
};
