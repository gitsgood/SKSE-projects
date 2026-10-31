#include "hook.h"

#include "settings.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace Hooks
{
	namespace
	{
		// REFR record flag set once a flora/tree reference has been harvested.
		// (TESObjectREFR::RecordFlags::kHarvested = 1 << 13)
		constexpr std::uint32_t kHarvestedFlag = 1u << 13;

		// Activate is vfunc 0x37 on TESBoundObject-derived types (TESFlora, TESObjectTREE, ...).
		constexpr std::size_t kActivateIndex = 0x37;

		constexpr RE::FormID kPlayerRefID = 0x14;

		// References created at runtime (e.g. spawned critters) have form IDs starting with 0xFF.
		constexpr RE::FormID kDynamicRefStart = 0xFF000000;

		void AwardHarvestXP(std::string_view a_what)
		{
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				for (const auto skill : Settings::skills) {
					player->AddSkillExperience(skill, Settings::xpPerHarvest);
				}
				SKSE::log::debug("Harvested {} -> +{} XP to {} skill(s)", a_what, Settings::xpPerHarvest,
					Settings::skills.size());
			}
		}

		// ---------------------------------------------------------------------------------------
		// Flora / trees: hook Activate and award XP if the reference was consumed by the call.
		// ---------------------------------------------------------------------------------------

		// True if the reference can still be harvested/caught.
		bool IsAvailable(const RE::TESObjectREFR* a_ref)
		{
			return (a_ref->formFlags & kHarvestedFlag) == 0 && !a_ref->IsDisabled() && !a_ref->IsDeleted();
		}

		bool WasConsumed(const RE::ObjectRefHandle& a_handle)
		{
			auto ref = a_handle.get();
			if (!ref) {
				return true;  // reference no longer exists
			}
			return !IsAvailable(ref.get());
		}

		template <class T>
		struct ActivateHook
		{
			static bool thunk(T* a_this, RE::TESObjectREFR* a_targetRef, RE::TESObjectREFR* a_activatorRef,
				std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_targetCount)
			{
				const bool candidate =
					a_targetRef && a_activatorRef && a_activatorRef->IsPlayerRef() &&
					a_this->produceItem && a_this->produceItem->Is(RE::FormType::Ingredient) &&
					IsAvailable(a_targetRef);

				// Hold a handle so we can safely re-check the ref after the original runs.
				RE::ObjectRefHandle handle = candidate ? a_targetRef->GetHandle() : RE::ObjectRefHandle{};

				const bool result = func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);

				if (candidate) {
					const bool consumed = WasConsumed(handle);
					SKSE::log::trace("{}::Activate on {} (result={}, consumed={})", typeid(T).name(),
						a_this->produceItem->GetName(), result, consumed);

					if (consumed) {
						AwardHarvestXP(a_this->produceItem->GetName());
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

		// ---------------------------------------------------------------------------------------
		// Critters (butterflies, torchbugs, ...): plain activators with no produce data on the base
		// form. At data load we link each critter activator to an ingredient by name
		// ("Blue Butterfly" -> "Blue Butterfly Wing"), then simply look the activated base up.
		// ---------------------------------------------------------------------------------------
		std::string ToLower(std::string_view a_str)
		{
			std::string out(a_str);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// "torchbug" matches "torchbug thorax" (whole leading words only), and "bee" matches "bee".
		bool NameMatches(const std::string& a_critter, const std::string& a_ingredient)
		{
			if (a_critter.size() < 3 || a_ingredient.size() < a_critter.size()) {
				return false;
			}
			if (a_ingredient.compare(0, a_critter.size(), a_critter) != 0) {
				return false;
			}
			return a_ingredient.size() == a_critter.size() || a_ingredient[a_critter.size()] == ' ';
		}

		// Base form IDs of activators that are harvestable critters.
		std::unordered_set<RE::FormID> g_critterBases;

		void BuildCritterMap()
		{
			g_critterBases.clear();

			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return;
			}

			std::vector<std::string> ingredientNames;
			for (auto* ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
				if (ingredient && ingredient->GetName() && *ingredient->GetName()) {
					ingredientNames.push_back(ToLower(ingredient->GetName()));
				}
			}

			for (auto* activator : dataHandler->GetFormArray<RE::TESObjectACTI>()) {
				if (!activator || !activator->GetName() || !*activator->GetName()) {
					continue;
				}

				const auto name = ToLower(activator->GetName());
				for (const auto& ingredientName : ingredientNames) {
					if (NameMatches(name, ingredientName)) {
						g_critterBases.insert(activator->GetFormID());
						SKSE::log::info("Critter map: {:08X} '{}' -> ingredient '{}'", activator->GetFormID(),
							activator->GetName(), ingredientName);
						break;
					}
				}
			}

			SKSE::log::info("Critter map built: {} activator(s)", g_critterBases.size());
		}

		class CritterTracker : public RE::BSTEventSink<RE::TESActivateEvent>
		{
			// Safety valve so the set can't grow without bound in a very long session.
			static constexpr std::size_t kMaxTracked = 256;

		public:
			static CritterTracker* GetSingleton()
			{
				static CritterTracker singleton;
				return std::addressof(singleton);
			}

			void Reset()
			{
				std::scoped_lock lock(_lock);
				_awardedRefs.clear();
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event,
				RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				using Ctrl = RE::BSEventNotifyControl;

				if (!a_event || !a_event->objectActivated || !a_event->actionRef ||
					!a_event->actionRef->IsPlayerRef()) {
					return Ctrl::kContinue;
				}

				auto* ref = a_event->objectActivated.get();
				auto* base = ref->GetBaseObject();

				// Must be a spawned critter reference whose base we linked to an ingredient.
				if (!base || ref->GetFormID() < kDynamicRefStart || !g_critterBases.contains(base->GetFormID())) {
					return Ctrl::kContinue;
				}

				{
					// The same catch can raise more than one activation event; pay out once per reference.
					std::scoped_lock lock(_lock);
					if (_awardedRefs.size() >= kMaxTracked) {
						_awardedRefs.clear();
					}
					if (!_awardedRefs.insert(ref->GetFormID()).second) {
						return Ctrl::kContinue;
					}
				}

				AwardHarvestXP(base->GetName());
				return Ctrl::kContinue;
			}

		private:
			CritterTracker() = default;

			std::mutex                 _lock;
			std::unordered_set<RE::FormID> _awardedRefs;
		};

		// ---------------------------------------------------------------------------------------
		// Debug aid: logs the record type of everything the player activates.
		// ---------------------------------------------------------------------------------------
		class ActivateLogger : public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			static ActivateLogger* GetSingleton()
			{
				static ActivateLogger singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event,
				RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (a_event && a_event->objectActivated && a_event->actionRef && a_event->actionRef->IsPlayerRef()) {
					auto* ref = a_event->objectActivated.get();
					auto* base = ref->GetBaseObject();
					SKSE::log::info("[ActivateLog] ref {:08X} base {:08X} '{}' type={}", ref->GetFormID(),
						base ? base->GetFormID() : 0, base ? base->GetName() : "<none>",
						base ? RE::FormTypeToString(base->GetFormType()) : "<none>");
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			ActivateLogger() = default;
		};
	}

	void Install()
	{
		ActivateHook<RE::TESFlora>::Install(RE::VTABLE_TESFlora[0], "TESFlora");
		ActivateHook<RE::TESObjectTREE>::Install(RE::VTABLE_TESObjectTREE[0], "TESObjectTREE");

		BuildCritterMap();

		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESActivateEvent>(CritterTracker::GetSingleton());
			SKSE::log::info("Critter harvest tracking enabled");

			if (Settings::debugLogActivations) {
				holder->AddEventSink(ActivateLogger::GetSingleton());
				SKSE::log::info("Activation debug logging enabled");
			}
		}
	}

	void ResetState()
	{
		CritterTracker::GetSingleton()->Reset();
	}
}