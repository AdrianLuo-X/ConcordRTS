// Private CLI trace of actual original result-font input. Normal state is null.
#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
struct RuntimeResultBitmapTraceCall {
    std::wstring text;
    RuntimeInputPixelRect client, base;
    std::uint32_t packedColor = 0;
    bool centered = false;
};
bool writeRuntimeResultBitmapTrace(const std::filesystem::path& path,
    const std::vector<unsigned char>& fontBytes,
    const std::vector<RuntimeResultBitmapTraceCall>& calls, std::string& error) {
    if (calls.empty() || fontBytes.size() > std::numeric_limits<std::uint32_t>::max()
        || calls.size() > std::numeric_limits<std::uint32_t>::max()) { error="empty/oversized result font trace";return false; }
    std::ofstream out(path,std::ios::binary);if(!out){error="cannot create result font trace";return false;}
    out.write("W015R001",8);
    const auto u32=[&](std::uint32_t x){out.write(reinterpret_cast<const char*>(&x),sizeof(x));};
    const auto rect=[&](const RuntimeInputPixelRect& r){u32(static_cast<std::uint32_t>(r.left));u32(static_cast<std::uint32_t>(r.top));u32(static_cast<std::uint32_t>(r.right));u32(static_cast<std::uint32_t>(r.bottom));};
    u32(static_cast<std::uint32_t>(fontBytes.size()));u32(static_cast<std::uint32_t>(calls.size()));
    out.write(reinterpret_cast<const char*>(fontBytes.data()),static_cast<std::streamsize>(fontBytes.size()));
    for(const auto& c:calls){
        if(c.text.size()>std::numeric_limits<std::uint32_t>::max()){error="oversized result label";return false;}
        u32(static_cast<std::uint32_t>(c.text.size()));rect(c.client);rect(c.base);u32(c.packedColor);u32(c.centered?1U:0U);
        for(const auto ch:c.text){const auto unit=static_cast<std::uint16_t>(ch);out.write(reinterpret_cast<const char*>(&unit),sizeof(unit));}
    }
    if(!out){error="result font trace write failed";return false;}
    std::cout<<"Result bitmap trace: calls="<<calls.size()<<" fontBytes="<<fontBytes.size()<<" path="<<path.string()<<"\n";return true;
}
