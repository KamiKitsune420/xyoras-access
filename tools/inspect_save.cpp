/*
 * XYORAS Access — read a party out of a Generation 6 save file.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Validation, not a feature. The host tests prove our PK6 code is
 * self-consistent -- encrypt, decrypt, get the same bytes back -- but
 * self-consistency is exactly what a subtly wrong algorithm also has. This
 * runs the same shipped code against a real save and prints what it finds. If
 * the Pokemon are real, the algorithm is right.
 *
 *     inspect_save <path-to-save> [--box N]
 *
 * Reads only. Never writes, and prints nothing that is not already the
 * player's own data.
 */
#include "xyoras/pk6.hpp"
#include "xyoras/names.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace xyoras;

namespace {

    // Offsets within an unpacked XY save (PKHeX SAV6XY).
    constexpr u32 kSaveSizeXY = 0x65600;
    constexpr u32 kPartyOffset = 0x14200;
    constexpr u32 kBoxOffset   = 0x22600;
    constexpr u32 kPartySlots  = 6;
    constexpr u32 kBoxSlots    = 30;

    const char *kNatures[] = {
        "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
        "Bold", "Docile", "Relaxed", "Impish", "Lax",
        "Timid", "Hasty", "Serious", "Jolly", "Naive",
        "Modest", "Mild", "Quiet", "Bashful", "Rash",
        "Calm", "Gentle", "Sassy", "Careful", "Quirky",
    };

    const char *NatureName(u8 n)
    {
        return n < 25 ? kNatures[n] : "unknown";
    }

    const char *GenderName(u8 g)
    {
        return g == 0 ? "male" : (g == 1 ? "female" : "genderless");
    }

    std::vector<u8> ReadFile(const char *path)
    {
        std::vector<u8> data;
        FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
            return data;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size > 0)
        {
            data.resize(static_cast<size_t>(size));
            if (std::fread(data.data(), 1, data.size(), f) != data.size())
                data.clear();
        }
        std::fclose(f);
        return data;
    }

    /// Prints one slot. Returns true if it held a real Pokemon.
    bool Show(const u8 *encrypted, u32 size, u32 index)
    {
        std::vector<u8> slot(encrypted, encrypted + size);

        if (!pk6::Decrypt(slot.data(), size))
        {
            std::printf("  %u: decrypt refused\n", index);
            return false;
        }

        const u8 *p = slot.data();

        if (!pk6::IsValid(p))
        {
            // Empty slots are normal; only say so quietly.
            if (pk6::Species(p) == 0)
                std::printf("  %u: (empty)\n", index);
            else
                std::printf("  %u: INVALID (species %u, checksum %04X vs computed %04X)\n",
                            index, pk6::Species(p),
                            pk6::Read16(p, pk6::kOffChecksum), pk6::Checksum(p));
            return false;
        }

        const std::string nick = pk6::Nickname(p);
        std::printf("  %u: %-12s", index, names::Species(pk6::Species(p)));

        if (size == pk6::kPartySize)
            std::printf(" Lv%-3u %3u/%-3u HP", pk6::Level(p), pk6::HPCurrent(p), pk6::HPMax(p));

        std::printf("  %s, %s", NatureName(pk6::Nature(p)), GenderName(pk6::Gender(p)));

        if (pk6::IsNicknamed(p) && !nick.empty())
            std::printf("  \"%s\"", nick.c_str());

        std::printf("\n      ability %s", names::Ability(pk6::Ability(p)));
        if (pk6::HeldItem(p) != 0)
            std::printf(", holding %s", names::Item(pk6::HeldItem(p)));

        std::printf("\n      IVs %u/%u/%u/%u/%u/%u",
                    pk6::IV(p, pk6::HP), pk6::IV(p, pk6::Atk), pk6::IV(p, pk6::Def),
                    pk6::IV(p, pk6::Spe), pk6::IV(p, pk6::SpA), pk6::IV(p, pk6::SpD));
        std::printf("   EVs %u/%u/%u/%u/%u/%u\n",
                    pk6::EV(p, pk6::HP), pk6::EV(p, pk6::Atk), pk6::EV(p, pk6::Def),
                    pk6::EV(p, pk6::Spe), pk6::EV(p, pk6::SpA), pk6::EV(p, pk6::SpD));

        std::printf("      moves");
        for (u32 m = 0; m < 4; ++m)
        {
            const u16 move = pk6::Move(p, m);
            if (move != 0)
                std::printf(" %s(%u)", names::Move(move), pk6::MovePP(p, m));
        }
        std::printf("\n");
        return true;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: inspect_save <save file> [--box N]\n");
        return 2;
    }

    const std::vector<u8> save = ReadFile(argv[1]);
    if (save.empty())
    {
        std::printf("could not read %s\n", argv[1]);
        return 1;
    }

    std::printf("save: %zu bytes", save.size());
    if (save.size() == kSaveSizeXY)
        std::printf("  (matches XY exactly)");
    std::printf("\n\n");

    if (save.size() < kPartyOffset + kPartySlots * pk6::kPartySize)
    {
        std::printf("too small to hold a party at 0x%X\n", kPartyOffset);
        return 1;
    }

    std::printf("party @ 0x%X\n", kPartyOffset);
    u32 found = 0;
    for (u32 i = 0; i < kPartySlots; ++i)
    {
        const u8 *slot = save.data() + kPartyOffset + i * pk6::kPartySize;
        if (Show(slot, pk6::kPartySize, i + 1))
            ++found;
    }
    std::printf("\n%u of %u party slots decrypted to valid Pokemon\n", found, kPartySlots);

    // Scan mode. A decrypted PK6 authenticates itself: the stored checksum has
    // to match the payload. That makes it possible to find Pokemon without
    // knowing the save layout at all -- try every aligned offset and keep the
    // ones that validate. Useful when an assumed offset turns up empty, and a
    // second, independent check on the crypto: false positives are vanishingly
    // unlikely at 1-in-65536 per offset combined with a sane species.
    bool scan = false;
    for (int a = 2; a < argc; ++a)
        if (std::strcmp(argv[a], "--scan") == 0)
            scan = true;

    if (scan)
    {
        std::printf("\nscanning every 4-byte-aligned offset for valid PK6...\n");
        u32 hits = 0;
        std::vector<u8> slot(pk6::kStoredSize);

        for (u32 off = 0; off + pk6::kStoredSize <= save.size(); off += 4)
        {
            std::memcpy(slot.data(), save.data() + off, pk6::kStoredSize);
            if (!pk6::Decrypt(slot.data(), pk6::kStoredSize))
                continue;
            if (!pk6::IsValid(slot.data()))
                continue;

            ++hits;
            if (hits <= 40)
            {
                std::printf("  0x%05X  %-12s  nature %s\n", off,
                            names::Species(pk6::Species(slot.data())),
                            NatureName(pk6::Nature(slot.data())));
            }
        }
        std::printf("\n%u valid PK6 structures found by scanning\n", hits);
        return hits > 0 ? 0 : 1;
    }

    int box = -1;
    for (int a = 2; a + 1 < argc; ++a)
        if (std::strcmp(argv[a], "--box") == 0)
            box = std::atoi(argv[a + 1]);

    if (box >= 0)
    {
        const u32 base = kBoxOffset + static_cast<u32>(box) * kBoxSlots * pk6::kStoredSize;
        if (base + kBoxSlots * pk6::kStoredSize <= save.size())
        {
            std::printf("\nbox %d @ 0x%X\n", box, base);
            u32 boxFound = 0;
            for (u32 i = 0; i < kBoxSlots; ++i)
            {
                const u8 *slot = save.data() + base + i * pk6::kStoredSize;
                if (Show(slot, pk6::kStoredSize, i + 1))
                    ++boxFound;
            }
            std::printf("\n%u of %u box slots decrypted to valid Pokemon\n", boxFound, kBoxSlots);
        }
    }

    return found > 0 ? 0 : 1;
}
