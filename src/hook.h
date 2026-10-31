#pragma once

namespace Hooks
{
	// Patches TESFlora::Activate and TESObjectTREE::Activate so we can see successful
	// in-world harvests by the player. Call once, after SKSE::Init.
	void Install();
}