#pragma once

#include <Windows.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace Settings
{
    inline constexpr auto kIniPath = ".\\Data\\SKSE\\Plugins\\XPMultipliers.ini";

    struct SkillEntry
    {
        const char* name;  // INI key
        RE::ActorValue av;
        float          multiplier{ 1.0f };
    };

    inline std::array<SkillEntry, 18> skills{ {
        { "OneHanded", RE::ActorValue::kOneHanded },
        { "TwoHanded", RE::ActorValue::kTwoHanded },
        { "Archery", RE::ActorValue::kArchery },
        { "Block", RE::ActorValue::kBlock },
        { "Smithing", RE::ActorValue::kSmithing },
        { "HeavyArmor", RE::ActorValue::kHeavyArmor },
        { "LightArmor", RE::ActorValue::kLightArmor },
        { "Pickpocket", RE::ActorValue::kPickpocket },
        { "Lockpicking", RE::ActorValue::kLockpicking },
        { "Sneak", RE::ActorValue::kSneak },
        { "Alchemy", RE::ActorValue::kAlchemy },
        { "Speech", RE::ActorValue::kSpeech },
        { "Alteration", RE::ActorValue::kAlteration },
        { "Conjuration", RE::ActorValue::kConjuration },
        { "Destruction", RE::ActorValue::kDestruction },
        { "Illusion", RE::ActorValue::kIllusion },
        { "Restoration", RE::ActorValue::kRestoration },
        { "Enchanting", RE::ActorValue::kEnchanting },
    } };

    inline void WriteDefaultIni()
    {
        const std::filesystem::path path{ kIniPath };

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(path);
        if (!out) {
            SKSE::log::warn("Could not create default INI at {}", path.string());
            return;
        }

        out << "[Multipliers]\n"
            "; Skill XP gained is multiplied by these values.\n"
            "; 1.0 = vanilla, 2.0 = double XP, 0.5 = half XP, 0.0 = no XP from skill use.\n";
        for (const auto& skill : skills) {
            out << skill.name << "=1.0\n";
        }

        SKSE::log::info("Created default INI at {}", path.string());
    }

    inline void Load()
    {
        if (!std::filesystem::exists(kIniPath)) {
            WriteDefaultIni();
        }

        for (auto& skill : skills) {
            char buf[64]{};
            GetPrivateProfileStringA("Multipliers", skill.name, "1.0", buf, sizeof(buf), kIniPath);

            float value = 1.0f;
            try {
                value = std::stof(buf);
            }
            catch (...) {
                SKSE::log::warn("Invalid value '{}' for {}, using 1.0", buf, skill.name);
                value = 1.0f;
            }

            if (!std::isfinite(value) || value < 0.0f) {
                SKSE::log::warn("Value {} for {} is not allowed, using 0.0", value, skill.name);
                value = 0.0f;
            }

            skill.multiplier = value;
        }

        SKSE::log::info("Settings loaded");
    }
}