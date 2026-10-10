#pragma once

#include <array>
#include <cstddef>
#include <istream>
#include <iterator>
#include <string>

// Read exactly what the original stream-buffer iterator sees. The reported
// file size is deliberately not an EOF boundary; a loose file can change
// between the stat and the read. Do not impose the provider's size limit here:
// its final check historically leaves the complete oversized text in output.
namespace RuntimeIniTextReadDetail {

inline constexpr std::size_t chunkBytes = 64U * 1024U;

inline void readToEnd(std::istream& input, std::string& text, bool reference) {
    if (reference) {
        text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return;
    }
    text.clear();
    std::array<char, chunkBytes> buffer;
    std::streambuf* source = input.rdbuf();
    using Traits = std::char_traits<char>;
    for (;;) {
        const std::streamsize count = source->sgetn(buffer.data(),
            static_cast<std::streamsize>(buffer.size()));
        if (count > 0) text.append(buffer.data(), static_cast<std::size_t>(count));
        if (count < static_cast<std::streamsize>(buffer.size())) {
            const auto next = source->sgetc();
            if (Traits::eq_int_type(next, Traits::eof())) break;
            // A short read need not be EOF. Even a stream buffer returning
            // zero from xsgetn can expose a character through its get area.
            if (count == 0) {
                const auto consumed = source->sbumpc();
                if (Traits::eq_int_type(consumed, Traits::eof())) break;
                text.push_back(Traits::to_char_type(consumed));
            }
        }
    }
}

} // namespace RuntimeIniTextReadDetail
