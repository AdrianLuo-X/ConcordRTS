#include "assets/archive/WestwoodCrypto.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace {

#include "assets/archive/WestwoodBlowfishConstants.inc"

constexpr std::array<std::uint8_t, 40> kWestwoodPublicModulusBe = {
    0x51, 0xbc, 0xda, 0x08, 0x6d, 0x39, 0xfc, 0xe4,
    0x56, 0x51, 0x60, 0xd6, 0x51, 0x71, 0x3f, 0xa2,
    0xe8, 0xaa, 0x54, 0xfa, 0x66, 0x82, 0xb0, 0x4a,
    0xab, 0xdd, 0x0e, 0x6a, 0xf8, 0xb0, 0xc1, 0xe6,
    0xd1, 0xfb, 0x4f, 0x3d, 0xaa, 0x43, 0x7f, 0x15
};

constexpr std::size_t kBigIntLimbs = 16;

struct BigInt {
    std::array<std::uint32_t, kBigIntLimbs> limbs = {};
};

BigInt bigIntFromLittleEndianBytes(
    const std::uint8_t* bytes,
    std::size_t byteCount
) {
    BigInt result;
    const std::size_t clampedBytes =
        std::min(byteCount, kBigIntLimbs * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < clampedBytes; ++i) {
        result.limbs[i / 4] |=
            static_cast<std::uint32_t>(bytes[i]) << ((i % 4) * 8);
    }
    return result;
}

BigInt bigIntFromBigEndianBytes(
    const std::uint8_t* bytes,
    std::size_t byteCount
) {
    std::array<std::uint8_t, kBigIntLimbs * sizeof(std::uint32_t)> little = {};
    const std::size_t clampedBytes =
        std::min(byteCount, little.size());
    for (std::size_t i = 0; i < clampedBytes; ++i) {
        little[i] = bytes[clampedBytes - 1U - i];
    }
    return bigIntFromLittleEndianBytes(little.data(), clampedBytes);
}

int compareBigInt(const BigInt& left, const BigInt& right) {
    for (std::size_t i = kBigIntLimbs; i > 0; --i) {
        const std::size_t index = i - 1U;
        if (left.limbs[index] < right.limbs[index]) {
            return -1;
        }
        if (left.limbs[index] > right.limbs[index]) {
            return 1;
        }
    }
    return 0;
}

void subtractInPlace(BigInt& target, const BigInt& value) {
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < kBigIntLimbs; ++i) {
        const std::uint64_t lhs = target.limbs[i];
        const std::uint64_t rhs = static_cast<std::uint64_t>(value.limbs[i]) + borrow;
        if (lhs >= rhs) {
            target.limbs[i] = static_cast<std::uint32_t>(lhs - rhs);
            borrow = 0;
        } else {
            target.limbs[i] = static_cast<std::uint32_t>(
                (std::numeric_limits<std::uint64_t>::max() - rhs + 1U) + lhs
            );
            borrow = 1;
        }
    }
}

BigInt addMod(const BigInt& left, const BigInt& right, const BigInt& modulus) {
    BigInt result;
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < kBigIntLimbs; ++i) {
        const std::uint64_t sum =
            static_cast<std::uint64_t>(left.limbs[i])
            + right.limbs[i]
            + carry;
        result.limbs[i] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32U;
    }

    if (carry != 0 || compareBigInt(result, modulus) >= 0) {
        subtractInPlace(result, modulus);
    }
    return result;
}

BigInt doubleMod(const BigInt& value, const BigInt& modulus) {
    return addMod(value, value, modulus);
}

bool testBit(const BigInt& value, std::size_t bitIndex) {
    const std::size_t limb = bitIndex / 32U;
    if (limb >= kBigIntLimbs) {
        return false;
    }
    return (value.limbs[limb] & (1U << (bitIndex % 32U))) != 0;
}

std::size_t highestBitIndex(const BigInt& value) {
    for (std::size_t i = kBigIntLimbs; i > 0; --i) {
        const std::size_t limbIndex = i - 1U;
        const std::uint32_t limb = value.limbs[limbIndex];
        if (limb == 0) {
            continue;
        }

        for (int bit = 31; bit >= 0; --bit) {
            if ((limb & (1U << bit)) != 0) {
                return limbIndex * 32U + static_cast<std::size_t>(bit);
            }
        }
    }
    return 0;
}

BigInt modBigInt(const BigInt& value, const BigInt& modulus) {
    BigInt result;
    const std::size_t topBit = highestBitIndex(value);
    for (std::size_t step = 0; step <= topBit; ++step) {
        const std::size_t bit = topBit - step;
        result = doubleMod(result, modulus);
        if (testBit(value, bit)) {
            BigInt one;
            one.limbs[0] = 1;
            result = addMod(result, one, modulus);
        }
    }
    return result;
}

BigInt multiplyMod(const BigInt& left, const BigInt& right, const BigInt& modulus) {
    BigInt result;
    BigInt addend = left;
    const std::size_t topBit = highestBitIndex(right);

    for (std::size_t bit = 0; bit <= topBit; ++bit) {
        if (testBit(right, bit)) {
            result = addMod(result, addend, modulus);
        }
        addend = doubleMod(addend, modulus);
    }

    return result;
}

BigInt powMod65537(BigInt base, const BigInt& modulus) {
    BigInt result;
    result.limbs[0] = 1;
    base = modBigInt(base, modulus);

    std::uint32_t exponent = 65537U;
    while (exponent != 0) {
        if ((exponent & 1U) != 0) {
            result = multiplyMod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0) {
            base = multiplyMod(base, base, modulus);
        }
    }

    return result;
}

void appendLittleEndianBytes(
    const BigInt& value,
    std::size_t byteCount,
    std::vector<std::uint8_t>& output
) {
    for (std::size_t i = 0; i < byteCount; ++i) {
        const std::size_t limb = i / 4U;
        const std::size_t shift = (i % 4U) * 8U;
        output.push_back(static_cast<std::uint8_t>(value.limbs[limb] >> shift));
    }
}

bool decryptMixBlowfishKey(
    const std::vector<std::uint8_t>& keyBlock,
    std::vector<std::uint8_t>& key,
    std::string& errorMessage
) {
    constexpr std::size_t encryptedChunkBytes = 40;
    constexpr std::size_t decryptedChunkBytes = 39;
    constexpr std::size_t outputKeyBytes = 56;

    key.clear();
    if (keyBlock.size() != encryptedChunkBytes * 2U) {
        errorMessage = "MIX encrypted key block must be exactly 80 bytes.";
        return false;
    }

    const BigInt modulus = bigIntFromBigEndianBytes(
        kWestwoodPublicModulusBe.data(),
        kWestwoodPublicModulusBe.size()
    );

    std::vector<std::uint8_t> decrypted;
    decrypted.reserve(decryptedChunkBytes * 2U);
    for (std::size_t chunk = 0; chunk < 2U; ++chunk) {
        const std::uint8_t* chunkData =
            keyBlock.data() + chunk * encryptedChunkBytes;
        const BigInt encrypted =
            bigIntFromLittleEndianBytes(chunkData, encryptedChunkBytes);
        const BigInt plain = powMod65537(encrypted, modulus);
        appendLittleEndianBytes(plain, decryptedChunkBytes, decrypted);
    }

    key.assign(decrypted.begin(), decrypted.begin() + outputKeyBytes);
    return true;
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void writeLe32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::uint32_t byteSwap(std::uint32_t value) {
    value = (value << 16U) | (value >> 16U);
    return ((value << 8U) & 0xFF00FF00U)
        | ((value >> 8U) & 0x00FF00FFU);
}

class BlowfishCipher {
public:
    explicit BlowfishCipher(const std::vector<std::uint8_t>& key)
        : p_(kBlowfishPInitial),
          s_(kBlowfishSInitial) {
        for (std::size_t i = 0, j = 0; i < p_.size(); ++i) {
            std::uint32_t word = 0;
            for (int byte = 0; byte < 4; ++byte) {
                word = (word << 8U) | key[j % key.size()];
                ++j;
            }
            p_[i] ^= word;
        }

        std::uint32_t left = 0;
        std::uint32_t right = 0;
        for (std::size_t i = 0; i < p_.size(); i += 2U) {
            encryptPair(left, right);
            p_[i] = left;
            p_[i + 1U] = right;
        }

        for (std::size_t i = 0; i < s_.size(); i += 2U) {
            encryptPair(left, right);
            s_[i] = left;
            s_[i + 1U] = right;
        }
    }

    void decryptBlocks(
        const std::vector<std::uint8_t>& encrypted,
        std::vector<std::uint8_t>& decrypted
    ) const {
        decrypted.assign(encrypted.size(), 0);
        for (std::size_t offset = 0; offset < encrypted.size(); offset += 8U) {
            std::uint32_t left = byteSwap(readLe32(encrypted, offset));
            std::uint32_t right = byteSwap(readLe32(encrypted, offset + 4U));
            decryptPair(left, right);
            writeLe32(decrypted, offset, byteSwap(left));
            writeLe32(decrypted, offset + 4U, byteSwap(right));
        }
    }

private:
    std::array<std::uint32_t, 18> p_;
    std::array<std::uint32_t, 1024> s_;

    std::uint32_t sBox(std::uint32_t value, int index) const {
        const std::size_t byte =
            (value >> static_cast<unsigned int>((3 - index) * 8)) & 0xFFU;
        return s_[static_cast<std::size_t>(index) * 256U + byte];
    }

    std::uint32_t function(std::uint32_t value) const {
        return ((sBox(value, 0) + sBox(value, 1)) ^ sBox(value, 2))
            + sBox(value, 3);
    }

    void round(std::uint32_t& left, std::uint32_t right, std::size_t index) const {
        left ^= function(right) ^ p_[index];
    }

    void encryptPair(std::uint32_t& left, std::uint32_t& right) const {
        std::uint32_t workLeft = left;
        std::uint32_t workRight = right;
        workLeft ^= p_[0];

        bool alternate = false;
        for (std::size_t i = 1; i <= 16U; ++i, alternate = !alternate) {
            if (alternate) {
                round(workLeft, workRight, i);
            } else {
                round(workRight, workLeft, i);
            }
        }

        workRight ^= p_[17];
        left = workRight;
        right = workLeft;
    }

    void decryptPair(std::uint32_t& left, std::uint32_t& right) const {
        std::uint32_t workLeft = left;
        std::uint32_t workRight = right;
        workLeft ^= p_[17];

        bool alternate = false;
        for (std::size_t i = 16; i >= 1U; --i, alternate = !alternate) {
            if (alternate) {
                round(workLeft, workRight, i);
            } else {
                round(workRight, workLeft, i);
            }
            if (i == 1U) {
                break;
            }
        }

        workRight ^= p_[0];
        left = workRight;
        right = workLeft;
    }
};

} // namespace

namespace westwood {

bool decryptMixIndexBlocks(
    const std::vector<std::uint8_t>& keyBlock,
    const std::vector<std::uint8_t>& encryptedBlocks,
    std::vector<std::uint8_t>& decryptedBlocks,
    std::string& errorMessage
) {
    decryptedBlocks.clear();
    errorMessage.clear();

    if ((encryptedBlocks.size() % 8U) != 0U) {
        errorMessage = "MIX encrypted index length is not a multiple of 8 bytes.";
        return false;
    }

    std::vector<std::uint8_t> blowfishKey;
    if (!decryptMixBlowfishKey(keyBlock, blowfishKey, errorMessage)) {
        return false;
    }

    BlowfishCipher cipher(blowfishKey);
    cipher.decryptBlocks(encryptedBlocks, decryptedBlocks);
    return true;
}

} // namespace westwood
