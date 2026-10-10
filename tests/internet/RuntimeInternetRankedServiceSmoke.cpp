#include "network/internet/RuntimeInternetCommunity.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
using Status = RuntimeInternetRankedStatus;
using Result = RuntimeInternetCommunityQuickMatchResult;
using Operation = RuntimeInternetCommunityOperation;
struct Fixture {
    std::uint64_t now = 1000, utc = 1700000000000ULL, tx = 100;
    RuntimeInternetCommunityService service;
    RuntimeInternetCommunityServiceConfig config;
    std::array<RuntimeInternetCommunityClient, 3> clients;
    std::array<std::string, 3> peers{"alice", "bob", "outsider"};
    std::string error;
    explicit Fixture(const std::filesystem::path& path, std::size_t capacity = 64) {
        config.serviceId = "ranked-service"; config.statePath = path;
        config.maximumAuthorizedQuickMatches = capacity;
        config.nowMilliseconds = [&] { return now; }; config.utcMilliseconds = [&] { return utc; };
        config.rankedPolicy = {100, 40, 200};
        require(service.open(config, error), "open: " + error);
        for (std::size_t i = 0; i < clients.size(); ++i) {
            RuntimeInternetCommunityClientConfig cc; cc.serviceId = config.serviceId; cc.localPeerId = peers[i];
            require(clients[i].open(cc, error), "client open: " + error);
            RuntimeNetworkPacket packet;
            require(clients[i].makeRegisterProfileRequest(++tx, peers[i], packet, error), "registration encode");
            require(submit(i, packet), "register: " + error);
        }
    }
    void deliver(std::vector<RuntimeInternetCommunityServiceAction> actions) {
        for (const auto& action : actions) for (std::size_t i = 0; i < peers.size(); ++i)
            if (action.targetPeerId == peers[i]) require(clients[i].receive(action.packet, config.serviceId, error), "receive: " + error);
    }
    bool submit(std::size_t player, RuntimeNetworkPacket packet) {
        RuntimeInternetCommunityServiceEnvelope envelope{config.serviceId, peers[player], std::move(packet)};
        std::vector<RuntimeInternetCommunityServiceAction> actions;
        if (!service.receive(envelope, actions, error)) { require(actions.empty(), "failed write must not publish success"); return false; }
        deliver(std::move(actions)); return true;
    }
    bool request(std::size_t player, Operation op, std::string id = {}, Result result = Result::None, std::string opponent = {}) {
        RuntimeInternetCommunityRequest r;
        r.operation = op; r.transactionId = ++tx; r.serviceId = config.serviceId; r.senderPeerId = peers[player];
        r.matchId = std::move(id); r.quickMatchResult = result;
        r.target = opponent.empty() ? peers[player == 0 ? 1 : 0] : std::move(opponent);
        RuntimeNetworkPacket packet;
        require(encodeRuntimeInternetCommunityRequest(r, packet, error), "request encode: " + error);
        return submit(player, packet);
    }
    void authorize(const std::string& id, bool start = true) {
        require(service.authorizeQuickMatch(id, std::span(peers).first(2), error, "room-" + id), "authorize: " + error);
        if (start) {
            std::vector<RuntimeInternetCommunityServiceAction> actions;
            require(service.rankedRoomStarted("room-" + id, actions, error), "start: " + error); deliver(std::move(actions));
        }
    }
    RuntimeInternetRankedRecord record(const std::string& id, std::size_t player = 0) {
        require(request(player, Operation::QuickMatchHistory), "history request");
        for (const auto& r : clients[player].matchHistory()) if (r.matchId == id) return r;
        throw std::runtime_error("missing history: " + id);
    }
    void advance(std::uint64_t ms) {
        now += ms; utc += ms;
        std::vector<RuntimeInternetCommunityServiceAction> actions;
        require(service.poll(actions, error), "expire: " + error); deliver(std::move(actions));
    }
    void refresh() { require(request(0, Operation::Refresh) && request(1, Operation::Refresh), "refresh"); }
    void report(std::size_t player, const std::string& id, Result result) {
        require(request(player, Operation::ReportQuickMatch, id, result), "report: " + error);
    }
};
std::string contents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); return {std::istreambuf_iterator<char>(stream), {}};
}
}

#include "RuntimeInternetRankedPersistenceProcess.inl"

int main(int argc, char** argv) try {
    if (argc > 1) return rankedPersistenceProcess(argc, argv);
    const auto root = std::filesystem::current_path() / ("ranked-ledger-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    Fixture f(root / "ledger.state");
    f.authorize("not-started", false);
    f.report(0, "not-started", Result::Win);
    require(f.record("not-started").status == Status::Assigned, "assignment alone must not authorize results");
    require(f.request(2, Operation::AbandonQuickMatch, "not-started"), "outsider rejection delivery");
    require(f.record("not-started").status == Status::Assigned, "outsider must not cancel another game");
    f.authorize("confirmed"); f.report(0, "confirmed", Result::Win); f.report(1, "confirmed", Result::Loss);
    require(f.record("confirmed").status == Status::Confirmed, "opposite reports final");
    f.refresh(); require(f.clients[0].snapshot().profile.wins == 1 && f.clients[0].snapshot().profile.points == 1032, "score once");
    f.report(1, "confirmed", Result::Win); f.request(0, Operation::AbandonQuickMatch, "confirmed");
    require(f.record("confirmed").winnerPeerId == "alice", "late conflict and abandon cannot change final result");
    f.authorize("missing"); f.report(0, "missing", Result::Win);
    auto deadline = f.record("missing").deadlineUtcMilliseconds;
    f.advance(39); f.report(0, "missing", Result::Win);
    require(f.record("missing").deadlineUtcMilliseconds == deadline, "duplicate cannot extend deadline");
    f.advance(1); require(f.record("missing").status == Status::VoidMissingReport, "single report must finish without claiming a winner");
    f.authorize("conflict"); f.report(0, "conflict", Result::Win); f.report(1, "conflict", Result::Win);
    f.advance(40); require(f.record("conflict").status == Status::VoidConflict, "conflict expires");
    f.report(1, "conflict", Result::Loss); require(f.record("conflict").status == Status::VoidConflict, "late correction rejected");
    f.authorize("corrected"); f.report(0, "corrected", Result::Win); f.report(1, "corrected", Result::Win);
    f.advance(39); f.report(1, "corrected", Result::Loss);
    require(f.record("corrected").status == Status::Confirmed && f.record("corrected").conflictObserved
        && f.record("corrected").firstReports[1] == RuntimeInternetRankedResult::Win, "correction retains audit evidence");
    require(f.record("not-started").status == Status::VoidNotStarted, "unstarted assignment expires");
    f.authorize("no-reports"); f.advance(200);
    require(f.record("no-reports").status == Status::VoidNoReports, "zero reports expire");
    f.authorize("forfeit"); require(f.request(0, Operation::AbandonQuickMatch, "forfeit"), "forfeit accepted");
    require(f.record("forfeit").status == Status::Forfeited && f.record("forfeit").winnerPeerId == "bob", "own departure awards only opponent");
    f.authorize("cancelled", false); f.request(1, Operation::AbandonQuickMatch, "cancelled");
    require(f.record("cancelled").status == Status::VoidNotStarted, "pre-start cancel no points");
    f.authorize("disconnect"); f.report(0, "disconnect", Result::Win);
    std::vector<RuntimeInternetCommunityServiceAction> actions;
    require(f.service.authenticatedPeerDisconnected(f.config.serviceId, "bob", false, actions, f.error), "disconnect finalization");
    f.deliver(std::move(actions)); require(f.record("disconnect").status == Status::VoidDisconnected, "disconnected result void");
    f.report(1, "disconnect", Result::Loss); f.refresh();
    require(f.clients[1].snapshot().profile.disconnects == 1, "ranked final disconnection counted once");
    const auto wins = f.clients[0].snapshot().profile.wins;
    const auto losses = f.clients[0].snapshot().profile.losses;
    require(wins == 2 && losses == 1, "void records never score");

    // An interrupted atomic commit must not mutate the ledger or either score.
    f.authorize("write-retry"); f.report(0, "write-retry", Result::Win);
    const auto originalBytes = contents(f.config.statePath);
#if defined(_WIN32)
    HANDLE lock = CreateFileW(f.config.statePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(lock != INVALID_HANDLE_VALUE, "lock destination against replacement");
    const bool wrote = f.request(1, Operation::ReportQuickMatch, "write-retry", Result::Loss);
    CloseHandle(lock);
    require(!wrote && contents(f.config.statePath) == originalBytes, "failed score commit preserves existing state file");
    require(f.record("write-retry").status == Status::AwaitingReport, "failed commit rolls back memory");
    f.refresh(); require(f.clients[0].snapshot().profile.wins == wins, "failed write rolls back scores");
#endif
    f.report(1, "write-retry", Result::Loss);
    require(f.record("write-retry").status == Status::Confirmed, "later transaction commits once");
    f.authorize("restart-pending"); f.report(0, "restart-pending", Result::Win);
    f.service.close(); f.now = 1; f.utc += 100;
    require(f.service.open(f.config, f.error), "restart: " + f.error);
    require(f.record("restart-pending").status == Status::VoidServiceRestart, "restart cannot leave stale deadlines running");
    f.report(0, "write-retry", Result::Win); f.report(1, "write-retry", Result::Loss); f.refresh();
    require(f.clients[0].snapshot().profile.wins == wins + 1 && f.clients[0].snapshot().profile.losses == losses, "restart never scores terminal twice");
    require(contents(f.config.statePath).find("CONCORD_WOL_COMMUNITY_V4") == 0, "durable ledger version");
    f.request(2, Operation::QuickMatchHistory); require(f.clients[2].matchHistory().empty(), "history only discloses authenticated own games");
    RuntimeInternetCommunityRequest forged; forged.operation = Operation::AbandonQuickMatch; forged.transactionId = 777;
    forged.serviceId = f.config.serviceId; forged.senderPeerId = "alice"; forged.target = "bob"; forged.matchId = "confirmed";
    RuntimeNetworkPacket packet; require(encodeRuntimeInternetCommunityRequest(forged, packet, f.error), "forge encode");
    require(!f.submit(2, packet), "outer identity rejects forged departure");
    Fixture bounded(root / "bounded.state", 2);
    bounded.authorize("one"); bounded.authorize("two");
    require(!bounded.service.authorizeQuickMatch("third", std::span(bounded.peers).first(2), bounded.error), "capacity must preserve unresolved records");
    bounded.request(0, Operation::AbandonQuickMatch, "one"); bounded.authorize("third");
    require(bounded.service.stats().authorizedQuickMatches == 2 && bounded.service.stats().unresolvedQuickMatches == 2, "only terminal record pruned");
    require(!bounded.service.authorizeQuickMatch("third", std::span(bounded.peers).first(2), bounded.error, "wrong-room"), "same id different room rejected");
    // Legacy accounts retain scores when upgraded; no invented historic matches.
    auto legacyBytes = contents(f.config.statePath); std::string legacy;
    std::istringstream input(legacyBytes); std::string line;
    while (std::getline(input, line)) if (!line.starts_with("R\t") && !line.starts_with("Z\t")) legacy += line + '\n';
    legacy.replace(0, std::string("CONCORD_WOL_COMMUNITY_V4").size(), "CONCORD_WOL_COMMUNITY_V2");
    const auto legacyPath = root / "legacy.state"; { std::ofstream output(legacyPath, std::ios::binary); output << legacy; }
    auto legacyConfig = f.config; legacyConfig.statePath = legacyPath;
    RuntimeInternetCommunityService legacyService;
    require(legacyService.open(legacyConfig, f.error) && legacyService.stats().registeredAccounts == 3
        && legacyService.stats().authorizedQuickMatches == 0, "V2 migration preserves accounts without fabricated results");
    std::cout << "Ranked service smoke passed: authority, deadlines, correction, forfeit, disconnect, atomic rollback, restart, bounded history, migration.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
