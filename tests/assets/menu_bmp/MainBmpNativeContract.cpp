#include "OriginalMainBmpReference.h"
#ifdef CONCORD_MAIN_BMP_COMPARE
#include "assets/menu/win32/MenuBmpReaderWin32.h"
#endif
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using concord::assets::MenuImage;
unsigned checks=0,failures=0;
void require(bool v,const char* message){++checks;if(!v){++failures;std::cerr<<message<<"\n";}}
fs::path environment(const wchar_t* name){std::vector<wchar_t> s(GetEnvironmentVariableW(name,nullptr,0));if(s.empty())return{};GetEnvironmentVariableW(name,s.data(),static_cast<DWORD>(s.size()));return fs::path(s.data());}
MenuImage dirty(){MenuImage v;v.width=13;v.height=17;v.bgra={1,2,3};v.source="stale";v.indices={7};v.paletteBgra.fill(0x12345678);v.indexWidth=3;v.indexHeight=5;v.indexLeft=-2;v.indexTop=9;v.transparentZero=true;return v;}
bool same(const MenuImage&a,const MenuImage&b){return a.width==b.width&&a.height==b.height&&a.bgra==b.bgra&&a.source==b.source&&a.indices==b.indices&&a.paletteBgra==b.paletteBgra&&a.indexWidth==b.indexWidth&&a.indexHeight==b.indexHeight&&a.indexLeft==b.indexLeft&&a.indexTop==b.indexTop&&a.transparentZero==b.transparentZero;}
void store(const fs::path& path,bool result,const MenuImage& image){std::ofstream f(path,std::ios::binary);require(bool(f),"open retained image");auto scalar=[&](auto n){f.write(reinterpret_cast<const char*>(&n),sizeof(n));};scalar(result);scalar(image.width);scalar(image.height);scalar(image.bgra.size());f.write(reinterpret_cast<const char*>(image.bgra.data()),static_cast<std::streamsize>(image.bgra.size()));scalar(image.source.size());f.write(image.source.data(),static_cast<std::streamsize>(image.source.size()));require(bool(f),"write retained image");}
int main(){
 const auto input=environment(L"CONCORD_MAIN_BMP_INPUT"),parent=environment(L"CONCORD_MAIN_BMP_OUTPUT");
 require(!input.empty()&&!parent.empty(),"explicit fixture directories required");if(failures)return 1;
 fs::create_directories(parent);auto output=parent/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));require(fs::create_directory(output),"unique owned output");
 std::ifstream list(input/"inputs.txt");std::vector<fs::path> paths;std::string line;while(std::getline(list,line))if(!line.empty())paths.push_back(input/fs::path(std::u8string(reinterpret_cast<const char8_t*>(line.data()),line.size())));require(paths.size()==23,"complete23 inputs");
#ifdef CONCORD_MAIN_BMP_COMPARE
 const auto& reader=concord::assets::menuWin32BmpReader();require(reader.capability()==concord::assets::MenuBmpReadCapability::Bmp,"Windows advertised capability");require(&reader==&concord::assets::menuWin32BmpReader(),"stable provider identity");
#endif
 std::uint64_t pixelBytes=0;unsigned success=0,failure=0,alpha=0;
 for(std::size_t n=0;n<paths.size();++n){
  auto old=dirty();bool a=original_main_bmp::loadBmp(paths[n],old);
  if(a){++success;require(old.valid(),"original owned valid");for(std::size_t at=3;at<old.bgra.size();at+=4)require(old.bgra[at]==255,"original alpha255");alpha+=static_cast<unsigned>(old.bgra.size()/4);}
  else{++failure;require(same(old,MenuImage{}),"original full reset including indexed extras");}
  pixelBytes+=old.bgra.size();store(output/(std::to_string(n)+".bin"),a,old);
#ifdef CONCORD_MAIN_BMP_COMPARE
  auto now=dirty();bool b=reader.read(paths[n],now);require(a==b,"same result");require(same(old,now),"all owned fields and complete pixels/source exact");
  auto retained=now;if(!now.bgra.empty())now.bgra.front()^=0xff;require(same(old,retained),"returned data independent copy");
#endif
 }
 require(success>=15&&failure>=5,"actual supported BMP and failure branches");
 // Warm the exact same complete read sequence before testing steady resource ownership.
 DWORD warm[2]{};
 for(int pass=0;pass<2;++pass){for(const auto&path:paths){MenuImage a;original_main_bmp::loadBmp(path,a);
#ifdef CONCORD_MAIN_BMP_COMPARE
 MenuImage b;reader.read(path,b);
#endif
 }warm[pass]=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);}
 require(warm[0]==warm[1],"complete warm resource stability");
 auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"process handles before");
 for(unsigned pass=0;pass<64;++pass)for(const auto& path:paths){auto old=dirty();bool a=original_main_bmp::loadBmp(path,old);
#ifdef CONCORD_MAIN_BMP_COMPARE
 auto now=dirty();bool b=reader.read(path,now);require(a==b&&same(old,now),"repeated actual old/new exact");
#else
 (void)a;
#endif
 }
 DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"process handles after");auto gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(gdiBefore==gdiAfter,"native bitmap and screenDC released");require(handlesBefore==handlesAfter,"process handle stability");
 std::cout<<"Main BMP actual: checks="<<checks<<" inputs="<<paths.size()<<" success="<<success<<" failure="<<failure<<" alphaPixels="<<alpha<<" pixelBytes="<<pixelBytes<<" repeatedReads="<<64*paths.size()<<" gdiBefore="<<gdiBefore<<" gdiAfter="<<gdiAfter<<" handlesBefore="<<handlesBefore<<" handlesAfter="<<handlesAfter<<" failures="<<failures<<" skipped=0\n";
 std::cout<<"Retained output: "<<output.generic_string()<<"\n";
 return failures?1:0;
}
