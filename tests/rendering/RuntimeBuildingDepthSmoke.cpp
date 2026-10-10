#include "render/buildings/RuntimeBuildingDepth.h"
#include "RuntimeBuildingDepthOracle.h"
#include "render/buildings/diagnostics/RuntimeBuildingDepthRowKernelContract.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// The oracle below follows the retail instruction order directly. It does not call
// the production SHP decoder, ART loader, projection, or depth implementation.
namespace {
using namespace RuntimeBuildingDepthOracle;

struct Check {
    std::uint64_t checks = 0;
    void require(bool good, const std::string& label) {
        ++checks; if (!good) throw std::runtime_error(label);
    }
};

// Read the signed value without relying on the implementation-defined result
// of converting an out-of-range unsigned byte to signed char.
int retailTemplateByte(int raw) {
    if (raw == 0) return 0;
    int value = raw - 65;
    if (value < -128) value += 256;
    if (value > 127) value -= 256;
    return value;
}

bool retailBody(const RuntimeBuildingDepthFrame& frame, const OriginalFrame& shape,
    int x, int y, int& answer) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return false;
    if (frame.foundationWidth >= 8) {
        const int originalBase = 32768 - frame.nativeFrameTopY - frame.height
            + frame.normalZAdjust - frame.heightPixel - 1;
        answer = (originalBase / 3) * 3 - (frame.height - y - 1) / 3;
        return true;
    }
    // Two separate original projections: authored canvas pixel relative to
    // its center, and foundation far-corner (W-1,H-1) at native 30/15 scales.
    const int sourceX = frame.offsetX + x - frame.canvasWidth / 2;
    const int sourceY = frame.offsetY + y - frame.canvasHeight / 2;
    const int projectedX = 30 * (frame.foundationWidth - 1) - 30 * (frame.foundationHeight - 1);
    const int projectedY = 15 * (frame.foundationWidth - 1) + 15 * (frame.foundationHeight - 1);
    const int u = sourceX + 198 + frame.moveX - projectedX;
    const int v = sourceY + 446 + frame.moveY - projectedY;
    if (u < 0 || u >= shape.width || v < 0 || v >= shape.height) return false;
    // Retail 4AF0D6..4AF16F substitutes the template intersection height in
    // initialDestRect without shifting its original destination top. This
    // differs from later viewport clipping, which must not alter the base.
    const int originY = frame.offsetY - frame.canvasHeight / 2 + 446 + frame.moveY - projectedY;
    const int firstRow = std::max(0, -originY);
    const int lastRow = std::min(frame.height, shape.height - originY);
    const int originalBase = 32768 - frame.nativeFrameTopY - (lastRow - firstRow)
        + frame.normalZAdjust - frame.heightPixel - 1;
    answer = originalBase - retailTemplateByte(shape.pixels[static_cast<std::size_t>(v) * shape.width + u]);
    return true;
}

void compareBody(Check& check, const RuntimeBuildingDepthFrame& frame,
    const OriginalFrame& shape, int x, int y, const std::string& label) {
    int expected = 0, actual = 0;
    const bool reference = retailBody(frame, shape, x, y, expected);
    const bool accepted = runtimeBuildingDepthAtPixel(frame, shape.pixels, shape.width, shape.height, x, y, actual);
    check.require(accepted == reference && (!reference || actual == expected),
        label + " x=" + std::to_string(x) + " y=" + std::to_string(y)
        + " expected=" + (reference ? std::to_string(expected) : "clipped")
        + " actual=" + (accepted ? std::to_string(actual) : "clipped"));
    int rendered = 0;
    const bool renderAccepted = runtimeBuildingRenderDepthAtPixel(frame, shape.pixels,
        shape.width, shape.height, x, y, rendered);
    const bool inside = x >= 0 && y >= 0 && x < frame.width && y < frame.height;
    const int borderBase = 32768 - frame.nativeFrameTopY - frame.height
        + frame.normalZAdjust - frame.heightPixel - 1;
    const int renderExpected = reference ? expected
        : (borderBase / 3) * 3 - (frame.height - 1 - y) / 3;
    check.require(renderAccepted == inside && (!inside || rendered == renderExpected),
        label + " original-silhouette-preserving-border-depth");
}

void syntheticContract(Check& check, const OriginalFrame& original) {
    RuntimeBuildingDepthFrame frame;
    frame.width = frame.height = 1; frame.moveX = -198; frame.moveY = -446;
    OriginalFrame one; one.width = one.height = 1; one.pixels.resize(1);
    for (int raw = 0; raw < 256; ++raw) {
        one.pixels[0] = static_cast<unsigned char>(raw);
        compareBody(check, frame, one, 0, 0, "all-unsigned-template-values");
    }
    // Hard-coded retail edge values guard both the oracle and the helper.
    const std::array<std::pair<int, int>, 8> anchors{{
        {0,32766},{1,32830},{32,32799},{64,32767},
        {65,32766},{192,32639},{193,32894},{255,32832}}};
    for (const auto [raw, expected] : anchors) {
        one.pixels[0] = static_cast<unsigned char>(raw); int actual = 0;
        check.require(runtimeBuildingDepthAtPixel(frame, one.pixels, 1, 1, 0, 0, actual)
            && actual == expected, "hard-coded signed-byte anchor");
    }
    frame.width = frame.height = 3; frame.moveX = -199; frame.moveY = -447;
    for (int y = -1; y <= 3; ++y) for (int x = -1; x <= 3; ++x)
        compareBody(check, frame, one, x, y, "four-template-edges-clip-not-clamp");

    for (const int canvasWidth : {141,142,143}) for (const int canvasHeight : {109,110,111})
        for (const int foundationWidth : {1,2,4,7,8,9}) for (const int foundationHeight : {1,2,3,7,9})
        for (const auto move : {std::array<int,2>{0,0},{30,15},{-30,15},{30,-15},{-24,-48},{-400,500}}) {
            frame.canvasWidth = canvasWidth; frame.canvasHeight = canvasHeight;
            frame.width = 17; frame.height = 13; frame.offsetX = 29; frame.offsetY = 13;
            frame.foundationWidth = foundationWidth; frame.foundationHeight = foundationHeight;
            frame.moveX = move[0]; frame.moveY = move[1]; frame.normalZAdjust = -37;
            frame.nativeFrameTopY = 681; frame.heightPixel = 60;
            for (const int y : {0,1,6,11,12}) for (const int x : {0,1,8,15,16})
                compareBody(check, frame, original, x, y, "crop-odd-canvas-foundation-signed-move");
        }

    frame = {}; frame.width = 4; frame.height = 11; frame.layer = RuntimeBuildingDepthLayer::Animation;
    const Bytes empty;
    for (const int top : {-81,0,1,2,581,582,583}) for (const int z : {-130,-40,-32,0,27})
        for (const int drawOffset : {-17,0,23}) for (const int height : {0,15,60,180}) {
            frame.nativeFrameTopY = top; frame.heightPixel = height; frame.zAdjust = z; frame.yDrawOffset = drawOffset;
            for (int y = 0; y < frame.height; ++y) {
                const int base = 32768 - top - frame.height + z + drawOffset - height - 1;
                const int expected = base / 3 * 3 - (frame.height - 1 - y) / 3;
                int actual = 0;
                check.require(runtimeBuildingDepthAtPixel(frame, empty, 0, 0, 2, y, actual)
                    && actual == expected, "animation-original-32768-phase-and-three-row-gradient");
                // An equal physical height change moves the native frame up
                // and increases heightPixel by the same amount; base is stable.
                auto lifted = frame; lifted.nativeFrameTopY -= 45; lifted.heightPixel += 45;
                int elevated = 0;
                check.require(runtimeBuildingDepthAtPixel(lifted, empty, 0, 0, 2, y, elevated)
                    && elevated == actual, "animation-world-height-cancels-native-screen-lift");
            }
        }
    for (const auto layer : {RuntimeBuildingDepthLayer::Flat,RuntimeBuildingDepthLayer::Bib}) {
        frame.layer=layer;frame.normalZAdjust=12345; // Not a Flat/Bib input.
        for(const int top:{-21,0,601})for(const int height:{0,15,60})for(const int offset:{-17,0,23}) {
            frame.nativeFrameTopY=top;frame.heightPixel=height;
            frame.zAdjust=layer==RuntimeBuildingDepthLayer::Bib?0:-40;
            frame.yDrawOffset=layer==RuntimeBuildingDepthLayer::Bib?0:offset;
            for(int y=0;y<frame.height;++y) {
                const int expected=32768-top-y+frame.zAdjust+frame.yDrawOffset-height-3;int actual=0;
                check.require(runtimeBuildingDepthAtPixel(frame,empty,0,0,2,y,actual)&&actual==expected,
                    "flat-and-bib-ground-gradient-zero-original-minus-three");
            }
        }
    }
}

void ownerContract(Check& check) {
    RuntimeBuildingDepthPixel pixel;
    check.require(runtimeBuildingDepthAccept(pixel, 10, 150, true)
        && pixel.depth == 150 && pixel.depthOwner == 10 && pixel.visibleOwner == 10, "first-body-writes-depth-and-color");
    check.require(!runtimeBuildingDepthAccept(pixel, 20, 150, true)
        && pixel.depth == 150 && pixel.depthOwner == 10 && pixel.visibleOwner == 10, "strict-tie-keeps-first-body");
    check.require(!runtimeBuildingDepthAccept(pixel, 20, 151, false)
        && pixel.visibleOwner == 10, "far-animation-rejected");
    check.require(runtimeBuildingDepthAccept(pixel, 20, 149, false)
        && pixel.depth == 150 && pixel.depthOwner == 10 && pixel.visibleOwner == 20, "near-animation-color-owner-without-depth-write");
    check.require(runtimeBuildingDepthAccept(pixel, 30, 149, true)
        && pixel.depth == 149 && pixel.depthOwner == 30 && pixel.visibleOwner == 30, "next-body-tests-stored-body-not-read-only-animation");
    check.require(!runtimeBuildingDepthAccept(pixel, 40, 149, false)
        && pixel.visibleOwner == 30, "strict-animation-tie-keeps-first-color");
    // Explicit inter-building-only policy: the same building retains its
    // pre-existing internal layer order. This is not a retail Z equivalence claim.
    check.require(runtimeBuildingDepthAccept(pixel, 30, 200, false)
        && pixel.depth == 149 && pixel.depthOwner == 30 && pixel.visibleOwner == 30, "same-building-internal-order-compatibility");
    RuntimeBuildingDepthPixel reversed;
    check.require(runtimeBuildingDepthAccept(reversed, 20, 150, true)
        && !runtimeBuildingDepthAccept(reversed, 10, 150, true)
        && reversed.visibleOwner == 20, "strict-tie-reversed-submission-keeps-new-first");
    RuntimeBuildingDepthPixel readOnly;
    check.require(runtimeBuildingDepthAccept(readOnly, 70, 100, false)
        && readOnly.depthOwner == -1 && readOnly.visibleOwner == 70
        && runtimeBuildingDepthAccept(readOnly, 80, 200, true)
        && readOnly.visibleOwner == 80, "read-only-pixel-does-not-occlude-later-body");
    RuntimeBuildingDepthPixel interleaved;
    check.require(runtimeBuildingDepthAccept(interleaved, 10, 100, true)
        && runtimeBuildingDepthAccept(interleaved, 20, 50, false)
        && !runtimeBuildingDepthAccept(interleaved, 10, 120, false)
        && interleaved.depth == 100 && interleaved.depthOwner == 10 && interleaved.visibleOwner == 20,
        "same-depth-owner-cannot-bypass-another-visible-building");
    RuntimeBuildingDepthPixel internal;
    check.require(runtimeBuildingDepthAccept(internal, 10, 100, true)
        && runtimeBuildingDepthAccept(internal, 10, 120, true)
        && internal.depth == 100 && internal.depthOwner == 10 && internal.visibleOwner == 10
        && !runtimeBuildingDepthAccept(internal, 20, 110, true),
        "same-building-later-layer-cannot-erase-closer-stored-depth");
}

void animationSlotContract(Check& check) {
    check.require(runtimeBuildingAnimationSlotYSort(-90,60,0)==1024,"native-slot-minus90-plus60-is1024");
    check.require(runtimeBuildingAnimationSlotYSort(-67,26,0)==443,"native-slot-minus67-plus26-is443");
    check.require(runtimeBuildingAnimationSlotYSort(90,-60,0)==-1024,"native-negative-slot-boundary");
    check.require(runtimeBuildingAnimationSlotYSort(67,-26,0)==-443,"native-negative-truncation-boundary");
    // 0x408888CE is exactly 8947918 / 2^21. The Y coefficient is twice
    // this value, so combine integer axes first, then reproduce only the
    // retail float store and truncation. No production helper feeds expected.
    const auto axis=[](int value){return static_cast<int>(static_cast<float>(
        static_cast<double>(value)*8947918.0/2097152.0));};
    for(const int x:{-128,-91,-90,-89,-68,-67,-66,-1,0,1,66,67,68,89,90,91,128})
        for(const int y:{-128,-61,-60,-59,-27,-26,-25,-1,0,1,25,26,27,59,60,61,128})
            for(const int adjust:{-130,0,362,724}) {
                const int expected=axis(x+2*y)+axis(-x+2*y)+adjust;
                check.require(runtimeBuildingAnimationSlotYSort(x,y,adjust)==expected,
                    "native-slot-axis-store-before-truncation x="+std::to_string(x)+" y="+std::to_string(y));
            }
}

void stockContract(Check& check, const std::filesystem::path& root, const OriginalFrame& shape) {
    struct Stock { const char* id; const char* sha; int cw,ch,w,h,x,y,fw,fh,mx,my,opaque; };
    constexpr std::array<Stock,3> stock{{
        {"GAPOWR","0c31af7a95252b9f4ce5c67e5288f13277fe0ebaba0e45a6b2b79d09f4b5e6ac",142,110,84,92,28,14,2,2,0,0,4292},
        {"GAPILE","ff652d8ec56891cd19cee6a04720cdc741029b674bcd38fe546916521cc2ada2",250,148,130,125,74,18,3,2,0,0,7003},
        {"GAREFN","09ed55d3bce2c3cb4c521603334abe5a73af507901bc2964e98cf156ca3e1080",210,196,141,116,32,59,4,3,30,15,8277}
    }};
    std::uint64_t opaqueSamples = 0, clippedSamples = 0;
    for (const auto& item : stock) {
        const auto source = decodeFrameZero(readPinned(root / "local_data/resources/mix_exports/extracted_buildings/all01umd"
            / (std::string(item.id) + ".shp"), item.sha));
        check.require(source.canvasWidth==item.cw && source.canvasHeight==item.ch && source.width==item.w
            && source.height==item.h && source.x==item.x && source.y==item.y
            && std::count_if(source.pixels.begin(),source.pixels.end(),[](auto value){return value!=0;})==item.opaque,
            std::string(item.id)+" independent-original-frame-geometry-and-opaque-count");
        for (const int level : {0,1,4,12}) for (const int normalZ : {-37,0,19}) {
            RuntimeBuildingDepthFrame frame;
            frame.canvasWidth=source.canvasWidth;frame.canvasHeight=source.canvasHeight;
            frame.width=source.width;frame.height=source.height;frame.offsetX=source.x;frame.offsetY=source.y;
            frame.foundationWidth=item.fw;frame.foundationHeight=item.fh;frame.moveX=item.mx;frame.moveY=item.my;
            frame.normalZAdjust=normalZ;frame.heightPixel=15*level;
            // Original foundation origin (20,20); the SHP's authored center
            // and frame crop determine top, not the foundation far corner.
            frame.nativeFrameTopY=15*(20+20)-frame.heightPixel-source.canvasHeight/2+source.y;
            for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x) {
                if(!source.pixels[static_cast<std::size_t>(y)*source.width+x]) continue;
                compareBody(check,frame,shape,x,y,std::string(item.id)+" original-opaque-pixel");
                int depth=0;if(retailBody(frame,shape,x,y,depth))++opaqueSamples;else ++clippedSamples;
            }
        }
    }
    check.require(opaqueSamples==234864 && clippedSamples==0,"all-three-original-body-opaque-pixels-accounted-for");
    std::cout<<"Stock native body samples="<<opaqueSamples<<" clipped="<<clippedSamples<<'\n';
}
}

int main(int argc, char** argv) {
    try {
        Check check;
        std::string rowKernelError;
        const bool rowKernelValid = runRuntimeBuildingDepthRowKernelContractCheck(rowKernelError);
        check.require(rowKernelValid, rowKernelError);
        check.require(sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "independent-SHA256-empty-known-answer");
        check.require(sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "independent-SHA256-abc-known-answer");
        const auto root=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path();
        const auto shape=decodeFrameZero(retailTemplate(root));
        check.require(shape.canvasWidth==396&&shape.canvasHeight==477&&shape.width==396&&shape.height==477
            &&shape.x==0&&shape.y==0&&shape.pixels.size()==188892,"retail-template-shape");
        check.require(shape.pixels[0]==158&&shape.pixels[198]==222&&shape.pixels[476*396]==0
            &&shape.pixels[476*396+198]==64,"retail-template-raw-anchors");
        check.require(std::count(shape.pixels.begin(),shape.pixels.end(),0)==19404,"retail-template-zero-count");
        syntheticContract(check,shape);ownerContract(check);animationSlotContract(check);stockContract(check,root,shape);
        std::cout<<"Runtime building depth: "<<check.checks<<" independent checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Runtime building depth FAIL: "<<error.what()<<'\n';return 1;
    }
}
