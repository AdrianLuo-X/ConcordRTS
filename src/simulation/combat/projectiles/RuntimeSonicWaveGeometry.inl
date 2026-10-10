struct RuntimeSonicWavePoint { double x=0.0,y=0.0,z=0.0; };
struct RuntimeSonicWaveGeometry {
    RuntimeSonicWavePoint source,target,lowerFrom,lowerTo,upperFrom,upperTo;
};

RuntimeSonicWaveGeometry runtimeSonicWaveGeometry(const RuntimeProjectileEffect& wave) {
    // Convert centred engine cells to the retail integer lepton coordinates.
    const auto point=[](double x,double y,double z) {
        return RuntimeSonicWavePoint{std::trunc((x+0.5)*256.0),
            std::trunc((y+0.5)*256.0),std::trunc(z*256.0/std::sqrt(6.0))};
    };
    RuntimeSonicWaveGeometry g;
    g.source=point(wave.sourceX,wave.sourceY,wave.sourceZ);
    g.target=point(wave.targetX,wave.targetY,wave.targetZ);
    const double dx=g.target.x-g.source.x,dy=g.target.y-g.source.y;
    const double distance=std::hypot(dx,dy);
    if(distance<1.0) return g;
    const double length=std::trunc(distance);
    const float ux=static_cast<float>(dx/distance),uy=static_cast<float>(dy/distance);
    const auto edge=[&](double forward,double sideways) {
        const float x=static_cast<float>(forward),y=static_cast<float>(sideways);
        return RuntimeSonicWavePoint{std::trunc(g.source.x+static_cast<float>(x*ux-y*uy)),
            std::trunc(g.source.y+static_cast<float>(x*uy+y*ux)),g.source.z};
    };
    // Sonic mode's original rotated vertices: (+30,+/-100) at the source,
    // (length-30,+/-100) at the destination (0x761640 and 0x761BC7).
    g.lowerFrom=edge(30.0,-100.0);g.lowerTo=edge(length-30.0,-100.0);
    g.upperFrom=edge(30.0,100.0);g.upperTo=edge(length-30.0,100.0);
    return g;
}

int runtimeSonicWaveInitialDirection(const RuntimeProjectileEffect& wave) {
    const auto g=runtimeSonicWaveGeometry(wave);
    // 0x75F230 chooses an eight-way direction from the projected source to
    // the upper destination vertex. It is stored once in the constructor.
    const double dx=g.upperTo.x-g.source.x,dy=g.upperTo.y-g.source.y;
    const double screenX=dx-dy,screenY=(dx+dy)*0.5;
    constexpr double pi=3.14159265358979323846;
    const double angle=std::atan2(screenX,-screenY);
    return (static_cast<int>(std::floor(angle/(pi/4.0)+0.5))+8)%8;
}

std::vector<int> runtimeSonicWaveDamageCells(const RuntimeWindowState& state,
    const RuntimeProjectileEffect& wave,double tail) {
    constexpr double step=static_cast<double>(0.05f);
    const auto g=runtimeSonicWaveGeometry(wave);
    const auto key=[](const RuntimeSonicWavePoint& p) {
        return runtimeCellKey(static_cast<int>(p.x)/256,static_cast<int>(p.y)/256);
    };
    const auto interpolate=[](const RuntimeSonicWavePoint& a,const RuntimeSonicWavePoint& b,double t) {
        return RuntimeSonicWavePoint{std::trunc(a.x*(1.0-t)+b.x*t),
            std::trunc(a.y*(1.0-t)+b.y*t),std::trunc(a.z*(1.0-t)+b.z*t)};
    };
    std::vector<int> cells;
    std::set<int> seen;
    // Preserve insertion order: reactions and deaths may affect later cells.
    const auto add=[&](int cell) {
        if(state.terrainByCell.contains(cell) && seen.insert(cell).second) cells.push_back(cell);
    };
    const bool tracking=wave.speedCellsPerTick>0.0;
    int previousCenter=key(tracking?g.source:g.target);
    const auto& edgeFrom=wave.rot<4?g.lowerFrom:g.upperFrom;
    const auto& edgeTo=wave.rot<4?g.lowerTo:g.upperTo;
    int previousEdge=key(edgeTo);
    for(double t=tracking?step:tail;t<=wave.progress+0.5*step+1e-9;t+=step) {
        if(!tracking) {
            // Retail samples one side as well as the centre after release;
            // it does not fill the entire rectangle or apply a blast radius.
            const double edgeT=wave.rot<4?t:static_cast<double>(static_cast<float>(t));
            const int cell=key(interpolate(edgeFrom,edgeTo,edgeT));
            if(cell!=previousEdge) {add(cell);previousEdge=cell;}
        }
        const int cell=key(interpolate(g.source,g.target,t));
        if(cell!=previousCenter) {add(cell);previousCenter=cell;}
    }
    return cells;
}
