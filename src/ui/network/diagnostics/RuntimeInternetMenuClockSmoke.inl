// Compiled inside the actual native Internet UI assembly, not PURE_HELPERS_ONLY.
class InternetMenuContractClock final : public RuntimeUiClock {
public:
    std::uint64_t value = 0;
    mutable std::vector<std::uint64_t> reads;
    std::uint64_t nowMs() const noexcept override {
        reads.push_back(value);
        return value;
    }
};

class InternetMenuContractAdapter final : public RuntimeInternetServiceAdapter {
public:
    std::vector<RuntimeNetworkPacket> sent;
    std::vector<int> polls;
    int closes = 0;
    std::string_view authenticatedServiceId() const noexcept override { return "clock.service"; }
    std::string_view authenticatedPeerId() const noexcept override { return "clock.peer"; }
    bool sendCoordinator(const RuntimeNetworkPacket& packet, std::string& error) override {
        sent.push_back(packet);error.clear();return true;
    }
    bool openAuthenticatedTcpRelay(const RuntimeInternetServiceTcpOpenRequest&, std::string& error) override {
        error="Clock fixture has no relay";return false;
    }
    std::vector<RuntimeInternetServiceEvent> poll(int timeout) override {
        polls.push_back(timeout);return {};
    }
    void close() noexcept override { ++closes; }
};

bool internetMenuClockPacketsEqual(
    const std::vector<RuntimeNetworkPacket>& a, const std::vector<RuntimeNetworkPacket>& b
) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].type!=b[i].type || a[i].payload!=b[i].payload)return false;
    return true;
}

int internetMenuClockContract(const std::filesystem::path& output) {
    if(!output.is_absolute() || std::filesystem::exists(output)) {
        std::cerr<<"Internet clock requires a new absolute output directory\n";return 1;
    }
    std::filesystem::create_directories(output);
    std::ofstream trace(output/"internet-clock.txt",std::ios::binary);
    int checks=0,failures=0;std::size_t comparisons=0,reads=0;
    auto expect=[&](bool value,const char* why) {
        ++checks;if(!value){++failures;std::cerr<<"Internet clock FAILED: "<<why<<'\n';}
    };
    InternetMenuContractClock clock; // Outlives every UI state that borrows it.
    RuntimeInternetMultiplayerMenuUiState state;
    expect(state.clock==&runtimeWin32UiClock(),"default is existing immutable native provider");
    for(int i=0;i<8;++i) {
        const auto first=GetTickCount64();const auto value=internetMenuNowMilliseconds(state);const auto last=GetTickCount64();
        expect(first<=value && value<=last,"default clock is bracketed by native monotonic samples");
    }
    state.clock=&clock;
    std::string error;
    state.window=createInternetMenuWindow(state,error);
    expect(state.window!=nullptr,"actual production Internet window created");
    if(!state.window)return 1;
    createInternetMenuControls(state);
    expect(IsWindow(state.roomList) && IsWindow(state.statusText) && IsWindow(state.playerNameEdit),"actual production controls created");
    pollInternetMenu(state);expect(clock.reads.empty(),"missing flow returns before clock read");
    joinSelectedRoom(state);expect(clock.reads.empty(),"missing room selection returns before clock read");
    beginQuickMatch(state);expect(clock.reads.empty(),"empty name returns before host setup or clock read");
    expect(!beginRoomRefresh(state,error) && !error.empty(),"invalid config fails closed before flow use");
    expect(clock.reads.empty(),"invalid query configuration does not read clock");
    state.serviceConfig.serviceAddress="127.0.0.1";state.serviceConfig.servicePort=443;
    state.serviceConfig.serverName="clock.example.test";state.serviceConfig.serviceId="clock.service";
    state.serviceConfig.peerId="clock.peer";state.serviceConfig.credential.assign(16U,0x5aU);
    state.compatibility.protocolVersion=runtimeMultiplayerProtocolVersion;
    state.compatibility.protocolFingerprint=0x1122334455667788ULL;
    state.compatibility.contentFingerprint=0xa1b2c3d4e5f60718ULL;
    // These are UI-clock cases. The unchanged production Flow is the policy oracle;
    // no local implementation of retry/timeout, no global fake, no FakeWorld.
    const std::array<std::uint64_t,5> bases{{0U,1U,0xffffffffULL-128U,0x100000000ULL+500U,
        std::numeric_limits<std::uint64_t>::max()-3000U}};
    for(std::size_t scene=0;scene<bases.size();++scene) {
        const auto base=bases[scene];const auto before=clock.reads.size();
        auto adapter=std::make_unique<InternetMenuContractAdapter>();auto* current=adapter.get();
        state.flowBackend=std::make_unique<RuntimeInternetLobbyFlow>(std::move(adapter));
        state.page=InternetMenuPage::Browser;clock.value=base;
        expect(beginRoomRefresh(state,error),"actual UI room refresh begins in production Flow");
        expect(clock.reads.size()==before+1U && clock.reads.back()==base,"query reads exactly once at original call site");
        expect(state.flowBackend->mode()==RuntimeInternetLobbyFlowMode::QueryingRooms,"query mode is active");
        expect(current->sent.empty(),"UI query did not force early transport dispatch");
        pollInternetMenu(state);
        expect(clock.reads.size()==before+2U,"poll reads once");
        expect(current->sent.size()==1U,"production poll dispatched initial query packet");
        if(current->sent.size()!=1U)break;
        RuntimeInternetControlMessage decoded;
        expect(decodeRuntimeInternetControlMessage(current->sent[0],decoded,error),"real query packet decodes");
        RuntimeInternetLobbyQueryOptions options;
        expect(makeRuntimeInternetMultiplayerQueryOptions(state.serviceConfig,state.compatibility,decoded.transactionId,options,error),"same captured transaction produces reference options");
        auto oracleAdapter=std::make_unique<InternetMenuContractAdapter>();auto* oracle=oracleAdapter.get();
        RuntimeInternetLobbyFlow reference(std::move(oracleAdapter));
        expect(reference.beginRoomQuery(std::move(options),base,error),"unchanged production reference begins same query");
        reference.poll(base,0);
        const std::array<std::uint64_t,8> deltas{{0U,249U,250U,251U,4999U,5000U,5001U,5002U}};
        auto compare=[&]() {
            ++comparisons;
            expect(state.flowBackend->mode()==reference.mode(),"UI/reference complete exposed mode equal");
            expect(state.flowBackend->primaryConnectionState()==reference.primaryConnectionState(),"connection state equal");
            expect(state.flowBackend->rooms().size()==reference.rooms().size(),"rooms equal");
            expect(internetMenuClockPacketsEqual(current->sent,oracle->sent),"all packet types/payload bytes/order equal");
            expect(current->polls==oracle->polls,"all adapter poll timeout values/order equal");
            expect(current->closes==oracle->closes,"adapter close counts equal");
            trace<<scene<<' '<<clock.value<<' '<<static_cast<int>(state.flowBackend->mode())<<' '
                 <<static_cast<int>(state.flowBackend->primaryConnectionState())<<' '<<current->sent.size()<<' '
                 <<current->polls.size()<<' '<<current->closes<<'\n';
        };
        compare();
        for(const auto delta:deltas) {
            clock.value=base+delta;const auto count=clock.reads.size();
            pollInternetMenu(state);reference.poll(clock.value,0);
            expect(clock.reads.size()==count+1U && clock.reads.back()==clock.value,"every poll samples once, preserving zero/late/wrap values");
            compare();
        }
        const auto count=clock.reads.size();state.page=InternetMenuPage::Kicked;
        pollInternetMenu(state);expect(clock.reads.size()==count,"kicked page returns before reading clock");
        expect(current->polls==oracle->polls,"kicked page does not poll transport");
        state.flowBackend.reset(); // Reference and all adapters are also destroyed before clock.
    }
    // Direct helper inputs preserve backwards and full-width values without clamp.
    for(const auto value:std::array<std::uint64_t,5>{{0U,100U,99U,0x100000001ULL,std::numeric_limits<std::uint64_t>::max()}}) {
        clock.value=value;const auto count=clock.reads.size();
        expect(internetMenuNowMilliseconds(state)==value,"helper preserves complete uint64 clock value");
        expect(clock.reads.size()==count+1U,"helper samples once");
    }
    reads=clock.reads.size();
    DestroyWindow(state.window);expect(state.window==nullptr,"owned production window destroyed");
    trace<<"checks="<<checks<<" failures="<<failures<<" comparisons="<<comparisons<<" sourceReads="<<reads<<'\n';
    trace.close();expect(trace.good(),"explicit material trace closed successfully");
    std::cout<<"Internet menu clock contract: checks="<<checks<<" failures="<<failures
        <<" skipped=0 comparisons="<<comparisons<<" sourceReads="<<reads
        <<" productionUi=yes productionFlow=yes nativeBrackets=8 externalTls=not-run hostSetup=not-forced\n";
    return failures==0 ? 0 : 1;
}
