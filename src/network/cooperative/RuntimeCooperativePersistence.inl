



// The same previous-frame observations must survive a save/reconnect. Rebuilding
// them from the loaded world would erase a just-destroyed/captured target event.




#define RUNTIME_COOPERATIVE_OBSERVATIONS(OP) \
    OP(campaignPreviousCellByObject) \
    OP(campaignPreviousHealthByObject) \
    OP(campaignInitialHealthByObject) \
    OP(campaignPreviousAliveByObject) \
    OP(campaignInitialOwnerByObject) \
    OP(campaignPreviousOwnerByObject) \
    OP(campaignAttackedTickByObject) \
    OP(campaignAttackerHouseByObject) \
    OP(campaignDestroyedTickByObject) \
    OP(campaignDestroyerHouseByObject) \
    OP(campaignEnteredTickByTargetObject) \
    OP(campaignEntrantHouseByTargetObject) \
    OP(campaignSpiedTickByTargetObject) \
    OP(campaignSpyHouseByTargetObject) \
    OP(campaignBridgeDestroyedTickByCell)




#undef RUNTIME_COOPERATIVE_OBSERVATIONS
