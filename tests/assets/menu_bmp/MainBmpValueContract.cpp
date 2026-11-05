#include "assets/menu/MenuBmpReader.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace concord::assets;
static_assert(std::is_abstract_v<MenuBmpReader>);
static_assert(!noexcept(std::declval<const MenuBmpReader&>().read(
    std::declval<const std::filesystem::path&>(),std::declval<MenuImage&>())));
static_assert(noexcept(std::declval<const MenuBmpReader&>().capability()));
class Probe final: public MenuBmpReader {
public:
 mutable std::filesystem::path observed;
 MenuBmpReadCapability support=MenuBmpReadCapability::Unsupported;
 bool throwAllocation=false;
 MenuBmpReadCapability capability() const noexcept override{return support;}
 bool read(const std::filesystem::path& path,MenuImage& out) const override{
  observed=path;out={};if(throwAllocation)throw std::bad_alloc{};
  if(support==MenuBmpReadCapability::Unsupported)return false;
  out.width=1;out.height=1;out.bgra={17,31,53,255};out.source="owned";return true;
 }
};
int main(){
 unsigned checks=0,failures=0;auto require=[&](bool v){++checks;if(!v)++failures;};
 Probe probe;const MenuBmpReader& reader=probe;MenuImage out;out.indices={7};out.source="stale";
 std::filesystem::path path="relative-pixels.bmp";
 require(reader.capability()==MenuBmpReadCapability::Unsupported);
 require(!reader.read(path,out));require(probe.observed==path);
 require(out.indices.empty()&&out.source.empty()&&out.bgra.empty());
 probe.support=MenuBmpReadCapability::Bmp;require(reader.capability()==MenuBmpReadCapability::Bmp);
 require(reader.read(path,out));require(out.valid());require(out.bgra==std::vector<unsigned char>({17,31,53,255}));
 auto copy=out;out.bgra[0]=0;require(copy.bgra[0]==17);require(copy.source=="owned");
 probe.throwAllocation=true;bool propagated=false;try{reader.read(path,out);}catch(const std::bad_alloc&){propagated=true;}require(propagated);
 require(out.bgra.empty()&&out.source.empty());require(probe.observed==path);
 std::cout<<"Main BMP standard: checks="<<checks<<" nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
