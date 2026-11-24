for (const auto& event : objects) {
            if (event.kind != SuperWeaponObjectLifecycleKind::OwnerChanged) continue;
            auto* target = findMutableObjectById(state, event.objectId);
            if (!target || target->owner != event.newOwner) continue;
            state.specialMechanisms.establishDominatorControl(event.objectId,
                event.newOwner, event.oldOwner, event.tick);
            target->mission = "MindControlledPermanent";
        }
