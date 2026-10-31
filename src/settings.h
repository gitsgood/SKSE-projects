#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Settings
{
	inline constexpr auto kIniPath = ".\\Data\\SKSE\\Plugins\\XPOnIngredientHarvest.ini";

	// Every skill in this list receives xpPerHarvest on each harvest.
	inline std::vector<RE::ActorValue> skills{ RE::ActorValue::kAlchemy };
	inline float                       xpPerHarvest{ 50.0f };
	inline bool                        debugLogActivations{ false };

	// Critter overrides from the INI, all lowercase.
	// Include: { activator name, ingredient name } pairs that automatic name matching misses.
	// Exclude: activator names to ignore even when name matching picks them up.
	inline std::vector<std::pair<std::string, std::string>> critterIncludes;
	inline std::unordered_set<std::string>                  critterExcludes;


	inline std::string Trim(std::string a_str)
	{
		const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		a_str.erase(a_str.begin(), std::find_if(a_str.begin(), a_str.end(), notSpace));
		a_str.erase(std::find_if(a_str.rbegin(), a_str.rend(), notSpace).base(), a_str.end());
		return a_str;
	}

	inline std::optional<RE::ActorValue> ParseSkill(std::string a_name)
	{
		a_name = Trim(a_name);
		std::transform(a_name.begin(), a_name.end(), a_name.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		struct Entry
		{
			std::string_view name;
			RE::ActorValue   av;
		};
		static constexpr std::array<Entry, 18> table{ {
			{ "onehanded", RE::ActorValue::kOneHanded },
			{ "twohanded", RE::ActorValue::kTwoHanded },
			{ "archery", RE::ActorValue::kArchery },
			{ "block", RE::ActorValue::kBlock },
			{ "smithing", RE::ActorValue::kSmithing },
			{ "heavyarmor", RE::ActorValue::kHeavyArmor },
			{ "lightarmor", RE::ActorValue::kLightArmor },
			{ "pickpocket", RE::ActorValue::kPickpocket },
			{ "lockpicking", RE::ActorValue::kLockpicking },
			{ "sneak", RE::ActorValue::kSneak },
			{ "alchemy", RE::ActorValue::kAlchemy },
			{ "speech", RE::ActorValue::kSpeech },
			{ "alteration", RE::ActorValue::kAlteration },
			{ "conjuration", RE::ActorValue::kConjuration },
			{ "destruction", RE::ActorValue::kDestruction },
			{ "illusion", RE::ActorValue::kIllusion },
			{ "restoration", RE::ActorValue::kRestoration },
			{ "enchanting", RE::ActorValue::kEnchanting },
		} };

		for (auto& e : table) {
			if (e.name == a_name) {
				return e.av;
			}
		}
		return std::nullopt;
	}

	// Parses "Alchemy, Speech, Sneak" into a de-duplicated list of skills.
	inline std::vector<RE::ActorValue> ParseSkillList(const std::string& a_list)
	{
		std::vector<RE::ActorValue> result;
		std::size_t                 start = 0;

		while (start <= a_list.size()) {
			auto end = a_list.find(',', start);
			if (end == std::string::npos) {
				end = a_list.size();
			}

			const auto token = Trim(a_list.substr(start, end - start));
			if (!token.empty()) {
				if (auto av = ParseSkill(token)) {
					if (std::find(result.begin(), result.end(), *av) == result.end()) {
						result.push_back(*av);
					}
				}
				else {
					SKSE::log::warn("Unknown skill '{}' in INI, ignoring", token);
				}
			}

			start = end + 1;
		}

		return result;
	}

	inline std::string Lower(std::string a_str)
	{
		std::transform(a_str.begin(), a_str.end(), a_str.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_str;
	}

	// Splits "A, B, C" into trimmed, lowercased, non-empty tokens.
	inline std::vector<std::string> SplitList(const std::string& a_list)
	{
		std::vector<std::string> result;
		std::size_t			  start = 0;

		while (start <= a_list.size()) {
			auto end = a_list.find(',', start);
			if (end == std::string::npos) {
				end = a_list.size();
			}

			auto token = Lower(Trim(a_list.substr(start, end - start)));
			if (!token.empty()) {
				result.push_back(std::move(token));
			}

			start = end + 1;
		}

		return result;
	}

	inline constexpr std::string_view kIncludeBlock =
		"[CritterInclude]\n"
		"; Critters that automatic name matching misses, one per line:  Activator name=Ingredient name\n"
		"; Use the in-game names (case-insensitive). Add a line per critter, e.g. a modded one.\n"
		"Monarch Butterfly=Butterfly Wing\n";

	inline constexpr std::string_view kExcludeBlock =
		"[CritterExclude]\n"
		"; Activator names to ignore even if automatic name matching picks them up (false positives).\n"
		"; Comma-separated, case-insensitive.\n"
		"Names=Fire, Vampire, Spawn, Shade, Root\n";

	// INIs created by older versions lack the critter sections; add whichever are missing
	// without touching anything the user has already edited.
	inline void AppendMissingCritterSections()
	{
		std::string content;
		{
			std::ifstream in(kIniPath);
			if (!in) {
				return;
			}
			content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		std::string toAppend;
		if (content.find("[CritterInclude]") == std::string::npos) {
			toAppend += "\n";
			toAppend += kIncludeBlock;
		}
		if (content.find("[CritterExclude]") == std::string::npos) {
			toAppend += "\n";
			toAppend += kExcludeBlock;
		}

		if (!toAppend.empty()) {
			std::ofstream out(kIniPath, std::ios::app);
			if (out) {
				out << toAppend;
				SKSE::log::info("Added missing critter sections to INI");
			}
		}
	}

	inline void LoadCritterOverrides()
	{
		critterIncludes.clear();
		critterExcludes.clear();

		// [CritterInclude]: every "Activator=Ingredient" line.
		std::vector<char> section(8192, '\0');
		GetPrivateProfileSectionA("CritterInclude", section.data(), static_cast<DWORD>(section.size()), kIniPath);
		for (const char* p = section.data(); *p;) {
			const std::string line(p);
			p += line.size() + 1;

			if (line.front() == ';' || line.front() == '#') {
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			auto activator = Lower(Trim(line.substr(0, eq)));
			auto ingredient = Lower(Trim(line.substr(eq + 1)));
			if (!activator.empty() && !ingredient.empty()) {
				critterIncludes.emplace_back(std::move(activator), std::move(ingredient));
			}
		}

		// [CritterExclude]: Names=A, B, C
		char names[2048]{};
		GetPrivateProfileStringA("CritterExclude", "Names", "", names, sizeof(names), kIniPath);
		for (auto& name : SplitList(names)) {
			critterExcludes.insert(std::move(name));
		}

		SKSE::log::info("Critter overrides: {} include(s), {} exclude(s)", critterIncludes.size(), critterExcludes.size());
	}

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

		out << "[General]\n"
			"; Comma-separated list of skills that receive XP on each harvest. Valid names:\n"
			"; OneHanded, TwoHanded, Archery, Block, Smithing, HeavyArmor, LightArmor,\n"
			"; Pickpocket, Lockpicking, Sneak, Alchemy, Speech, Alteration, Conjuration,\n"
			"; Destruction, Illusion, Restoration, Enchanting\n"
			"; Example: Skills=Alchemy, Sneak\n"
			"Skills=Alchemy\n"
			"\n"
			"; Skill XP awarded to EACH listed skill every time the player harvests an ingredient plant\n"
			"XPPerHarvest=50.0\n"
			"\n"
			"; 1 = log the record type of everything the player activates (for troubleshooting)\n"
			"DebugLogActivations=0\n"
			"\n";
		out << kIncludeBlock << "\n" << kExcludeBlock;

		SKSE::log::info("Created default INI at {}", path.string());
	}

	inline void Load()
	{
		if (!std::filesystem::exists(kIniPath)) {
			WriteDefaultIni();
		}
		else {
			AppendMissingCritterSections();
		}

		char buf[256]{};

		GetPrivateProfileStringA("General", "Skills", "Alchemy", buf, sizeof(buf), kIniPath);
		skills = ParseSkillList(buf);
		if (skills.empty()) {
			SKSE::log::warn("No valid skills in INI, falling back to Alchemy");
			skills = { RE::ActorValue::kAlchemy };
		}

		GetPrivateProfileStringA("General", "XPPerHarvest", "50.0", buf, sizeof(buf), kIniPath);
		try {
			xpPerHarvest = std::stof(buf);
		}
		catch (...) {
			xpPerHarvest = 50.0f;
		}

		debugLogActivations = GetPrivateProfileIntA("General", "DebugLogActivations", 0, kIniPath) != 0;

		LoadCritterOverrides();

		SKSE::log::info("Settings loaded: {} skill(s), xpPerHarvest={}, debugLogActivations={}", skills.size(),
			xpPerHarvest, debugLogActivations);
	}
}