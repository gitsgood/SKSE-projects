#include "log.h"

#include "settings.h"

namespace
{
    // Skill XP per use = (use multiplier on the skill's ActorValueInfo) * (base amount of the action).
    // Scaling the use multiplier therefore scales all skill-use XP by the same factor.
    // This runs after all plugins are loaded, so it scales whatever value other mods left behind.
    void ApplyMultipliers()
    {
        auto* list = RE::ActorValueList::GetSingleton();
        if (!list) {
            SKSE::log::error("ActorValueList not available, no multipliers applied");
            return;
        }

        for (const auto& entry : Settings::skills) {
            if (entry.multiplier == 1.0f) {
                continue;
            }

            auto* info = list->GetActorValue(entry.av);
            if (!info || !info->skill) {
                SKSE::log::warn("No skill data found for {}", entry.name);
                continue;
            }

            const float before = info->skill->useMult;
            info->skill->useMult = before * entry.multiplier;
            SKSE::log::info("{}: useMult {} -> {} (x{})", entry.name, before, info->skill->useMult, entry.multiplier);
        }
    }

    void OnDataLoaded()
    {
        Settings::Load();
        ApplyMultipliers();
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
    {
        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            OnDataLoaded();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", MessageHandler)) {
        return false;
    }

    return true;
}