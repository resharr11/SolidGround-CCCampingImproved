// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Where a camp may not stand: water, roads, built things, farms, cliffs.
	// ---- What a camp may not stand on, and camps in dungeons ----

	// what the ground at a spot is, as far as a camp cares: ice (floes, glaciers), a road (the road pieces laid over
	// the land), water over it, or none of these
	enum class BadGround
	{
		kNone,
		kIce,
		kRoad,
		kWater,
		kNearRoad,   // the camp is off the road, but its earth mound would run down onto it
		kNearWater,  // the same with water: the mound would run down into it
		kBuilt,      // a fence, wall, building, cart or the like where the camp or its mound would stand
		kCliff,      // the ground falls away like a cliff just past the camp: its mound would hang out over the drop
		kFarm        // a farm's crops (cabbages, potatoes, wheat, leeks, gourds...) where the camp or its mound would stand
	};

	RE::TESLandTexture* VisibleLandTexture(const RE::NiPoint3& a_at, float a_radius);

	// a road: one of the road pieces laid over the land (Landscape\Roads\*.nif), or the land itself painted with a
	// road texture (Landscape\Roads\Road01*.dds: most of Skyrim's roads are only that)
	bool IsRoad(const GroundHit& a_hit, const RE::NiPoint3& a_at)
	{
		auto lower = [](std::string a_s) {
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		};
		if (a_hit.terrain || !a_hit.ref) {
			auto tex = VisibleLandTexture(a_at, 0.0f);
			auto set = tex ? tex->textureSet : nullptr;
			const char* name = set ? set->textures[0].textureName.c_str() : nullptr;
			return name && lower(name).find("roads\\") != std::string::npos;
		}
		if (!a_hit.ref->GetBaseObject()) {
			return false;
		}
		auto model = a_hit.ref->GetBaseObject()->As<RE::TESModel>();
		std::string path = model && model->GetModel() ? model->GetModel() : "";
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return path.find("\\roads\\") != std::string::npos || path.find("mainroad") != std::string::npos;
	}

	// the water level at a point: the game's own lookup (TESObjectCELL::GetWaterHeight, ID 19002 in the 1.6.1170
	// Address Library) finds rivers and lakes with their own level; the cell's single level is the fallback
	float WaterHeightAt(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_at)
	{
		if (!a_cell || !a_cell->IsExteriorCell()) {
			return -std::numeric_limits<float>::infinity();
		}
		if (float water = 0.0f; a_cell->GetWaterHeight(a_at, water)) {
			return water;
		}
		return a_cell->GetExteriorWaterHeight();
	}

	// streams, creeks and brooks: water laid as models instead of the land's water level (the planes in Water\ that
	// creeks and tundra streams use, and the rapids, creek flows and waterfalls drawn over them); not puddles
	struct WaterPatch
	{
		GroundBox box;
		float z;
	};
	// What a model's path says it is (stream water, something built, a crop), worked out once per base object: a
	// path never changes, and the ground checks see the same rocks, fences and streams every second (copying and
	// lowercasing every object's path each time was most of their work)
	enum ModelKind : std::uint8_t
	{
		kModelWater = 1,
		kModelBuilt = 2,
		kModelCrop = 4,
	};
	bool WaterModelPath(std::string path);
	bool BuiltModelPath(const std::string& a_path);
	bool CropModelPath(const std::string& a_path);
	std::mutex g_modelKindsLock;
	std::unordered_map<const RE::TESBoundObject*, std::uint8_t> g_modelKinds;

	std::uint8_t ModelKinds(const RE::TESBoundObject* a_base)
	{
		if (!a_base) {
			return 0;
		}
		std::lock_guard guard(g_modelKindsLock);
		if (const auto it = g_modelKinds.find(a_base); it != g_modelKinds.end()) {
			return it->second;
		}
		auto model = a_base->As<RE::TESModel>();
		std::string path = model && model->GetModel() ? model->GetModel() : "";
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::uint8_t kinds = 0;
		if (WaterModelPath(path)) {
			kinds |= kModelWater;
		}
		if (BuiltModelPath(path)) {
			kinds |= kModelBuilt;
		}
		if (CropModelPath(path)) {
			kinds |= kModelCrop;
		}
		if (!a_base->IsDynamicForm()) {
			g_modelKinds.emplace(a_base, kinds);  // (one made during play may be freed and its address reused: not kept)
		}
		return kinds;
	}

	bool IsWaterModel(RE::TESBoundObject* a_base)
	{
		return a_base && a_base->Is(RE::FormType::Activator, RE::FormType::MovableStatic, RE::FormType::Static) && (ModelKinds(a_base) & kModelWater);
	}
	// a_path lowercased
	bool WaterModelPath(std::string path)
	{
		if (path.empty() || path.find("puddle") != std::string::npos || path.find("barrel") != std::string::npos) {
			return false;
		}
		if (path.starts_with("meshes\\")) {
			path = path.substr(7);
		}
		// Water\..., and the DLCs' own DLC01\Water\..., DLC02\Water\...
		if (path.starts_with("water\\") || (path.starts_with("dlc0") && path.find("\\water\\") != std::string::npos)) {
			return true;
		}
		const auto slash = path.find_last_of('\\');
		const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
		// the rapids (Dawnguard's DLCFXFasterRapids too), creek flows and waterfalls
		return file.find("rapids") != std::string::npos || file.find("fxcreek") != std::string::npos || file.find("fxwaterfall") != std::string::npos ||
		       file.starts_with("fxriver") || file.starts_with("fxsplash");
	}
	// everything within a_radius of a_origin, only gathered: the game holds each cell's lock while it hands over the
	// cell's objects (CommonLibSSE TESObjectCELL::ForEachReference), so all the looking at them is done afterwards, on
	// the same thread and before anything else runs (they are used at once, never kept)
	std::vector<RE::TESObjectREFR*> RefsNear(RE::TESObjectREFR* a_origin, float a_radius)
	{
		std::vector<RE::TESObjectREFR*> refs;
		auto tes = RE::TES::GetSingleton();
		if (!tes || !a_origin) {
			return refs;
		}
		refs.reserve(1024);
		tes->ForEachReferenceInRange(a_origin, a_radius, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref) {
				refs.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return refs;
	}

	// the water models among a_refs, each as its outline (a little grown) and its height
	std::vector<WaterPatch> WaterModelsIn(const std::vector<RE::TESObjectREFR*>& a_refs)
	{
		std::vector<WaterPatch> found;
		for (auto ref : a_refs) {
			if (!ref->IsDisabled() && !ref->IsDeleted() && IsWaterModel(ref->GetBaseObject())) {
				found.push_back({ BoxOf(ref, nullptr, 0.0f), ref->GetPosition().z });
			}
		}
		return found;
	}
	std::vector<WaterPatch> WaterModelsNear(RE::TESObjectREFR* a_origin, float a_radius)
	{
		return WaterModelsIn(RefsNear(a_origin, a_radius));
	}
	// is there stream water at a spot whose ground is a_ground high? Only where its surface is above the ground there
	// (a water plane reaches out over the banks, but there it lies under the ground), as with the land's own water
	bool InWaterModel(const std::vector<WaterPatch>* a_water, const RE::NiPoint3& a_at, float a_ground)
	{
		if (!a_water) {
			return false;
		}
		const GroundBox spot{ a_at.x, a_at.y, 1.0f, 0.0f, 1.0f, 1.0f };
		return std::ranges::any_of(*a_water, [&](const WaterPatch& a_w) {
			return a_w.z > a_ground + 2.0f && a_w.z < a_ground + 1000.0f && BoxesOverlap(spot, a_w.box);
		});
	}

	// something built: fences, walls, buildings, carts, docks, furniture and the like the world places (not rocks,
	// plants, roads or landscape, which count as ground, nor anything a camp placed). Nothing of ours may stand in it
	// and the mound may not cover it (a fence half buried in a heap of earth)
	bool IsBuiltObstacle(RE::TESObjectREFR* a_ref)
	{
		auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || a_ref->IsDynamicForm() || a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->IsMarkedForDeletion() ||
			!base->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Activator, RE::FormType::Furniture, RE::FormType::Container, RE::FormType::Door)) {
			return false;
		}
		if (!(ModelKinds(base) & kModelBuilt)) {
			return false;
		}
		// not a scrap of a thing: at least a little tall and a little wide
		const auto& b = base->boundData;
		const float k = a_ref->GetScale();
		return (b.boundMax.z - b.boundMin.z) * k >= 20.0f && (std::max)(b.boundMax.x - b.boundMin.x, b.boundMax.y - b.boundMin.y) * k >= 20.0f;
	}
	// a_path lowercased: a model the world builds with (not ground)
	bool BuiltModelPath(const std::string& a_path)
	{
		for (const char* ground : { "landscape\\", "rock", "plants\\", "trees\\", "\\roads\\", "mainroad", "effects\\", "fx", "marker", "water", "\\grass" }) {
			if (a_path.find(ground) != std::string::npos) {
				return false;
			}
		}
		bool built = a_path.starts_with("architecture\\") || a_path.starts_with("furniture\\") || a_path.starts_with("dungeons\\");
		for (const char* word : { "fence", "wall", "cart", "wagon", "palisade", "gate", "bridge", "barricade", "scaffold", "shack", "hut", "dock", "post", "sign" }) {
			built = built || a_path.find(word) != std::string::npos;
		}
		return built;
	}
	// the built things among a_refs, each as its outline grown by a step
	std::vector<GroundBox> BuiltIn(const std::vector<RE::TESObjectREFR*>& a_refs)
	{
		std::vector<GroundBox> found;
		for (auto ref : a_refs) {
			if (IsBuiltObstacle(ref)) {
				found.push_back(BoxOf(ref, nullptr, 15.0f));
			}
		}
		return found;
	}
	std::vector<GroundBox> BuiltNear(RE::TESObjectREFR* a_origin, float a_radius)
	{
		return BuiltIn(RefsNear(a_origin, a_radius));
	}

	// a farm's crop: the planted cabbages, potatoes, wheat, leeks, gourds and ash yams (Skyrim.esm TreeFlora*, Dragonborn's
	// ash yam, Hearthfire's garden plots), by their models; wild plants are not crops. And the plantable soil, planted or
	// not: Hearthfire's planters (BYOHPlanterSoil) and the Farming creation's (ccVSVSSE004 PlanterSoil), both Soil01.nif
	bool IsCrop(RE::TESObjectREFR* a_ref)
	{
		auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->IsMarkedForDeletion() ||
			!base->Is(RE::FormType::Tree, RE::FormType::Flora, RE::FormType::Activator, RE::FormType::Static)) {
			return false;
		}
		return (ModelKinds(base) & kModelCrop) != 0;
	}
	// a_path lowercased
	bool CropModelPath(const std::string& a_path)
	{
		for (const char* crop : { "floracabbage", "florapotato", "florawheat", "floraleek", "gourdcluster", "floradlc2ashyam", "byohhouseflora", "byohhouseingrd", "house crafting\\soil" }) {
			if (a_path.find(crop) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	// the crops round a spot, each with the ground round it (cropmargin: fields have rows a step or two apart, so the
	// field between the plants is kept clear too)
	std::vector<GroundBox> CropsIn(const std::vector<RE::TESObjectREFR*>& a_refs)
	{
		std::vector<GroundBox> found;
		const float margin = CampSetting(CampSettings(), "cropmargin", 80.0f);
		for (auto ref : a_refs) {
			if (IsCrop(ref)) {
				found.push_back(BoxOf(ref, nullptr, margin));
			}
		}
		return found;
	}
	std::vector<GroundBox> CropsNear(RE::TESObjectREFR* a_origin, float a_radius)
	{
		return CropsIn(RefsNear(a_origin, a_radius));
	}

	BadGround GroundAt(const RE::NiPoint3& a_at, const std::vector<WaterPatch>* a_water = nullptr)
	{
		GroundHit hit;
		float z = 0.0f;
		if (!GroundZ(a_at, z, nullptr, &hit)) {
			return BadGround::kNone;
		}
		auto tes = RE::TES::GetSingleton();
		// the water level right here (rivers and mountain lakes have their own, so not only the cell's)
		if (auto cell = tes ? tes->GetCell({ a_at.x, a_at.y, z }) : nullptr) {
			if (const float water = WaterHeightAt(cell, { a_at.x, a_at.y, z }); water > z + 5.0f && water < z + 1000.0f) {
				return BadGround::kWater;
			}
		}
		if (InWaterModel(a_water, a_at, z) || (hit.ref && IsWaterModel(hit.ref->GetBaseObject()))) {
			return BadGround::kWater;  // a stream, creek or brook
		}
		if (OnIce(a_at, true)) {
			return BadGround::kIce;
		}
		return IsRoad(hit, a_at) ? BadGround::kRoad : BadGround::kNone;
	}

	// would the camp's earth mound reach a road or water? Its top is about the camp's size and as high as the highest ground under
	// the pieces; out from it in every direction the side falls as the mound's profile does (MoundDrop, at the scale a
	// camp this size gets) until it meets the ground. Anywhere it covers must not be road
	BadGround MoundReaches(const std::vector<RE::NiPoint3>& a_spots, const std::vector<WaterPatch>* a_water = nullptr, const std::vector<GroundBox>* a_built = nullptr,
		const std::vector<GroundBox>* a_crops = nullptr)
	{
		auto tes = RE::TES::GetSingleton();
		if (a_spots.empty() || !tes) {
			return BadGround::kNone;
		}
		RE::NiPoint3 c{};
		for (const auto& p : a_spots) {
			c += p;
		}
		c /= static_cast<float>(a_spots.size());
		float r = 0.0f, top = -std::numeric_limits<float>::max();
		for (const auto& p : a_spots) {
			r = (std::max)(r, std::hypot(p.x - c.x, p.y - c.y));
			if (float g = 0.0f; GroundZ(p, g) && std::abs(g - p.z) < 300.0f) {
				top = (std::max)(top, g);
			}
		}
		if (top == -std::numeric_limits<float>::max()) {
			return BadGround::kNone;
		}
		r += 90.0f;                            // the pieces reach past their middles
		const float scale = std::clamp(r / 128.0f, 0.2f, 4.0f);  // the main mound's half width is 128 at scale 1
		// no camp at a cliff's edge: just past the camp in any direction (cliffscan out from its top's edge) the ground
		// may not lie more than cliffdrop below where the mound's own side would be (the side falls at 30 then 45
		// degrees: ground falling away faster than that is a cliff, and the mound would hang out over it). Measured
		// against the mound's side, not its top: a path running downhill at a walkable slope stays close under the side
		// and is fine; at first it was measured against the top, and a downhill path read as a cliff edge
		{
			const auto settings = CampSettings();
			const float scan = CampSetting(settings, "cliffscan", 300.0f), gap = CampSetting(settings, "cliffdrop", 150.0f);
			if (scan > 0.0f) {
				for (int k = 0; k < 24; ++k) {
					const float a = k * std::numbers::pi_v<float> / 12.0f;
					const float dx = std::sin(a), dy = std::cos(a);
					// from the ground at the camp's edge this way, if lower than the top: the top is the highest
					// ground under any piece, and one piece on a rock or against a cliff's foot lifted it well above
					// the flat round the camp, so flat snow beside it read as a drop
					// (looked at where the pieces stand, a step inside the mound's edge: a cliff starting right at the
					// edge must not lower it)
					float from = top;
					const float in = (std::max)(0.0f, r - 90.0f);
					if (float edge = 0.0f; GroundZ({ c.x + dx * in, c.y + dy * in, top }, edge, nullptr, nullptr, 3000.0f) && edge < from) {
						from = edge;
					}
					for (float d = 30.0f; d <= scan; d += 30.0f) {
						const float side = from - MoundDrop(d, scale);
						// the ray from well above (at the foot of a cliff the ground there is the cliff itself, high above
						// the camp; from just above, the ray started inside the rock and found nothing) down to below the
						// line: no ground found means it lies further down still
						const RE::NiPoint3 p{ c.x + dx * (r + d), c.y + dy * (r + d), side - gap };
						float g = 0.0f;
						if (!GroundZ(p, g, nullptr, nullptr, 3000.0f + gap + (top - side)) || g < side - gap) {
							static long long lastLog = 0;
							if (const auto now = SteadyMs(); now - lastLog > 10000) {
								lastLog = now;
								logger::info("Camp: a cliff edge {:.0f} past the camp, towards ({:.2f}, {:.2f}): the ground at the camp's edge {:.0f} below its top, the line {:.0f} below that, the ground {}",
									d, dx, dy, top - from, from - side, g == 0.0f ? std::string("out of reach below") : std::format("{:.0f} below its top", top - g));
							}
							return BadGround::kCliff;
						}
					}
				}
			}
		}
		std::array<float, 16> foot{};  // how far out from the middle the mound reaches in each direction
		for (int k = 0; k < 16; ++k) {
			const float a = k * std::numbers::pi_v<float> / 8.0f;
			const float dx = std::sin(a), dy = std::cos(a);
			// under the top (between the pieces), then down the side
			foot[k] = r;
			for (float e = -0.5f * r; e < g_moundShape.reach + 60.0f; e += 30.0f) {
				const float out = r + e;
				foot[k] = (std::max)(foot[k], out);
				const RE::NiPoint3 p{ c.x + dx * out, c.y + dy * out, top };
				GroundHit hit;
				float g = 0.0f;
				if (!GroundZ(p, g, nullptr, &hit)) {
					break;
				}
				// only what the mound itself would cover: its top and its sides down to where they meet the ground
				if (e > 0.0f && g >= top - MoundDrop(e, scale) - 1.0f) {
					break;
				}
				if (IsRoad(hit, p)) {
					return BadGround::kNearRoad;
				}
				if (InWaterModel(a_water, p, g) || (hit.ref && IsWaterModel(hit.ref->GetBaseObject()))) {
					return BadGround::kNearWater;  // a stream, creek or brook
				}
				if (auto cell = tes->GetCell({ p.x, p.y, g }); cell && cell->IsExteriorCell()) {
					if (const float water = WaterHeightAt(cell, { p.x, p.y, g }); water > g + 2.0f && water < g + 1000.0f) {
						return BadGround::kNearWater;
					}
				}
			}
		}
		// nothing built under the camp or its mound: each built thing's outline, walked round in short steps, must stay
		// outside the mound's reach in every direction (its reach between two looked-at directions taken in between)
		auto reached = [&](const std::vector<GroundBox>& a_boxes) {
			for (const auto& box : a_boxes) {
				const float ax = box.ux, ay = box.uy, bx = -box.uy, by = box.ux;  // the box's own axes in the world
				auto inside = [&](float a_x, float a_y) {
					const float dx = a_x - c.x, dy = a_y - c.y;
					const float dist = std::hypot(dx, dy);
					float ang = std::atan2(dx, dy);  // as the directions above: 0 north, a quarter turn east
					if (ang < 0.0f) {
						ang += 2.0f * std::numbers::pi_v<float>;
					}
					const float f = ang / (std::numbers::pi_v<float> / 8.0f);
					const int k0 = static_cast<int>(f) % 16, k1 = (k0 + 1) % 16;
					const float t = f - std::floor(f);
					return dist < foot[k0] * (1.0f - t) + foot[k1] * t;
				};
				const int nx = (std::max)(1, static_cast<int>(2.0f * box.hx / 25.0f)), ny = (std::max)(1, static_cast<int>(2.0f * box.hy / 25.0f));
				for (int i = 0; i <= nx; ++i) {
					for (int j = 0; j <= ny; ++j) {
						if (i != 0 && i != nx && j != 0 && j != ny) {
							continue;  // its outline (anything inside it is reached across the outline first)
						}
						const float u = -box.hx + 2.0f * box.hx * i / nx, v = -box.hy + 2.0f * box.hy * j / ny;
						if (inside(box.cx + ax * u + bx * v, box.cy + ay * u + by * v)) {
							return true;
						}
					}
				}
				// a big thing (a house) can hold the whole camp inside its outline
				if (std::abs((c.x - box.cx) * ax + (c.y - box.cy) * ay) < box.hx && std::abs((c.x - box.cx) * bx + (c.y - box.cy) * by) < box.hy) {
					return true;
				}
			}
			return false;
		};
		if (a_built && reached(*a_built)) {
			return BadGround::kBuilt;
		}
		// nor on a farm's fields: its crops are kept clear the same way
		if (a_crops && reached(*a_crops)) {
			return BadGround::kFarm;
		}
		return BadGround::kNone;
	}

	// the spots a camp's pieces take out of doors, round where its fire goes (as CampObstacles): the fire 150 ahead of
	// the player at 60 degrees to the right, then the tent, seat, chopping block and pot in the fire's own axes
	std::vector<RE::NiPoint3> CampSpots(RE::Actor* a_player)
	{
		const auto me = a_player->GetPosition();
		const float fh = a_player->GetAngleZ() + 60.0f * std::numbers::pi_v<float> / 180.0f;
		const RE::NiPoint3 fire{ me.x + 150.0f * std::sin(fh), me.y + 150.0f * std::cos(fh), me.z };
		std::vector<RE::NiPoint3> spots;
		for (const auto& [shift, side] : { std::pair{ 0.0f, 0.0f }, { -171.0f, -104.0f }, { 125.0f, 85.0f }, { -80.0f, 170.0f }, { 160.0f, 0.0f } }) {
			spots.push_back({ fire.x + shift * std::sin(fh) + side * std::cos(fh), fire.y + shift * std::cos(fh) - side * std::sin(fh), fire.z });
		}
		return spots;
	}

	// what the camp would stand on out of doors, looked at every second (the ground cannot be read at the moment the
	// supplies are used)
	std::atomic<int> g_campGround{ 0 };

	void WatchCampGround()
	{
		RepeatAlways(1000, []() {
			if (g_loadingSave) {
				return true;
			}
			auto p = Player();
			if (!p || !p->GetParentCell() || p->GetParentCell()->IsInteriorCell()) {
				g_campGround = 0;
				return true;
			}
			LiftActorsOutOfMounds();  // a follower the game put on the ground under a camp's mound to catch up with you
			// the ground checks only matter for pitching a camp: none of their scans and rays without Camping Supplies
			if (auto kit = CampingForm(CAMP_SUPPLIES); !kit || ItemCount(p, kit) < 1) {
				g_campGround = 0;
				return true;
			}
			BadGround worst = BadGround::kNone;
			const auto around = RefsNear(p, 2500.0f);  // gathered once for all three (not "near": a Windows macro)
			const auto water = WaterModelsIn(around);  // streams, creeks and brooks round you
			const auto built = BuiltIn(around);      // fences, walls, buildings round you
			const auto crops = CropsIn(around);      // farm fields round you
			for (const auto& spot : CampSpots(p)) {
				if (const auto g = GroundAt(spot, &water); g != BadGround::kNone) {
					worst = g;
					break;
				}
			}
			// (the mound's reach takes many looks at the ground: only while you carry Camping Supplies)
			if (auto supplies = CampingForm(CAMP_SUPPLIES); worst == BadGround::kNone && supplies && ItemCount(p, supplies) > 0 &&
				CampSetting(CampSettings(), "foundation", 1.0f) == 1.0f) {
				worst = MoundReaches(CampSpots(p), &water, &built, &crops);
			}
			g_campGround = static_cast<int>(worst);
			return true;
		});
	}

	// the camp is not made: Camping Supplies kept, and why. A refused use (DrinkPotion returning false) leaves the
	// supplies with the player: the game's equip code stops before taking the item. The refund that used to follow,
	// counted a moment later, could copy supplies dropped in between
	void KeepSupplies(const char* a_why, [[maybe_unused]] int a_before = -1)
	{
		RE::SendHUDMessage::ShowHUDMessage(Tr(a_why));
	}


	// kit pieces ([Piece] in a kit file) round a newly pitched outdoor camp, before its mound is laid (the mound takes
	// them in, LayCampFoundation). Each goes where its author put it (Along and Across from the fire, Turn). Where the
	// camp already has something there (the fire, the tent, the pot, the chopping block, another piece) it moves straight
	// out from the fire until it stands clear, so the camp grows that way: no limit ([stated] 2026-11-16 "why the camp
	// doesn't expand to fit?", "dont limit the authors they will fine tune by themselves"). Left out (with a log line)
	// only where it would stand on a road, water, ice, a farm's crops or something built: the camp is still made
	// ([stated] 2026-11-16 "only skip the piece")
	void PlaceKitPieces(RE::TESObjectREFR* a_campfire)
	{
		g_pitchKitPieces.clear();
		auto tes = RE::TES::GetSingleton();
		if (g_kitPieces.empty() || !a_campfire || !tes) {
			return;
		}
		const auto fire = a_campfire->GetPosition();
		const float h = a_campfire->GetAngleZ();
		auto shelter = CampShelter(a_campfire);
		// what the camp has already: the fire's ring of stones and every piece round it (a disabled one is the tent or
		// its bedroll, about to show)
		std::vector<GroundBox> taken{ { fire.x, fire.y, 1.0f, 0.0f, 75.0f, 75.0f } };
		tes->ForEachReferenceInRange(a_campfire, 800.0f, [&](RE::TESObjectREFR* a_ref) {
			auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (base && a_ref != a_campfire && !a_ref->IsMarkedForDeletion() && a_ref->IsDynamicForm() && !IsFoundation(base) && !IsCampNavCut(base) &&
				!IsCampGrass(base) && !IsCampRug(base) &&
				base->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Furniture, RE::FormType::Activator, RE::FormType::Container)) {
				taken.push_back(BoxOf(a_ref, shelter, 10.0f));
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		const auto water = WaterModelsNear(a_campfire, 3000.0f);
		const auto built = BuiltNear(a_campfire, 3000.0f);
		const auto crops = CropsNear(a_campfire, 3000.0f);
		for (std::size_t i = 0; i < g_kitPieces.size(); ++i) {
			const auto& kit = g_kitPieces[i];
			auto base = RE::TESForm::LookupByID<RE::TESBoundObject>(kit.form);
			auto ref = base ? PlaceOwned(a_campfire, base, 0, PieceRole::kKitPiece).get() : nullptr;
			if (!ref) {
				continue;
			}
			const RE::NiPoint3 angle{ 0.0f, 0.0f, h + kit.turn * std::numbers::pi_v<float> / 180.0f };
			// the author's spot, in the fire's own axes (as the pot's: BesideFire)
			const RE::NiPoint3 spot{ fire.x + kit.along * std::sin(h) + kit.across * std::cos(h), fire.y + kit.along * std::cos(h) - kit.across * std::sin(h), fire.z };
			float dx = spot.x - fire.x, dy = spot.y - fire.y;
			if (const float len = std::hypot(dx, dy); len > 1.0f) {
				dx /= len;
				dy /= len;
			} else {
				dx = std::sin(h);  // a piece on the fire itself moves out along the fire's own axis
				dy = std::cos(h);
			}
			MoveRef(ref, spot, angle);  // (turned first: its footprint below goes with its turn)
			RE::NiPoint3 at = spot;
			float moved = 0.0f;
			// straight out from the fire, a step at a time, until it overlaps nothing of the camp (a guard of 20000
			// steps only stops a loop: the camp is far smaller than that)
			for (int step = 0; step < 20000; ++step) {
				at = { spot.x + dx * moved, spot.y + dy * moved, spot.z };
				const auto box = BoxOf(ref, shelter, 10.0f, &at);
				if (std::ranges::none_of(taken, [&](const GroundBox& a_box) { return BoxesOverlap(box, a_box); })) {
					break;
				}
				moved += 10.0f;
			}
			const auto box = BoxOf(ref, shelter, 0.0f, &at);
			const char* why = nullptr;
			// the ground under its middle and its four corners, and (on a mound) along its strip back to the camp, a
			// step every 100: before, only its middle, so one end of a wide piece or its strip could lie on a road
			std::vector<RE::NiPoint3> probes{ at };
			for (const float sx : { -1.0f, 1.0f }) {
				for (const float sy : { -1.0f, 1.0f }) {
					probes.push_back({ box.cx + sx * box.hx * box.ux - sy * box.hy * box.uy, box.cy + sx * box.hx * box.uy + sy * box.hy * box.ux, at.z });
				}
			}
			if (kit.mound) {
				const float out = std::hypot(at.x - fire.x, at.y - fire.y);
				for (float d = out - 100.0f; d > 250.0f; d -= 100.0f) {
					probes.push_back({ fire.x + (at.x - fire.x) * d / out, fire.y + (at.y - fire.y) * d / out, at.z });
				}
			}
			for (const auto& p : probes) {
				switch (GroundAt(p, &water)) {
				case BadGround::kRoad: why = "on a road"; break;
				case BadGround::kWater: why = "in water"; break;
				case BadGround::kIce: why = "on ice"; break;
				default: break;
				}
				if (why) {
					break;
				}
			}
			if (!why && std::ranges::any_of(crops, [&](const GroundBox& a_b) { return BoxesOverlap(box, a_b); })) {
				why = "on a farm's crops";
			}
			if (!why && std::ranges::any_of(built, [&](const GroundBox& a_b) { return BoxesOverlap(box, a_b); })) {
				why = "on something built";
			}
			if (why) {
				DeleteCampRef(ref);
				logger::info("Camp: kit piece {} left out: it would stand {}", kit.name, why);
				continue;
			}
			if (float ground = 0.0f; GroundZ(at, ground, shelter)) {
				at.z = ground;
			}
			MoveRef(ref, at, angle);
			g_campSpots.push_back(ref->GetFormID());  // saved and packed up by reference, like the spots round the fire
			g_pitchKitPieces.emplace_back(ref->GetFormID(), i);
			taken.push_back(BoxOf(ref, shelter, 10.0f));
			logger::info("Camp: kit piece {} placed{}", kit.name, moved > 0.0f ? std::format(", moved {:.0f} out from the fire to stand clear", moved) : std::string{});
		}
	}
