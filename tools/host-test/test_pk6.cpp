/*
 * XYORAS Access — host tests for the PK6 structure.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * PK6 decryption is easy to get subtly wrong and impossible to debug on a
 * console: a wrong shuffle order or an off-by-one in the LCRNG yields a
 * structure that looks plausible, passes a glance, and reports a Pokemon that
 * does not exist. These tests pin the algorithm down off-target.
 *
 * No game data is used or needed. The fixtures are synthetic Pokemon built
 * here, encrypted with our own Encrypt, and decrypted again -- which exercises
 * the real code path without shipping anything copyrighted.
 */
#include "test.hpp"
#include "../../plugin/include/xyoras/pk6.hpp"

#include <cstring>
#include <vector>

using namespace xyoras;

namespace {

    /// Builds a decrypted PK6 with recognisable values in every field we read.
    std::vector<u8> MakePokemon(u32 encryptionConstant, u32 size = pk6::kPartySize)
    {
        std::vector<u8> d(size, 0);

        std::memcpy(&d[pk6::kOffEncryptionConstant], &encryptionConstant, 4);

        const u16 species = 25;      // Pikachu
        std::memcpy(&d[pk6::kOffSpecies], &species, 2);
        const u16 item = 1;
        std::memcpy(&d[pk6::kOffHeldItem], &item, 2);
        const u16 tid = 12345, sid = 54321;
        std::memcpy(&d[pk6::kOffTID], &tid, 2);
        std::memcpy(&d[pk6::kOffSID], &sid, 2);
        const u32 exp = 125000;
        std::memcpy(&d[pk6::kOffExp], &exp, 4);
        d[pk6::kOffAbility] = 9;     // Static
        const u32 pid = 0xDEADBEEF;
        std::memcpy(&d[pk6::kOffPID], &pid, 4);
        d[pk6::kOffNature] = 3;

        // EVs in internal order: HP, ATK, DEF, SPE, SPA, SPD
        const u8 evs[6] = {252, 0, 0, 252, 4, 0};
        std::memcpy(&d[pk6::kOffEVs], evs, 6);

        // Gender female (1) in bits 1-2, form 0.
        d[pk6::kOffGenderForm] = static_cast<u8>(1 << 1);

        // Nickname "SPARKY" as UTF-16LE.
        const char *nick = "SPARKY";
        for (u32 i = 0; nick[i] != '\0'; ++i)
        {
            const u16 c = static_cast<u16>(nick[i]);
            std::memcpy(&d[pk6::kOffNickname + i * 2], &c, 2);
        }

        const u16 moves[4] = {84, 98, 86, 87};   // Thunder Shock, Quick Attack, Thunder Wave, Thunderbolt
        for (u32 i = 0; i < 4; ++i)
            std::memcpy(&d[pk6::kOffMoves + i * 2], &moves[i], 2);
        const u8 pp[4] = {30, 30, 20, 15};
        std::memcpy(&d[pk6::kOffMovePP], pp, 4);

        // IVs: HP 31, ATK 30, DEF 29, SPE 28, SPA 27, SPD 26; egg 0, nicknamed 1.
        const u32 iv32 = 31u | (30u << 5) | (29u << 10) | (28u << 15)
                       | (27u << 20) | (26u << 25) | (1u << 31);
        std::memcpy(&d[pk6::kOffIV32], &iv32, 4);

        if (size == pk6::kPartySize)
        {
            const u32 status = 0x10;      // burned
            std::memcpy(&d[pk6::kOffStatusCondition], &status, 4);
            d[pk6::kOffLevel] = 50;
            const u16 hpCur = 118, hpMax = 155;
            std::memcpy(&d[pk6::kOffHPCurrent], &hpCur, 2);
            std::memcpy(&d[pk6::kOffHPMax], &hpMax, 2);
            const u16 stats[5] = {120, 90, 200, 150, 110};   // ATK DEF SPE SPA SPD
            for (u32 i = 0; i < 5; ++i)
                std::memcpy(&d[pk6::kOffStats + i * 2], &stats[i], 2);
        }

        // Stamp a valid checksum last, over the finished payload.
        const u16 chk = pk6::Checksum(d.data());
        std::memcpy(&d[pk6::kOffChecksum], &chk, 2);
        return d;
    }

    void TestSizes(void)
    {
        test::Section("structure sizes");
        test::Equal(pk6::kStoredSize, 232u, "stored is 232 bytes");
        test::Equal(pk6::kPartySize, 260u, "party is 260 bytes");
        test::Equal(pk6::kBlockSize * pk6::kBlockCount,
                    pk6::kStoredSize - pk6::kCryptStart,
                    "four 56-byte blocks exactly fill the payload");
    }

    void TestShuffleIsPermutation(void)
    {
        test::Section("shuffle is a permutation for every value");

        // If any row repeated or dropped a block, decryption would silently
        // duplicate one 56-byte region over another.
        bool allGood = true;
        const u8 *order = pk6::BlockOrder();
        for (u32 sv = 0; sv < 32; ++sv)
        {
            bool seen[4] = {false, false, false, false};
            for (u32 i = 0; i < 4; ++i)
            {
                const u8 b = order[sv * 4 + i];
                if (b > 3 || seen[b])
                    allGood = false;
                else
                    seen[b] = true;
            }
        }
        test::Check(allGood, "all 32 rows are permutations of 0-3");

        // Rows 24-31 duplicate rows 0-7 so no modulus is needed.
        bool dupesMatch = true;
        for (u32 i = 0; i < 32; ++i)
            if (order[24 * 4 + i] != order[i])
                dupesMatch = false;
        test::Check(dupesMatch, "rows 24-31 duplicate rows 0-7");
    }

    void TestInverseTable(void)
    {
        test::Section("inverse table really inverts");

        // Applying sv then its inverse must return every block home. This is
        // the property Encrypt relies on, and getting it wrong corrupts saves
        // rather than merely failing to read them.
        bool allGood = true;
        for (u32 sv = 0; sv < 32; ++sv)
        {
            u8 buf[pk6::kBlockSize * pk6::kBlockCount];
            for (u32 b = 0; b < 4; ++b)
                std::memset(buf + b * pk6::kBlockSize, static_cast<int>(b), pk6::kBlockSize);

            pk6::ShuffleBlocks(buf, sv);
            pk6::ShuffleBlocks(buf, pk6::BlockOrderInverse()[sv]);

            for (u32 b = 0; b < 4; ++b)
                if (buf[b * pk6::kBlockSize] != static_cast<u8>(b))
                    allGood = false;
        }
        test::Check(allGood, "shuffle then inverse-shuffle is identity for all 32 values");
    }

    void TestRoundTrip(void)
    {
        test::Section("encrypt/decrypt round trip");

        // Encryption constants chosen to hit a spread of shuffle values,
        // including 0 (the identity case that skips shuffling entirely).
        const u32 constants[] = {
            0x00000000u, 0x00002000u, 0x0000E000u, 0xDEADBEEFu,
            0x12345678u, 0xFFFFFFFFu, 0x0001A000u, 0xABCD1234u,
        };

        bool allGood = true;
        bool coveredNonZero = false;
        for (u32 i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i)
        {
            const std::vector<u8> original = MakePokemon(constants[i]);
            std::vector<u8> work = original;

            if (((constants[i] >> 13) & 31) != 0)
                coveredNonZero = true;

            pk6::Encrypt(work.data(), static_cast<u32>(work.size()));
            if (std::memcmp(work.data(), original.data(), work.size()) == 0
                && ((constants[i] >> 13) & 31) != 0)
            {
                allGood = false;   // encryption changed nothing: it did not run
            }

            pk6::Decrypt(work.data(), static_cast<u32>(work.size()));
            if (std::memcmp(work.data(), original.data(), work.size()) != 0)
                allGood = false;
        }

        test::Check(allGood, "decrypt(encrypt(x)) == x across 8 encryption constants");
        test::Check(coveredNonZero, "the sample includes non-identity shuffles");
    }

    void TestStoredSizeRoundTrip(void)
    {
        test::Section("stored (box) size round trip");

        const std::vector<u8> original = MakePokemon(0x5A5A5A5Au, pk6::kStoredSize);
        std::vector<u8> work = original;

        test::Check(pk6::Encrypt(work.data(), pk6::kStoredSize), "encrypt accepts 232 bytes");
        test::Check(pk6::Decrypt(work.data(), pk6::kStoredSize), "decrypt accepts 232 bytes");
        test::Check(std::memcmp(work.data(), original.data(), original.size()) == 0,
                    "box-size round trip is exact");
    }

    void TestRejectsBadSize(void)
    {
        test::Section("size validation");

        std::vector<u8> buf(100, 0);
        test::Check(!pk6::Decrypt(buf.data(), 100), "decrypt rejects a wrong size");
        test::Check(!pk6::Encrypt(buf.data(), 100), "encrypt rejects a wrong size");
        test::Check(!pk6::Decrypt(nullptr, pk6::kStoredSize), "decrypt rejects null");
    }

    void TestChecksumAndValidity(void)
    {
        test::Section("checksum and validity");

        std::vector<u8> d = MakePokemon(0x11223344u);
        test::Equal(pk6::Read16(d.data(), pk6::kOffChecksum), pk6::Checksum(d.data()),
                    "stamped checksum matches the computed one");
        test::Check(pk6::IsValid(d.data()), "a well-formed Pokemon validates");

        // A single flipped byte must be caught -- this is the guard that stops
        // us narrating a stale pointer as if it were a Pokemon.
        d[pk6::kOffEVs] ^= 0xFF;
        test::Check(!pk6::IsValid(d.data()), "a corrupted payload fails validation");

        std::vector<u8> empty(pk6::kPartySize, 0);
        const u16 chk = pk6::Checksum(empty.data());
        std::memcpy(&empty[pk6::kOffChecksum], &chk, 2);
        test::Check(!pk6::IsValid(empty.data()), "an empty slot (species 0) is not valid");

        std::vector<u8> beyond = MakePokemon(0x22u);
        const u16 tooBig = 900;
        std::memcpy(&beyond[pk6::kOffSpecies], &tooBig, 2);
        const u16 chk2 = pk6::Checksum(beyond.data());
        std::memcpy(&beyond[pk6::kOffChecksum], &chk2, 2);
        test::Check(!pk6::IsValid(beyond.data()), "a species beyond Gen 6 is not valid");
    }

    void TestAccessors(void)
    {
        test::Section("field accessors");

        const std::vector<u8> d = MakePokemon(0xCAFEBABEu);
        const u8 *p = d.data();

        test::Equal(pk6::Species(p), 25u, "species");
        test::Equal(pk6::HeldItem(p), 1u, "held item");
        test::Equal(pk6::TID(p), 12345u, "trainer ID");
        test::Equal(pk6::SID(p), 54321u, "secret ID");
        test::Equal(pk6::Exp(p), 125000u, "experience");
        test::Equal(pk6::Ability(p), 9u, "ability");
        test::Equal(pk6::PID(p), 0xDEADBEEFu, "PID");
        test::Equal(pk6::Nature(p), 3u, "nature");
        test::Equal(pk6::Gender(p), 1u, "gender (female)");
        test::Equal(pk6::Form(p), 0u, "form");
        test::EqualStr(pk6::Nickname(p), "SPARKY", "nickname");
    }

    void TestEvsIvsMoves(void)
    {
        test::Section("EVs, IVs and moves");

        const std::vector<u8> d = MakePokemon(0x0BADF00Du);
        const u8 *p = d.data();

        test::Equal(pk6::EV(p, pk6::HP), 252u, "EV HP");
        test::Equal(pk6::EV(p, pk6::Spe), 252u, "EV Speed");
        test::Equal(pk6::EV(p, pk6::SpA), 4u, "EV Special Attack");
        test::Equal(pk6::EV(p, pk6::Atk), 0u, "EV Attack");

        test::Equal(pk6::IV(p, pk6::HP), 31u, "IV HP");
        test::Equal(pk6::IV(p, pk6::Atk), 30u, "IV Attack");
        test::Equal(pk6::IV(p, pk6::Def), 29u, "IV Defense");
        test::Equal(pk6::IV(p, pk6::Spe), 28u, "IV Speed");
        test::Equal(pk6::IV(p, pk6::SpA), 27u, "IV Special Attack");
        test::Equal(pk6::IV(p, pk6::SpD), 26u, "IV Special Defense");

        test::Check(!pk6::IsEgg(p), "not an egg");
        test::Check(pk6::IsNicknamed(p), "is nicknamed");

        test::Equal(pk6::Move(p, 0), 84u, "move 1");
        test::Equal(pk6::Move(p, 3), 87u, "move 4");
        test::Equal(pk6::MovePP(p, 0), 30u, "move 1 PP");
        test::Equal(pk6::MovePP(p, 3), 15u, "move 4 PP");
    }

    void TestPartyStats(void)
    {
        test::Section("party stats");

        const std::vector<u8> d = MakePokemon(0x13579BDFu);
        const u8 *p = d.data();

        test::Equal(pk6::Level(p), 50u, "level");
        test::Equal(pk6::HPCurrent(p), 118u, "current HP");
        test::Equal(pk6::HPMax(p), 155u, "max HP");
        test::Equal(pk6::BattleStat(p, pk6::HP), 155u, "BattleStat(HP) is max HP");
        test::Equal(pk6::BattleStat(p, pk6::Atk), 120u, "battle Attack");
        test::Equal(pk6::BattleStat(p, pk6::Spe), 200u, "battle Speed");
        test::Equal(pk6::BattleStat(p, pk6::SpD), 110u, "battle Special Defense");
        test::Equal(pk6::StatusCondition(p), 0x10u, "status condition (burn)");
        test::Check(!pk6::IsAsleep(p), "burn is not sleep");
    }

    void TestSleepCounter(void)
    {
        test::Section("sleep is a counter, not a flag");

        std::vector<u8> d = MakePokemon(0x2468ACE0u);
        const u32 sleep3 = 3;
        std::memcpy(&d[pk6::kOffStatusCondition], &sleep3, 4);
        test::Check(pk6::IsAsleep(d.data()), "a sleep counter of 3 reads as asleep");

        const u32 none = 0;
        std::memcpy(&d[pk6::kOffStatusCondition], &none, 4);
        test::Check(!pk6::IsAsleep(d.data()), "zero is awake");
    }

    void TestCryptIsSelfInverse(void)
    {
        test::Section("LCRNG stream is self-inverse");

        u8 buf[64], original[64];
        for (u32 i = 0; i < 64; ++i)
            buf[i] = original[i] = static_cast<u8>(i * 7);

        pk6::CryptRegion(buf, 64, 0x12345678u);
        test::Check(std::memcmp(buf, original, 64) != 0, "crypt changes the data");

        pk6::CryptRegion(buf, 64, 0x12345678u);
        test::Check(std::memcmp(buf, original, 64) == 0, "crypting twice restores it");
    }
}

int main(void)
{
    std::printf("\nPK6 structure\n=============\n");

    TestSizes();
    TestShuffleIsPermutation();
    TestInverseTable();
    TestCryptIsSelfInverse();
    TestRoundTrip();
    TestStoredSizeRoundTrip();
    TestRejectsBadSize();
    TestChecksumAndValidity();
    TestAccessors();
    TestEvsIvsMoves();
    TestPartyStats();
    TestSleepCounter();

    return test::Report("pk6");
}
