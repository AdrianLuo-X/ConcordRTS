// Air traffic has its own occupancy domain. Ground foundations/subcells do
// not obstruct flight, but another flyer (including its unfinished segment)
// does. This is derived from authoritative objects/behaviors, not a second
// reservation state that could go stale after orders, deaths or save/load.
bool runtimeObjectOccupiesAirNavigation(const WorldObjectInstance& object) {
    return object.alive && object.airborne
        && (object.kind == WorldObjectKind::Infantry
            || object.kind == WorldObjectKind::Unit);
}

std::optional<RuntimeCellPoint> runtimeActiveAirSegment(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object
) {
    const auto flight = state.jumpjetFlights.find(object.instanceId);
    if (flight != state.jumpjetFlights.end() && flight->second.active)
        return RuntimeCellPoint{flight->second.fromX, flight->second.fromY};
    const auto found = state.behaviors.find(object.instanceId);
    return found != state.behaviors.end()
            && found->second.moveCooldownTicks > 0
            && found->second.moveSegmentFrom.x >= 0 && found->second.moveSegmentFrom.y >= 0
        ? std::optional<RuntimeCellPoint>(found->second.moveSegmentFrom) : std::nullopt;
}

bool runtimeAirCellIsClear(
    const RuntimeWindowState& state,
    int moverId,
    const RuntimeCellPoint& cell
) {
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        if (runtimeCellPointForObject(other) == cell) return false;
        const auto segment = runtimeActiveAirSegment(state, other);
        if (segment && *segment == cell) return false;
    }
    return true;
}

int runtimeAirDiagonalKey(const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    return (from.y + to.y) * 1024 + from.x + to.x;
}

bool runtimeAirTransitionIsClear(
    const RuntimeWindowState& state,
    int moverId,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to
) {
    if (to.x < 0 || to.y < 0 || to.x >= 512 || to.y >= 512) return false;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        if (runtimeCellPointForObject(other) == to) return false;
        const auto segment = runtimeActiveAirSegment(state, other);
        if (!segment) continue;
        if (*segment == to) return false;
        // Two diagonals can intersect even when all four endpoints differ.
        if (from.x != to.x && from.y != to.y
            && segment->x != other.x
            && segment->y != other.y
            && runtimeAirDiagonalKey(from, to)
                == runtimeAirDiagonalKey(*segment, runtimeCellPointForObject(other))) return false;
    }
    return true;
}

struct RuntimeAirTraffic {
    std::unordered_set<int> occupied;
    std::unordered_set<int> diagonals;

    bool canStep(const RuntimeCellPoint& from, const RuntimeCellPoint& to) const {
        return to.x >= 0 && to.y >= 0 && to.x < 512 && to.y < 512
            && occupied.find(runtimeCellKey(to.x, to.y)) == occupied.end()
            && (from.x == to.x || from.y == to.y
                || diagonals.find(runtimeAirDiagonalKey(from, to)) == diagonals.end());
    }
};

RuntimeAirTraffic buildRuntimeAirTraffic(const RuntimeWindowState& state, int moverId) {
    RuntimeAirTraffic traffic;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        traffic.occupied.insert(runtimeCellKey(other.x, other.y));
        const auto segment = runtimeActiveAirSegment(state, other);
        if (!segment) continue;
        traffic.occupied.insert(runtimeCellKey(segment->x, segment->y));
        if (segment->x != other.x
            && segment->y != other.y) {
            traffic.diagonals.insert(runtimeAirDiagonalKey(*segment, runtimeCellPointForObject(other)));
        }
    }
    return traffic;
}

std::vector<RuntimeCellPoint> findRuntimeAirPath(
    const RuntimeWindowState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal
) {
    if (start == goal || goal.x < 0 || goal.y < 0 || goal.x >= 512 || goal.y >= 512) return {};
    const auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    if (traffic.occupied.count(runtimeCellKey(goal.x, goal.y)) != 0) return {};

    // Rasterize the whole line, rather than greedily reducing distance each
    // tick (which produces a long diagonal followed by a sharp axial turn).
    std::vector<RuntimeCellPoint> line;
    RuntimeCellPoint current = start;
    const int dx = std::abs(goal.x - start.x), dy = std::abs(goal.y - start.y);
    const int sx = start.x < goal.x ? 1 : -1, sy = start.y < goal.y ? 1 : -1;
    int error = dx - dy;
    while (current != goal) {
        RuntimeCellPoint next = current;
        const int twiceError = error * 2;
        if (twiceError > -dy) { error -= dy; next.x += sx; }
        if (twiceError < dx) { error += dx; next.y += sy; }
        if (!traffic.canStep(current, next)) break;
        line.push_back(next);
        current = next;
    }
    if (current == goal) return line;

    // Only air traffic needs a detour. A bounded search over the air plane
    // also crosses holes/water/cliffs without inheriting ground penalties.
    const auto estimate = [&](const RuntimeCellPoint& cell) {
        const int x = std::abs(goal.x - cell.x), y = std::abs(goal.y - cell.y);
        return 10 * std::max(x, y) + 4 * std::min(x, y);
    };
    struct Node { int cost; int parent; RuntimeCellPoint cell; };
    using Entry = std::tuple<int, int, int, int>; // f, line deviation, cell key, g
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
    std::unordered_map<int, Node> nodes;
    const int startKey = runtimeCellKey(start.x, start.y);
    const int goalKey = runtimeCellKey(goal.x, goal.y);
    nodes.emplace(startKey, Node{0, -1, start});
    frontier.emplace(estimate(start), 0, startKey, 0);
    int expanded = 0;
    while (!frontier.empty() && expanded < runtimePathExpandedNodeBudget) {
        const auto [score, deviation, key, cost] = frontier.top();
        (void)score; (void)deviation;
        frontier.pop();
        if (nodes.at(key).cost != cost) continue;
        if (key == goalKey) {
            std::vector<RuntimeCellPoint> path;
            for (int cursor = key; cursor != startKey; cursor = nodes.at(cursor).parent) {
                path.push_back(nodes.at(cursor).cell);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
        ++expanded;
        const RuntimeCellPoint from = nodes.at(key).cell;
        for (const auto& next : runtimeNeighborsOf(from)) {
            if (!traffic.canStep(from, next)) continue;
            const int nextKey = runtimeCellKey(next.x, next.y);
            const int nextCost = cost + (next.x != from.x && next.y != from.y ? 14 : 10);
            const auto found = nodes.find(nextKey);
            if (found != nodes.end() && found->second.cost <= nextCost) continue;
            nodes.insert_or_assign(nextKey, Node{nextCost, key, next});
            const int lineDeviation = std::abs((goal.x - start.x) * (next.y - start.y)
                - (goal.y - start.y) * (next.x - start.x));
            frontier.emplace(nextCost + estimate(next), lineDeviation, nextKey, nextCost);
        }
    }
    return {};
}

std::optional<RuntimeCellPoint> findRuntimeAirMoveTarget(
    const RuntimeWindowState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    const std::unordered_set<int>* reserved = nullptr,
    const std::unordered_set<int>* replacingOrders = nullptr,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr
) {
    auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    // Separate commands issued before either flyer arrives must reserve
    // separate destinations too. A new group order replaces its old goals.
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId || (replacingOrders != nullptr && replacingOrders->count(id) != 0)
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        const auto* other = findObjectById(state, id);
        if (other != nullptr && runtimeObjectOccupiesAirNavigation(*other)) {
            traffic.occupied.insert(runtimeCellKey(behavior.moveTarget.x, behavior.moveTarget.y));
        }
    }
    for (int radius = 0; radius <= 24; ++radius) {
        std::optional<RuntimeCellPoint> best;
        const int maximum = std::numeric_limits<int>::max();
        std::tuple<int, int, int> bestScore{maximum, maximum, maximum};
        for (int y = desired.y - radius; y <= desired.y + radius; ++y) {
            for (int x = desired.x - radius; x <= desired.x + radius; ++x) {
                if (std::max(std::abs(x - desired.x), std::abs(y - desired.y)) != radius
                    || x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                const int key = runtimeCellKey(x, y);
                if (state.terrainByCell.count(key) == 0 || traffic.occupied.count(key) != 0
                    || (reserved != nullptr && reserved->count(key) != 0)
                    || (manualMappedFilter != nullptr && !manualMappedFilter->allows({x,y}))) continue;
                const auto score = std::make_tuple(
                    (x - desired.x) * (x - desired.x) + (y - desired.y) * (y - desired.y),
                    (x - mover.x) * (x - mover.x) + (y - mover.y) * (y - mover.y), key);
                if (!best.has_value() || score < bestScore) { best = RuntimeCellPoint{x, y}; bestScore = score; }
            }
        }
        if (best.has_value()) return best;
    }
    return std::nullopt;
}

// Find a free destination that can actually be reached. Testing only the
// nearest few empty cells repeatedly can trap a dense formation around an
// enclosed pocket, especially when its flyers now reserve cells for longer.
bool findRuntimeReachableAirMoveTarget(const RuntimeWindowState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& anchor, int radius,
    RuntimeCellPoint& target, std::vector<RuntimeCellPoint>& path) {
    const auto start = runtimeCellPointForObject(mover);
    const auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    std::unordered_set<int> destinations;
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        const auto* other = findObjectById(state, id);
        if (other && runtimeObjectOccupiesAirNavigation(*other))
            destinations.insert(runtimeCellKey(behavior.moveTarget.x, behavior.moveTarget.y));
    }
    radius=std::clamp(radius,1,512);
    const int minX=std::max(0,std::min(start.x,anchor.x-radius)-8);
    const int minY=std::max(0,std::min(start.y,anchor.y-radius)-8);
    const int maxX=std::min(511,std::max(start.x,anchor.x+radius)+8);
    const int maxY=std::min(511,std::max(start.y,anchor.y+radius)+8);
    const int width=maxX-minX+1, height=maxY-minY+1;
    if (width<=0 || height<=0 || start.x<minX || start.y<minY || start.x>maxX || start.y>maxY) return false;
    const auto index=[&](const RuntimeCellPoint& p){return (p.y-minY)*width+p.x-minX;};
    const auto cell=[&](int i){return RuntimeCellPoint{minX+i%width,minY+i/width};};
    std::vector<int> parent(width*height,-2),queue;
    queue.reserve(std::min(width*height,65536));
    parent[index(start)]=-1;queue.push_back(index(start));
    int best=-1;
    std::tuple<int,int,int> bestScore{INT_MAX,INT_MAX,INT_MAX};
    constexpr std::array<RuntimeCellPoint,8> offsets{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for (std::size_t head=0;head<queue.size() && head<65536;++head) {
        const int here=queue[head];const auto point=cell(here);
        const int key=runtimeCellKey(point.x,point.y);
        const int d=(point.x-anchor.x)*(point.x-anchor.x)+(point.y-anchor.y)*(point.y-anchor.y);
        if (d<=radius*radius && state.terrainByCell.count(key) && !traffic.occupied.count(key)
            && !destinations.count(key)) {
            const auto score=std::make_tuple(d,(point.x-start.x)*(point.x-start.x)+(point.y-start.y)*(point.y-start.y),key);
            if (score<bestScore) {best=here;bestScore=score;}
            if (d==0) break;
        }
        for (const auto& offset:offsets) {
            const RuntimeCellPoint next{point.x+offset.x,point.y+offset.y};
            if(next.x<minX || next.y<minY || next.x>maxX || next.y>maxY
                || parent[index(next)]!=-2 || !traffic.canStep(point,next)) continue;
            parent[index(next)]=here;queue.push_back(index(next));
        }
    }
    if(best<0) return false;
    target=cell(best);path.clear();
    for(int here=best;parent[here]!=-1;here=parent[here])path.push_back(cell(here));
    std::reverse(path.begin(),path.end());
    return true;
}

// Keep the already committed flight segment when a move order interrupts
// another order. Snapping to its endpoint both looks wrong and releases the
// departure cell before the visible flyer has actually left it.
struct RuntimeAirMoveContinuation {
    bool active = false;
    bool visible = false;
    double x = 0, y = 0, level = 0;
    RuntimeObjectBehavior segment;

    RuntimeAirMoveContinuation(const WorldObjectInstance& object, const RuntimeObjectBehavior* previous) {
        active = object.airborne && previous != nullptr && previous->moveCooldownTicks > 0;
        visible = active && object.renderMoveActive && previous->renderMoveTicksRemaining > 0;
        if (active) { x = object.renderX; y = object.renderY; level = object.renderLevel; segment = *previous; }
    }

    void restore(WorldObjectInstance& object, RuntimeObjectBehavior& behavior) const {
        if (!active) return;
        behavior.moveCooldownTicks = segment.moveCooldownTicks;
        behavior.moveSegmentFrom = segment.moveSegmentFrom;
        behavior.moveIntervalRemainderQ16 = segment.moveIntervalRemainderQ16;
        if (!visible) return;
        behavior.renderFrom = segment.renderFrom;
        behavior.renderTo = segment.renderTo;
        behavior.renderFromOffsetX = segment.renderFromOffsetX;
        behavior.renderFromOffsetY = segment.renderFromOffsetY;
        behavior.renderToOffsetX = segment.renderToOffsetX;
        behavior.renderToOffsetY = segment.renderToOffsetY;
        behavior.renderArrivalDistanceAfter = segment.renderArrivalDistanceAfter;
        behavior.renderFromLevel = segment.renderFromLevel;
        behavior.renderToLevel = segment.renderToLevel;
        behavior.renderFromOnBridge = segment.renderFromOnBridge;
        behavior.renderToOnBridge = segment.renderToOnBridge;
        behavior.renderMoveTicksTotal = segment.renderMoveTicksTotal;
        behavior.renderMoveTicksRemaining = segment.renderMoveTicksRemaining;
        object.hasRenderPosition = object.renderMoveActive = true;
        object.renderX = x; object.renderY = y; object.renderLevel = level;
        object.renderOnBridge = false;
    }
};
