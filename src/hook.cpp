#include "hook.h"

#include "settings.h"

namespace Hooks
{
	namespace
	{
		// REFR record flag set once a flora/tree reference has been harvested.
		// (TESObjectREFR::RecordFlags::kHarvested = 1 << 13)
		constexpr std::uint32_t kHarvestedFlag = 1u << 13;

		// Activate is vfunc 0x37 on TESBoundObject-derived types (TESFlora, TESObjectTREE, ...).
		constexpr std::size_t kActivateIndex = 0x37;

		bool IsHarvested(const RE::TESObjectREFR* a_ref)
		{
			return (a_ref->formFlags & kHarvestedFlag) != 0;
		}

		template <class T>
		struct ActivateHook
		{
			static bool thunk(T* a_this, RE::TESObjectREFR* a_targetRef, RE::TESObjectREFR* a_activatorRef,
				std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_targetCount)
			{
				// Only care about the player harvesting an ingredient-producing plant.
				const bool candidate =
					a_targetRef && a_activatorRef && a_activatorRef->IsPlayerRef() &&
					a_this->produceItem && a_this->produceItem->Is(RE::FormType::Ingredient) &&
					!IsHarvested(a_targetRef);

				// Hold a handle so we can safely re-check the ref after the original runs.
				RE::ObjectRefHandle handle = candidate ? a_targetRef->GetHandle() : RE::ObjectRefHandle{};

				const bool result = func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);

				if (candidate) {
					if (auto ref = handle.get(); ref && IsHarvested(ref.get())) {
						if (auto* player = RE::PlayerCharacter::GetSingleton()) {
							for (const auto skill : Settings::skills) {
								player->AddSkillExperience(skill, Settings::xpPerHarvest);
							}
							SKSE::log::debug("Harvested {} -> +{} XP to {} skill(s)", a_this->produceItem->GetName(),
								Settings::xpPerHarvest, Settings::skills.size());
						}
					}
				}

				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;

			static void Install(const REL::VariantID& a_vtable, std::string_view a_name)
			{
				REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
				func = vtbl.write_vfunc(kActivateIndex, thunk);
				SKSE::log::info("Hooked {}::Activate", a_name);
			}
		};
	}

	void Install()
	{
		ActivateHook<RE::TESFlora>::Install(RE::VTABLE_TESFlora[0], "TESFlora");
		ActivateHook<RE::TESObjectTREE>::Install(RE::VTABLE_TESObjectTREE[0], "TESObjectTREE");
	}
}