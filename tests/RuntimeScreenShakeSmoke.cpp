#include "RuntimeScreenShake.h"
#include "RulesDatabase.h"
#include <climits>
#include <iostream>
#include <stdexcept>
int main(){try{
    const auto check=[](bool ok,const char* error){if(!ok)throw std::runtime_error(error);};
    IniFile ini;check(ini.loadFromString("[Warheads]\n0=SHAKE\n[SHAKE]\nShakeXlo=5\nShakeXhi=10\nShakeYlo=10\nShakeYhi=20\n"),"INI");
    RulesDatabase rules;check(rules.loadFromIni(ini),"rules");
    RulesDatabase copied=rules;RulesDatabase assigned;assigned=copied;RulesDatabase moved=std::move(assigned);
    for(const auto* database:{&rules,&copied,&moved}){
        const auto* p=database->findWarheadLifecycleProfile("shake");
        check(p && p->shakeXlo==5 && p->shakeXhi==10 && p->shakeYlo==10 && p->shakeYhi==20,"lifecycle profile lost after copy/move");
    }
    for(int frame=0;frame<=8;++frame)for(int hz:{15,30,60}){
        const auto offset=runtimeScreenShakeRange(5,10,10,20,frame*hz/15,hz,27);
        const auto reference=runtimeScreenShakeRange(5,10,10,20,frame,15,27);
        check(offset.x==reference.x && offset.y==reference.y,"tick-rate-dependent shake phase");
        check(std::abs(offset.x)<=10 && std::abs(offset.y)<=20,"range overflow");
        if(frame==8)check(!offset.x && !offset.y,"shake did not end");
    }
    auto zero=runtimeScreenShakeRange(0,0,0,0,0,30,0);check(!zero.x && !zero.y,"default shake");
    auto extreme=runtimeScreenShakeRange(INT_MIN,INT_MAX,INT_MAX,INT_MIN,0,30,0);
    check(std::abs(extreme.x)==64 && std::abs(extreme.y)==64,"safe extreme bounds");
    for(int dx:{-64,-2,0,2,64})for(int dy:{-64,-2,0,2,64}){
        std::vector<std::uint32_t> pixels(64),scratch;for(unsigned i=0;i<64;++i)pixels[i]=i;
        runtimeTranslateScreenShakePixels(pixels.data(),8,8,2,2,6,6,{dx,dy},scratch);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x){
            const auto expected=(x>=2 && x<6 && y>=2 && y<6)?std::clamp(y-dy,2,5)*8+std::clamp(x-dx,2,5):y*8+x;
            check(pixels[y*8+x]==expected,"viewport or stationary UI pixels changed incorrectly");
        }
    }
    std::cout<<"Screen shake core passed: lifecycle copies, 15/30/60Hz, bounded axes, fade and viewport isolation\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
