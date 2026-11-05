#include "ui/campaign/win32/RuntimeCampaignDeviceBitmapWin32.h"
#include <algorithm>
#include <iostream>
#include <vector>
using namespace concord::ui;
using namespace concord::ui::win32;
namespace {
int checks=0,failures=0,fault=0,uploads=0,seenW=0,seenH=0;
void expect(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<label<<'\n';}}
HDC WINAPI createDc(HDC dc){return fault==1?nullptr:CreateCompatibleDC(dc);}
HBITMAP WINAPI createBitmap(HDC dc,int w,int h){return fault==2?nullptr:CreateCompatibleBitmap(dc,w,h);}
HGDIOBJ WINAPI selectObject(HDC dc,HGDIOBJ object){if(fault==3)return nullptr;if(fault==4)return HGDI_ERROR;return SelectObject(dc,object);}
BOOL WINAPI deleteObject(HGDIOBJ object){if(fault==7){fault=0;return FALSE;}return DeleteObject(object);}
BOOL WINAPI deleteDc(HDC dc){if(fault==8){fault=0;return FALSE;}return DeleteDC(dc);}
int WINAPI upload(HDC dc,int x,int y,DWORD w,DWORD h,int sx,int sy,UINT start,UINT lines,const void* pixels,const BITMAPINFO* info,UINT usage){
 ++uploads;seenW=info->bmiHeader.biWidth;seenH=info->bmiHeader.biHeight;
 if(fault==6)return 0;return SetDIBitsToDevice(dc,x,y,w,h,sx,sy,start,lines,pixels,info,usage);
}
SkirmishBackBufferGdiApi api(){SkirmishBackBufferGdiApi a;a.createDc=createDc;a.createDeviceBitmap=createBitmap;a.select=selectObject;a.deleteObject=deleteObject;a.deleteDc=deleteDc;return a;}
}
int main(){
 const HDC screen=GetDC(nullptr);expect(screen!=nullptr,"actual current desktop device reference");
 {CampaignDeviceBitmapWin32 warm;expect(warm.ensure(screen,2,2),"warm compatible bitmap");expect(warm.release(),"warm release");}
 const auto baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);std::uint64_t comparedBytes=0;
 for(int n=0;n<24;++n){
  const int w=n%2?800:19,h=n%2?600:11;const auto size=static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4;
  std::vector<unsigned char> pixels(size);for(std::size_t i=0;i<size;++i)pixels[i]=static_cast<unsigned char>(i*31+static_cast<std::size_t>(n)*17);
  {
   CampaignDeviceBitmapWin32 current(api(),upload);SkirmishBackBufferWin32 original,before,after;
   expect(current.ensure(screen,w,h)&&original.ensureDeviceBitmap(screen,w,h),"same real compatible bitmap representation");
   BITMAP bm{};expect(GetObjectW(GetCurrentObject(current.dc(),OBJ_BITMAP),sizeof(bm),&bm)==sizeof(bm)&&bm.bmBits==nullptr,"actual device bitmap has no CPU pointer/DIB");
   const HDC first=current.dc();expect(current.ensure(screen,w,h)&&current.dc()==first,"reuse same native resource dimensions");
   PatBlt(current.dc(),0,0,w,h,BLACKNESS);PatBlt(original.dc(),0,0,w,h,BLACKNESS);
   if(n%3==0){IntersectClipRect(current.dc(),2,1,w-2,h-1);IntersectClipRect(original.dc(),2,1,w-2,h-1);}
   BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
   (void)SetDIBitsToDevice(original.dc(),0,0,w,h,0,0,0,h,pixels.data(),&info,DIB_RGB_COLORS);
   const int oldCalls=uploads;expect(current.upload({pixels,w,h,static_cast<std::size_t>(w)*4,0})&&uploads==oldCalls+1&&seenW==w&&seenH==-h,"exact original topdown info and one synchronous transfer");
   expect(before.ensure(screen,w,h)&&after.ensure(screen,w,h),"full actual readback buffers");
   expect(BitBlt(before.dc(),0,0,w,h,original.dc(),0,0,SRCCOPY)&&BitBlt(after.dc(),0,0,w,h,current.dc(),0,0,SRCCOPY)&&GdiFlush(),"real whole native capture");
   expect(std::equal(before.pixels(),before.pixels()+size,after.pixels()),"entire BGRA including alpha and clip exact vs direct original upload");comparedBytes+=size;
   const auto snapshot=std::vector<unsigned char>(after.pixels(),after.pixels()+size);fault=6;expect(current.upload({pixels,w,h,static_cast<std::size_t>(w)*4,0}),"original zero transfer return ignored");fault=0;
   expect(BitBlt(after.dc(),0,0,w,h,current.dc(),0,0,SRCCOPY)&&GdiFlush()&&std::equal(snapshot.begin(),snapshot.end(),after.pixels()),"zero-write device failure preserves actual prior bitmap");
   const int previous=uploads;expect(!current.upload({{pixels.data(),3},w,h,static_cast<std::size_t>(w)*4,0})&&uploads==previous,"short synchronous borrow rejected before device");
   expect(!current.upload({pixels,w,h,static_cast<std::size_t>(w)*4+4,0})&&uploads==previous,"original tight source contract, no implicit padded reinterpretation");
   expect(current.release()&&!current.ready()&&current.dc()==nullptr&&current.release(),"idempotent release invalidates borrow");
  }
  expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"24 real compatible bitmap scopes strict balance");
 }
 for(const int kind:{1,2,3,4})for(int n=0;n<5;++n){
  CampaignDeviceBitmapWin32 current(api());fault=kind;expect(!current.ensure(screen,800,600)&&!current.ready()&&current.dc()==nullptr,"actual partial DC/deviceBitmap/select failure rolls back");fault=0;expect(current.ensure(screen,800,600),"same owner retries original compatible bitmap");expect(current.release(),"retry release");expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"failure/retry actual GDI equal");
 }
 for(const int kind:{7,8}){
  CampaignDeviceBitmapWin32 current(api());expect(current.ensure(screen,800,600),"cleanup failure owns exact bitmap");fault=kind;expect(!current.release(),"first delete failure reported and retained");fault=0;expect(current.release(),"remaining precise resource retry");expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"cleanup retry strict native balance");
 }
 expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"all native scopes balanced");expect(ReleaseDC(nullptr,screen)!=0,"desktop reference released");
 std::cout<<"Campaign device surface: checks="<<checks<<" failures="<<failures<<" skipped=0 pairs=24 rawBytes="<<comparedBytes<<" borrows=48 partialRollback=20 cleanupRetries=2 gdiBefore="<<baseline<<" gdiAfter="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" compatibleBitmap=yes originalIgnoredTransferZero=yes\n";return failures?1:0;
}
