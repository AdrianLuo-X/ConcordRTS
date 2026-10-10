#pragma once
#include <cstdint>
#include <locale>
#include <map>
#include <sstream>
#include <string>

// Ordinary Fly locomotion owns a continuous position, independently of the
// terrain cell used for lookups. Q16 cells keep saves and peers identical.
struct RuntimeAircraftFlight {
    std::int64_t x = 0, y = 0;
    int lastTick = -1;
    bool moving = false;
    // Dir65536 retains a smooth heading between rendered Dir256 poses.
    // -1 imports a legacy flight's current world-object facing on first move.
    int heading = -1;
    // -1 imports the cruise height of pre-descent saves. The dock envelope
    // survives a stopped/cancelled approach so neither height nor sprite
    // clearance snaps when the next order starts climbing again.
    std::int64_t z = -1;
    std::int64_t dockFlightStartZ = -1, dockFlightTargetZ = -1;
    int landingHostId = -1, landingDockIndex = -1;
    bool landingSoundPlayed = false;
};
using RuntimeAircraftFlights = std::map<int, RuntimeAircraftFlight>;
constexpr std::int64_t runtimeAircraftCellQ16 = 65536;

inline std::string serializeRuntimeAircraftFlights(const RuntimeAircraftFlights& flights) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"AircraftFlights 3 "<<flights.size()<<'\n';
    for(const auto& [id,f]:flights)out<<id<<' '<<f.x<<' '<<f.y<<' '<<f.lastTick<<' '<<f.moving<<' '<<f.heading
        <<' '<<f.z<<' '<<f.dockFlightStartZ<<' '<<f.dockFlightTargetZ
        <<' '<<f.landingHostId<<' '<<f.landingDockIndex<<' '<<f.landingSoundPlayed<<'\n';
    return out.str();
}

inline bool deserializeRuntimeAircraftFlights(const std::string& text, RuntimeAircraftFlights& flights) {
    if(text.size()>16U*1024U*1024U)return false;
    std::istringstream in(text);in.imbue(std::locale::classic());
    std::string magic;int schema=0,count=0;
    if(!(in>>magic>>schema>>count)||magic!="AircraftFlights"||schema<1||schema>3||count<0||count>100000)return false;
    RuntimeAircraftFlights staged;
    for(int n=0;n<count;++n){int id=-1,moving=-1;RuntimeAircraftFlight f;
        if(!(in>>id>>f.x>>f.y>>f.lastTick>>moving)||id<0||f.x<0||f.y<0
            ||f.x>=512*runtimeAircraftCellQ16||f.y>=512*runtimeAircraftCellQ16
            ||f.lastTick< -1||moving<0||moving>1)return false;
        if(schema>=2&&(!(in>>f.heading)||f.heading< -1||f.heading>65535))return false;
        if(schema==3) {
            int sound=-1;
            if(!(in>>f.z>>f.dockFlightStartZ>>f.dockFlightTargetZ
                >>f.landingHostId>>f.landingDockIndex>>sound)
                ||f.z< -1||f.z>4096*runtimeAircraftCellQ16
                ||f.dockFlightStartZ< -1||f.dockFlightStartZ>4096*runtimeAircraftCellQ16
                ||f.dockFlightTargetZ< -1||f.dockFlightTargetZ>4096*runtimeAircraftCellQ16
                ||f.landingHostId< -1||f.landingDockIndex< -1||f.landingDockIndex>100000
                ||sound<0||sound>1)return false;
            const bool envelope=f.dockFlightStartZ>=0;
            if(envelope!=(f.dockFlightTargetZ>=0)
                ||(envelope&&(f.z<0||f.dockFlightStartZ<=f.dockFlightTargetZ
                    ||f.z<f.dockFlightTargetZ||f.z>f.dockFlightStartZ))
                ||((f.landingHostId>=0)!=(f.landingDockIndex>=0))
                ||(f.landingHostId>=0&&!envelope)||(!envelope&&sound!=0))return false;
            f.landingSoundPlayed=sound!=0;
        }
        f.moving=moving!=0;if(!staged.emplace(id,f).second)return false;
    }
    in>>std::ws;if(!in.eof())return false;
    flights=std::move(staged);return true;
}
