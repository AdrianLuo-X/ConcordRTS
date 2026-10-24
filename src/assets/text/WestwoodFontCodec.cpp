#include "assets/text/WestwoodFontCodec.h"
#include <cstddef>

namespace concord::assets {
namespace {
constexpr std::size_t mapOffset = 0x1c;
constexpr std::size_t dataOffset = mapOffset + 65536U * 2U;
std::uint32_t read32(std::span<const unsigned char> bytes, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(bytes[at])
        | (static_cast<std::uint32_t>(bytes[at+1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[at+2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[at+3]) << 24U);
}
enum class Unavailable { None, Reference, Width, Font };
struct Lookup { const unsigned char* record; Unavailable reason; };
Lookup mapped(std::span<const unsigned char> bytes, const WestwoodFontMetrics& m,
    std::uint16_t code) noexcept {
    if(bytes.size()<dataOffset || m.recordSize<=1 || m.lineHeight==0
        || m.stride==0 || m.stride>16 || m.bitmapRows==0 || m.bitmapRows>64)
        return {nullptr,Unavailable::Font};
    const std::size_t at=mapOffset+static_cast<std::size_t>(code)*2U;
    const std::uint16_t ref=static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[at]) | (static_cast<std::uint16_t>(bytes[at+1]) << 8U));
    if(ref==0 || static_cast<std::uint32_t>(ref)>m.glyphCount)
        return {nullptr,Unavailable::Reference};
    const std::size_t index=static_cast<std::size_t>(ref-1U);
    if(index >= (bytes.size()-dataOffset)/m.recordSize)
        return {nullptr,Unavailable::Font};
    const auto* record=bytes.data()+dataOffset+index*m.recordSize;
    return record[0]<=m.stride*8U ? Lookup{record,Unavailable::None}
        : Lookup{nullptr,Unavailable::Width};
}
}
bool decodeWestwoodFontHeader(std::span<const unsigned char> bytes,
    WestwoodFontMetrics& metrics) noexcept {
    if(bytes.size()<dataOffset || bytes[0]!='f' || bytes[1]!='o'
        || bytes[2]!='n' || bytes[3]!='T') return false;
    metrics={read32(bytes,8),read32(bytes,12),read32(bytes,16),read32(bytes,20),read32(bytes,24)};
    return metrics.stride>0 && metrics.stride<=16
        && metrics.bitmapRows>0 && metrics.bitmapRows<=64
        && metrics.lineHeight>=metrics.bitmapRows && metrics.lineHeight<=64
        && metrics.glyphCount>0
        && metrics.recordSize==1U+metrics.stride*metrics.bitmapRows
        && metrics.glyphCount<=(bytes.size()-dataOffset)/metrics.recordSize;
}
const unsigned char* westwoodFontGlyph(std::span<const unsigned char> bytes,
    const WestwoodFontMetrics& metrics, std::uint16_t codeUnit,
    WestwoodGlyphFallback fallback) noexcept {
    const auto result=mapped(bytes,metrics,codeUnit);
    if(result.record || codeUnit==static_cast<std::uint16_t>('?'))return result.record;
    if(result.reason==Unavailable::Reference
        || (result.reason==Unavailable::Width && fallback==WestwoodGlyphFallback::AnyUnavailableRecord))
        return mapped(bytes,metrics,static_cast<std::uint16_t>('?')).record;
    return nullptr;
}
} // namespace concord::assets
