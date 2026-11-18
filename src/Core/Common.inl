// Part of the shared code: included inside the including file's anonymous namespace (not compiled on its own). Helpers
// both mods' code uses.
	RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

	// a save is loading (kPreLoadGame until kPostLoadGame/kNewGame): the background watchers wait, so they never act on
	// the save being left or on one only half loaded
	std::atomic<bool> g_loadingSave{ false };

	// ---- a plugin's record, by its form ID within that plugin ----
	// TESDataHandler::LookupForm(id, "file") looks the plugin up by name on every call, walking every plugin file in the
	// Data folder with a string compare (CommonLibSSE TESDataHandler.cpp); with thousands of plugins that is thousands
	// of compares per lookup (SkyPatcher's utility.cpp notes the same and caches it). So each plugin's place in the load
	// order is found once and kept, missing plugins too: no plugin is added or removed once the game's data has loaded
	// (SkyPatcher, same note). Before then nothing is kept, as the load order isn't final.
	std::atomic<bool> g_gameDataLoaded{ false };  // set at kDataLoaded (CampSetup, Setup.inl)

	struct PluginPlace
	{
		std::string name;              // as asked for
		std::optional<RE::FormID> id;  // the plugin's form ID prefix; none: not loaded
	};
	std::mutex g_pluginPlacesLock;
	std::vector<PluginPlace> g_pluginPlaces;  // a handful: looked through in order

	std::optional<RE::FormID> FindPluginPlace(std::string_view a_file)
	{
		auto data = RE::TESDataHandler::GetSingleton();
		auto file = data ? data->LookupModByName(a_file) : nullptr;
		if (!file || file->compileIndex == 0xFF) {
			return std::nullopt;
		}
		// (as LookupForm builds it: a light plugin's slot goes under the FE)
		return (static_cast<RE::FormID>(file->compileIndex) << 24) + (static_cast<RE::FormID>(file->smallFileCompileIndex) << 12);
	}

	std::optional<RE::FormID> PluginPrefix(std::string_view a_file)
	{
		if (!g_gameDataLoaded) {
			return FindPluginPlace(a_file);
		}
		auto same = [&](const std::string& a_name) {
			return a_name.size() == a_file.size() &&
			       std::ranges::equal(a_name, a_file, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		};
		std::lock_guard guard(g_pluginPlacesLock);
		for (const auto& place : g_pluginPlaces) {
			if (same(place.name)) {
				return place.id;
			}
		}
		g_pluginPlaces.push_back({ std::string(a_file), FindPluginPlace(a_file) });
		return g_pluginPlaces.back().id;
	}

	// what LookupForm(a_id, a_file) gives, without searching for the plugin each time
	RE::TESForm* PluginForm(RE::FormID a_id, std::string_view a_file)
	{
		const auto place = PluginPrefix(a_file);
		return place ? RE::TESForm::LookupByID(*place + a_id) : nullptr;
	}
	template <class T>
	T* PluginForm(RE::FormID a_id, std::string_view a_file)
	{
		auto form = PluginForm(a_id, a_file);
		return form ? form->As<T>() : nullptr;
	}
