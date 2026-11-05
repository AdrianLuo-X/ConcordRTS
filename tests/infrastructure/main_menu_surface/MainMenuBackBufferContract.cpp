#include "ui/main_menu/win32/RuntimeMainMenuBackBufferWin32.h"
#include <array>
#include <iostream>
#include <type_traits>
#include <vector>
using namespace concord::ui;
using namespace concord::ui::win32;
namespace {
int checks=0,failures=0,fault=0,flushes=0;
void expect(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<label<<'\n';}}
HDC WINAPI createDc(HDC dc){return fault==1?nullptr:CreateCompatibleDC(dc);}
HBITMAP WINAPI createDib(HDC dc,const BITMAPINFO* info,UINT use,void** bits,HANDLE section,DWORD offset){
 if(fault==2)return nullptr;auto result=CreateDIBSection(dc,info,use,bits,section,offset);if(fault==5)*bits=nullptr;return result;
}
HGDIOBJ WINAPI selectObject(HDC dc,HGDIOBJ object){if(fault==3)return nullptr;if(fault==4)return HGDI_ERROR;return SelectObject(dc,object);}
BOOL WINAPI flush(){++flushes;return fault==6?FALSE:GdiFlush();}
BOOL WINAPI deleteObject(HGDIOBJ object){if(fault==7){fault=0;return FALSE;}return DeleteObject(object);}
BOOL WINAPI deleteDc(HDC dc){if(fault==8){fault=0;return FALSE;}return DeleteDC(dc);}
SkirmishBackBufferGdiApi api(){SkirmishBackBufferGdiApi a;a.createDc=createDc;a.createDib=createDib;a.select=selectObject;a.flush=flush;a.deleteObject=deleteObject;a.deleteDc=deleteDc;return a;}
}
int main(){
 static_assert(!std::is_copy_constructible_v<MainMenuBackBufferWin32>);
 {MainMenuBackBufferWin32 warm;expect(warm.ensure(nullptr,2,2),"real warm owner");expect(warm.release(),"warm release");}
 const auto baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);std::uint64_t pixelBytes=0;
 for(int n=0;n<24;++n){
  const int width=n%2?800:13,height=n%2?600:9;const auto stride=static_cast<std::size_t>(width)*4+8;
  std::vector<unsigned char> bytes(stride*static_cast<std::size_t>(height),0x5b);
  for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i*17+static_cast<std::size_t>(n)*31);
  const ConstMenuPixelSurfaceView source{bytes,width,height,stride,0};
  MainMenuBackBufferWin32 owner(api());expect(owner.ensure(nullptr,width,height),"real actual native allocation");const auto dc=owner.dc();
  expect(owner.ensure(nullptr,width,height)&&owner.dc()==dc,"same dimensions reuse actual owner");
  const int before=flushes;fault=n%2?6:0;expect(owner.uploadLegacy(source)&&flushes==before+1,"original ignored flush result policy including false");fault=0;
  for(int y=0;y<height;++y){const auto* expected=bytes.data()+static_cast<std::size_t>(y)*stride;const auto* got=owner.pixels()+static_cast<std::size_t>(y)*static_cast<std::size_t>(width)*4;expect(std::equal(expected,expected+width*4,got),"whole BGRA exact including alpha, no source padding");pixelBytes+=static_cast<std::uint64_t>(width)*4;}
  auto invalid=source;invalid.bytes=invalid.bytes.first(3);const int oldFlush=flushes;expect(!owner.uploadLegacy(invalid)&&flushes==oldFlush,"invalid short borrow has no device writes");
  expect(owner.release()&&!owner.ready()&&owner.dc()==nullptr&&owner.pixels()==nullptr,"release invalidates native borrows");expect(owner.release(),"repeat release idempotent");
  expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"strict repeated real GDI balance");
 }
 for(const int kind:{1,2,3,4,5})for(int n=0;n<4;++n){
  MainMenuBackBufferWin32 owner(api());fault=kind;expect(!owner.ensure(nullptr,4,3),"real partial allocation or selection failure");expect(!owner.ready()&&owner.dc()==nullptr&&owner.pixels()==nullptr,"rollback empties actual resources");fault=0;expect(owner.ensure(nullptr,4,3),"retry same owner after failure");expect(owner.release(),"release successful retry");expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"failed creation never leaks");
 }
 for(const int kind:{7,8}){
  MainMenuBackBufferWin32 owner(api());expect(owner.ensure(nullptr,4,3),"cleanup retry initial owner");fault=kind;expect(!owner.release(),"first cleanup failure retained");fault=0;expect(owner.release(),"second cleanup succeeds on precise remaining resource");expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"cleanup retry GDI balance");
 }
 expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"final GDI equality");
 std::cout<<"Main menu surface owner: checks="<<checks<<" failures="<<failures<<" skipped=0 rawBytes="<<pixelBytes<<" borrows=24 gdiBefore="<<baseline<<" gdiAfter="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" originalIgnoredFlushFalse=yes actualFailureRollback=20 cleanupRetries=2\n";
 return failures?1:0;
}
