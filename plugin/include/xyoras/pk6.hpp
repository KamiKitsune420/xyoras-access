/*
 * XYORAS Access — the Generation 6 Pokemon structure (PK6).
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * A Gen 6 Pokemon is 232 bytes stored and 260 bytes in party. The payload is
 * encrypted with an LCRNG seeded by the encryption constant at offset 0, and
 * its four 56-byte blocks are shuffled in an order derived from that same
 * constant. Both apply identically whether the Pokemon sits in a box or in RAM.
 *
 * The algorithm and every field offset here follow PKHeX (`PokeCrypto.cs`,
 * `PK6.cs`), which is the reference implementation the community relies on.
 * Nothing is guessed.
 *
 * Header-only and free of 3DS dependencies, so the host tests exercise this
 * exact code rather than a copy of it.
 */
#ifndef XYORAS_PK6_HPP
#define XYORAS_PK6_HPP

#include "xyoras/common.hpp"

#include <cstring>
#include <string>

namespace xyoras { namespace pk6 {

    constexpr u32 kStoredSize = 0xE8;   ///< 232 bytes: a boxed Pokemon
    constexpr u32 kPartySize  = 0x104;  ///< 260 bytes: adds battle stats
    constexpr u32 kBlockSize  = 56;
    constexpr u32 kBlockCount = 4;
    constexpr u32 kCryptStart = 0x08;   ///< header (constant, sanity, checksum) is plaintext

    // ---- Field offsets (PKHeX PK6.cs) ---------------------------------------

    constexpr u32 kOffEncryptionConstant = 0x00;
    constexpr u32 kOffSanity             = 0x04;
    constexpr u32 kOffChecksum           = 0x06;
    constexpr u32 kOffSpecies            = 0x08;
    constexpr u32 kOffHeldItem           = 0x0A;
    constexpr u32 kOffTID                = 0x0C;
    constexpr u32 kOffSID                = 0x0E;
    constexpr u32 kOffExp                = 0x10;
    constexpr u32 kOffAbility            = 0x14;
    constexpr u32 kOffAbilityNumber      = 0x15;
    constexpr u32 kOffPID                = 0x18;
    constexpr u32 kOffNature             = 0x1C;
    constexpr u32 kOffGenderForm         = 0x1D;
    constexpr u32 kOffEVs                = 0x1E;   ///< 6 bytes
    constexpr u32 kOffNickname           = 0x40;   ///< 26 bytes, UTF-16LE
    constexpr u32 kOffMoves              = 0x5A;   ///< 4 x u16
    constexpr u32 kOffMovePP             = 0x62;   ///< 4 x u8
    constexpr u32 kOffMovePPUp           = 0x66;   ///< 4 x u8
    constexpr u32 kOffIV32               = 0x74;

    // Party-only, present when the buffer is kPartySize.
    constexpr u32 kOffStatusCondition    = 0xE8;
    constexpr u32 kOffLevel              = 0xEC;
    constexpr u32 kOffHPCurrent          = 0xF0;
    constexpr u32 kOffHPMax              = 0xF2;
    constexpr u32 kOffStats              = 0xF4;   ///< ATK, DEF, SPE, SPA, SPD

    /// Internal stat order, shared by EVs, IVs and battle stats.
    enum Stat { HP = 0, Atk = 1, Def = 2, Spe = 3, SpA = 4, SpD = 5 };

    // ---- Little-endian access ----------------------------------------------

    inline u16 Read16(const u8 *d, u32 off)
    {
        u16 v;
        std::memcpy(&v, d + off, 2);
        return v;
    }

    inline u32 Read32(const u8 *d, u32 off)
    {
        u32 v;
        std::memcpy(&v, d + off, 4);
        return v;
    }

    inline void Write16(u8 *d, u32 off, u16 v) { std::memcpy(d + off, &v, 2); }

    // ---- Crypto -------------------------------------------------------------

    /// Block orders indexed by the shuffle value. 32 rows rather than 24: the
    /// last 8 duplicate the first 8 so the caller never needs a modulus, which
    /// is how PKHeX stores it too.
    inline const u8 *BlockOrder(void)
    {
        static const u8 kOrder[32 * 4] = {
            0,1,2,3,  0,1,3,2,  0,2,1,3,  0,3,1,2,
            0,2,3,1,  0,3,2,1,  1,0,2,3,  1,0,3,2,
            2,0,1,3,  3,0,1,2,  2,0,3,1,  3,0,2,1,
            1,2,0,3,  1,3,0,2,  2,1,0,3,  3,1,0,2,
            2,3,0,1,  3,2,0,1,  1,2,3,0,  1,3,2,0,
            2,1,3,0,  3,1,2,0,  2,3,1,0,  3,2,1,0,
            // duplicates of rows 0-7
            0,1,2,3,  0,1,3,2,  0,2,1,3,  0,3,1,2,
            0,2,3,1,  0,3,2,1,  1,0,2,3,  1,0,3,2,
        };
        return kOrder;
    }

    /// For each shuffle value, the value whose permutation undoes it.
    inline const u8 *BlockOrderInverse(void)
    {
        static const u8 kInverse[32] = {
             0,  1,  2,  4,
             3,  5,  6,  7,
            12, 18, 13, 19,
             8, 10, 14, 20,
            16, 22,  9, 11,
            15, 21, 17, 23,
            // duplicates of 0-7
             0,  1,  2,  4,
             3,  5,  6,  7,
        };
        return kInverse;
    }

    /// XORs the payload with the LCRNG stream seeded by `seed`, in u16 steps.
    /// Self-inverse, so the same call encrypts and decrypts.
    inline void CryptRegion(u8 *data, u32 length, u32 seed)
    {
        for (u32 i = 0; i + 1 < length; i += 2)
        {
            seed = (0x41C64E6Du * seed) + 0x00006073u;
            const u16 x = static_cast<u16>(seed >> 16);
            u16 v = Read16(data, i);
            v = static_cast<u16>(v ^ x);
            Write16(data, i, v);
        }
    }

    /// Rearranges the four blocks so output block i becomes input block
    /// order[i].
    inline void ShuffleBlocks(u8 *blocks, u32 sv)
    {
        if (sv == 0)
            return;     // identity

        const u8 *order = BlockOrder() + (sv * kBlockCount);

        u8 tmp[kBlockSize * kBlockCount];
        std::memcpy(tmp, blocks, sizeof(tmp));

        for (u32 i = 0; i < kBlockCount; ++i)
            std::memcpy(blocks + i * kBlockSize, tmp + order[i] * kBlockSize, kBlockSize);
    }

    /// Decrypts in place. `size` must be kStoredSize or kPartySize.
    inline bool Decrypt(u8 *data, u32 size)
    {
        if (data == nullptr || (size != kStoredSize && size != kPartySize))
            return false;

        const u32 pv = Read32(data, kOffEncryptionConstant);
        const u32 sv = (pv >> 13) & 31;

        CryptRegion(data + kCryptStart, kStoredSize - kCryptStart, pv);

        // Party stats are a separate region crypted with a FRESH stream from
        // the same seed, not a continuation of the one above.
        if (size > kStoredSize)
            CryptRegion(data + kStoredSize, size - kStoredSize, pv);

        ShuffleBlocks(data + kCryptStart, sv);
        return true;
    }

    /// Encrypts in place, the exact inverse of Decrypt. Needed only by tests
    /// and by any future write path.
    inline bool Encrypt(u8 *data, u32 size)
    {
        if (data == nullptr || (size != kStoredSize && size != kPartySize))
            return false;

        const u32 pv = Read32(data, kOffEncryptionConstant);
        const u32 sv = BlockOrderInverse()[(pv >> 13) & 31];

        ShuffleBlocks(data + kCryptStart, sv);
        CryptRegion(data + kCryptStart, kStoredSize - kCryptStart, pv);

        if (size > kStoredSize)
            CryptRegion(data + kStoredSize, size - kStoredSize, pv);

        return true;
    }

    /// Sum of the payload as u16s. A decrypted structure whose stored checksum
    /// disagrees with this is not a valid Pokemon -- the cheapest guard against
    /// reading a pointer that no longer points at one.
    inline u16 Checksum(const u8 *data)
    {
        u32 sum = 0;
        for (u32 i = kCryptStart; i < kStoredSize; i += 2)
            sum += Read16(data, i);
        return static_cast<u16>(sum & 0xFFFF);
    }

    /// True if a decrypted buffer looks like a real Pokemon.
    inline bool IsValid(const u8 *data)
    {
        if (data == nullptr)
            return false;
        if (Read16(data, kOffChecksum) != Checksum(data))
            return false;

        const u16 species = Read16(data, kOffSpecies);
        return species != 0 && species <= 721;   // Gen 6 national dex ends at Volcanion
    }

    // ---- Accessors (operate on a DECRYPTED buffer) --------------------------

    inline u32 EncryptionConstant(const u8 *d) { return Read32(d, kOffEncryptionConstant); }
    inline u16 Species(const u8 *d)            { return Read16(d, kOffSpecies); }
    inline u16 HeldItem(const u8 *d)           { return Read16(d, kOffHeldItem); }
    inline u16 TID(const u8 *d)                { return Read16(d, kOffTID); }
    inline u16 SID(const u8 *d)                { return Read16(d, kOffSID); }
    inline u32 Exp(const u8 *d)                { return Read32(d, kOffExp); }
    inline u8  Ability(const u8 *d)            { return d[kOffAbility]; }
    inline u32 PID(const u8 *d)                { return Read32(d, kOffPID); }
    inline u8  Nature(const u8 *d)             { return d[kOffNature]; }
    inline u8  EV(const u8 *d, Stat s)         { return d[kOffEVs + static_cast<u32>(s)]; }
    inline u16 Move(const u8 *d, u32 i)        { return Read16(d, kOffMoves + i * 2); }
    inline u8  MovePP(const u8 *d, u32 i)      { return d[kOffMovePP + i]; }

    /// Six 5-bit IVs packed into one word, with the egg and nickname flags in
    /// the top two bits.
    inline u8 IV(const u8 *d, Stat s)
    {
        return static_cast<u8>((Read32(d, kOffIV32) >> (5 * static_cast<u32>(s))) & 0x1F);
    }

    inline bool IsEgg(const u8 *d)       { return ((Read32(d, kOffIV32) >> 30) & 1) != 0; }
    inline bool IsNicknamed(const u8 *d) { return ((Read32(d, kOffIV32) >> 31) & 1) != 0; }

    /// Gender is packed into the same byte as form.
    /// 0 = male, 1 = female, 2 = genderless.
    inline u8 Gender(const u8 *d) { return static_cast<u8>((d[kOffGenderForm] >> 1) & 3); }
    inline u8 Form(const u8 *d)   { return static_cast<u8>(d[kOffGenderForm] >> 3); }

    // Party-only. Meaningless unless the buffer is kPartySize.
    inline u8  Level(const u8 *d)     { return d[kOffLevel]; }
    inline u16 HPCurrent(const u8 *d) { return Read16(d, kOffHPCurrent); }
    inline u16 HPMax(const u8 *d)     { return Read16(d, kOffHPMax); }

    inline u16 BattleStat(const u8 *d, Stat s)
    {
        if (s == HP)
            return Read16(d, kOffHPMax);
        // ATK, DEF, SPE, SPA, SPD follow HP max in the same internal order.
        return Read16(d, kOffStats + (static_cast<u32>(s) - 1) * 2);
    }

    /// Status condition is a bitfield; these are the values the games use.
    enum Status { None = 0, Poison = 0x08, Burn = 0x10, Freeze = 0x20,
                  Paralysis = 0x40, Toxic = 0x80 };

    inline u32 StatusCondition(const u8 *d) { return Read32(d, kOffStatusCondition); }

    /// Sleep is a counter in the low three bits rather than a flag.
    inline bool IsAsleep(const u8 *d) { return (StatusCondition(d) & 0x07) != 0; }

    /// Nickname, UTF-16LE, terminated by 0x0000 or 0xFFFF.
    ///
    /// Non-ASCII becomes '?' rather than corrupting the utterance; proper
    /// transcoding arrives with multi-language support.
    inline std::string Nickname(const u8 *d)
    {
        std::string out;
        out.reserve(12);
        for (u32 i = 0; i < 12; ++i)
        {
            const u16 c = Read16(d, kOffNickname + i * 2);
            if (c == 0x0000 || c == 0xFFFF)
                break;
            out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        return out;
    }

}} // namespace xyoras::pk6

#endif
