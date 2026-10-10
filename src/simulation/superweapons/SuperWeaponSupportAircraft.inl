std::vector<std::string> SuperWeaponSystem::supportInfantryManifest(
    const Instance& instance, const SuperWeaponWorldContext& context) const {
    const auto* rule = findRule(instance.definitionId);
    if (!rule) return {};
    std::string types = rule->infantryTypes, numbers = rule->infantryCounts;
    if (superWeaponTypeKey(*rule).find("amer") == std::string::npos && context.rules) {
        const std::string country = lowerAscii(context.ownerCountry
            ? context.ownerCountry(instance.owner) : instance.owner);
        int sideIndex = 0;
        for (const auto& side : context.rules->getSides()) {
            if (std::any_of(side.members.begin(), side.members.end(), [&](const auto& member) {
                    return lowerAscii(member) == country;
                })) {
                const auto name = lowerAscii(side.name);
                sideIndex = (name.find("yuri") != std::string::npos || name == "thirdside") ? 2
                    : name.find("nod") != std::string::npos || name.find("sov") != std::string::npos ? 1 : 0;
            }
        }
        if (!rule->sideInfantryTypes[sideIndex].empty()) {
            types = rule->sideInfantryTypes[sideIndex]; numbers = rule->sideInfantryCounts[sideIndex];
        }
    }
    const auto typeList = splitValues(types); const auto counts = parseInts(numbers);
    std::vector<std::string> result;
    for (std::size_t i=0; i<typeList.size(); ++i) {
        const int count = std::clamp(i<counts.size() ? counts[i] : 1, 0, 128);
        for (int j=0; j<count && result.size()<256; ++j) result.push_back(typeList[i]);
    }
    return result;
}

void SuperWeaponSystem::launchSupportAircraft(Instance& instance,
    const SuperWeaponWorldContext& context, bool spy) {
    const auto* rule = findRule(instance.definitionId);
    const auto* type = !rule || !context.rules ? nullptr
        : context.rules->findUnitType("AircraftType", rule->planeType);
    if (!rule || rule->planeType.empty() || !context.objects || (context.rules && !type)) {
        emit(context, instance, spy ? "SpyPlaneAircraftUnavailable" : "ParadropAircraftUnavailable",
            "original aircraft type unavailable", &instance.firstTarget);
        instance.activeUntilTick = context.tick + 1;
        return;
    }
    int left = 0, right = std::max(0, context.mapWidth-1);
    if (context.cellLegal) {
        while (left<right && !context.cellLegal(left, instance.firstTarget.cell.y, SuperWeaponTargetMode::Cell)) ++left;
        while (right>left && !context.cellLegal(right, instance.firstTarget.cell.y, SuperWeaponTargetMode::Cell)) --right;
    }
    const bool fromLeft = instance.firstTarget.cell.x <= (left+right)/2;
    WorldObjectInstance plane;
    plane.instanceId = context.allocateObjectId ? context.allocateObjectId() : -1;
    plane.kind=WorldObjectKind::Unit; plane.type=rule->planeType; plane.owner=instance.owner;
    plane.health=type ? type->strength : 100; plane.alive=true; plane.authoredAircraft=true;
    plane.x=fromLeft ? left-2 : right+2; plane.y=instance.firstTarget.cell.y;
    plane.cell=superWeaponObjectCellKey(plane.x,plane.y);
    if (type && type->ammo>=0) plane.currentAmmo=std::clamp(type->initialAmmo<0
        ? type->ammo : type->initialAmmo,0,type->ammo);
    plane.airborne=true; plane.hasRenderPosition=true; plane.renderMoveActive=true;
    plane.renderX=plane.x; plane.renderY=plane.y;
    const int ground=context.terrainLevel ? context.terrainLevel(instance.firstTarget.cell.x, plane.y) : 0;
    plane.renderLevel=ground + rule->supportFlightLevelLeptons * 2.449489742783178 / 256.0;
    plane.flightLevel=plane.level=static_cast<int>(std::lround(plane.renderLevel));
    plane.facing=plane.turretFacing=context.facingForDelta ? context.facingForDelta(fromLeft?1:-1,0) : fromLeft?64:192;
    // Direction is part of the existing, saved mission text; all other flight
    // authority lives in the saved world pose and Instance::ActiveObjects.
    plane.mission=fromLeft ? "SupportFlightEast" : "SupportFlightWest";
    const int id=plane.instanceId;
    context.objects->push_back(std::move(plane));
    if (context.onObjectCreated) context.onObjectCreated(context.objects->back());
    instance.activeObjectIds={id}; instance.targetStage=0;
    instance.nextEffectTick=context.tick; instance.activeUntilTick=-1;
    emit(context, instance, spy ? "SpyPlaneAircraftLaunched" : "ParadropAircraftLaunched",
        "original aircraft edge ingress", &instance.firstTarget, nullptr, id, -1, 0,
        type ? type->moveSound : rule->startSound, "", rule->planeType);
}

void SuperWeaponSystem::advanceSupportAircraft(Instance& instance,
    const SuperWeaponWorldContext& context, bool spy) {
    const auto* rule=findRule(instance.definitionId);
    if (!rule || !context.objects || instance.activeObjectIds.empty()) {
        instance.activeUntilTick=context.tick; return;
    }
    const auto manifest=supportInfantryManifest(instance,context);
    const int planeId=instance.activeObjectIds.front();
    auto* plane=findObject(planeId,context);
    const auto* type=context.rules ? context.rules->findUnitType("AircraftType",rule->planeType) : nullptr;
    if (plane && plane->alive && plane->health>0) {
        const int direction=plane->mission=="SupportFlightWest" ? -1 : 1;
        const double step=std::max(1.0,type?double(type->speed):15.0) / double(std::max(1,context.fixedTickHz));
        plane->renderX+=direction*step;
        plane->x=static_cast<int>(std::lround(plane->renderX)); plane->cell=superWeaponObjectCellKey(plane->x,plane->y);
        const double dx=plane->renderX-instance.firstTarget.cell.x;
        if (spy) {
            const auto* camera=!type || !context.rules ? nullptr : context.rules->findWeaponType(type->primaryWeapon);
            const int radius=camera && camera->damage>0 ? camera->damage : std::max(1,rule->radius);
            const double cameraRange=camera && camera->range>0 ? camera->range : double(radius);
            if (std::abs(dx)<=cameraRange && context.tick>=instance.nextEffectTick && plane->currentAmmo!=0) {
                if (plane->currentAmmo>0) --plane->currentAmmo;
                SuperWeaponTarget photo{{plane->x,plane->y},-1};
                emit(context,instance,"SpyPlaneRevealArea","aircraft camera exposure",&photo,nullptr,
                    planeId,-1,radius,rule->startSound);
                instance.nextEffectTick=context.tick+std::max(1,convertRuleFrames(rule->spyCameraFrames,context.fixedTickHz));
                ++instance.targetStage;
            }
        } else if (instance.targetStage<static_cast<int>(manifest.size())
            && std::abs(dx)<=std::max(0.5,rule->paradropRadiusLeptons/256.0)
            && context.tick>=instance.nextEffectTick) {
            WorldObjectInstance soldier;
            soldier.type=manifest[instance.targetStage]; soldier.kind=WorldObjectKind::Infantry;
            soldier.owner=instance.owner; soldier.alive=true;
            const auto* infantry=context.rules ? context.rules->findUnitType("InfantryType",soldier.type) : nullptr;
            soldier.health=infantry ? infantry->strength : 100;
            // Retail unload alternates facing +/- 0x3FFF after decrementing
            // the remaining passenger count, using a 128-lepton search offset.
            // Use world flight direction; the host's facing is isometric.
            const int remaining = static_cast<int>(manifest.size()) - instance.targetStage - 1;
            const double angle = (remaining & 1 ? -1.0 : 1.0)
                * 16383.0 * (6.2831853071795864769 / 65536.0);
            const double dropX = plane->renderX + direction * std::cos(angle) * 0.5;
            const double dropY = plane->renderY + direction * std::sin(angle) * 0.5;
            const int dropCellX = static_cast<int>(std::lround(dropX));
            const int dropCellY = static_cast<int>(std::lround(dropY));
            bool landingFound=false;
            for (int radius=0; radius<=4 && !landingFound; ++radius) {
                for (int y=-radius; y<=radius && !landingFound; ++y) for (int x=-radius; x<=radius; ++x) {
                    if (std::max(std::abs(x),std::abs(y))!=radius) continue;
                    const int tx=dropCellX+x,ty=dropCellY+y;
                    if (context.cellLegal && !context.cellLegal(tx,ty,SuperWeaponTargetMode::Cell)) continue;
                    if (context.canPlaceObject && !context.canPlaceObject(soldier,tx,ty)) continue;
                    if (std::any_of(context.objects->begin(),context.objects->end(),[&](const auto& other) {
                            return other.alive && other.kind!=WorldObjectKind::Structure && other.x==tx && other.y==ty
                                && other.instanceId!=planeId;
                        })) continue;
                    int slot = -1;
                    if (context.paratrooperSubcell) {
                        slot = context.paratrooperSubcell(soldier, tx, ty, dropX, dropY);
                    } else {
                        double nearest = std::numeric_limits<double>::infinity();
                        for (int candidate : {2, 4, 3}) {
                            // Keep the standalone module free of host world
                            // linkage; these are the original legal slot offsets.
                            const double offsetX = candidate == 3 ? -0.25 : 0.25;
                            const double offsetY = candidate == 2 ? -0.25 : 0.25;
                            const double distance = std::pow(tx + offsetX - dropX, 2)
                                + std::pow(ty + offsetY - dropY, 2);
                            if (distance < nearest) { nearest = distance; slot = candidate; }
                        }
                    }
                    if (slot != 2 && slot != 4 && slot != 3) continue;
                    soldier.x=tx; soldier.y=ty; soldier.subCell=slot; landingFound=true; break;
                }
            }
            if (landingFound) {
                soldier.instanceId=context.allocateObjectId ? context.allocateObjectId() : -1;
                soldier.cell=superWeaponObjectCellKey(soldier.x,soldier.y);
                soldier.airborne=true; soldier.hasRenderPosition=true; soldier.renderMoveActive=true;
                soldier.renderX=soldier.x; soldier.renderY=soldier.y;
                soldier.renderLevel=plane->renderLevel; soldier.flightLevel=soldier.level=plane->flightLevel;
                soldier.facing=plane->facing; soldier.mission="ParadropDrop";
                const int id=soldier.instanceId;
                context.objects->push_back(std::move(soldier));
                if (context.onObjectCreated) context.onObjectCreated(context.objects->back());
                instance.activeObjectIds.push_back(id);
                emit(context,instance,"ParadropInfantryDropped","original parachute descent",&instance.firstTarget,
                    nullptr,id,-1,instance.targetStage,"","",rule->parachuteAnim,planeId);
            }
            ++instance.targetStage;
            instance.nextEffectTick=context.tick+std::max(1,convertRuleFrames(1,context.fixedTickHz));
            plane=findObject(planeId,context);
        }
        const bool crossedExit = plane && direction*dx>0 && context.cellLegal
            && !context.cellLegal(plane->x-direction*2, plane->y, SuperWeaponTargetMode::Cell);
        if (plane && (crossedExit || plane->renderX< -3 || plane->renderX>context.mapWidth+2)) {
            plane->alive=false; plane->health=0; plane->mission="SupportDeparted";
            if (context.onObjectRemoved) {
                auto exit=lifecycleEvent(SuperWeaponObjectLifecycleKind::Removed,instance,context.tick,planeId);
                exit.removalDisposition=SuperWeaponObjectRemovalDisposition::Consumed;
                exit.reason="support aircraft departed map"; context.onObjectRemoved(std::move(exit));
            }
        }
    }
    bool descending=false;
    for (std::size_t i=1;i<instance.activeObjectIds.size();++i) {
        auto* soldier=findObject(instance.activeObjectIds[i],context);
        if (!soldier || !soldier->alive || !soldier->airborne || soldier->mission!="ParadropDrop") continue;
        const int ground=context.terrainLevel ? context.terrainLevel(soldier->x,soldier->y) : 0;
        const double previousHeight = soldier->renderLevel;
        soldier->renderLevel=std::max(double(ground),soldier->renderLevel-rule->parachuteFallRate
            *15.0/std::max(1,context.fixedTickHz)*2.449489742783178/256.0);
        soldier->movementAnimationDistance += previousHeight - soldier->renderLevel;
        soldier->flightLevel=soldier->level=static_cast<int>(std::lround(soldier->renderLevel));
        if (soldier->renderLevel<=ground) {
            soldier->airborne=false; soldier->flightLevel=0; soldier->mission="Guard";
            if (context.onObjectRelocated) {
                auto landed=lifecycleEvent(SuperWeaponObjectLifecycleKind::Relocated,instance,context.tick,soldier->instanceId);
                landed.oldCell=landed.newCell={soldier->x,soldier->y}; landed.reason="paratrooper landed";
                context.onObjectRelocated(std::move(landed));
            }
            emit(context,instance,"ParadropInfantryLanded","ready for orders",&instance.firstTarget,
                nullptr,soldier->instanceId);
        } else descending=true;
    }
    plane=findObject(planeId,context);
    if ((!plane || !plane->alive || plane->health<=0) && !descending) instance.activeUntilTick=context.tick;
}
