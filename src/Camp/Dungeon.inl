// Part of the shared camp code (Free Cooking's CookingMenu.cpp, Solid Ground Camping's main.cpp): included in order inside an anonymous namespace (not compiled on its own). Dungeon camps, fires that burn out, fades.
	// Dungeons (caves, ruins, mines, forts, lairs; not homes, inns or anything in a town): Camping makes no camp
	// indoors, so ours is made here, smaller: no tent, only the fire, a bedroll on a straw mat, and the
	// pot on its stand with the cookware upgrade. No earth mound under it: the floor must be nearly level, and every piece
	// needs its room (it may touch a wall a little)
	constexpr RE::FormID CAMPFIRE_LIGHT = 0x000AF8BA;   // Skyrim.esm: LightCampFire01
	constexpr RE::FormID DUNGEON_BEDROLL = 0x0003B3CD;  // Skyrim.esm: Bedroll01F, a fur bedroll you lie down in
	constexpr RE::FormID FIRE_LOOP = 0x00037E3E;        // Skyrim.esm: FXFireCampfireLP, the fire's crackle (a sound marker)

	// the planes of Oblivion and the afterlife, as Survival Mode counts them (Survival_OblivionAreas, _Locations, _Cells:
	// where it says "You do not feel hunger, exhaustion, or cold here."), and the Deadlands (The Cause). No camp there
	bool InOtherPlane(RE::Actor* a_player)
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!a_player || !data) {
			return false;
		}
		struct Place
		{
			RE::FormID id;
			const char* file;
		};
		static constexpr Place PLACES[]{
			{ 0x02EE41, "Skyrim.esm" }, { 0x0192B2, "Skyrim.esm" }, { 0x0F0A18, "Skyrim.esm" },  // Sovngarde: world, its location, the Hall of Heroes
			{ 0x034240, "Skyrim.esm" },                                                          // Pelagius's mind (The Mind of Madness)
			{ 0x018EE6, "Skyrim.esm" },                                                          // inside Azura's Star (The Black Star)
			{ 0x03D62B, "Skyrim.esm" },                                                          // Misty Grove (A Night to Remember)
			{ 0x001408, "Dawnguard.esm" }, { 0x003B8F, "Dawnguard.esm" }, { 0x00528D, "Dawnguard.esm" },  // the Soul Cairn, its Boneyard
			{ 0x01C0B2, "Dragonborn.esm" }, { 0x016E2B, "Dragonborn.esm" },                      // Apocrypha
			{ 0x0E1592, "ccbgssse067-daedinv.esm" }, { 0x033F39, "ccbgssse067-daedinv.esm" },    // the Deadlands
		};
		auto world = a_player->GetWorldspace();
		for (const auto& place : PLACES) {
			auto form = PluginForm(place.id, place.file);
			if (!form) {
				continue;
			}
			if ((world && form == world) || form == a_player->GetParentCell()) {
				return true;
			}
			for (auto loc = a_player->GetCurrentLocation(); loc; loc = loc->parentLoc) {
				if (form == loc) {
					return true;
				}
			}
		}
		return false;
	}

	// Survival Mode leaves the Deadlands (The Cause) off its planes of Oblivion: it is added to them, so needs pause there
	// as in the other planes (its lists only, as the game loads; nothing saved)
	void DeadlandsIsAPlane()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		auto areas = data->LookupForm<RE::BGSListForm>(0x92E, "ccqdrsse001-survivalmode.esl");      // Survival_OblivionAreas
		auto locations = data->LookupForm<RE::BGSListForm>(0x92D, "ccqdrsse001-survivalmode.esl");  // Survival_OblivionLocations
		auto world = data->LookupForm(0x0E1592, "ccbgssse067-daedinv.esm");                           // ccBGSSSE067DeadlandsWorld
		auto location = data->LookupForm(0x033F39, "ccbgssse067-daedinv.esm");                        // ccBGSSSE067_DeadlandsLocation
		int added = 0;
		if (areas && world && !areas->HasForm(world)) {
			areas->forms.push_back(world);
			++added;
		}
		if (locations && location && !locations->HasForm(location)) {
			locations->forms.push_back(location);
			++added;
		}
		if (added > 0) {
			logger::info("Survival Mode: the Deadlands counted among the planes of Oblivion");
		}
	}

	// whether a camp may be made where you stand indoors (a dungeon camp). Checked against every indoor place of
	// Skyrim, its DLCs and the Creation Club content (2026-10-30):
	// - a place's own kind wins over its town's: a barrow, ship or cave inside a town (Shroud Hearth Barrow in
	//   Ivarstead, the Katariah at Solitude, Gromm's Pass under Riften) is a dungeon; a house, inn or home is never one
	// - every mine counts, worked or not (for consistency), and the Civil War forts (only LocTypeMilitaryFort)
	// - the Ratway counts (Riften's sewers, only marked "habitation")
	// - never: the Thalmor Embassy, the Dawnguard's and the vampires' halls, Sovngarde, the Soul Cairn, Apocrypha
	// - a place the game never gave a kind at all (Japhet's Folly, The Chill...): a dungeon if nobody owns the room
	bool InDungeon(RE::Actor* a_player)
	{
		if (InCavernWorld(a_player)) {
			return true;  // a cavern world is a dungeon all through
		}
		auto cell = a_player ? a_player->GetParentCell() : nullptr;
		if (!cell || !cell->IsInteriorCell()) {
			return false;
		}
		auto form = [](RE::FormID a_id, const char* a_file) { return PluginForm(a_id, a_file); };
		auto has = [](RE::BGSLocation* a_loc, const auto& a_ids) {
			for (const auto id : a_ids) {
				if (auto kw = RE::TESForm::LookupByID<RE::BGSKeyword>(id); kw && a_loc->HasKeyword(kw)) {
					return true;
				}
			}
			return false;
		};
		const std::array<RE::TESForm*, 6> never{ form(0x018C92, "Skyrim.esm"),  // ThalmorEmbassyLocation
			form(0x0192B2, "Skyrim.esm"),                                         // SovngardeLocation
			form(0x004C1F, "Dawnguard.esm"),                                      // DLC1HunterHQLocation (Fort Dawnguard)
			form(0x004C20, "Dawnguard.esm"),                                      // DLC1VampireCastleLocation (Castle Volkihar)
			form(0x003B8F, "Dawnguard.esm"),                                      // DLC1SoulCairnLocation
			form(0x016E2B, "Dragonborn.esm") };                                   // DLC2ApocryphaLocation
		auto ratway = form(0x03B871, "Skyrim.esm");                               // RiftenRatwayLocation
		static constexpr std::array<RE::FormID, 3> home{ 0x1CB85u, 0x1CB87u, 0xFC1A3u };              // house, inn, player home
		static constexpr std::array<RE::FormID, 3> settled{ 0x39793u, 0x13168u, 0x13166u };          // habitation, city, town
		static constexpr std::array<RE::FormID, 4> wild{ 0x130DBu, 0xF5E80u, 0x18EF1u, 0x130E7u };   // dungeon, clearable, mine, fort
		auto own = a_player->GetCurrentLocation();
		for (auto loc = own; loc; loc = loc->parentLoc) {
			if (std::ranges::find(never, static_cast<RE::TESForm*>(loc)) != never.end()) {
				return false;
			}
		}
		if (own) {
			if (has(own, home)) {
				return false;
			}
			if (static_cast<RE::TESForm*>(own) == ratway || has(own, wild)) {
				return true;  // its own kind, whatever town it lies in
			}
		}
		bool dungeon = false;
		bool typed = false;  // any location this cell belongs to said what kind of place it is
		for (auto loc = own; loc; loc = loc->parentLoc) {
			if (has(loc, home) || has(loc, settled)) {
				return false;
			}
			if (has(loc, wild)) {
				dungeon = true;
				typed = true;
			}
		}
		// a place the game never gave a kind (Japhet's Folly's ice caves near Winterhold, The Chill, Wayward Pass...: no
		// keyword at all on their locations): wild if nobody owns it. Owned rooms (someone's house or shop) stay indoors
		if (!typed && !cell->GetOwner()) {
			return true;
		}
		return dungeon;
	}

	// a straight ray between two points meets something solid (walls, rocks, furniture, clutter; not actors or our own pieces)
	bool RayBlocked(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
	{
		auto player = Player();
		auto cell = player ? player->GetParentCell() : nullptr;
		auto world = cell ? cell->GetbhkWorld() : nullptr;
		if (!world) {
			return false;
		}
		const float s = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick;
		pick.rayInput.from = RE::hkVector4(a_from.x * s, a_from.y * s, a_from.z * s, 0.0f);
		pick.rayInput.to = RE::hkVector4(a_to.x * s, a_to.y * s, a_to.z * s, 0.0f);
		pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
		{
			RE::BSReadLockGuard lock(world->worldLock);
			world->PickObject(pick);
		}
		if (!pick.rayOutput.HasHit()) {
			return false;
		}
		const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
		auto ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
		if (ref && ref->IsDynamicForm()) {
			return false;
		}
		using L = RE::COL_LAYER;
		return layer == L::kStatic || layer == L::kAnimStatic || layer == L::kTerrain || layer == L::kGround || layer == L::kTrees ||
		       layer == L::kProps || layer == L::kClutter || layer == L::kDebrisLarge || layer == L::kInvisibleWall;
	}

	// something lies on the floor at a point (a fallen log, a beam, a crate, a pile of bones): looking straight down
	// from a_reach above the floor, the first solid thing met stands more than a_clear above it. (The rays sideways from a
	// piece's middle miss what lies right under it: a ray starting inside a log never meets it)
	bool SomethingOnFloor(const RE::NiPoint3& a_at, float a_floor, float a_clear, float a_reach)
	{
		auto player = Player();
		auto cell = player ? player->GetParentCell() : nullptr;
		auto world = cell ? cell->GetbhkWorld() : nullptr;
		if (!world) {
			return false;
		}
		const float s = RE::bhkWorld::GetWorldScale();
		float top = a_floor + a_reach;
		const float bottom = a_floor - 5.0f;
		using L = RE::COL_LAYER;
		for (int tries = 0; tries < 4 && top > bottom; ++tries) {
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(a_at.x * s, a_at.y * s, top * s, 0.0f);
			pick.rayInput.to = RE::hkVector4(a_at.x * s, a_at.y * s, bottom * s, 0.0f);
			pick.rayInput.filterInfo.SetCollisionLayer(L::kLOS);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) {
				return false;
			}
			const float z = top + (bottom - top) * pick.rayOutput.hitFraction;
			const auto layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
			auto ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			const bool solid = layer == L::kStatic || layer == L::kAnimStatic || layer == L::kTrees || layer == L::kProps ||
			                   layer == L::kClutter || layer == L::kDebrisLarge || layer == L::kGround || layer == L::kTerrain;
			if (solid && !(ref && ref->IsDynamicForm())) {
				return z > a_floor + a_clear;
			}
			top = z - 1.0f;  // something of ours, an actor or a plant: look on below it
		}
		return false;
	}

	struct DungeonPiece
	{
		const char* name;
		float shift, side;   // in the fire's axes: along its front (away from the player) and across it
		float halfX, halfY;  // its footprint: half across and half along
		float turn;          // its heading from the fire's, in degrees
	};

	// the dungeon camp's layouts round the fire, tried in turn: the open one (the bedroll beside the fire, the pot in
	// front) and a line for corridors (pot, fire and bedroll one after another, the bedroll lying
	// along the line), so the camp fits where a corridor is barely wider than the fire
	std::vector<std::vector<DungeonPiece>> DungeonLayouts(bool a_pot)
	{
		std::vector<std::vector<DungeonPiece>> layouts{
			{
				{ "fire", 0.0f, 0.0f, 40.0f, 40.0f, 0.0f },
				{ "bed", 10.0f, 120.0f, 40.0f, 85.0f, 0.0f },
			},
			{
				{ "fire", 0.0f, 0.0f, 40.0f, 40.0f, 0.0f },
				{ "bed", 0.0f, 135.0f, 40.0f, 85.0f, 90.0f },
			},
		};
		if (a_pot) {
			layouts[0].push_back({ "pot", 110.0f, 0.0f, 40.0f, 40.0f, 180.0f });
			layouts[1].push_back({ "pot", 0.0f, -90.0f, 40.0f, 40.0f, 90.0f });
		}
		return layouts;
	}

	void QueueMessageBox(RE::MessageBoxData* a_box)
	{
		RE::MessageBoxMenu::QueueMessage(a_box);
	}

	// how a dungeon camp would fit with its fire at a spot, laid out one way: why not (empty if it fits), and the floor
	// under each piece
	struct DungeonFit
	{
		const char* why = nullptr;
		std::string detail;  // for the log: what failed, and for which piece
		std::vector<float> floors;
		std::vector<std::array<float, 3>> slopes;  // the floor under each piece as a plane: height at its middle, and its rise across and along
		float rise = 0.0f;
	};

	DungeonFit FitDungeonCamp(const std::vector<DungeonPiece>& a_layout, const RE::NiPoint3& a_fire, float a_heading, const RE::NiPoint3& a_me,
		const std::unordered_map<std::string, float>& a_settings)
	{
		DungeonFit fit;
		// natural cavern floors (the cavern worlds) are rougher than built dungeons: a little more rise allowed there
		auto player = Player();
		const float level = InCavernWorld(player) ? CampSetting(a_settings, "dgcavernlevel", 28.0f) : CampSetting(a_settings, "dglevel", 18.0f);
		const float room = CampSetting(a_settings, "dgroom", 0.7f);  // how much of each piece's footprint must be clear
		auto place = [&](float a_shift, float a_side) {
			return RE::NiPoint3{ a_fire.x + a_shift * std::sin(a_heading) + a_side * std::cos(a_heading),
				a_fire.y + a_shift * std::cos(a_heading) - a_side * std::sin(a_heading), a_fire.z };
		};
		float low = std::numeric_limits<float>::max(), high = -std::numeric_limits<float>::max();
		for (const auto& piece : a_layout) {
			const auto c = place(piece.shift, piece.side);
			// not on top of you
			const float dx = c.x - a_me.x, dy = c.y - a_me.y;
			if (std::sqrt(dx * dx + dy * dy) < (std::max)(piece.halfX, piece.halfY) + 30.0f) {
				fit.detail = std::string("on you: ") + piece.name;
				fit.why = "There isn't enough room here to make camp.";
				return fit;
			}
			const float h = a_heading + piece.turn * std::numbers::pi_v<float> / 180.0f;
			float pieceLow = std::numeric_limits<float>::max();
			float sum = 0.0f, sx = 0.0f, sy = 0.0f, sxx = 0.0f, syy = 0.0f;
			std::array<std::array<float, 3>, 5> samples{};
			size_t taken = 0;
			for (const auto& [fx, fy] : { std::pair{ 0.0f, 0.0f }, { -0.8f, -0.8f }, { 0.8f, -0.8f }, { 0.8f, 0.8f }, { -0.8f, 0.8f } }) {
				const float x = fx * piece.halfX, y = fy * piece.halfY;
				const RE::NiPoint3 q{ c.x + x * std::cos(h) + y * std::sin(h), c.y - x * std::sin(h) + y * std::cos(h), a_me.z };
				float ground = 0.0f;
				if (!GroundZ(q, ground, nullptr, nullptr, 40.0f) || std::abs(ground - a_me.z) > 60.0f) {
					fit.detail = std::string("no floor: ") + piece.name;
					fit.why = "There isn't enough room here to make camp.";
					return fit;
				}
				low = (std::min)(low, ground);
				high = (std::max)(high, ground);
				pieceLow = (std::min)(pieceLow, ground);
				samples[taken++] = { x, y, ground };
				sum += ground;
				sx += x * ground;
				sy += y * ground;
				sxx += x * x;
				syy += y * y;
			}
			fit.floors.push_back(pieceLow);
			// the plane through the samples (they are balanced round the middle, so across and along solve apart)
			fit.slopes.push_back({ sum / 5.0f, sxx > 1.0f ? sx / sxx : 0.0f, syy > 1.0f ? sy / syy : 0.0f });
			// a slope a piece can lie along is fine; a step, a rock or a crack under it is not: no point of the floor under it
			// may stand off that slope by more than a little (dgbump)
			const auto& [mid, across, along] = fit.slopes.back();
			for (const auto& [x, y, g] : samples) {
				if (std::abs(g - (mid + across * x + along * y)) > CampSetting(a_settings, "dgbump", 6.0f)) {
					fit.detail = std::string("bumpy: ") + piece.name;
					fit.why = "The floor here is too uneven to make camp.";
					return fit;
				}
			}
		}
		fit.rise = high - low;
		if (fit.rise > level) {
			fit.detail = "uneven";
			fit.why = "The floor here is too uneven to make camp.";
			return fit;
		}
		// room round each piece: nothing solid across most of its footprint, at knee and chest height
		for (size_t i = 0; i < a_layout.size(); ++i) {
			const auto& piece = a_layout[i];
			auto c = place(piece.shift, piece.side);
			const float h = a_heading + piece.turn * std::numbers::pi_v<float> / 180.0f;
			for (const float up : { 25.0f, 80.0f }) {
				c.z = fit.floors[i] + up;
				for (int d = 0; d < 8; ++d) {
					const float a = d * std::numbers::pi_v<float> / 4.0f;
					const float x = std::sin(a) * piece.halfX * room, y = std::cos(a) * piece.halfY * room;
					const RE::NiPoint3 to{ c.x + x * std::cos(h) + y * std::sin(h), c.y - x * std::sin(h) + y * std::cos(h), c.z };
					if (RayBlocked(c, to)) {
						fit.detail = std::string("blocked: ") + piece.name;
						fit.why = "There isn't enough room here to make camp.";
						return fit;
					}
				}
			}
		}
		// nothing lying on the floor under a piece: looked at straight down over a grid of points across its footprint
		for (size_t i = 0; i < a_layout.size(); ++i) {
			const auto& piece = a_layout[i];
			const auto c = place(piece.shift, piece.side);
			const float h = a_heading + piece.turn * std::numbers::pi_v<float> / 180.0f;
			const auto& [mid, across, along] = fit.slopes[i];
			for (const float fx : { -0.9f, -0.45f, 0.0f, 0.45f, 0.9f }) {
				for (const float fy : { -0.9f, -0.45f, 0.0f, 0.45f, 0.9f }) {
					const float x = fx * piece.halfX, y = fy * piece.halfY;
					const RE::NiPoint3 q{ c.x + x * std::cos(h) + y * std::sin(h), c.y - x * std::sin(h) + y * std::cos(h), 0.0f };
					if (SomethingOnFloor(q, mid + across * x + along * y, CampSetting(a_settings, "dgclear", 10.0f), CampSetting(a_settings, "dgclearreach", 110.0f))) {
						fit.detail = std::string("something on the floor: ") + piece.name;
						fit.why = "There isn't enough room here to make camp.";
						return fit;
					}
				}
			}
		}
		// the fire needs room overhead
		const auto f = place(0.0f, 0.0f);
		if (RayBlocked({ f.x, f.y, fit.floors[0] + 20.0f }, { f.x, f.y, fit.floors[0] + CampSetting(a_settings, "dgheadroom", 220.0f) })) {
			fit.detail = "ceiling";
			fit.why = "The ceiling is too low here for a campfire.";
		}
		return fit;
	}

	constexpr RE::FormID COLD_CAMPFIRE = 0xA05;  // FreeCooking.esp: FC_ColdCampfire, a dungeon camp's fire laid but not lit

	// A camp's fire burns for a while on its 1 Firewood (fireburnhours, 7 game hours: a night's sleep always ends with
	// a cold fire, a nap or an evening of cooking does not), then goes out: the cold fire takes its place and the pot
	// stops. Each burning fire and when it was lit are kept in the save
	std::unordered_map<RE::FormID, float> g_burningFires;  // fire reference -> game days passed when it was lit

	void FireLit(RE::TESObjectREFR* a_fire)
	{
		if (a_fire) {
			g_burningFires[a_fire->GetFormID()] = GameDays();
		}
	}

	void BlockCampPots(RE::TESObjectREFR* a_near, bool a_blocked, std::uint32_t a_camp = 0);

	// the fire has burned out: its flames, light and crackle go, the cold fire stands in its place
	void PutOutFire(RE::TESObjectREFR* a_fire)
	{
		auto tes = RE::TES::GetSingleton();
		if (!a_fire || !tes) {
			return;
		}
		const bool indoors = Underground(a_fire);
		// (its camp, read before its parts go: a dungeon camp's fire is deleted below and forgotten with it)
		const auto camp = CampOf(a_fire);
		const auto at = a_fire->GetPosition();
		const float heading = a_fire->GetAngleZ();
		// (the fire's own parts, recorded by its camp: never anything else standing there)
		std::vector<RE::TESObjectREFR*> parts{ a_fire };
		for (auto ref : FireParts(a_fire, false)) {
			if (!ref->IsDisabled() && RoleOf(ref) != PieceRole::kColdFire) {
				parts.push_back(ref);
			}
		}
		if (auto coldBase = PluginItem(0xA05)) {
			if (auto cold = PlaceCampPiece(a_fire, coldBase, 0, PieceRole::kColdFire)) {
				// its ash bed a little into the ground: flush on it, the two flickered
				MoveRef(cold.get(), { at.x, at.y, at.z - CampSetting(CampSettings(), "coldsink", 2.0f) }, { 0.0f, 0.0f, heading });
				ScaleCampPiece(cold, indoors ? CampSetting(CampSettings(), "dgfirescale", 0.7f) : CampSetting(CampSettings(), "coldscale", 1.0f));
			}
		}
		for (auto part : parts) {
			HideCampPiece(part);
			// a dungeon camp's fire and our own crackle are ours to take away; Camping's own fire outside is only put
			// away (Camping takes it when the camp is packed up, and lighting the cold fire brings it back) (only
			// pieces we placed: Camping's own crackle, recorded with its camp, is hidden like its fire, not deleted)
			if (IsCampOwned(part) && (indoors || (part->GetBaseObject() && part->GetBaseObject()->GetFormID() == 0x00037E3E))) {
				DeleteCampRef(part);
			}
		}
		BlockCampPots(a_fire, true, camp);
		RE::SendHUDMessage::ShowHUDMessage(Tr("The campfire has burned out."));
		logger::info("Camp: a fire burned out ({} pieces put away)", parts.size());
	}

	void WatchBurningFires()
	{
		RepeatAlways(2000, []() {
			if (g_loadingSave) {
				return true;
			}
			if (g_burningFires.empty()) {
				return true;
			}
			const float burn = CampSetting(CampSettings(), "fireburnhours", 7.0f) / 24.0f;
			const float now = GameDays();
			for (auto it = g_burningFires.begin(); it != g_burningFires.end();) {
				auto fire = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->first);
				if (!fire) {
					// its area is away (a placed fire isn't in memory then): kept, unless it was lit long ago (the fire goes out
					// when you come back)
					it = now - it->second > 60.0f ? g_burningFires.erase(it) : std::next(it);
				} else if (fire->IsDisabled() || fire->IsMarkedForDeletion() || fire->IsDeleted() || RoleOf(fire) != PieceRole::kBurningFire) {
					// packed up, or out already; or no camp records this fire any more (its camp was packed up, or the
					// number now belongs to another mod's fire with the same base: only a recorded fire is put out)
					it = g_burningFires.erase(it);
				} else if (now - it->second >= burn && fire->Is3DLoaded()) {
					PutOutFire(fire);  // (a fire far away goes out when you come back to it)
					it = g_burningFires.erase(it);
				} else {
					++it;
				}
			}
			return true;
		});
	}

	// a camp's burning fire: Camping's campfire, smaller indoors (dgfirescale), with its light and crackle
	RE::TESObjectREFR* PlaceDungeonFire(RE::TESObjectREFR* a_near, const RE::NiPoint3& a_at, float a_heading, std::uint32_t a_camp = 0)
	{
		const bool indoors = Underground(a_near);
		const float k = indoors ? CampSetting(CampSettings(), "dgfirescale", 0.7f) : 1.0f;
		auto put = [&](RE::TESForm* a_base, const RE::NiPoint3& a_spot) -> RE::TESObjectREFR* {
			auto base = a_base && a_base->IsBoundObject() ? static_cast<RE::TESBoundObject*>(a_base) : nullptr;
			// (owned outdoors, where a cold fire relit this way is part of a camp: packed up with it, far away too)
			auto ref = base ? PlaceCampPiece(a_near, base, a_camp) : RE::NiPointer<RE::TESObjectREFR>();
			if (ref) {
				MoveRef(ref.get(), a_spot, { 0.0f, 0.0f, a_heading });
			}
			return ref.get();
		};
		auto f = put(CampingForm(CAMP_FIRE), a_at);
		if (f && k != 1.0f) {
			ScaleCampPiece(f, k);
		}
		put(RE::TESForm::LookupByID(CAMPFIRE_LIGHT), { a_at.x, a_at.y, a_at.z + 60.0f * k });
		put(RE::TESForm::LookupByID(FIRE_LOOP), { a_at.x, a_at.y, a_at.z + 20.0f });
		return f;
	}

	const char* GatherText(RE::TESObjectREFR* a_where);
	void BlockCampPots(RE::TESObjectREFR* a_near, bool a_blocked, std::uint32_t a_camp);

	// lights a cold campfire (the burning fire takes its place): fed 1 Firewood, or with what an hour's search turned up
	void LightColdCampfireWith(RE::ObjectRefHandle a_cold, bool a_gathered)
	{
		auto cold = a_cold.get();
		auto player = Player();
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		// only a cold fire a camp recorded is lit (and taken away): never another mod's copy of it. Its camp is read
		// now: the cold fire is forgotten when it goes, before the ignite sound is placed
		const auto camp = cold ? CampOf(cold.get()) : 0;
		if (!cold || !player || !firewood || !firewood->IsBoundObject() || !IsCampOwned(cold.get()) || !camp) {
			return;
		}
		std::string told = Tr("You light the fire. It used 1 Firewood.");
		if (a_gathered) {
			if (auto axe = RE::TESForm::LookupByID(0x0002F2F4); !axe || ItemCount(player, axe) < 1) {
				RE::SendHUDMessage::ShowHUDMessage(Tr("You need a Woodcutter's Axe to gather something to burn."));
				return;
			}
			// an hour goes by
			if (auto calendar = RE::Calendar::GetSingleton(); calendar && calendar->gameHour && calendar->gameDaysPassed) {
				calendar->gameDaysPassed->value += 1.0f / 24.0f;
				calendar->gameHour->value += 1.0f;
				if (calendar->gameHour->value >= 24.0f) {
					calendar->gameHour->value -= 24.0f;
					// past midnight: the date is kept in its own globals, which the hour never moves (the weekday
					// follows GameDaysPassed, advanced above). Months are 0 (Morning Star) to 11 (Evening Star).
					if (calendar->gameDay && calendar->gameMonth && calendar->gameYear) {
						static constexpr int DAYS_IN_MONTH[12]{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
						const int month = std::clamp(static_cast<int>(calendar->gameMonth->value), 0, 11);
						calendar->gameDay->value += 1.0f;
						if (calendar->gameDay->value > static_cast<float>(DAYS_IN_MONTH[month])) {
							calendar->gameDay->value = 1.0f;
							if (month == 11) {
								calendar->gameMonth->value = 0.0f;
								calendar->gameYear->value += 1.0f;
							} else {
								calendar->gameMonth->value = static_cast<float>(month + 1);
							}
						}
					}
				}
			}
			told = TrF("{0} The fire catches.", Tr(GatherText(cold.get())));
		} else {
			if (ItemCount(player, firewood) < 1) {
				RE::SendHUDMessage::ShowHUDMessage(Tr("You need 1 Firewood to light the fire."));
				return;
			}
			player->RemoveItem(static_cast<RE::TESBoundObject*>(firewood), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		// an outdoor camp's own fire was only put away (LightOrLayCampFire): it comes back, so Camping still knows it;
		// a dungeon camp's fire is placed new
		// (only the camp's recorded pieces: something else hidden there is never shown)
		std::vector<RE::TESObjectREFR*> hidden;
		RE::TESObjectREFR* hiddenFire = nullptr;
		for (auto ref : FireParts(cold.get(), true)) {
			if (ref->IsDisabled()) {
				hidden.push_back(ref);
				if (RoleOf(ref) == PieceRole::kBurningFire) {
					hiddenFire = ref;
				}
			}
		}
		if (hiddenFire) {
			for (auto ref : hidden) {
				ShowCampPiece(ref);
			}
			FireLit(hiddenFire);
			// its crackle (Camping played it once, silenced while the fire was cold): a sound marker of ours
			if (auto loop = RE::TESForm::LookupByID(FIRE_LOOP); loop && loop->IsBoundObject()) {
				if (auto marker = PlaceCampPiece(cold.get(), static_cast<RE::TESBoundObject*>(loop), 0, PieceRole::kCrackle)) {
					auto at = cold->GetPosition();
					at.z += 20.0f;
					MoveRef(marker.get(), at, {});
				}
			}
		} else {
			FireLit(PlaceDungeonFire(cold.get(), cold->GetPosition(), cold->GetAngleZ()));
		}
		BlockCampPots(cold.get(), false);  // the pot cooks again
		DeleteCampRef(cold.get());
		// the fire catching: Camping's own ignite sound at the fire (back to normal first: it is silenced while a camp
		// goes up), its marker taken away once it has played
		if (auto ignite = CampingForm(IGNITE_SOUND); ignite && ignite->IsBoundObject()) {
			SilenceSound(ignite, false);
			// (owned outdoors: a save within the 10 s left it for good, its job dropped by the load; now the pack-up
			// takes it then)
			if (auto marker = PlaceCampPiece(cold.get(), static_cast<RE::TESBoundObject*>(ignite), camp)) {
				MoveRef(marker.get(), cold->GetPosition(), {});
				const auto handle = marker->GetHandle();
				After(10000, [handle]() {
					if (auto m = handle.get()) {
						DeleteCampRef(m.get());
					}
				});
			}
		}
		RE::SendHUDMessage::ShowHUDMessage(told.c_str());
	}

	// the crafting menu of a camp's pot over a cold fire closes, if the player sits at that pot (main thread)
	void CloseCampPotMenu(RE::TESObjectREFR* a_pot)
	{
		auto player = Player();
		auto seat = player ? player->GetOccupiedFurniture().get() : nullptr;
		auto ui = RE::UI::GetSingleton();
		if (!a_pot || !seat || seat.get() != a_pot || !ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
			return;
		}
		RE::SendHUDMessage::ShowHUDMessage(Tr("The fire isn't lit. Feed it 1 Firewood to cook."));
		if (auto queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::CraftingMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
		logger::info("Camp: the pot's crafting menu closed, its fire is out");
	}

	// a camp's cooking pot near a fire: usable only while the fire burns (a blocked pot does nothing when used, so no
	// one sits down to cook; using it still tells why: BedrollHook)
	void BlockCampPots(RE::TESObjectREFR* a_near, bool a_blocked, std::uint32_t a_camp)
	{
		const auto camp = a_camp ? a_camp : CampOf(a_near);
		// the pot is a crafting seat: blocking its activation did not keep you from sitting down at it, so over a cold
		// fire it is put away and an activator with its model stands in (using it says why nothing cooks)
		auto tes = RE::TES::GetSingleton();
		auto potBase = PluginItem(POT_PLACED);
		auto coldBase = PluginItem(POT_COLD);
		if (!tes || !a_near || !potBase || !coldBase || !coldBase->IsBoundObject()) {
			return;
		}
		std::vector<RE::TESObjectREFR*> pots, stands;
		// the camp's own pots and stand-ins, from its record: wherever the layout settings put them (a pot at the far end
		// of the sliders stood beyond the old 300-unit search)
		if (!camp) {
			return;
		}
		for (const auto& [id, piece] : g_campOwned) {
			if (piece.camp != camp) {
				continue;
			}
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			if (!SameRef(ref, piece.base)) {
				continue;
			}
			if (IsCampBase(ref->GetBaseObject(), POT_PLACED) && ref->IsDisabled() != a_blocked) {
				pots.push_back(ref);
			} else if (IsCampBase(ref->GetBaseObject(), POT_COLD) && !ref->IsDisabled()) {
				stands.push_back(ref);
			}
		}
		logger::info("Camp: {} pots {} ({} stand-ins)", pots.size(), a_blocked ? "put away over a cold fire" : "back over the fire", stands.size());
		if (a_blocked) {
			for (auto pot : pots) {
				if (auto cold = PlaceCampPiece(pot, static_cast<RE::TESBoundObject*>(coldBase), 0, PieceRole::kPotStandIn)) {
					MoveRef(cold.get(), pot->GetPosition(), pot->GetAngle());
					ScaleCampPiece(cold, pot->GetScale());
				}
				HideCampPiece(pot);
				CloseCampPotMenu(pot);  // (sitting at it, cooking: the menu closes as the fire goes out)
			}
		} else {
			for (auto pot : pots) {
				ShowCampPiece(pot);
			}
			for (auto cold : stands) {
				DeleteCampRef(cold);
			}
		}
	}

	// an outdoor camp just put up: its fire uses 1 Firewood, or with none it is laid cold (Camping's fire, its light,
	// sound and flames give way to the cold one)
	std::atomic<bool> g_pitchLit{ true };  // the camp going up has Firewood for its fire (DrinkPotion)

	void LightOrLayCampFire(RE::TESObjectREFR* a_campfire)
	{
		auto player = Player();
		auto tes = RE::TES::GetSingleton();
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		if (!a_campfire || !player || !tes || !firewood || !firewood->IsBoundObject() || a_campfire->IsDisabled() || a_campfire->IsMarkedForDeletion() ||
			Underground(a_campfire)) {
			return;  // no camp after all (refused), or a dungeon camp's fire (paid for as it was made)
		}
		if (g_pitchLit && ItemCount(player, firewood) >= 1) {
			player->RemoveItem(static_cast<RE::TESBoundObject*>(firewood), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			FireLit(a_campfire);
			RE::SendHUDMessage::ShowHUDMessage(Tr("The fire used 1 Firewood."));
			return;
		}
		const auto fire = a_campfire->GetPosition();
		// (Camping's recorded pieces over its fire: its light and crackle)
		std::vector<RE::TESObjectREFR*> parts{ a_campfire };
		for (auto ref : FireParts(a_campfire, true)) {
			if (!ref->IsDisabled() && (RoleOf(ref) == PieceRole::kLight || RoleOf(ref) == PieceRole::kCrackle)) {
				parts.push_back(ref);
			}
		}
		if (auto coldBase = PluginItem(COLD_CAMPFIRE)) {
			if (auto cold = PlaceOwned(a_campfire, coldBase, 0, PieceRole::kColdFire)) {
				// its ash bed a little into the ground: flush on it, the two flickered
				MoveRef(cold.get(), { fire.x, fire.y, fire.z - CampSetting(CampSettings(), "coldsink", 2.0f) }, { 0.0f, 0.0f, a_campfire->GetAngleZ() });
				ScaleCampPiece(cold, CampSetting(CampSettings(), "coldscale", 1.0f));  // Camping's own ring, its flames out
			}
		}
		for (auto part : parts) {
			HideCampPiece(part);  // Camping takes its own pieces away when the camp is packed up
		}
		BlockCampPots(a_campfire, true);
		RE::SendHUDMessage::ShowHUDMessage(Tr("The fire pit is ready to light."));
		logger::info("Camp: the fire pit ready, waiting to be lit by hand ({} pieces of the burning fire put away)", parts.size());
	}

	// Underground, a cold fire can also be lit with whatever there is to burn, gathered over an hour (no Firewood used):
	// what you find is told by the kind of place
	const char* GatherText(RE::TESObjectREFR* a_where)
	{
		struct Kind
		{
			RE::FormID keyword;
			const char* text;
		};
		static constexpr Kind KINDS[]{
			{ 0x18EF1, "You split old planks into kindling." },                  // mine
			{ 0x130E3, "You scrape oily grime from the machinery." },        // Dwemer ruin
			{ 0x130E4, "You gather dry fungus and twisted roots." },                              // Falmer hive
			{ 0x130E2, "You gather old burial linen and coffin wood." },                                     // draugr crypt
			{ 0x130E1, "You gather old burial linen and coffin wood." },                                     // dragon priest lair
			{ 0x130E7, "You break up an old barrel and some planks." },                  // fort
			{ 0x130EC, "You break up an old stool and some book covers." },                                           // warlock lair
			{ 0x130EB, "You gather candle stubs and scraps of old cloth." },                                               // vampire lair
			{ 0x130E6, "You gather twigs and feathers from the nest." },                                         // hagraven nest
			{ 0x130DE, "You gather dry roots and bones from the den." },                              // animal den
			{ 0x130ED, "You gather dry bones and branches from the lair." },                                 // werewolf lair
			{ 0x130EA, "You gather branches and dry leaves." },                                         // spriggan grove
			{ 0x1929F, "You break up a crate and some loose planks." },    // shipwreck
		};
		// from the DLCs and the Creation Club, by their own files: Dragonborn's werebear lairs, The Cause's Ayleid ruins
		struct PluginKind
		{
			RE::FormID keyword;
			const char* file;
			const char* text;
		};
		static constexpr PluginKind PLUGIN_KINDS[]{
			{ 0x02C96A, "Dragonborn.esm", "You gather dry moss and bark from the cave." },
			{ 0x000D61, "ccbgssse067-daedinv.esm", "You gather dry roots from the white stone." },
		};
		for (auto loc = a_where ? a_where->GetCurrentLocation() : nullptr; loc; loc = loc->parentLoc) {
			for (const auto& kind : KINDS) {
				if (auto kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kind.keyword); kw && loc->HasKeyword(kw)) {
					return kind.text;
				}
			}
			for (const auto& kind : PLUGIN_KINDS) {
				if (auto kw = PluginForm<RE::BGSKeyword>(kind.keyword, kind.file); kw && loc->HasKeyword(kw)) {
					return kind.text;
				}
			}
		}
		return InCavernWorld(a_where) ? "You gather dry fungus stalks from the cavern." :
		                                "You gather enough dry scraps to burn.";
	}

	// lights a cold fire: with 1 Firewood, or (underground) with what an hour's search turns up
	void LightColdCampfireWith(RE::ObjectRefHandle a_cold, bool a_gathered);

	// the screen fades to black and back, as Camping's own does when it puts a camp up (its BuildCampQuestScript:
	// FadeToBlackImod for 2.8 seconds, held black by FadeToBlackHoldImod while the work is done, then FadeToBlackBackImod
	// after 1 second; Skyrim.esm's own image space modifiers, started from here as Papyrus's Apply and PopTo do).
	// a_whileDark runs on the game's thread once the screen is black
	// the fade holds the player's controls (moving, fighting, menus, activating) while the screen is black
	constexpr auto FADE_HELD = static_cast<RE::UserEvents::USER_EVENT_FLAG>(
		std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kMovement) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kFighting) |
		std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kMenu) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kActivate));
	std::atomic<bool> g_fadeHoldsControls{ false };

	// the fade's end: controls given back and its screen effects stopped. Also when a save loads mid-fade (its timers are
	// dropped then), so nobody is left without controls or under a black screen
	void EndFade()
	{
		for (const RE::FormID id : { 0x000F756D, 0x000F756E, 0x000F756F }) {  // FadeToBlack, FadeToBlackHold, FadeToBlackBack
			if (auto m = RE::TESForm::LookupByID<RE::TESImageSpaceModifier>(id)) {
				RE::ImageSpaceModifierInstanceForm::Stop(m);
			}
		}
		if (g_fadeHoldsControls.exchange(false)) {
			if (auto controls = RE::ControlMap::GetSingleton()) {
				controls->ToggleControls(FADE_HELD, true, false);
			}
			logger::info("Camp: controls given back (save loaded during a fade)");
		}
	}

	void FadeThroughBlack(std::function<void()> a_whileDark)
	{
		auto imod = [](RE::FormID a_id) { return RE::TESForm::LookupByID<RE::TESImageSpaceModifier>(a_id); };
		auto out = imod(0x000F756D), hold = imod(0x000F756E), back = imod(0x000F756F);  // FadeToBlack, FadeToBlackHold, FadeToBlackBack
		if (!out || !hold || !back) {
			a_whileDark();
			return;
		}
		// your controls held as Camping holds them (DisablePlayerControls: moving, fighting, menus, activating), so
		// nobody walks off while the screen is black
		if (auto controls = RE::ControlMap::GetSingleton()) {
			controls->ToggleControls(FADE_HELD, false, false);
			g_fadeHoldsControls = true;
		}
		// the menu you used the supplies from closes, as with Camping (its DisablePlayerControls closes them): left open,
		// the Favorites menu paused the game and the fade with it
		if (auto queue = RE::UIMessageQueue::GetSingleton()) {
			for (const auto name : { RE::FavoritesMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME, RE::TweenMenu::MENU_NAME }) {
				queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		}
		RE::ImageSpaceModifierInstanceForm::Trigger(out, 1.0f, nullptr);
		After(2800, [out, hold, back, a_whileDark]() {
			RE::ImageSpaceModifierInstanceForm::Stop(out);
			RE::ImageSpaceModifierInstanceForm::Trigger(hold, 1.0f, nullptr);
			a_whileDark();
			After(1000, [hold, back]() {
				RE::ImageSpaceModifierInstanceForm::Stop(hold);
				RE::ImageSpaceModifierInstanceForm::Trigger(back, 1.0f, nullptr);
				if (g_fadeHoldsControls.exchange(false)) {
					if (auto controls = RE::ControlMap::GetSingleton()) {
						controls->ToggleControls(FADE_HELD, true, false);
					}
				}
			});
		});
	}

	class LightFireBox : public RE::IMessageBoxCallback
	{
	public:
		LightFireBox(RE::ObjectRefHandle a_cold, std::vector<int> a_choices) :
			cold(a_cold), choices(std::move(a_choices)) {}
		void Run(std::uint8_t a_button) override
		{
			if (a_button >= choices.size()) {
				return;
			}
			const int choice = choices[a_button];  // 1: feed it Firewood, 2: gather something to burn, 0: leave it
			if (choice == 0) {
				return;
			}
			const auto handle = cold;
			if (choice == 2) {
				// an hour of searching: the screen fades out and back as when the camp went up
				OnMain([handle]() { FadeThroughBlack([handle]() { LightColdCampfireWith(handle, true); }); });
				return;
			}
			OnMain([handle, choice]() { LightColdCampfireWith(handle, choice == 2); });
		}
		RE::ObjectRefHandle cold;
		std::vector<int> choices;
	};

	void AskToLightFire(RE::TESObjectREFR* a_cold)
	{
		auto factory = RE::MessageDataFactoryManager::GetSingleton();
		auto strings = RE::InterfaceStrings::GetSingleton();
		auto creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
		auto box = creator ? creator->Create() : nullptr;
		auto player = Player();
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		if (!box || !player) {
			return;
		}
		const auto wood = firewood ? ItemCount(player, firewood) : 0;
		const bool underground = Underground(a_cold);
		// gathering needs the Woodcutter's Axe, as chopping wood does (Skyrim.esm: Axe01)
		auto axe = RE::TESForm::LookupByID(0x0002F2F4);
		const bool hasAxe = axe && ItemCount(player, axe) > 0;
		std::vector<int> choices;
		// (gathering is only for when you carry no Firewood: with wood to hand, nobody spends an hour scraping roots)
		box->bodyText = Tr(underground && !hasAxe && wood == 0 ? "The fire pit is ready to light. You need a Woodcutter's Axe to gather fuel." :
		                                                         "The fire pit is ready to light.");
		if (wood > 0) {
			box->buttonText.push_back(Tr("Feed it 1 Firewood"));
			choices.push_back(1);
		}
		if (underground && hasAxe && wood == 0) {
			box->buttonText.push_back(Tr("Gather something to burn (1 hour)"));
			choices.push_back(2);
		}
		if (choices.empty()) {
			box->bodyText = Tr(underground ? "The fire needs 1 Firewood, or a Woodcutter's Axe to gather fuel." :
			                                 "The fire needs 1 Firewood to burn, and you have none.");
		}
		box->buttonText.push_back(Tr(choices.empty() ? "OK" : "Leave it"));
		choices.push_back(0);
		box->callback = RE::make_smart<LightFireBox>(a_cold->GetHandle(), std::move(choices));
		QueueMessageBox(box);
		// the first time gathering is offered: the one-time tutorial on what it does (an hour passes; Basics.inl)
		if (underground && hasAxe && wood == 0) {
			ShowTutorialOnce(HINT_GATHER, true);
		}
	}

	// makes the dungeon camp where the player stands, or says why not (on the game's own thread). The fire goes a
	// little ahead of you; each layout is tried turned every way round it, and the fire a little nearer or further
	// round you, until one fits
	// (a_faded: the second pass, with the screen black: the first only finds a fit, then fades out and builds it, as
	// Camping fades when it puts a camp up; no fit, no fade, just why)
	int CampAlreadyHere(RE::Actor* a_player);  // (MenusAndHooks.inl)

	void MakeDungeonCamp(int a_suppliesBefore, bool a_faded = false)
	{
		struct Done
		{
			bool waiting = false;  // (the first pass hands over to the fade's)
			~Done()
			{
				if (!waiting) {
					g_dungeonPitching = false;
				}
			}
		} done;
		auto player = Player();
		auto supplies = CampingForm(CAMP_SUPPLIES);
		if (!player || !supplies) {
			return;
		}
		// not on top of a dungeon camp already standing (asked here, on the main thread, where the camp record lives; the
		// supplies were kept by the hook)
		if (!a_faded && CampAlreadyHere(player) == 1) {
			RE::SendHUDMessage::ShowHUDMessage(Tr("There is already a camp here."));
			return;
		}
		// no trees underground: the fire's 1 Firewood is used when it is lit (packing up costs nothing). Without wood the
		// camp goes up with its fire laid but cold, to be fed 1 Firewood later (LightColdCampfire)
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		const bool lit = false;  // every camp goes up with its fire laid cold: you light it when you want it
		(void)firewood;
		const auto settings = CampSettings();
		const bool pot = PluginItem(POT_PLACED) && StandBase() && CampHasCookware();
		const auto layouts = DungeonLayouts(pot);
		const auto me = player->GetPosition();
		const float pi = std::numbers::pi_v<float>;
		const float facing = player->GetAngleZ();
		const std::vector<DungeonPiece>* chosen = nullptr;
		RE::NiPoint3 fire{};
		float fh = 0.0f;
		DungeonFit fit;
		const char* why = "There isn't enough room here to make camp.";
		int tried = 0;
		std::map<std::string, int> fails;
		for (const auto& layout : layouts) {
			for (const float dist : { 120.0f, 90.0f, 160.0f }) {
				// where the fire goes: ahead of you first, then further round to either side
				for (const float off : { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, pi }) {
					const float a = facing + off;
					const RE::NiPoint3 at{ me.x + dist * std::sin(a), me.y + dist * std::cos(a), me.z };
					// the layout turned every way round the fire (in eighths), facing you first
					for (int k = 0; k < 8; ++k) {
						const float h = a + k * pi / 4.0f;
						++tried;
						auto f = FitDungeonCamp(layout, at, h, me, settings);
						if (!f.why) {
							chosen = &layout;
							fire = at;
							fh = h;
							fit = std::move(f);
							goto found;
						}
						++fails[f.detail];
						// the most telling reason: uneven floor or low ceiling over "no room"
						if (std::string_view(f.why) != "There isn't enough room here to make camp.") {
							why = f.why;
						}
					}
				}
			}
		}
	found:
		if (!chosen) {
			KeepSupplies(why, a_suppliesBefore);
			std::string summary;
			for (const auto& [what, n] : fails) {
				summary += std::format("{} x{}; ", what, n);
			}
			logger::info("Camp: no dungeon camp fits here ({} ways tried): {} [{}]", tried, why, summary);
			return;
		}
		if (!a_faded) {
			done.waiting = true;
			FadeThroughBlack([a_suppliesBefore]() { MakeDungeonCamp(a_suppliesBefore, true); });
			return;
		}
		auto place = [&](float a_shift, float a_side) {
			return RE::NiPoint3{ fire.x + a_shift * std::sin(fh) + a_side * std::cos(fh), fire.y + a_shift * std::cos(fh) - a_side * std::sin(fh), fire.z };
		};
		// the camp: the supplies are used (the game may already have taken them)
		if (a_suppliesBefore > 0 && static_cast<int>(ItemCount(player, supplies)) >= a_suppliesBefore) {
			player->RemoveItem(static_cast<RE::TESBoundObject*>(supplies), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		if (lit) {
			player->RemoveItem(static_cast<RE::TESBoundObject*>(firewood), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		// each piece lies on the floor under it, tilted to its slope (on the plane through the floor there, not sunk to
		// its lowest point: the pieces sank into the higher side)
		// every piece recorded with this camp's own number: packed up by it, never by kind or by where it stands
		RE::NiPoint3 tilt{};
		const auto camp = NewDungeonCampNumber();
		auto put = [&](RE::TESForm* a_base, const RE::NiPoint3& a_at, float a_heading) -> RE::TESObjectREFR* {
			auto base = a_base && a_base->IsBoundObject() ? static_cast<RE::TESBoundObject*>(a_base) : nullptr;
			auto ref = base ? PlaceOwned(player, base, camp) : RE::NiPointer<RE::TESObjectREFR>();
			if (ref) {
				MoveRef(ref.get(), a_at, { tilt.x, tilt.y, a_heading });
			}
			return ref.get();
		};
		const auto& layout = *chosen;
		for (size_t i = 0; i < layout.size(); ++i) {
			const auto& piece = layout[i];
			auto at = place(piece.shift, piece.side);
			const auto& [mid, across, along] = fit.slopes[i];
			at.z = mid - 0.5f;
			const float h = fh + piece.turn * pi / 180.0f;
			const std::string_view name = piece.name;
			// (as the chopping block: a positive angle.x tips the front down, a positive angle.y lifts the right side)
			constexpr float limit = 0.3f;
			tilt = { g_pitchSign * std::clamp(-std::atan(along), -limit, limit), g_rollSign * std::clamp(std::atan(across), -limit, limit), 0.0f };
			if (name == "fire") {
				tilt = {};  // the fire's ring stays level (its stones sit into the floor a little)
			}
			if (name == "fire") {
				if (lit) {
					FireLit(PlaceDungeonFire(player, at, h, camp));
				} else if (auto cold = put(PluginItem(COLD_CAMPFIRE), at, h)) {
					ScaleCampPiece(cold, CampSetting(settings, "dgfirescale", 0.7f));  // the same size as the burning fire indoors
				}
			} else if (name == "bed") {
				if (auto rug = PluginItem(RUG_STRAW)) {
					if (auto mat = put(rug, { at.x, at.y, at.z + 0.2f }, h + CampSetting(settings, "rugturn", 90.0f) * pi / 180.0f)) {
						if (const float k = CampSetting(settings, "dgrugscale", 1.0f); k != 1.0f) {
							ScaleCampPiece(mat, k);
						}
					}
				}
				put(RE::TESForm::LookupByID(DUNGEON_BEDROLL), { at.x, at.y, at.z + 0.8f }, h + CampSetting(settings, "dgbedturn", 0.0f) * pi / 180.0f);
			} else if (name == "pot") {
				put(StandBase(), at, h);
				if (auto potRef = put(PluginItem(POT_PLACED), at, h); potRef && !lit) {
					BlockCampPots(potRef, true);  // no cooking over a cold fire
				}
			}
		}
		RE::SendHUDMessage::ShowHUDMessage(Tr(!lit ? "You make camp. The fire is ready to light." : "You make camp. The fire used 1 Firewood."));
		logger::info("Camp: a dungeon camp made, layout {} of {} ({} ways tried, floor rises {:.0f})", chosen == &layouts[0] ? 1 : 2, layouts.size(), tried, fit.rise);
	}

	// a bedroll of a dungeon camp: one a dungeon camp recorded
	bool IsDungeonCampBed(RE::TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->GetBaseObject() && a_ref->GetBaseObject()->GetFormID() == DUNGEON_BEDROLL && IsDungeonCampNumber(CampOf(a_ref));
	}

	// packs up the dungeon camp of its bedroll: every piece it recorded goes and the supplies come back (its fire's wood
	// was paid when lit)
	void PackUpDungeonCamp(RE::ObjectRefHandle a_bed)
	{
		auto bed = a_bed.get();
		auto player = Player();
		auto supplies = CampingForm(CAMP_SUPPLIES);
		const auto camp = bed ? CampOf(bed.get()) : 0;
		if (!bed || !player || !supplies || !IsDungeonCampNumber(camp)) {
			return;
		}
		RemoveCampPieces(camp);
		player->AddObjectToContainer(static_cast<RE::TESBoundObject*>(supplies), nullptr, 1, nullptr);
		RestoreSuppliesFavorite();
		RE::SendHUDMessage::ShowHUDMessage(Tr("You pack up your Camping Supplies."));
		logger::info("Camp: dungeon camp packed up");
	}

	std::atomic<bool> g_dungeonBedSleep{ false };  // the next use of a dungeon camp's bedroll sleeps (chosen in its menu)

	class DungeonBedMenu : public RE::IMessageBoxCallback
	{
	public:
		explicit DungeonBedMenu(RE::ObjectRefHandle a_bed) :
			bed(a_bed) {}
		void Run(std::uint8_t a_button) override
		{
			const auto handle = bed;
			OnMain([handle, a_button]() {
				auto b = handle.get();
				auto p = Player();
				if (!b || !p) {
					return;
				}
				if (a_button == 0) {
					g_dungeonBedSleep = true;
					b->ActivateRef(p, 0, nullptr, 1, false);
				} else if (a_button == 1) {
					FadeThroughBlack([handle]() { PackUpDungeonCamp(handle); });  // out of sight, as Camping packs up
				}
			});
		}
		RE::ObjectRefHandle bed;
	};

	void ShowDungeonBedMenu(RE::TESObjectREFR* a_bed)
	{
		auto factory = RE::MessageDataFactoryManager::GetSingleton();
		auto strings = RE::InterfaceStrings::GetSingleton();
		auto creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
		auto box = creator ? creator->Create() : nullptr;
		auto player = Player();
		if (!box || !player) {
			return;
		}
		box->bodyText = Tr("What do you do?");
		box->buttonText.push_back(Tr("Sleep"));
		box->buttonText.push_back(Tr("Pack up"));
		box->buttonText.push_back(Tr("Cancel"));
		box->callback = RE::make_smart<DungeonBedMenu>(a_bed->GetHandle());
		QueueMessageBox(box);
	}

