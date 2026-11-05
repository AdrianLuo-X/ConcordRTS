#pragma once
#include "runtime/menu/RuntimeInternetMenuController.h"
#include <map>
#include <string>
#include <utility>
#include <vector>
using namespace concord::internet_menu;
struct Flow final : RuntimeInternetMenuFlow {
    std::vector<std::string> localTrace,calls;std::vector<std::string>* trace=&localTrace;
    bool presentValue=true,transportValue=true,authenticated=false,operationOK=true;
    unsigned closes=0,polls=0;std::uint64_t lastNow=0,lastTransaction=0;int lastTimeout=-1;std::string lastText;
    RuntimeInternetLobbyFlowMode modeValue=RuntimeInternetLobbyFlowMode::QueryingRooms;
    RuntimeInternetConnectionState connectionValue{};
    std::vector<RuntimeInternetRoomRecord> roomValues;
    RuntimeInternetCommunitySnapshot snapshot;
    std::vector<RuntimeInternetRankedRecord> history;
    RuntimeInternetLobbyFlowPollResult polled;
    bool beginRoomQuery(RuntimeInternetLobbyQueryOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override {(void)options;(void)nowMilliseconds;(void)errorMessage;calls.push_back("beginRoomQuery");lastNow=nowMilliseconds;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool beginMatchmaking(RuntimeInternetLobbyMatchmakingOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override {(void)options;(void)nowMilliseconds;(void)errorMessage;calls.push_back("beginMatchmaking");lastNow=nowMilliseconds;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool startHost(RuntimeInternetLobbyHostOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override {(void)options;(void)nowMilliseconds;(void)errorMessage;calls.push_back("startHost");lastNow=nowMilliseconds;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool join(RuntimeInternetLobbyJoinOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override {(void)options;(void)nowMilliseconds;(void)errorMessage;calls.push_back("join");lastNow=nowMilliseconds;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityLogin(std::uint64_t transactionId, std::string& errorMessage) override {(void)transactionId;(void)errorMessage;calls.push_back("requestCommunityLogin");lastTransaction=transactionId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityRegistration(std::uint64_t transactionId, const std::string& displayName, std::string& errorMessage) override {(void)transactionId;(void)displayName;(void)errorMessage;calls.push_back("requestCommunityRegistration");lastTransaction=transactionId;lastText=displayName;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityRefresh(std::uint64_t transactionId, std::string& errorMessage) override {(void)transactionId;(void)errorMessage;calls.push_back("requestCommunityRefresh");lastTransaction=transactionId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityChannelJoin(std::uint64_t transactionId, const std::string& channelId, std::string& errorMessage) override {(void)transactionId;(void)channelId;(void)errorMessage;calls.push_back("requestCommunityChannelJoin");lastTransaction=transactionId;lastText=channelId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityChannelMessage(std::uint64_t transactionId, const std::string& text, std::string& errorMessage) override {(void)transactionId;(void)text;(void)errorMessage;calls.push_back("requestCommunityChannelMessage");lastTransaction=transactionId;lastText=text;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityFriendAdd(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) override {(void)transactionId;(void)handleOrPeerId;(void)errorMessage;calls.push_back("requestCommunityFriendAdd");lastTransaction=transactionId;lastText=handleOrPeerId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityFriendRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override {(void)transactionId;(void)peerId;(void)errorMessage;calls.push_back("requestCommunityFriendRemove");lastTransaction=transactionId;lastText=peerId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanCreate(std::uint64_t transactionId, const std::string& clanName, const std::string& clanTag, std::string& errorMessage) override {(void)transactionId;(void)clanName;(void)clanTag;(void)errorMessage;calls.push_back("requestCommunityClanCreate");lastTransaction=transactionId;lastText=clanName;lastText=clanTag;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanInvite(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) override {(void)transactionId;(void)handleOrPeerId;(void)errorMessage;calls.push_back("requestCommunityClanInvite");lastTransaction=transactionId;lastText=handleOrPeerId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanInvitationAccept(std::uint64_t transactionId, const std::string& clanId, std::string& errorMessage) override {(void)transactionId;(void)clanId;(void)errorMessage;calls.push_back("requestCommunityClanInvitationAccept");lastTransaction=transactionId;lastText=clanId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanMemberRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override {(void)transactionId;(void)peerId;(void)errorMessage;calls.push_back("requestCommunityClanMemberRemove");lastTransaction=transactionId;lastText=peerId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanLeave(std::uint64_t transactionId, std::string& errorMessage) override {(void)transactionId;(void)errorMessage;calls.push_back("requestCommunityClanLeave");lastTransaction=transactionId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    bool requestCommunityClanOwnershipTransfer(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override {(void)transactionId;(void)peerId;(void)errorMessage;calls.push_back("requestCommunityClanOwnershipTransfer");lastTransaction=transactionId;lastText=peerId;if(!operationOK)errorMessage="flow denied";return operationOK;}
    RuntimeInternetLobbyFlowPollResult poll(std::uint64_t nowMilliseconds, int transportTimeoutMilliseconds) override {(void)nowMilliseconds;(void)transportTimeoutMilliseconds;++polls;lastNow=nowMilliseconds;lastTimeout=transportTimeoutMilliseconds;return polled;}
    RuntimeInternetLobbyFlowMode mode() const override {return modeValue;}
    const std::vector<RuntimeInternetRoomRecord>& rooms() const override {return roomValues;}
    RuntimeInternetConnectionState primaryConnectionState() const override {return connectionValue;}
    const RuntimeInternetCommunitySnapshot& communitySnapshot() const override {return snapshot;}
    const std::vector<RuntimeInternetRankedRecord>& communityMatchHistory() const override {return history;}
    void close() noexcept override {++closes;trace->push_back("flow-close");}
    bool present() const override { return presentValue; }
    bool transportPresent() const override { return transportValue; }
    bool coordinatorAuthenticated() const override { return authenticated; }
    concord::lan_menu::RuntimeLanMenuFlow* lobbyFlow() const override { return nullptr; }
};
struct View final : RuntimeInternetMenuView {
    std::map<RuntimeInternetMenuWidget,std::string> texts;
    std::map<RuntimeInternetMenuWidget,bool> visible,enabled;
    std::map<RuntimeInternetMenuWidget,std::vector<std::string>> lists;
    std::map<RuntimeInternetMenuWidget,std::int64_t> selected;
    std::map<RuntimeInternetMenuWidget,std::size_t> top;
    std::vector<std::string> trace;std::string peer="peer-fixture";
    bool widgetPresent=true,checkedValue=false,acceptedValue=false,readyValue=false,entropyOK=true,validText=true,optionsOK=true,setupOK=false,resumeOK=false;
    mutable bool lastSpectator=false;mutable std::size_t lastRoomCount=0;
    unsigned clears=0,destroys=0,credentialClears=0,invalidations=0,entropyReads=0,queries=0,hosts=0,joins=0,matches=0,setups=0,resumes=0,finishes=0;
    mutable unsigned clockReads=0;std::uint64_t clockValue=0,transaction=11;
    RuntimeInternetLeaveLabel leaveValue=RuntimeInternetLeaveLabel::Close;
    std::vector<RuntimeInternetMultiplayerRoomRow> roomRowsValue;
    void showWidget(RuntimeInternetMenuWidget p0,bool p1) override {(void)p0;(void)p1;visible[p0]=p1;}
    void enableWidget(RuntimeInternetMenuWidget p0,bool p1) override {(void)p0;(void)p1;enabled[p0]=p1;}
    std::string text(RuntimeInternetMenuWidget p0) const override {(void)p0;auto it=texts.find(p0);return it==texts.end()?std::string{}:it->second;}
    void setText(RuntimeInternetMenuWidget p0,std::string_view p1) override {(void)p0;(void)p1;texts[p0]=p1;}
    bool hasWidget(RuntimeInternetMenuWidget p0) const override {(void)p0;return widgetPresent;}
    bool checked(RuntimeInternetMenuWidget p0) const override {(void)p0;return checkedValue;}
    void clearText(RuntimeInternetMenuWidget p0) override {(void)p0;++clears;texts[p0].clear();trace.push_back("clear-text");}
    void listClear(RuntimeInternetMenuWidget p0) override {(void)p0;lists[p0].clear();}
    void listAdd(RuntimeInternetMenuWidget p0,const std::string& p1) override {(void)p0;(void)p1;lists[p0].push_back(p1);}
    void replaceRows(RuntimeInternetMenuWidget p0,std::span<const std::string> p1) override {(void)p0;(void)p1;lists[p0]=std::vector<std::string>(p1.begin(),p1.end());}
    std::int64_t listSelected(RuntimeInternetMenuWidget p0) const override {(void)p0;auto it=selected.find(p0);return it==selected.end()?-1:it->second;}
    void listSelect(RuntimeInternetMenuWidget p0,std::int64_t p1) override {(void)p0;(void)p1;selected[p0]=p1;}
    void listTop(RuntimeInternetMenuWidget p0,std::size_t p1) override {(void)p0;(void)p1;top[p0]=p1;}
    void readyLabel(bool p0) override {(void)p0;readyValue=p0;}
    void leaveLabel(RuntimeInternetLeaveLabel p0) override {(void)p0;leaveValue=p0;}
    void invalidateSkinned() override {++invalidations;}
    void invalidateSkin() override {++invalidations;}
    void destroyWindow() override {++destroys;trace.push_back("destroy");}
    bool accepted() const override {return acceptedValue;}
    const std::string& peerId() const override {return peer;}
    std::uint64_t nowMs() const override {++clockReads;return clockValue;}
    void clearCredentials() override {++credentialClears;trace.push_back("credentials");}
    bool entropy(RuntimeInternetMultiplayerMenuEntropy& p0,std::string& p1) override {(void)p0;(void)p1;++entropyReads;p0.queryTransactionId=transaction;p0.joinTransactionId=17;p0.gameplayRandomSeed=23;if(!entropyOK)p1="entropy denied";return entropyOK;}
    std::string trim(std::string&& p0) const override {(void)p0;const auto a=p0.find_first_not_of(" ");return a==std::string::npos?std::string{}:p0.substr(a,p0.find_last_not_of(" ")-a+1);}
    bool validateDisplayText(std::string& p0,std::size_t p1,std::string_view p2,std::string& p3) override {(void)p0;(void)p1;(void)p2;(void)p3;if(!validText)p3="invalid display";return validText;}
    const RuntimeMultiplayerMatchSlot* slotForPeer(const RuntimeMultiplayerMatchSetup* p0,std::string_view p1) const override {(void)p0;(void)p1;return nullptr;}
    bool queryOptions(const RuntimeMultiplayerCompatibility& p0,std::uint64_t p1,RuntimeInternetLobbyQueryOptions& p2,std::string& p3) override {(void)p0;(void)p1;(void)p2;(void)p3;++queries;if(!optionsOK)p3="query denied";return optionsOK;}
    bool hostOptions(const RuntimeMultiplayerCompatibility& p0,const RuntimeMultiplayerHostSelectionProjection& p1,const RuntimeInternetMultiplayerMenuEntropy& p2,RuntimeInternetLobbyHostOptions& p3,std::string& p4,const std::shared_ptr<RuntimeMultiplayerResumeSave>& p5) override {(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;++hosts;if(!optionsOK)p4="host options denied";return optionsOK;}
    bool joinOptions(const RuntimeMultiplayerCompatibility& p0,const RuntimeInternetRoomRecord& p1,bool p2,std::uint64_t p3,RuntimeInternetLobbyJoinOptions& p4,std::string& p5) override {(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;++joins;if(!optionsOK)p5="join options denied";lastSpectator=p2;return optionsOK;}
    bool matchmakingOptions(const RuntimeMultiplayerCompatibility& p0,const RuntimeMultiplayerHostSelectionProjection& p1,const std::string& p2,std::int32_t p3,const RuntimeInternetMultiplayerMenuEntropy& p4,RuntimeInternetLobbyMatchmakingOptions& p5,std::string& p6) override {(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;++matches;if(!optionsOK)p6="matchmaking options denied";return optionsOK;}
    bool selectHostSetup(const std::string& p0,const RuntimeInternetMultiplayerMenuEntropy& p1,RuntimeMultiplayerHostSelectionProjection& p2,std::string& p3) override {(void)p0;(void)p1;(void)p2;(void)p3;++setups;if(!setupOK)p3="setup cancelled";return setupOK;}
    bool selectResumeWithOriginalFailureStatus(const RuntimeMultiplayerCompatibility& p0,RuntimeMultiplayerResumeSave& p1,RuntimeMultiplayerHostSelectionProjection& p2,std::string& p3) override {(void)p0;(void)p1;(void)p2;(void)p3;++resumes;if(!resumeOK){p3="save cancelled";texts[RuntimeInternetMenuWidget::statusText]=p3;}return resumeOK;}
    bool resolveMap(const RuntimeConfig& p0,const RuntimeMultiplayerMatchMap& p1,std::filesystem::path& p2,std::string& p3) override {(void)p0;(void)p1;(void)p2;(void)p3;return false;}
    std::vector<RuntimeInternetMultiplayerRoomRow> roomRows(std::span<const RuntimeInternetRoomRecord> p0,bool p1) const override {(void)p0;(void)p1;lastRoomCount=p0.size();lastSpectator=p1;return roomRowsValue;}
    std::vector<RuntimeInternetMultiplayerRosterRow> rosterRows(std::span<const RuntimeMultiplayerRoomMember> p0,const RuntimeMultiplayerMatchSetup* p1,std::string_view p2,bool p3) const override {(void)p0;(void)p1;(void)p2;(void)p3;return {};}
    std::string connectionStatus(bool p0,RuntimeInternetLobbyFlowMode p1,RuntimeInternetConnectionState p2) const override {(void)p0;(void)p1;(void)p2;return "connection";}
    std::string profileSummary(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return "profile";}
    std::string clanSummary(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return "clan";}
    std::vector<std::string> channelRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> chatRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> participantRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> friendRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> ladderRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> clanMemberRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> clanInvitationRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> clanLadderRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> newsRows(const RuntimeInternetCommunitySnapshot& p0) const override {(void)p0;return {};}
    std::vector<std::string> historyRows(std::span<const RuntimeInternetRankedRecord> p0,std::string_view p1) const override {(void)p0;(void)p1;return std::vector<std::string>(p0.size(),"history");}
    std::string historyDetail(const RuntimeInternetRankedRecord& p0,std::string_view p1) const override {(void)p0;(void)p1;return p0.matchId;}
    void finishMatch() override {++finishes;trace.push_back("finish");}
};
