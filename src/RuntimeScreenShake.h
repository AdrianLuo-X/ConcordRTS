#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

struct RuntimeScreenShakeOffset { int x=0; int y=0; };
inline constexpr int runtimeScreenShakeFrames=8;
inline constexpr int runtimeScreenShakeMaximumPixels=64;

inline std::uint32_t runtimeScreenShakeMix(std::uint32_t value) {
    value^=value>>16;value*=0x7feb352dU;value^=value>>15;
    value*=0x846ca68bU;return value^(value>>16);
}
inline int runtimeScreenShakeAxis(int low,int high,int frame,std::uint32_t seed) {
    if(frame<0 || frame>=runtimeScreenShakeFrames)return 0;
    const auto magnitude=[](int value){return std::abs(std::clamp(value,-runtimeScreenShakeMaximumPixels,runtimeScreenShakeMaximumPixels));};
    int minimum=magnitude(low),maximum=magnitude(high);
    if(minimum>maximum)std::swap(minimum,maximum);
    const auto random=runtimeScreenShakeMix(seed+static_cast<std::uint32_t>(frame)*0x9e3779b9U);
    const int amplitude=minimum+static_cast<int>(random%static_cast<std::uint32_t>(maximum-minimum+1));
    const int faded=(amplitude*(runtimeScreenShakeFrames-frame)+runtimeScreenShakeFrames/2)/runtimeScreenShakeFrames;
    return ((seed+static_cast<std::uint32_t>(frame))&1U)?-faded:faded;
}
inline RuntimeScreenShakeOffset runtimeScreenShakeRange(int xLow,int xHigh,int yLow,int yHigh,
        int ageTicks,int hz,std::uint32_t seed) {
    if(ageTicks<0 || hz<=0)return {};
    const auto ageFrames=static_cast<std::int64_t>(ageTicks)*15/hz;
    if(ageFrames>=runtimeScreenShakeFrames)return {};
    return {runtimeScreenShakeAxis(xLow,xHigh,static_cast<int>(ageFrames),seed),
        runtimeScreenShakeAxis(yLow,yHigh,static_cast<int>(ageFrames),seed^0x92d68ca3U)};
}

// Translate only the battlefield rectangle. Edge pixels extend into exposed
// strips, so shaking never exposes stale sidebar pixels or a black border.
inline bool runtimeTranslateScreenShakePixels(std::uint32_t* pixels,int width,int height,
        int left,int top,int right,int bottom,RuntimeScreenShakeOffset offset,
        std::vector<std::uint32_t>& scratch) {
    if(!pixels || width<=0 || height<=0 || (!offset.x && !offset.y))return false;
    left=std::clamp(left,0,width);right=std::clamp(right,left,width);
    top=std::clamp(top,0,height);bottom=std::clamp(bottom,top,height);
    const int columns=right-left,rows=bottom-top;
    if(columns==0 || rows==0)return false;
    const int dx=std::clamp(offset.x,-columns,columns),dy=std::clamp(offset.y,-rows,rows);
    scratch.resize(static_cast<std::size_t>(columns)*rows);
    for(int y=0;y<rows;++y)std::copy_n(pixels+static_cast<std::size_t>(top+y)*width+left,columns,
        scratch.data()+static_cast<std::size_t>(y)*columns);
    for(int y=0;y<rows;++y){
        const auto* source=scratch.data()+static_cast<std::size_t>(std::clamp(y-dy,0,rows-1))*columns;
        auto* destination=pixels+static_cast<std::size_t>(top+y)*width+left;
        if(dx>=0){std::fill_n(destination,dx,source[0]);std::copy_n(source,columns-dx,destination+dx);}
        else{std::copy_n(source-dx,columns+dx,destination);std::fill_n(destination+columns+dx,-dx,source[columns-1]);}
    }
    return true;
}
