#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Room passwords are deliberately kept outside public advertisements and
// canonical gameplay packets.  These helpers provide portable SHA-256/HMAC
// derivation for the LAN challenge and the authenticated Internet service
// envelope without adding a platform crypto dependency to the pure helpers.
inline constexpr std::size_t runtimeRoomPasswordMaximumBytes = 128U;
inline constexpr std::size_t runtimeRoomPasswordVerifierBytes = 32U;

using RuntimeRoomPasswordVerifier =
    std::array<std::uint8_t, runtimeRoomPasswordVerifierBytes>;
using RuntimeRoomPasswordProof =
    std::array<std::uint8_t, runtimeRoomPasswordVerifierBytes>;

namespace runtime_room_password_detail {

inline constexpr std::array<std::uint32_t, 64U> sha256Constants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
}};

inline std::uint32_t rotateRight(std::uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32U - bits));
}

template <typename Byte>
inline void secureClearBytes(Byte* bytes, std::size_t size) noexcept {
    volatile Byte* cursor = bytes;
    while (size-- != 0U) *cursor++ = static_cast<Byte>(0);
}

class Sha256 {
public:
    Sha256() = default;

    ~Sha256() {
        secureClearBytes(state_.data(), state_.size());
        secureClearBytes(block_.data(), block_.size());
        bitCount_ = 0U;
        blockSize_ = 0U;
    }

    void update(std::span<const std::uint8_t> bytes) {
        for (const std::uint8_t byte : bytes) {
            block_[blockSize_++] = byte;
            bitCount_ += 8U;
            if (blockSize_ == block_.size()) {
                transform(block_);
                blockSize_ = 0U;
            }
        }
    }

    void update(std::string_view text) {
        update(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size()
        ));
    }

    void updateU64(std::uint64_t value) {
        std::array<std::uint8_t, 8U> encoded{};
        for (std::size_t index = 0U; index < encoded.size(); ++index) {
            encoded[encoded.size() - 1U - index] =
                static_cast<std::uint8_t>(value >> (index * 8U));
        }
        update(encoded);
    }

    void updateSized(std::string_view text) {
        updateU64(static_cast<std::uint64_t>(text.size()));
        update(text);
    }

    RuntimeRoomPasswordProof finish() {
        const std::uint64_t originalBitCount = bitCount_;
        block_[blockSize_++] = 0x80U;
        if (blockSize_ > 56U) {
            while (blockSize_ < block_.size()) block_[blockSize_++] = 0U;
            transform(block_);
            blockSize_ = 0U;
        }
        while (blockSize_ < 56U) block_[blockSize_++] = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            block_[63U - index] = static_cast<std::uint8_t>(
                originalBitCount >> (index * 8U)
            );
        }
        transform(block_);
        RuntimeRoomPasswordProof digest{};
        for (std::size_t word = 0U; word < state_.size(); ++word) {
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                digest[word * 4U + byte] = static_cast<std::uint8_t>(
                    state_[word] >> ((3U - byte) * 8U)
                );
            }
        }
        return digest;
    }

private:
    void transform(const std::array<std::uint8_t, 64U>& block) {
        std::array<std::uint32_t, 64U> words{};
        for (std::size_t index = 0U; index < 16U; ++index) {
            const std::size_t offset = index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(block[offset]) << 24U)
                | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
                | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
                | static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t s0 = rotateRight(words[index - 15U], 7U)
                ^ rotateRight(words[index - 15U], 18U)
                ^ (words[index - 15U] >> 3U);
            const std::uint32_t s1 = rotateRight(words[index - 2U], 17U)
                ^ rotateRight(words[index - 2U], 19U)
                ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0
                + words[index - 7U] + s1;
        }

        std::uint32_t a = state_[0U];
        std::uint32_t b = state_[1U];
        std::uint32_t c = state_[2U];
        std::uint32_t d = state_[3U];
        std::uint32_t e = state_[4U];
        std::uint32_t f = state_[5U];
        std::uint32_t g = state_[6U];
        std::uint32_t h = state_[7U];
        for (std::size_t index = 0U; index < words.size(); ++index) {
            const std::uint32_t sum1 = rotateRight(e, 6U)
                ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 = h + sum1 + choice
                + sha256Constants[index] + words[index];
            const std::uint32_t sum0 = rotateRight(a, 2U)
                ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0U] += a;
        state_[1U] += b;
        state_[2U] += c;
        state_[3U] += d;
        state_[4U] += e;
        state_[5U] += f;
        state_[6U] += g;
        state_[7U] += h;
        secureClearBytes(words.data(), words.size());
    }

    std::array<std::uint32_t, 8U> state_{{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    }};
    std::array<std::uint8_t, 64U> block_{};
    std::uint64_t bitCount_ = 0U;
    std::size_t blockSize_ = 0U;
};

inline RuntimeRoomPasswordProof hmac(
    const RuntimeRoomPasswordVerifier& key,
    std::string_view domain,
    std::string_view roomId,
    std::string_view peerId,
    std::uint64_t first,
    std::uint64_t second = 0U,
    bool includeSecond = false
) {
    std::array<std::uint8_t, 64U> innerPad{};
    std::array<std::uint8_t, 64U> outerPad{};
    for (std::size_t index = 0U; index < innerPad.size(); ++index) {
        const std::uint8_t keyByte = index < key.size() ? key[index] : 0U;
        innerPad[index] = static_cast<std::uint8_t>(keyByte ^ 0x36U);
        outerPad[index] = static_cast<std::uint8_t>(keyByte ^ 0x5cU);
    }
    Sha256 inner;
    inner.update(innerPad);
    inner.updateSized(domain);
    inner.updateSized(roomId);
    inner.updateSized(peerId);
    inner.updateU64(first);
    if (includeSecond) inner.updateU64(second);
    RuntimeRoomPasswordProof innerDigest = inner.finish();
    Sha256 outer;
    outer.update(outerPad);
    outer.update(innerDigest);
    RuntimeRoomPasswordProof result = outer.finish();
    secureClearBytes(innerPad.data(), innerPad.size());
    secureClearBytes(outerPad.data(), outerPad.size());
    secureClearBytes(innerDigest.data(), innerDigest.size());
    return result;
}

} // namespace runtime_room_password_detail

inline bool runtimeRoomPasswordValid(std::string_view password) noexcept {
    return !password.empty()
        && password.size() <= runtimeRoomPasswordMaximumBytes
        && password.find('\0') == std::string_view::npos;
}

inline RuntimeRoomPasswordVerifier runtimeRoomPasswordVerifier(
    std::string_view roomId,
    std::string_view password
) {
    runtime_room_password_detail::Sha256 hash;
    hash.updateSized("ConcordRTS.RoomPasswordVerifier.v1");
    hash.updateSized(roomId);
    hash.updateSized(password);
    return hash.finish();
}

inline RuntimeRoomPasswordProof runtimeInternetRoomPasswordJoinProof(
    const RuntimeRoomPasswordVerifier& verifier,
    std::string_view roomId,
    std::string_view peerId,
    std::uint64_t transactionId
) {
    return runtime_room_password_detail::hmac(
        verifier,
        "ConcordRTS.InternetRoomPasswordJoin.v1",
        roomId,
        peerId,
        transactionId
    );
}

inline std::uint64_t runtimeLanRoomPasswordAdmissionProof(
    const RuntimeRoomPasswordVerifier& verifier,
    std::string_view roomId,
    std::string_view peerId,
    std::uint64_t roomGeneration,
    std::uint64_t admissionToken
) {
    RuntimeRoomPasswordProof digest = runtime_room_password_detail::hmac(
        verifier,
        "ConcordRTS.LanRoomPasswordAdmission.v1",
        roomId,
        peerId,
        roomGeneration,
        admissionToken,
        true
    );
    std::uint64_t proof = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        proof = (proof << 8U) | digest[index];
    }
    runtime_room_password_detail::secureClearBytes(
        digest.data(),
        digest.size()
    );
    return proof;
}

inline bool runtimeRoomPasswordConstantTimeEqual(
    std::span<const std::uint8_t> left,
    std::span<const std::uint8_t> right
) noexcept {
    if (left.size() != right.size()) return false;
    std::uint8_t difference = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | (left[index] ^ right[index])
        );
    }
    return difference == 0U;
}

inline void runtimeRoomPasswordSecureClear(std::string& value) noexcept {
    runtime_room_password_detail::secureClearBytes(
        value.data(),
        value.size()
    );
    value.clear();
}

inline void runtimeRoomPasswordSecureClear(
    std::vector<std::uint8_t>& value
) noexcept {
    runtime_room_password_detail::secureClearBytes(
        value.data(),
        value.size()
    );
    value.clear();
}

inline void runtimeRoomPasswordSecureClear(
    RuntimeRoomPasswordVerifier& value
) noexcept {
    runtime_room_password_detail::secureClearBytes(
        value.data(),
        value.size()
    );
}

