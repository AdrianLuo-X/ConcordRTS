#include "ui/radar/RuntimeRadarPixelStore.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>
static_assert(sizeof(RuntimeMapColorValue)==4 && alignof(RuntimeMapColorValue)==4);
static_assert(std::is_same_v<RuntimeMapColorValue,std::uint32_t>);
static_assert(!std::is_copy_constructible_v<RuntimeRadarPixelWriter>);
static_assert(std::is_same_v<decltype(RuntimeRadarPixelStoreSummary::stores),std::uint64_t>);
static_assert(sizeof(RuntimeRadarPixelStoreSummary)==48 && alignof(RuntimeRadarPixelStoreSummary)==8);
static_assert(offsetof(RuntimeRadarPixelStoreSummary,flushCalls)==40 && offsetof(RuntimeRadarPixelStoreSummary,reference)==44 && offsetof(RuntimeRadarPixelStoreSummary,flushSucceeded)==47);
int main() {
 unsigned checks=0,failures=0;auto check=[&](bool v){++checks;if(!v)++failures;};
 constexpr int w=19,h=13;constexpr std::size_t stride=w*4+12;
 std::vector<unsigned char> bytes(stride*h,0xa5);
 concord::ui::MenuPixelSurfaceView view{bytes,w,h,stride,17};
 RuntimeRadarPixelWriter writer({{},w,h,0,0});check(writer.prepareDirect(view,true));
 unsigned fallbackCalls=0;auto unexpected=[&](std::int32_t,std::int32_t,RuntimeMapColorValue) noexcept -> std::int32_t{++fallbackCalls;return -37;};
 for(unsigned r=0;r<256;++r)for(unsigned g=0;g<256;++g){
  unsigned b=(r*31+g*17)&255U;RuntimeMapColorValue color=r|(g<<8U)|(b<<16U);
  int x=static_cast<int>((r+g)%w),y=static_cast<int>((r*3+g)%h);
  check(writer.store(x,y,color,unexpected)==1);std::size_t at=static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*4;
  check(bytes[at]==b&&bytes[at+1]==g&&bytes[at+2]==r&&bytes[at+3]==0);
 }
 check(fallbackCalls==0);check(writer.summary().stores==65536&&writer.summary().directStores==65536&&writer.summary().gdiStores==0);
 for(int y=0;y<h;++y)for(std::size_t x=w*4;x<stride;++x)check(bytes[static_cast<std::size_t>(y)*stride+x]==0xa5);
 check(!writer.prepareDirect(view,true));
 auto unchanged=bytes;RuntimeMapColorValue seen=0;auto sink=[&](std::int32_t x,std::int32_t y,RuntimeMapColorValue c) noexcept -> std::int32_t{++fallbackCalls;seen=c;return x==7&&y==3?-37:7;};
 check(writer.store(7,3,0x81020304U,sink)==-37);check(seen==0x81020304U&&!writer.summary().directEnabled);
 check(writer.store(0,0,0x00030201U,sink)==7);check(bytes==unchanged);check(!writer.prepareDirect(view,true));
 check(writer.summary().nonRgbStores==1&&writer.summary().outsideStores==0&&writer.summary().gdiStores==2&&fallbackCalls==2);
 check(writer.summary().flushCalls==1&&writer.summary().flushSucceeded&&writer.summary().standardDib);
 for(std::int32_t result:std::array<std::int32_t,4>{0,1,-37,std::numeric_limits<std::int32_t>::max()}){
  RuntimeRadarPixelWriter native({{},w,h,0,0});unsigned count=0;
  auto fallback=[&](std::int32_t,std::int32_t,RuntimeMapColorValue)noexcept{++count;return result;};
  check(native.store(-1,h,0xff000000U,fallback)==result);check(native.store(1,1,0x00ffffffU,fallback)==result);
  check(count==2&&native.summary().outsideStores==1&&native.summary().nonRgbStores==1&&native.summary().flushCalls==0);
  check(!native.prepareDirect(view,true));
 }
 for(int kind=0;kind<7;++kind){
  auto invalid=view;
  if(kind==0)invalid.bytes={};if(kind==1)invalid.width=0;if(kind==2)invalid.height=-1;
  if(kind==3)invalid.rowStride=w*4-1;if(kind==4)invalid.bytes=std::span<unsigned char>(bytes.data(),w*4-1);
  if(kind==5)invalid.rowStride=std::numeric_limits<std::size_t>::max();if(kind==6)invalid.width=w-1;
  RuntimeRadarPixelWriter bad({{},w,h,0,0});check(!bad.prepareDirect(invalid,true));check(!bad.summary().standardDib&&bad.summary().flushCalls==0);
  auto before=bytes;check(bad.store(0,0,1U,unexpected)==-37);check(bytes==before);check(!bad.prepareDirect(view,true));
 }
 RuntimeRadarPixelWriter failed({{},w,h,0,0});check(!failed.prepareDirect(view,false));
 check(failed.summary().standardDib&&failed.summary().flushCalls==1&&!failed.summary().flushSucceeded&&!failed.summary().directEnabled);
 auto before=bytes;check(failed.store(1,1,0xffffffU,unexpected)==-37);check(bytes==before&&!failed.prepareDirect(view,true));
 RuntimeRadarPixelWriter reference(view,true);check(!reference.prepareDirect(view,true));check(reference.store(0,0,1U,unexpected)==-37);
 check(reference.summary().reference&&reference.summary().flushCalls==0&&reference.summary().directStores==0);
 const bool saved=runtimeRadarPixelStoreReference();{RuntimeRadarPixelStoreReferenceScope scope(!saved);check(runtimeRadarPixelStoreReference()!=saved);{RuntimeRadarPixelStoreReferenceScope inner(saved);check(runtimeRadarPixelStoreReference()==saved);}check(runtimeRadarPixelStoreReference()!=saved);}check(runtimeRadarPixelStoreReference()==saved);
 std::cout<<"Radar pixel standard: checks="<<checks<<" colors=65536 stridePadding=exact raw32FallbackAndReturn=exact nativeLatch=exact invalidViews=7 summaryBytes="<<sizeof(RuntimeRadarPixelStoreSummary)<<" failures="<<failures<<" skipped=0\n";
 return failures?1:0;
}
