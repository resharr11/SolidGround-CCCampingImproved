// Part of the shared camp code: included after the anonymous namespace (at file scope). What one mod asks the other's
// DLL (by GetProcAddress): its camp code version, and its camp state for the save (see Setup.inl).

// the camp code version this DLL carries
SKSE_EXPORT int Camp_CodeVersion() { return CAMP_CODE_VERSION; }

// this DLL's camp state as bytes: a_which 0, the live state (only meaningful from the mod running the camps); 1, the state
// read from this mod's save data. Returns its size; copies it into a_out when a_capacity is big enough
SKSE_EXPORT std::uint32_t Camp_State(std::uint8_t* a_out, std::uint32_t a_capacity, int a_which)
{
	const auto bytes = a_which == 0 ? (g_campActive ? CampStateBytes(g_campSaveCount) : std::vector<std::uint8_t>{}) : g_campLoaded;
	const auto size = static_cast<std::uint32_t>(bytes.size());
	if (a_out && a_capacity >= size && size) {
		std::memcpy(a_out, bytes.data(), size);
	}
	return size;
}

// Free Cooking asks whether a camp pot of ours is over a cold fire (it opens its cooking screen only over a lit one):
// 1 over a cold fire, 0 over a burning one, -1 not a pot our records name. Free Cooking asks from its own task thread, so
// the answer comes from the records the main thread publishes (PublishRecords), never the records themselves, and the
// pot must be recorded with the pot's role: a copy of our pot no camp recorded is not ours.
// Over a cold fire the recorded pot is put away (disabled) and a stand-in takes its place (BlockCampPots)
SKSE_EXPORT int Camp_PotUnlit(RE::FormID a_pot)
{
	const auto piece = PublicPieceOf(a_pot);
	if (!piece || piece->role != PieceRole::kPot) {
		return -1;
	}
	return piece->hidden ? 1 : 0;  // (from the copy: nothing of the live pot is read here)
}
