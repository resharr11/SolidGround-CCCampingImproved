// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Ground rays, camp.ini, tents (choice, cold, fitting), camp layout.
	// the height of the ground under a point: the first solid ground a ray straight down meets (terrain, or rock and
	// stone paths laid over it), passing through our camp's own pieces. Terrain height alone put things under the
	// stone paths and rock floors that cover the terrain in many places
	struct GroundHit
	{
		RE::TESObjectREFR* ref = nullptr;  // what the ray met (none for terrain)
		bool terrain = false;
		RE::MATERIAL_ID material = RE::MATERIAL_ID::kNone;  // its collision's material, as far as it tells
	};

	bool GroundZ(const RE::NiPoint3& a_at, float& a_out, RE::TESObjectREFR* a_lookThrough = nullptr, GroundHit* a_hit = nullptr, float a_above = 150.0f)
	{
		if (a_hit) {
			*a_hit = {};
		}
		std::vector<const void*> ignore;  // the collision of a_lookThrough (our tent)
		if (auto root = a_lookThrough ? a_lookThrough->Get3D() : nullptr) {
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_collision) {
				if (auto body = a_collision ? a_collision->body.get() : nullptr) {
					ignore.push_back(body->referencedObject.get());
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cell = player ? player->GetParentCell() : nullptr;
		auto world = cell ? cell->GetbhkWorld() : nullptr;
		if (world) {
			const float s = RE::bhkWorld::GetWorldScale();
			float top = a_at.z + a_above;  // (indoors from just above the floor: from higher up the ray met the ceiling)
			const float bottom = a_at.z - 300.0f;
			for (int tries = 0; tries < 6 && top > bottom; ++tries) {
				RE::bhkPickData pick;
				pick.rayInput.from = RE::hkVector4(a_at.x * s, a_at.y * s, top * s, 0.0f);
				pick.rayInput.to = RE::hkVector4(a_at.x * s, a_at.y * s, bottom * s, 0.0f);
				pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
				{
					RE::BSReadLockGuard lock(world->worldLock);
					world->PickObject(pick);
				}
				if (!pick.rayOutput.HasHit()) {
					break;
				}
				const float z = top + (bottom - top) * pick.rayOutput.hitFraction;
				const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
				auto ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
				// the tent itself is looked through by its collision (its reference is not always known while it reloads,
				// and its wall lifted the bedroll into the air)
				const auto owner = pick.rayOutput.rootCollidable->GetOwner();
				const bool ignored = std::ranges::find(ignore, owner) != ignore.end();
				if (!ignored && (layer == RE::COL_LAYER::kStatic || layer == RE::COL_LAYER::kAnimStatic || layer == RE::COL_LAYER::kTerrain || layer == RE::COL_LAYER::kGround) &&
					!(ref && ref->IsDynamicForm())) {
					a_out = z;
					if (a_hit) {
						a_hit->ref = ref;
						a_hit->terrain = layer == RE::COL_LAYER::kTerrain;
						auto shape = pick.rayOutput.rootCollidable->GetShape();
						if (shape && shape->userData) {
							a_hit->material = shape->userData->materialID;
						}
					}
					return true;
				}
				top = z - 1.0f;  // something of ours, an actor or a plant: look on below it
			}
		}
		// nothing solid in reach of the ray: the terrain's height, if it is in reach too. Off the edge of an ice floe (or
		// over a gap between floes) the terrain is the sea floor far below, and taking it sank the whole camp
		auto tes = RE::TES::GetSingleton();
		float land = 0.0f;
		if (!tes || !tes->GetLandHeight(a_at, land) || (world && land < a_at.z - 300.0f)) {
			return false;
		}
		if (a_hit) {
			a_hit->terrain = true;
		}
		a_out = land;
		return true;
	}

	// standing on ice: an ice floe, glacier slab or other ice (by its model, from landscape\\ice, or its collision's
	// material), or terrain painted with an ice texture
	bool OnIce(const RE::NiPoint3& a_at, bool a_quiet = false)
	{
		GroundHit hit;
		float z = 0.0f;
		if (!GroundZ(a_at, z, nullptr, &hit)) {
			return false;
		}
		auto lower = [](std::string a_s) {
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		};
		auto icy = [](const std::string& a_path) {
			return a_path.find("\\ice\\") != std::string::npos || a_path.find("glacier") != std::string::npos || a_path.find("icefloe") != std::string::npos;
		};
		std::string what;
		bool ice = hit.material == RE::MATERIAL_ID::kIce || hit.material == RE::MATERIAL_ID::kIceForm;
		if (hit.ref) {
			auto model = hit.ref->GetBaseObject() ? hit.ref->GetBaseObject()->As<RE::TESModel>() : nullptr;
			what = lower(model && model->GetModel() ? model->GetModel() : "");
			ice = ice || icy(what);
		} else if (hit.terrain) {
			auto tes = RE::TES::GetSingleton();
			auto land = tes ? tes->GetLandTexture({ a_at.x, a_at.y, z }) : nullptr;
			auto set = land ? land->textureSet : nullptr;
			what = "terrain " + lower(set && set->textures[0].textureName.c_str() ? set->textures[0].textureName.c_str() : "");
			ice = ice || icy(what) || (land && land->materialType && (land->materialType->materialID == RE::MATERIAL_ID::kIce || land->materialType->materialID == RE::MATERIAL_ID::kIceForm));
		}
		if (!a_quiet) {
			logger::info("Camp: ground under ({:.0f}, {:.0f}) is {} (material {:X}){}", a_at.x, a_at.y, what, static_cast<std::uint32_t>(hit.material), ice ? ": ice" : "");
		}
		return ice;
	}

	// Tilting to a slope: a positive angle.x tips a model's front down and a positive angle.y lifts its right side
	// (checked in game on the chopping block)
	constexpr float g_pitchSign = 1.0f;
	constexpr float g_rollSign = 1.0f;

	// layout survey for planning: every placed piece around a campfire, in the fire's own axes
	void LogCampLayout(RE::TESObjectREFR* a_campfire)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes || !a_campfire) {
			return;
		}
		const auto fire = a_campfire->GetPosition();
		const float h = a_campfire->GetAngleZ();
		tes->ForEachReferenceInRange(a_campfire, 600.0f, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref && a_ref->IsDynamicForm() && a_ref != a_campfire && a_ref->GetBaseObject() && !a_ref->IsDisabled() && !a_ref->IsMarkedForDeletion()) {
				const auto d = a_ref->GetPosition() - fire;
				const float shiftAt = d.x * std::sin(h) + d.y * std::cos(h);
				const float sideAt = d.x * std::cos(h) - d.y * std::sin(h);
				float turn = (a_ref->GetAngleZ() - h) * 180.0f / std::numbers::pi_v<float>;
				while (turn > 180.0f) { turn -= 360.0f; }
				while (turn < -180.0f) { turn += 360.0f; }
				const auto base = a_ref->GetBaseObject();
				logger::info("Camp layout: {:08X} {} shift {:.0f} side {:.0f} turn {:.0f} height {:.0f}", base->GetFormID(),
					base->GetName(), shiftAt, sideAt, turn, d.z);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	// moves a reference the way Papyrus's MoveTo does, so a model already on screen follows (SetPosition alone only
	// moved the chopping block's stored position: its model stayed where it was placed)
	// (a camp's recorded piece only: the gate's move)
	void MoveRef(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_angle)
	{
		if (!a_ref || !GateOk(a_ref, "moved")) {
			return;
		}
		using moveTo_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*,
			const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<moveTo_t> moveTo{ RELOCATION_ID(56227, 56626) };
		a_ref->SetAngle(a_angle);
		a_ref->SetPosition(a_pos);
		moveTo(a_ref, RE::ObjectRefHandle(), a_ref->GetParentCell(), a_ref->GetWorldspace(), a_pos, a_angle);
	}

	// a place beside a campfire, along and across the fire's own axes, on the ground there (terrain height, so uneven
	// ground leaves nothing floating or buried; the fire's height where there is no terrain)
	RE::NiPoint3 BesideFire(RE::TESObjectREFR* a_campfire, float a_shift, float a_side, float a_sink)
	{
		auto at = a_campfire->GetPosition();
		const float heading = a_campfire->GetAngleZ();
		at.x += a_shift * std::sin(heading) + a_side * std::cos(heading);
		at.y += a_shift * std::cos(heading) - a_side * std::sin(heading);
		if (auto tes = RE::TES::GetSingleton()) {
			float ground = 0.0f;
			if (GroundZ(at, ground)) {
				at.z = ground;
			}
		}
		at.z -= a_sink;
		return at;
	}

	RE::TESObjectREFR* CurrentShelter();

	// The player's own settings file, written by the in-game settings page (Menu.cpp): Documents\My Games\Skyrim
	// Special Edition\SKSE\SolidGroundCamping.ini (the folder SKSE and our log use). Never Data (his rule: nothing is
	// written to Data at runtime), and never the shipped file, so a mod update can't erase the player's settings. Its
	// lines win over the shipped SolidGroundCamping.ini. Empty when there is no Documents folder: the page can't save then
	std::filesystem::path CampUserIniPath()
	{
		static const std::filesystem::path path = []() -> std::filesystem::path {
			if (const auto dir = SKSE::log::log_directory()) {
				return *dir / L"SolidGroundCamping.ini";
			}
			return {};
		}();
		return path;
	}

	// key = number lines of one settings file into a_out (a key read again replaces the earlier value; ; starts a comment)
	void ReadCampSettingsFile(const std::filesystem::path& a_path, std::unordered_map<std::string, float>& a_out)
	{
		std::ifstream ini(a_path);
		std::string line;
		while (ini && std::getline(ini, line)) {
			if (const auto semi = line.find(';'); semi != std::string::npos) {
				line.erase(semi);
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			auto key = line.substr(0, eq);
			std::erase_if(key, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
			for (auto& ch : key) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			if (key.empty()) {
				continue;
			}
			try {
				if (const float v = std::stof(line.substr(eq + 1)); std::isfinite(v)) {
					a_out[key] = v;
				}
			} catch (...) {}
		}
	}

	// camp.ini: the shipped file, then the player's own file over it. Read once and again only when either file changes
	// (their times and sizes are looked at no more than once a second: a tool that keeps a file's time is still seen by
	// its size), not from disk on every call: some callers run every second
	std::unordered_map<std::string, float> CampSettings()
	{
		static std::mutex lock;
		static std::unordered_map<std::string, float> cached;
		static std::filesystem::file_time_type stamp{}, userStamp{};
		static std::uintmax_t size = 0, userSize = 0;
		static std::chrono::steady_clock::time_point looked{};
		static bool loaded = false;
		std::lock_guard guard(lock);
		const auto now = std::chrono::steady_clock::now();
		if (loaded && now - looked < std::chrono::seconds(1)) {
			return cached;
		}
		looked = now;
		const auto user = CampUserIniPath();
		std::error_code ec, userEc;
		auto changed = std::filesystem::last_write_time(CAMP_INI, ec);
		if (ec) {
			changed = {};
		}
		auto userChanged = user.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(user, userEc);
		if (userEc) {
			userChanged = {};
		}
		std::error_code sizeEc, userSizeEc;
		auto bytes = std::filesystem::file_size(CAMP_INI, sizeEc);
		auto userBytes = user.empty() ? std::uintmax_t{ 0 } : std::filesystem::file_size(user, userSizeEc);
		if (sizeEc) {
			bytes = 0;
		}
		if (userSizeEc) {
			userBytes = 0;
		}
		if (loaded && changed == stamp && userChanged == userStamp && bytes == size && userBytes == userSize) {
			return cached;
		}
		const bool again = loaded;
		stamp = changed;
		userStamp = userChanged;
		size = bytes;
		userSize = userBytes;
		loaded = true;
		cached.clear();
		ReadCampSettingsFile(CAMP_INI, cached);
		if (!user.empty()) {
			ReadCampSettingsFile(user, cached);
		}
		// (one line each time, so a test can see which values are in use: the fire's burn time is the one tests change)
		const auto burn = cached.find("fireburnhours");
		logger::info("Camp: settings {} ({} bytes shipped, {} bytes yours; a fire burns {} hours)", again ? "read again" : "read",
			bytes, userBytes, burn != cached.end() ? burn->second : 7.0f);
		return cached;
	}

	// the shipped SolidGroundCamping.ini alone (the settings page shows it for values the player hasn't set). Read again
	// only when it changes, looked at no more than once a second (the page asks every frame)
	std::unordered_map<std::string, float> CampShippedSettings()
	{
		static std::mutex lock;
		static std::unordered_map<std::string, float> cached;
		static std::filesystem::file_time_type stamp{};
		static std::chrono::steady_clock::time_point looked{};
		static bool loaded = false;
		std::lock_guard guard(lock);
		const auto now = std::chrono::steady_clock::now();
		if (loaded && now - looked < std::chrono::seconds(1)) {
			return cached;
		}
		looked = now;
		std::error_code ec;
		auto changed = std::filesystem::last_write_time(CAMP_INI, ec);
		if (ec) {
			changed = {};
		}
		if (loaded && changed == stamp) {
			return cached;
		}
		stamp = changed;
		loaded = true;
		cached.clear();
		ReadCampSettingsFile(CAMP_INI, cached);
		return cached;
	}

	float CampSetting(const std::unordered_map<std::string, float>& a_settings, const char* a_key, float a_default)
	{
		const auto it = a_settings.find(a_key);
		return it != a_settings.end() ? it->second : a_default;
	}

	// Camping's shelter: its own lean-to (0), or a base-game tent: 1 small Nordic tent, 2 large Nordic tent, 3 small
	// Imperial tent (the large Imperial tent was dropped: it did not feel right). The player picks it in the shelter's
	// menu (Change tent), kept in the save; Camping's lean-to until then. In the cold (snow or ice underfoot, or a
	// blizzard) a camp always gets the large Nordic tent. g_campTent: what the camp standing now was pitched with (-1:
	// none yet in this save)
	std::atomic<int> g_tentChoice{ 0 };
	std::atomic<int> g_campTent{ -1 };
	std::atomic<int> g_campTentBeforePitch{ -2 };  // the standing camp's shelter while a new camp may go up (-2: none waiting)
	// while Change tent swaps the shelter it is disabled for a moment (to reload its model): until this time (steady
	// clock, ms) that is not the camp being packed up. (The pack-up watcher took it for one: the mound, the fire and
	// the camp's other pieces were taken away and the supplies handed back)
	// The shelter and bedroll hidden for the swap are listed until they are shown again (not a time: a pause in the 300 ms
	// let the time run out while the job waited, and the watcher packed the camp up).
	// Kept in the save (record 'TSWP'), so a save made in those 300 ms shows them again when loaded
	std::mutex g_tentSwapLock;
	std::vector<RE::FormID> g_tentSwapRefs;
	long long SteadyMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	bool TentSwapPending(const RE::TESObjectREFR* a_ref)
	{
		std::scoped_lock lock(g_tentSwapLock);
		return a_ref && std::ranges::find(g_tentSwapRefs, a_ref->GetFormID()) != g_tentSwapRefs.end();
	}
	// hides these for a moment to reload their models, then shows them again (main thread)
	void SwapReload(const std::vector<RE::ObjectRefHandle>& a_reload)
	{
		std::vector<std::pair<RE::ObjectRefHandle, RE::FormID>> refs;
		for (const auto& h : a_reload) {
			if (auto r = h.get()) {
				refs.emplace_back(h, r->GetFormID());
			}
		}
		{
			std::scoped_lock lock(g_tentSwapLock);
			for (const auto& [h, id] : refs) {
				g_tentSwapRefs.push_back(id);
			}
		}
		for (const auto& [h, id] : refs) {
			if (auto r = h.get()) {
				HideCampPiece(r.get());
			}
		}
		After(300, [refs]() {
			for (const auto& [h, id] : refs) {
				// (still a recorded piece: one packed up meanwhile stays gone)
				if (auto r = h.get()) {
					ShowCampPiece(r.get());
				}
			}
			std::scoped_lock lock(g_tentSwapLock);
			for (const auto& [h, id] : refs) {
				if (auto it = std::ranges::find(g_tentSwapRefs, id); it != g_tentSwapRefs.end()) {
					g_tentSwapRefs.erase(it);  // (one entry each: by number, even if the reference is gone by now)
				}
			}
		});
	}
	// (named by kind, not size: every shelter is sized to the same spot. The large Nordic tent is the winter one)
	// (by what they are made of, words the player knows; no faction names: the small Nordic tent of hides, the large
	// Nordic tent of furs, the Imperial tent of leather, like the Roman legion's)
	// The shelters to choose from: ours (0 lean-to, 1 hide tent, 2 fur tent, 3 leather tent), then any a kit file adds
	// ([Tent] sections, LoadKits in PlantsMoundsFollowers.inl). Built once at kDataLoaded, only read afterwards. A kit
	// tent is kept in the save by its name (record 'TNTN'), so a kit added or removed later never mixes them up
	struct TentKind
	{
		std::string name, nameLower;  // English (shown through Tr)
		std::string model;            // under Data/Meshes (ours: Camping's own lean-to when empty)
		RE::TESBoundObject::BOUND_DATA bounds{};  // the model's footprint at full size (kits); ours read their records
		bool hasBounds = false;
		bool winter = false;      // good in places where a blizzard can blow (ours: only the fur tent)
		bool kit = false;
		float door = 0.0f;        // kits: which way the opening points, degrees from the model's front
		float size = 1.0f;        // kits: size tweak (1: fits the spot exactly)
	};
	constexpr int WINTER_TENT = 2;  // our fur tent: the one every camp gets in the cold, unless a kit's winter tent was chosen
	std::vector<TentKind> g_tents{
		{ "Lean-to", "lean-to", "" },
		{ "Hide tent", "hide tent", "Architecture\\Tents\\SmallNordicTent01.nif" },
		{ "Fur tent", "fur tent", "Architecture\\Tents\\LargeNordicTent01.nif", {}, false, true },
		{ "Leather tent", "leather tent", "Architecture\\Tents\\SmallImperialTent.nif" },
	};
	int TentCount() { return static_cast<int>(g_tents.size()); }
	int TentModelNow()
	{
		const int t = g_campTent >= 0 ? g_campTent.load() : g_tentChoice.load();
		return std::clamp(t, 0, TentCount() - 1);
	}
	const char* TentName(int a_model) { return g_tents[std::clamp(a_model, 0, TentCount() - 1)].name.c_str(); }
	const char* TentNameLower(int a_model) { return g_tents[std::clamp(a_model, 0, TentCount() - 1)].nameLower.c_str(); }
	// the shelter a camp gets at a place: the one chosen, or our fur tent where a blizzard can blow and the chosen one
	// is not a winter tent
	int TentForPlace(int a_choice, bool a_cold)
	{
		a_choice = std::clamp(a_choice, 0, TentCount() - 1);
		return a_cold && !g_tents[a_choice].winter ? WINTER_TENT : a_choice;
	}
	// a blizzard can blow where the player is: one of the weather regions over the player's cell can bring snowing weather
	// in a strong wind (read from the regions' own weather lists, not from the weather now). In the game's data these are
	// the snowy regions and the mountains (Winterhold, the Pale, Eastmarch, the coast, the mountains, Solstheim's snows,
	// the Forgotten Vale...: vanilla SkyrimStormSnow blows at 178 of 255, its light snow at 76); Blackreach's
	// spores count as snow but never blow (0), nor does Solstheim's ash (18). The answer: -1 the cell's regions are not
	// known, 0 no, 1 yes
	int BlizzardCanHappenHere()
	{
		auto player = Player();
		auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->IsInteriorCell()) {
			return cell ? 0 : -1;
		}
		// two frozen places whose own data says otherwise, counted whole: Dawnguard's Forgotten Vale (its grove's weather
		// has only light snow, though the valley is ice from end to end) and Castle Volkihar's island in the Sea of Ghosts
		// (its region, DLC01VampCastleStorm, has only light snow, and the coast's region doesn't reach it)
		auto vale = PluginForm<RE::TESWorldSpace>(0x000BB5, "Dawnguard.esm");     // DLC01FalmerValley
		auto volkihar = PluginForm<RE::TESRegion>(0x012DAD, "Dawnguard.esm");   // DLC01VampCastleStorm
		if (vale && player->GetWorldspace() == vale) {
			return 1;
		}
		auto regions = cell->GetRegionList(false);
		if (!regions) {
			return -1;
		}
		constexpr std::uint8_t BLIZZARD_WIND = 150;
		for (auto region : *regions) {
			if (region && region == volkihar) {
				return 1;
			}
			if (!region || !region->dataList) {
				continue;
			}
			for (auto data : region->dataList->regionDataList) {
				if (!data || data->GetType() != RE::TESRegionData::Type::kWeather) {
					continue;
				}
				for (auto type : static_cast<RE::TESRegionDataWeather*>(data)->weatherTypes) {
					auto weather = type ? type->weather : nullptr;
					if (weather && weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow) && weather->data.windSpeed >= BLIZZARD_WIND) {
						return 1;
					}
				}
			}
		}
		return 0;
	}

	// too cold for anything but the large Nordic tent: places where a blizzard can blow, the snowy regions and the
	// mountains, whatever the weather now. (Only if the cell's regions can't be read: snow or ice underfoot)
	bool TooColdForLightTent(const RE::NiPoint3& a_at)
	{
		if (const int blizzard = BlizzardCanHappenHere(); blizzard >= 0) {
			return blizzard == 1;
		}
		if (OnIce(a_at, true)) {
			return true;
		}
		auto ground = VisibleLandTexture(a_at, 256.0f);
		const auto material = ground && ground->materialType ? ground->materialType->materialID : RE::MATERIAL_ID::kNone;
		return material == RE::MATERIAL_ID::kSnow || material == RE::MATERIAL_ID::kSnowStairs || material == RE::MATERIAL_ID::kIce ||
		       material == RE::MATERIAL_ID::kIceForm;
	}
	void SetCampTentModel()
	{
		const int tent = TentModelNow();
		const char* tentModel = g_tents[tent].model.c_str();
		// Camping's own models, kept the first time, so the lean-to can come back
		static std::unordered_map<RE::FormID, std::string> original;
		for (auto id : { CAMP_SHELTER, static_cast<RE::FormID>(0x834), static_cast<RE::FormID>(0x819) }) {
			auto form = CampingForm(id);
			auto model = form ? form->As<RE::TESModel>() : nullptr;
			if (!model) {
				continue;
			}
			if (!original.contains(id)) {
				original[id] = model->GetModel() ? model->GetModel() : "";
			}
			if (tent == 0) {
				model->SetModel(original[id].c_str());
			} else if (id == 0x819) {
				// the lean-to's model held the bed's look; Camping's bedroll was only the place to sleep. In a tent it
				// shows the vanilla fur bedroll
				model->SetModel(g_campBedrollModel.c_str());
			} else {
				model->SetModel(tentModel);
			}
		}
		logger::info("Camp shelter: {}", tent == 0 ? "Camping's lean-to" : tentModel);
	}

	// a shelter model's own bounds (from its record): the vanilla tent statics, and Camping's lean-to static (its
	// activator has none)
	const RE::TESBoundObject::BOUND_DATA* TentModelBounds(int a_model)
	{
		static constexpr std::array<RE::FormID, 4> VANILLA_TENT{ 0, 0x800E2, 0x800DE, 0x800DC };
		if (a_model >= 4 && a_model < TentCount()) {
			return g_tents[a_model].hasBounds ? &g_tents[a_model].bounds : nullptr;
		}
		if (a_model >= 1 && a_model <= 3) {
			auto vanilla = RE::TESForm::LookupByID<RE::TESObjectSTAT>(VANILLA_TENT[a_model]);
			return vanilla ? &vanilla->boundData : nullptr;
		}
		auto leanTo = CampingForm(0x834);  // ccQDRSSE002_ShelterStaticReference: the lean-to's own model
		auto bound = leanTo ? leanTo->As<RE::TESBoundObject>() : nullptr;
		return bound ? &bound->boundData : nullptr;
	}
	// every shelter takes the same spot: a rectangle tentwidth across and tentdepth from its front (towards the fire)
	// to its back. Each model is sized to fit in it and centred on it, so changing tents changes nothing round it: its
	// place, the mound under it, the navigation cut and the bedroll all stay
	struct TentFit
	{
		float scale = 1.0f, rz = 0.0f;
		RE::NiPoint3 origin{};  // where the model's origin goes
		float cf = 0.0f, ca = 0.0f;  // the model's middle from its origin at full size: along the facing, and across it
	};
	float TentSlotWidth(const std::unordered_map<std::string, float>& a_settings) { return (std::max)(60.0f, CampSetting(a_settings, "tentwidth", 300.0f)); }
	float TentSlotDepth(const std::unordered_map<std::string, float>& a_settings) { return (std::max)(60.0f, CampSetting(a_settings, "tentdepth", 210.0f)); }
	// the settings keys of a shelter's size and door: tent0size ... tent3size for ours; a kit tent by its name (its
	// place in the list changes with the kits installed), kittent_<name, letters and digits only, lower case>_size a
	// kit tent's name as it goes into its settings keys: letters and digits, lower case; a byte outside plain ASCII, as
	// in a name in another script, as two hex digits, so two such names never share a key. Two names that still give
	// the same key ("Scout Tent", "Scout-Tent"): LoadKits leaves the second out
	std::string KitTentKeyName(const std::string& a_name)
	{
		std::string name;
		for (const char ch : a_name) {
			const auto c = static_cast<unsigned char>(ch);
			if (c >= 0x80) {
				name += std::format("{:02x}", c);
			} else if (std::isalnum(c)) {
				name += static_cast<char>(std::tolower(c));
			}
		}
		return name;
	}
	std::string TentSettingKey(int a_model, const char* a_what)
	{
		if (a_model >= 4 && a_model < TentCount()) {
			return std::format("kittent_{}_{}", KitTentKeyName(g_tents[a_model].name), a_what);
		}
		return std::format("tent{}{}", a_model, a_what);
	}
	// which way the model's opening points, degrees from its front: the settings (the page or camp.ini), else a kit
	// tent's own Door, else 0
	// the opening the standing camp's shelter was put up with (radians; NaN: not known, as in a save from before 1.1).
	// Saved (TDOR): the standing shelter's spot is worked back from its turn and this, so changing a tent's Opening on the
	// settings page later can't move or turn the next tent put up in that camp
	float g_tentDoorNow = std::numeric_limits<float>::quiet_NaN();
	float TentDoor(int a_model, const std::unordered_map<std::string, float>& a_settings)
	{
		const auto key = TentSettingKey(a_model, "door");
		const float kitDoor = a_model >= 4 && a_model < TentCount() ? g_tents[a_model].door : 0.0f;
		return (a_settings.contains(key) ? a_settings.at(key) : kitDoor) * std::numbers::pi_v<float> / 180.0f;
	}
	// (a_door: the opening to use, radians; NaN: the settings' own)
	TentFit FitTentToSlot(int a_model, const RE::NiPoint3& a_centre, float a_facing, const std::unordered_map<std::string, float>& a_settings,
		float a_door = std::numeric_limits<float>::quiet_NaN())
	{
		TentFit fit;
		fit.rz = a_facing - (std::isfinite(a_door) ? a_door : TentDoor(a_model, a_settings));
		const RE::NiPoint3 f{ std::sin(a_facing), std::cos(a_facing), 0.0f }, a{ std::cos(a_facing), -std::sin(a_facing), 0.0f };
		float fMin = -100.0f, fMax = 100.0f, aMin = -100.0f, aMax = 100.0f;
		if (auto b = TentModelBounds(a_model); b && b->boundMax.x > b->boundMin.x && b->boundMax.y > b->boundMin.y) {
			fMin = aMin = std::numeric_limits<float>::max();
			fMax = aMax = -std::numeric_limits<float>::max();
			for (const float x : { static_cast<float>(b->boundMin.x), static_cast<float>(b->boundMax.x) }) {
				for (const float y : { static_cast<float>(b->boundMin.y), static_cast<float>(b->boundMax.y) }) {
					const float wx = x * std::cos(fit.rz) + y * std::sin(fit.rz), wy = -x * std::sin(fit.rz) + y * std::cos(fit.rz);
					const float pf = wx * f.x + wy * f.y, pa = wx * a.x + wy * a.y;
					fMin = (std::min)(fMin, pf);
					fMax = (std::max)(fMax, pf);
					aMin = (std::min)(aMin, pa);
					aMax = (std::max)(aMax, pa);
				}
			}
		}
		const auto sizeKey = TentSettingKey(a_model, "size");  // a hand tweak per model (1: fits the spot exactly)
		const float tweak = a_settings.contains(sizeKey) ? a_settings.at(sizeKey) : a_model >= 4 && a_model < TentCount() ? g_tents[a_model].size : 1.0f;
		fit.scale = std::clamp((std::min)(TentSlotWidth(a_settings) / (aMax - aMin), TentSlotDepth(a_settings) / (fMax - fMin)) * tweak, 0.2f, 3.0f);
		fit.cf = 0.5f * (fMin + fMax);
		fit.ca = 0.5f * (aMin + aMax);
		fit.origin = { a_centre.x - (f.x * fit.cf + a.x * fit.ca) * fit.scale, a_centre.y - (f.y * fit.cf + a.y * fit.ca) * fit.scale, a_centre.z };
		return fit;
	}
	// the spot a standing shelter fills: its middle and the way it faces, worked back from its model, size and turn
	void TentSlotOf(RE::TESObjectREFR* a_shelter, RE::NiPoint3& a_centre, float& a_facing)
	{
		const auto settings = CampSettings();
		const int model = TentModelNow();
		const float door = std::isfinite(g_tentDoorNow) ? g_tentDoorNow : TentDoor(model, settings);
		a_facing = a_shelter->GetAngleZ() + door;
		const auto fit = FitTentToSlot(model, {}, a_facing, settings, door);
		const float k = a_shelter->GetScale();
		const RE::NiPoint3 f{ std::sin(a_facing), std::cos(a_facing), 0.0f }, a{ std::cos(a_facing), -std::sin(a_facing), 0.0f };
		const auto pos = a_shelter->GetPosition();
		a_centre = { pos.x + (f.x * fit.cf + a.x * fit.ca) * k, pos.y + (f.y * fit.cf + a.y * fit.ca) * k, pos.z };
	}

	// the tent's fur bedroll is only for looks: make it non-collidable, so looking at it looks through to the shelter
	// ("Use Shelter") and Camping's sleeping spot. Retries until its model has loaded.
	void MakeBedrollSeeThrough(RE::ObjectRefHandle a_bed)
	{
		auto tries = std::make_shared<int>(0);
		Repeat(250, [a_bed, tries]() {
			if (++*tries > 20) {
				return false;
			}
			auto bed = a_bed.get();
			if (!bed) {
				return false;
			}
			if (auto root = bed->Get3D()) {
				root->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);
				return false;
			}
			return true;  // its model not loaded yet
		});
	}

	// Camping's box seat goes to the fire's other side, facing the shelter across the flames (it stood between them).
	// It turns around the fire with its sitting markers, keeping its distance; if that spot is on the cooking pot's, it
	// moves on around the fire until it is clear of it. a_dir: from the fire towards the shelter
	void SeatAcrossFire(RE::TESObjectREFR* a_campfire, const RE::NiPoint3& a_dir, const std::unordered_map<std::string, float>& settings)
	{
		const auto fire = a_campfire->GetPosition();
		const auto dir = a_dir;
		if (auto tes = RE::TES::GetSingleton(); tes && CampingForm(0x825)) {
			// (this camp's own seat, by its record and role; not one found near the fire)
			const auto camp = CampOf(a_campfire);
			RE::TESObjectREFR* seat = CampRef(camp, PieceRole::kSeat);
			if (seat) {
				const auto d = seat->GetPosition() - fire;
				const float seatDist = std::sqrt(d.x * d.x + d.y * d.y);
				const float now = std::atan2(d.x, d.y);
				float want = std::atan2(-dir.x, -dir.y);  // straight across the fire from the tent
				const auto pot = BesideFire(a_campfire, CampSetting(settings, "shift", 160.0f), CampSetting(settings, "side", 0.0f), 0.0f);
				const float potAngle = std::atan2(pot.x - fire.x, pot.y - fire.y);
				const float potDist = std::sqrt((pot.x - fire.x) * (pot.x - fire.x) + (pot.y - fire.y) * (pot.y - fire.y));
				auto clearOfPot = [&](float a_angle) {
					const float x = fire.x + seatDist * std::sin(a_angle) - pot.x, y = fire.y + seatDist * std::cos(a_angle) - pot.y;
					return std::sqrt(x * x + y * y) >= 95.0f;  // the seat's and the stand's half sizes and a step
				};
				if (CampHasCookware() && potDist > 1.0f && !clearOfPot(want)) {
					const float away = std::remainder(want - potAngle, 2.0f * std::numbers::pi_v<float>) >= 0.0f ? 1.0f : -1.0f;
					for (int step = 0; step < 36 && !clearOfPot(want); ++step) {
						want += away * 5.0f * std::numbers::pi_v<float> / 180.0f;
					}
				}
				const float turnBy = want - now;
				std::vector<RE::TESObjectREFR*> group{ seat };
				// (the camp's recorded markers standing at the seat, like its sitting marker: only recorded pieces are looked at)
				ForCampPieces(camp, { PieceRole::kMarker }, true, [&](RE::TESObjectREFR* a_ref) {
					if (a_ref->GetPosition().GetDistance(seat->GetPosition()) < 40.0f) {
						group.push_back(a_ref);
					}
				});
				for (auto piece : group) {
					auto p = piece->GetPosition();
					const float px = p.x - fire.x, py = p.y - fire.y;
					p.x = fire.x + px * std::cos(turnBy) + py * std::sin(turnBy);
					p.y = fire.y - px * std::sin(turnBy) + py * std::cos(turnBy);
					auto a = piece->GetAngle();
					a.z += turnBy;
					MoveRef(piece, p, a);
				}
				logger::info("Camp: box seat moved across the fire from the tent, turned {:.0f} degrees ({} pieces)", turnBy * 57.3f, group.size());
			}
		}
	}

	void FitCampTent(RE::TESObjectREFR* a_campfire)
	{
		auto shelter = CurrentShelter();
		auto player = Player();
		if (!shelter || !player || !a_campfire || shelter->GetPosition().GetDistance(a_campfire->GetPosition()) > 800.0f) {
			return;
		}
		const auto settings = CampSettings();
		const int tentModel = TentModelNow();
		const auto fire = a_campfire->GetPosition();
		// the shelter stays on the side of the fire where Camping put it, turned to face the fire
		auto dir = shelter->GetPosition() - fire;
		dir.z = 0.0f;
		const float len = dir.Length();
		if (len < 1.0f) {
			return;
		}
		dir = dir * (1.0f / len);
		SeatAcrossFire(a_campfire, dir, settings);
		const float W = TentSlotWidth(settings), D = TentSlotDepth(settings);
		// its front a set gap from the fire ring (tentfiregap), clear of Camping's box seat between them
		float reach = 70.0f + CampSetting(settings, "tentfiregap", 15.0f);
		if (auto seat = CampRef(CampOf(a_campfire), PieceRole::kSeat)) {  // (the camp's own seat, by its record)
			const auto d = seat->GetPosition() - fire;
			const float along = d.x * dir.x + d.y * dir.y, across = std::abs(d.x * dir.y - d.y * dir.x);
			if (along > 0.0f && across < 0.5f * W + 30.0f) {
				reach = (std::max)(reach, along + 30.0f + 20.0f);  // the seat's half size and a step
			}
		}
		RE::NiPoint3 centre{ fire.x + dir.x * (reach + 0.5f * D), fire.y + dir.y * (reach + 0.5f * D), fire.z };
		if (float ground = 0.0f; GroundZ(centre, ground)) {
			centre.z = ground;
		}
		const float facing = std::atan2(-dir.x, -dir.y);  // towards the fire
		const auto fit = FitTentToSlot(tentModel, centre, facing, settings);
		g_tentDoorNow = TentDoor(tentModel, settings);
		auto at = fit.origin;
		const RE::NiPoint3 angle{ 0.0f, 0.0f, fit.rz };
		ScaleCampPiece(shelter, fit.scale);
		logger::info("Camp: {} sized {:.2f} to its spot ({:.0f} by {:.0f}), its middle {:.0f} from the fire", TentName(tentModel), fit.scale, W, D, reach + 0.5f * D);
		RE::ObjectRefHandle bedLook;
		// the bedroll Camping laid at the shelter: in the middle of the spot, lengthwise (bedturn: extra turn; bedback
		// and bedside move it from the middle)
		if (auto bedrollBase = CampingForm(0x819)) {
			if (auto tes = RE::TES::GetSingleton()) {
				// (this camp's own bedroll by its record and role: not one found near the shelter)
				std::vector<RE::TESObjectREFR*> bedrolls;
				ForCampPieces(CampOf(shelter), { PieceRole::kBedroll }, true, [&](RE::TESObjectREFR* a_ref) {
					if (a_ref->GetBaseObject() == bedrollBase && !a_ref->IsDisabled()) {
						bedrolls.push_back(a_ref);
					}
				});
				auto bedAngle = bedrolls.empty() ? angle : bedrolls.front()->GetAngle();
				bedAngle.z = facing + CampSetting(settings, "bedturn", 180.0f) * std::numbers::pi_v<float> / 180.0f;
				const float bedScale = CampSetting(settings, "bedscale", 0.8f);
				auto bedAt = centre;
				{
					const float back = CampSetting(settings, "bedback", 0.0f), across = CampSetting(settings, "bedside", 0.0f);
					bedAt.x += -dir.x * -back + dir.y * across;
					bedAt.y += -dir.y * -back - dir.x * across;
				}
				const auto at0 = at;
				at = centre;  // (the slope below is read round the bedroll's own spot)
				{
					const float h = bedAngle.z;
					const float len = 80.0f * (bedScale > 0.0f ? bedScale : 1.0f);
					const float wid = 40.0f * (bedScale > 0.0f ? bedScale : 1.0f);
					auto groundAt = [&](float x, float y) {
						RE::NiPoint3 p{ bedAt.x + x * std::cos(h) + y * std::sin(h), bedAt.y - x * std::sin(h) + y * std::cos(h), at.z };
						float ground = at.z;
						return GroundZ(p, ground, shelter) ? ground : at.z;
					};
					// a rock or root under one end would prop the bedroll up like a board: each end counts at most 15
					// above or below the ground under its middle
					const float middle = groundAt(0.0f, 0.0f);
					auto toMiddle = [middle](float a_z) { return std::clamp(a_z, middle - 15.0f, middle + 15.0f); };
					const float head = toMiddle(groundAt(0.0f, len)), foot = toMiddle(groundAt(0.0f, -len));
					const float right = toMiddle(groundAt(wid, 0.0f)), left = toMiddle(groundAt(-wid, 0.0f));
					const float limit = 0.2f;  // about 11 degrees: a bedroll lies flat, more or less
					if (CampSetting(settings, "bedtilt", 1.0f) != 0.0f) {
						bedAngle.x = g_pitchSign * std::clamp(-std::atan2(head - foot, 2.0f * len), -limit, limit);  // nose up when the head is higher
						bedAngle.y = g_rollSign * std::clamp(std::atan2(right - left, 2.0f * wid), -limit, limit);  // right side up when it is higher
						bedAt.z = (head + foot + right + left) / 4.0f + CampSetting(settings, "bedlift", 1.0f);
					} else {
						bedAt.z = (std::max)({ head, foot, right, left });
					}
					logger::info("Camp: bedroll on the slope (head {:.0f}, foot {:.0f}, left {:.0f}, right {:.0f}), tilt {:.1f} / {:.1f} degrees",
						head, foot, left, right, bedAngle.x * 57.3f, bedAngle.y * 57.3f);
				}
				at = at0;
				for (auto bedroll : bedrolls) {
					MoveRef(bedroll, bedAt, bedAngle);
					if (bedScale > 0.0f && bedScale != 1.0f) {
						ScaleCampPiece(bedroll, bedScale);
					}
					HideCampPiece(bedroll);
					bedLook = bedroll->GetHandle();
				}
				// the rug under the bedroll in a tent (none under the lean-to): fur in the large Nordic tent, straw in the others
				if (auto rugBase = g_tents[tentModel].winter ? FurRugBase() : PluginItem(RUG_STRAW);
					tentModel != 0 && rugBase && CampSetting(settings, "rug", 1.0f) != 0.0f) {
					if (auto rug = PlaceOwned(a_campfire, rugBase, 0, PieceRole::kTentRug).get()) {
						auto rugAt = bedAt;
						rugAt.z -= CampSetting(settings, "bedlift", 1.0f) - 0.5f;
						auto rugAngle = bedAngle;
						rugAngle.z += CampSetting(settings, "rugturn", 90.0f) * std::numbers::pi_v<float> / 180.0f;
						MoveRef(rug, rugAt, rugAngle);
					}
				}
			}
		}
		at.z = centre.z;
		MoveRef(shelter, at, angle);
		// hidden for a moment so their models reload, listed in the save meanwhile (TSWP): a save in those 300 ms shows
		// them again when loaded
		std::vector<RE::ObjectRefHandle> reload{ shelter->GetHandle() };
		if (bedLook) {
			reload.push_back(bedLook);
		}
		SwapReload(reload);
	}

