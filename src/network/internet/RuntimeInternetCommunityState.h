#pragma once
#include <cstdint>
#include <iosfwd>
#include <streambuf>
#include <string>

inline constexpr std::uint64_t runtimeInternetCommunityStateMaximumBytes = 256ULL * 1024 * 1024;

// Detects incomplete records, complete-line truncation and accidental edits
// before the service mutates any account or match. Rewinds on success.
bool verifyRuntimeInternetCommunityState(std::istream& input, bool& legacy, std::string& error);

// A bounded streaming envelope around the existing state writer. The footer
// covers the exact header/body bytes; it is written outside this buffer.
class RuntimeInternetCommunityStateBuffer final : public std::streambuf {
public:
    explicit RuntimeInternetCommunityStateBuffer(std::ostream& destination);
    bool finish(std::ostream& destination, std::string& error);
protected:
    std::streamsize xsputn(const char* data, std::streamsize length) override;
    int_type overflow(int_type character) override;
    int sync() override;
private:
    void track(const char* data, std::streamsize length);
    std::streambuf* destination_;
    std::uint64_t bytes_ = 0;
    std::uint64_t checksum_ = 14695981039346656037ULL;
};
