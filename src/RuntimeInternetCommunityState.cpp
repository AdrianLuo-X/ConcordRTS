#include "RuntimeInternetCommunityState.h"
#include <array>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>

namespace {
std::string footer(std::uint64_t bytes, std::uint64_t checksum) {
    std::ostringstream text;
    text << "Z\t" << bytes << '\t' << std::hex << std::setfill('0') << std::setw(16) << checksum;
    return text.str();
}
void mix(std::uint64_t& checksum, char character) {
    checksum ^= static_cast<unsigned char>(character); checksum *= 1099511628211ULL;
}
}

bool verifyRuntimeInternetCommunityState(std::istream& input, bool& legacy, std::string& error) {
    legacy = false;
    std::uint64_t bytes = 0, checksum = 14695981039346656037ULL;
    bool header = false, finished = false, sealed = false;
    std::string line;
    auto* source = input.rdbuf();
    for (;;) {
        const auto next = source->sbumpc();
        if (std::char_traits<char>::eq_int_type(next, std::char_traits<char>::eof())) {
            if (!line.empty() && !legacy) { error = "community state ended inside a record"; return false; }
            break;
        }
        if (finished) { error = "community state has data after its commit footer"; return false; }
        const char character = std::char_traits<char>::to_char_type(next);
        if (character != '\n') {
            if (line.size() >= 4096) { error = "community state line exceeds its bound"; return false; }
            line.push_back(character); continue;
        }
        if (!header) {
            sealed = line == "CONCORD_WOL_COMMUNITY_V4";
            legacy = line == "CONCORD_WOL_COMMUNITY_V1" || line == "CONCORD_WOL_COMMUNITY_V2" || line == "CONCORD_WOL_COMMUNITY_V3";
            if (!sealed && !legacy) { error = "community state header is invalid"; return false; }
            header = true;
        } else if (sealed && line.starts_with("Z\t")) {
            if (line != footer(bytes, checksum)) { error = "community state commit size or checksum differs"; return false; }
            finished = true; line.clear(); continue;
        }
        if (bytes + line.size() + 1 > runtimeInternetCommunityStateMaximumBytes) { error = "community state exceeds its size bound"; return false; }
        for (char byte : line) mix(checksum, byte);
        mix(checksum, '\n'); bytes += line.size() + 1; line.clear();
    }
    if (!header || (sealed && !finished)) { error = "community state commit footer is missing"; return false; }
    input.clear(); input.seekg(0);
    if (!input) { error = "community state cannot be rewound"; return false; }
    error.clear(); return true;
}

RuntimeInternetCommunityStateBuffer::RuntimeInternetCommunityStateBuffer(std::ostream& destination)
    : destination_(destination.rdbuf()) {}
void RuntimeInternetCommunityStateBuffer::track(const char* data, std::streamsize length) {
    for (std::streamsize i = 0; i < length; ++i) mix(checksum_, data[i]);
    bytes_ += static_cast<std::uint64_t>(length);
}
std::streamsize RuntimeInternetCommunityStateBuffer::xsputn(const char* data, std::streamsize length) {
    if (length < 0 || static_cast<std::uint64_t>(length) > runtimeInternetCommunityStateMaximumBytes - bytes_) return 0;
    const auto written = destination_->sputn(data, length);
    if (written > 0) track(data, written);
    return written;
}
RuntimeInternetCommunityStateBuffer::int_type RuntimeInternetCommunityStateBuffer::overflow(int_type character) {
    if (traits_type::eq_int_type(character, traits_type::eof())) return traits_type::not_eof(character);
    const char byte = traits_type::to_char_type(character);
    return xsputn(&byte, 1) == 1 ? character : traits_type::eof();
}
int RuntimeInternetCommunityStateBuffer::sync() { return destination_->pubsync(); }
bool RuntimeInternetCommunityStateBuffer::finish(std::ostream& destination, std::string& error) {
    if (sync() != 0) { error = "community state body could not be flushed"; return false; }
    destination << footer(bytes_, checksum_) << '\n';
    if (!destination) { error = "community state footer could not be written"; return false; }
    error.clear(); return true;
}
