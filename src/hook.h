#pragma once

namespace Hooks
{
	// Patches TESFlora::Activate and TESObjectTREE::Activate, and builds the critter lookup.
	// Call once on kDataLoaded, after Settings::Load().
	void Install();

	// Clears per-session critter bookkeeping. Call on kPreLoadGame / kNewGame.
	void ResetState();
}