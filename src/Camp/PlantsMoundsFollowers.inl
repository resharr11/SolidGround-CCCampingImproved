// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Hidden plants, compact layout, mound shape, lifting and moving actors, fire spots.
	// Camping: dresses a newly pitched camp. Every camp gets a chopping block for Firewood; with the cookware upgrade, the
	// iron stand with its hanging pot stands beside the fire for full cooking (the metal carries the heat)
	// A camp clears its ground: plants, bushes, herbs and grass clumps around it are hidden while it stands and come
	// back when it is packed up. Trees and rocks stay. Plants of a camp packed up far away come back when their area
	// loads again; both lists are kept in the save.
	std::vector<RE::FormID> g_hiddenPlants;  // hidden by the camp standing now
	std::vector<RE::FormID> g_plantsToShow;  // from a camp packed up while their area was not loaded

	// a small plant's model (grass, fern, shrub, flower...), as the landscape places them
	bool IsPlantModel(const RE::TESForm* a_base)
	{
		if (!a_base || (!a_base->Is(RE::FormType::Tree) && !a_base->Is(RE::FormType::Static) && !a_base->Is(RE::FormType::MovableStatic))) {
			return false;
		}
		auto model = a_base->As<RE::TESModel>();
		std::string path = model && model->GetModel() ? model->GetModel() : "";
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (path.find("plants\\") == std::string::npos) {
			return false;
		}
		for (const char* word : { "plant", "shrub", "bush", "fern", "flower", "grass", "reed", "weed", "thistle", "heather", "lavender", "clover", "scrub" }) {
			if (path.find(word) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
	// one of the plants we set round a camp's foundation (only ours are placed in game: the world's plants are not)
	bool IsSkirtPlant(RE::TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->IsDynamicForm() && IsPlantModel(a_ref->GetBaseObject());
	}

	bool IsCampPlant(RE::TESObjectREFR* a_ref)
	{
		auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || a_ref->IsDynamicForm() || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || a_ref->IsPersistent() ||
			a_ref->extraList.HasType(RE::ExtraDataType::kEnableStateParent)) {
			return false;
		}
		if (base->Is(RE::FormType::Flora)) {
			return true;  // herbs, mushrooms and other harvestable plants
		}
		if (!base->Is(RE::FormType::Tree) && !base->Is(RE::FormType::Static) && !base->Is(RE::FormType::MovableStatic)) {
			return false;
		}
		auto model = base->As<RE::TESModel>();
		std::string path = model && model->GetModel() ? model->GetModel() : "";
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (path.find("landscape\\") == std::string::npos) {
			return false;
		}
		for (const char* word : { "plant", "shrub", "bush", "fern", "flower", "grass", "reed", "weed", "fung", "mushroom", "thistle", "heather", "lavender", "ivy" }) {
			if (path.find(word) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	// the camp the hidden plants were cleared for (saved, 'HCMP'). A late pack-up of an older camp leaves the standing
	// camp's plants hidden. ALL: whichever camp hid them
	std::uint32_t g_hiddenPlantsCamp = 0;
	void ShowCampPlants(std::uint32_t a_camp = 0xFFFFFFFF)
	{
		if (a_camp != 0xFFFFFFFF && g_hiddenPlantsCamp != 0 && a_camp != g_hiddenPlantsCamp) {
			logger::info("Camp: the plants hidden now are camp {}'s, not camp {}'s: left hidden", g_hiddenPlantsCamp, a_camp);
			return;
		}
		for (auto id : g_hiddenPlants) {
			auto form = RE::TESForm::LookupByID(id);
			if (auto ref = form ? form->AsReference() : nullptr) {
				if (!ref->IsDynamicForm()) {  // (a world plant: one placed in game under that number since is not it; F11)
					ref->Enable(false);
				}
			} else {
				g_plantsToShow.push_back(id);
			}
		}
		g_hiddenPlants.clear();
		g_hiddenPlantsCamp = 0;
	}

	void ShowPendingPlants()
	{
		std::erase_if(g_plantsToShow, [](RE::FormID a_id) {
			auto form = RE::TESForm::LookupByID(a_id);
			if (auto ref = form ? form->AsReference() : nullptr) {
				if (!ref->IsDynamicForm()) {  // (F11, as above)
					ref->Enable(false);
				}
				return true;
			}
			return false;
		});
	}

	void HideCampPlants(RE::TESObjectREFR* a_campfire)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes || !a_campfire) {
			return;
		}
		ShowCampPlants();  // the last camp's plants, if it was not packed up the usual way
		const float radius = CampSetting(CampSettings(), "clearradius", 450.0f);
		// found first, switched off after the search: the search walks the cell's reference list under its lock
		std::vector<RE::TESObjectREFR*> plants;
		tes->ForEachReferenceInRange(a_campfire, radius, [&](RE::TESObjectREFR* a_ref) {
			if (IsCampPlant(a_ref)) {
				plants.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		for (auto plant : plants) {
			plant->Disable();
			g_hiddenPlants.push_back(plant->GetFormID());
		}
		g_hiddenPlantsCamp = CampOf(a_campfire) ? CampOf(a_campfire) : g_campNumber;
		logger::info("Camp: cleared {} plants within {:.0f}", g_hiddenPlants.size(), radius);
	}

	class PlantReturner : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
	{
	public:
		static PlantReturner* GetSingleton()
		{
			static PlantReturner singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent*, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
		{
			if (!g_plantsToShow.empty()) {
				OnMain([]() { ShowPendingPlants(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// The camp's foundation (foundation = 1): one deep block under the whole camp, Fallout 4 style, trimmed to what
	// stands on it (the pieces' own outlines and a small rim), painted with the ground under it, and every piece moved
	// onto its level top, flat. Trees and rocks may poke through it (it is ground); only the pieces must be clear of them
	// (CampObstacles)
	// how far a tent reaches past its object bounds, for the foundation under it: the hides hang over, and the small
	// Imperial tent's guy poles stand well out from its corners (tent{n}outline)
	// (every shelter now fills the same spot, so one outline for all: tentoutline)
	float TentOutline()
	{
		return CampSetting(CampSettings(), "tentoutline", 1.1f);
	}

	// this camp's shelter, also while it is switched off (it shows again a moment after it is moved and resized;
	// CurrentShelter skips it then)
	RE::TESObjectREFR* CampShelter(RE::TESObjectREFR* a_campfire)
	{
		// the shelter the fire's camp recorded, by its role (an older save's shelter has its role from its recorded base
		// from their recorded bases). No recorded shelter: none (the nearest shelter of its kind could be another mod's, and the layout
		// then fitted our pieces round it)
		return CampRef(CampOf(a_campfire), PieceRole::kShelter);
	}

	// a piece's outline (its object bounds). The tent's base record keeps Camping's lean-to bounds after we give it
	// another model, so a vanilla tent's own record gives them: small Nordic, large Nordic, small Imperial
	const RE::TESBoundObject::BOUND_DATA& PieceBounds(RE::TESObjectREFR* a_piece, RE::TESObjectREFR* a_shelter)
	{
		if (a_piece == a_shelter && a_shelter) {
			// the shelter: its spot (the same for every model), in the model's own axes and size, so whatever stands
			// there takes the same ground (the mound under it, the navigation cut, the clearance round it)
			thread_local RE::TESBoundObject::BOUND_DATA spot{};
			const auto settings = CampSettings();
			RE::NiPoint3 centre;
			float facing = 0.0f;
			TentSlotOf(a_shelter, centre, facing);
			const float W = TentSlotWidth(settings), D = TentSlotDepth(settings);
			const float k = (std::max)(a_shelter->GetScale(), 0.01f), rz = a_shelter->GetAngleZ();
			const RE::NiPoint3 f{ std::sin(facing), std::cos(facing), 0.0f }, a{ std::cos(facing), -std::sin(facing), 0.0f };
			const auto pos = a_shelter->GetPosition();
			float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
			for (const float sf : { -0.5f, 0.5f }) {
				for (const float sa : { -0.5f, 0.5f }) {
					const float wx = centre.x + f.x * D * sf + a.x * W * sa - pos.x, wy = centre.y + f.y * D * sf + a.y * W * sa - pos.y;
					const float lx = (wx * std::cos(rz) - wy * std::sin(rz)) / k, ly = (wx * std::sin(rz) + wy * std::cos(rz)) / k;
					x0 = (std::min)(x0, lx);
					x1 = (std::max)(x1, lx);
					y0 = (std::min)(y0, ly);
					y1 = (std::max)(y1, ly);
				}
			}
			spot.boundMin.x = static_cast<std::int16_t>(std::floor(x0));
			spot.boundMin.y = static_cast<std::int16_t>(std::floor(y0));
			spot.boundMax.x = static_cast<std::int16_t>(std::ceil(x1));
			spot.boundMax.y = static_cast<std::int16_t>(std::ceil(y1));
			if (auto b = TentModelBounds(TentModelNow())) {
				spot.boundMin.z = b->boundMin.z;
				spot.boundMax.z = b->boundMax.z;
			}
			return spot;
		}
		return a_piece->GetBaseObject()->boundData;
	}

	// a piece's outline on the ground: a box around its middle, turned like the piece (world axes ux, uy for its own x
	// and y), its half sizes grown by a_grow
	struct GroundBox
	{
		float cx, cy, ux, uy, hx, hy;
	};
	GroundBox BoxOf(RE::TESObjectREFR* a_piece, RE::TESObjectREFR* a_shelter, float a_grow, const RE::NiPoint3* a_at = nullptr)
	{
		const auto& b = PieceBounds(a_piece, a_shelter);
		const float k = a_piece->GetScale() * (a_piece == a_shelter ? TentOutline() : 1.0f);
		const float rz = a_piece->GetAngleZ();
		const auto pos = a_at ? *a_at : a_piece->GetPosition();
		const float mx = 0.5f * (b.boundMin.x + b.boundMax.x) * k, my = 0.5f * (b.boundMin.y + b.boundMax.y) * k;
		return { pos.x + mx * std::cos(rz) + my * std::sin(rz), pos.y - mx * std::sin(rz) + my * std::cos(rz), std::cos(rz), -std::sin(rz),
			0.5f * (b.boundMax.x - b.boundMin.x) * k + a_grow, 0.5f * (b.boundMax.y - b.boundMin.y) * k + a_grow };
	}
	// two turned boxes overlap unless one of their four axes separates them
	bool BoxesOverlap(const GroundBox& a_a, const GroundBox& a_b)
	{
		const float dx = a_b.cx - a_a.cx, dy = a_b.cy - a_a.cy;
		for (const auto& [ax, ay] : { std::pair{ a_a.ux, a_a.uy }, { -a_a.uy, a_a.ux }, { a_b.ux, a_b.uy }, { -a_b.uy, a_b.ux } }) {
			auto reach = [&](const GroundBox& a_box) {
				return a_box.hx * std::abs(a_box.ux * ax + a_box.uy * ay) + a_box.hy * std::abs(-a_box.uy * ax + a_box.ux * ay);
			};
			if (std::abs(dx * ax + dy * ay) > reach(a_a) + reach(a_b)) {
				return false;
			}
		}
		return true;
	}

	// the tent's navigation cut, laid once the camp's pieces stand where they stay (after the foundation): as wide as the
	// tent's longer side, turned with it. NPCs who set out after it is there path round the tent (the game only cuts
	// paths started after the cut exists, so anyone the camp was pitched on is still moved out first). Not indoors (no
	// shelter there). Every shelter gets one (they all fill the same spot). navcut = 0 in camp.ini turns it off
	void PlaceTentNavCut(RE::TESObjectREFR* a_campfire)
	{
		const auto settings = CampSettings();
		auto shelter = CurrentShelter();
		auto base = PluginItem(TENT_NAVCUT);
		auto cell = a_campfire ? a_campfire->GetParentCell() : nullptr;
		if (!shelter || !base || !cell || cell->IsInteriorCell() || CampSetting(settings, "navcut", 1.0f) == 0.0f) {
			return;
		}
		// the shelter's spot (not the wider outline kept clear round it)
		const auto& b = PieceBounds(shelter, shelter);
		const float k = shelter->GetScale();
		const float rz = shelter->GetAngleZ();
		const float mx = 0.5f * (b.boundMin.x + b.boundMax.x) * k, my = 0.5f * (b.boundMin.y + b.boundMax.y) * k;
		const float hx = 0.5f * (b.boundMax.x - b.boundMin.x) * k, hy = 0.5f * (b.boundMax.y - b.boundMin.y) * k;
		const auto pos = shelter->GetPosition();
		const RE::NiPoint3 at{ pos.x + mx * std::cos(rz) + my * std::sin(rz), pos.y - mx * std::sin(rz) + my * std::cos(rz), pos.z };
		auto cut = PlaceOwned(a_campfire, base);
		if (!cut) {
			return;
		}
		MoveRef(cut.get(), at, RE::NiPoint3{ 0.0f, 0.0f, rz });
		// at full size its flat sides stand 100 from its middle
		const float scale = std::clamp((std::max)(hx, hy) * CampSetting(settings, "navcutsize", 1.0f) / 100.0f, 0.3f, 10.0f);
		ScaleCampPiece(cut, scale);
		logger::info("Camp: navigation cut round the tent ({:.0f} by {:.0f}, size {:.2f})", hx * 2.0f, hy * 2.0f, scale);
	}

	// Before the foundation: the camp packed tighter, the same layout. The box seat and the chopping block slide in
	// toward the fire, along their own line from it, until they come within a step of the fire, the tent or each other
	// (the cooking pot stays: its spot is settled for the cooking animation). Each keeps its height above the ground
	void CompactCamp(RE::TESObjectREFR* a_campfire)
	{
		const auto settings = CampSettings();
		auto tes = RE::TES::GetSingleton();
		if (!tes || CampSetting(settings, "compact", 1.0f) == 0.0f) {
			return;
		}
		const auto fire = a_campfire->GetPosition();
		const auto shelter = CampShelter(a_campfire);
		const float gap = CampSetting(settings, "compactgap", 25.0f);
		// (the camp's own seat and chopping block, by their records and roles: a kit's block of the same kind is not
		// moved)
		const auto camp = CampOf(a_campfire);
		RE::TESObjectREFR* seat = CampRef(camp, PieceRole::kSeat);
		RE::TESObjectREFR* block = CampRef(camp, PieceRole::kBlock);
		std::vector<RE::TESObjectREFR*> fixed;  // what the moving pieces must keep clear of (only looked at, never moved)
		tes->ForEachReferenceInRange(a_campfire, 450.0f, [&](RE::TESObjectREFR* a_ref) {
			auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || !a_ref->IsDynamicForm() || a_ref->IsMarkedForDeletion() || a_ref == seat || a_ref == block) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (a_ref == shelter || IsCampBase(base, POT_PLACED) || IsCampStand(base)) {
				fixed.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		// the fire ring: a box as wide as its stones
		const GroundBox fireBox{ fire.x, fire.y, 1.0f, 0.0f, 70.0f + gap, 70.0f + gap };
		for (auto piece : { seat, block }) {
			if (!piece) {
				continue;
			}
			const auto from = piece->GetPosition();
			// the seat slides toward the fire; the chopping block toward the middle between the fire and the tent, so it
			// ends up beside both
			auto target = fire;
			if (piece == block && shelter) {
				target = (fire + shelter->GetPosition()) * 0.5f;
			}
			auto dir = from - target;
			dir.z = 0.0f;
			const float len = dir.Length();
			if (len < 1.0f) {
				continue;
			}
			dir = dir * (1.0f / len);
			// the chopping block turns its stump away from the camp: the spot where you stand to chop (its origin, the
			// stump 105 in front of it) is then on the camp's side, so you chop facing out from the fire and need no
			// ground of your own beyond the stump
			if (piece == block && CampSetting(settings, "blockfacecamp", 1.0f) != 0.0f) {
				auto a = piece->GetAngle();
				a.z = std::atan2(dir.x, dir.y);
				MoveRef(piece, from, a);
			}
			auto clear = [&](const RE::NiPoint3& a_at) {
				const auto box = BoxOf(piece, shelter, gap * 0.5f, &a_at);
				if (BoxesOverlap(box, fireBox)) {
					return false;
				}
				for (auto other : fixed) {
					if (other != piece && BoxesOverlap(box, BoxOf(other, shelter, gap * 0.5f))) {
						return false;
					}
				}
				return true;
			};
			float best = len;
			// turned, it may now reach into the fire or another piece: then it first moves out until it is clear
			for (float d = len; d < len + 250.0f; d += 5.0f) {
				const RE::NiPoint3 at{ target.x + dir.x * d, target.y + dir.y * d, from.z };
				if (clear(at)) {
					best = d;
					break;
				}
			}
			for (float d = best - 5.0f; d > 0.0f; d -= 5.0f) {
				const RE::NiPoint3 at{ target.x + dir.x * d, target.y + dir.y * d, from.z };
				if (!clear(at)) {
					break;
				}
				best = d;
			}
			if (std::abs(best - len) > 1.0f) {
				RE::NiPoint3 at{ target.x + dir.x * best, target.y + dir.y * best, from.z };
				float groundFrom = from.z, groundTo = from.z;
				if (GroundZ(from, groundFrom, shelter) && GroundZ(at, groundTo, shelter)) {
					at.z = from.z + (groundTo - groundFrom);
				}
				MoveRef(piece, at, piece->GetAngle());
				logger::info("Camp: {} moved {:.0f} closer to the {} (negative: out, to clear the others)", piece == seat ? "box seat" : "chopping block", len - best,
					piece == block ? "fire and the tent" : "fire");
			}
			fixed.push_back(piece);  // the next one keeps clear of it too
		}
	}

	// The mound's shape (his call 2026-11-16: for modders only, not a player setting). Our models (mound.cpp) have a
	// shoulder 64 across easing into 30 degrees for the first 105 down, then easing over 40 into 45, an outline that
	// bulges out up to 60, and sides 1536 deep. A modder who reshapes the mound models (Outfit Studio, Blender) gives the
	// new numbers in a kit file's [Mound] section (Data/SKSE/Plugins/SolidGroundCamping/Kits/*.ini), so the camp knows
	// where the sides meet the hill (how deep the mound sits, which ground it covers, the splashes at its foot). Read once
	// at kDataLoaded, only read afterwards
	struct MoundShapeSettings
	{
		float upperSlope = 30.0f;  // degrees, the gentle part under the shoulder
		float lowerSlope = 45.0f;  // degrees, further down
		float turnDepth = 105.0f;  // how far below the top the gentle part ends
		float turnSoftness = 40.0f;  // how far out the turn from one slope to the other takes
		float shoulder = 64.0f;    // how wide the rounded top edge is
		float bulge = 60.0f;       // how far the uneven outline swells out at most
		float depth = 1536.0f;     // how far below the top the sides go
		// worked out from the above
		float g1 = 0.5774f, g2 = 1.0f, inset = 18.0f, reach = 1640.0f;  // (reach: our sides meet 1536 down 1640 out; the old fixed limits 1700 and 1800 are reach + 60 and + 160)
	};
	MoundShapeSettings g_moundShape;

	float MoundDrop(float a_e, float a_scale)
	{
		const auto& m = g_moundShape;
		const float u = (std::max)(0.0f, a_e / a_scale);
		// as mound.cpp: the shoulder (from `inset` inside the top's edge to the rest of its width outside it) easing into
		// the upper slope down to turnDepth, then easing over turnSoftness into the lower slope. a_e counts from the
		// top's edge
		const float W = m.shoulder, INSET = m.inset, g1 = m.g1, g2 = m.g2, Dk = m.turnDepth, B = m.turnSoftness;
		const float s = u + INSET;
		float d = 0.0f;
		if (s <= W) {
			d = g1 * s * s / (2 * W);
		} else if (const float e1 = Dk / g1 + W / 2 - INSET; u <= e1) {
			d = g1 * (s - W / 2);
		} else if (const float t = u - e1; t <= B) {
			d = Dk + g1 * t + (B > 0.0f ? (g2 - g1) * t * t / (2 * B) : 0.0f);
		} else {
			d = Dk + g1 * B + (g2 - g1) * B / 2 + g2 * (t - B);
		}
		return a_scale * d;
	}

	// a form written in a kit file: "Skyrim.esm|0x0E8644" (as SkyPatcher) or "0x0E8644~Skyrim.esm" (as SPID); a
	// load-order prefix copied from the console (0x05000801, 0xFE012801) is dropped. 0 when it can't be found
	RE::FormID KitForm(const std::string& a_text, const std::string& a_file, const char* a_where)
	{
		std::string plugin, id;
		// (the plugin's name keeps its inner spaces, "My Camp.esp": only the spaces round each part go)
		auto trim = [](const std::string& a_s) {
			const auto b = a_s.find_first_not_of(" \t\r\n");
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return b == std::string::npos ? std::string{} : a_s.substr(b, e - b + 1);
		};
		if (const auto bar = a_text.find('|'); bar != std::string::npos) {
			plugin = trim(a_text.substr(0, bar));
			id = trim(a_text.substr(bar + 1));
		} else if (const auto tilde = a_text.find('~'); tilde != std::string::npos) {
			id = trim(a_text.substr(0, tilde));
			plugin = trim(a_text.substr(tilde + 1));
		} else {
			logger::warn("Kits: {} {}: \"{}\" needs its plugin, as in Skyrim.esm|0x0E8644", a_file, a_where, a_text);
			return 0;
		}
		std::uint32_t local = 0;
		try {
			local = static_cast<std::uint32_t>(std::stoul(id, nullptr, 16));
		} catch (...) {
			logger::warn("Kits: {} {}: \"{}\" is not a form ID", a_file, a_where, id);
			return 0;
		}
		auto data = RE::TESDataHandler::GetSingleton();
		const auto* file = data ? data->LookupModByName(plugin) : nullptr;
		if (!file) {
			logger::warn("Kits: {} {}: plugin {} is not loaded, skipped", a_file, a_where, plugin);
			return 0;
		}
		const bool light = file->IsLight();
		const std::uint32_t trimmed = light ? (local & 0xFFF) : (local & 0xFFFFFF);
		if (trimmed != local) {
			logger::info("Kits: {} {}: load-order prefix dropped from {}", a_file, a_where, id);
		}
		auto form = data->LookupForm(trimmed, plugin);
		if (!form) {
			logger::warn("Kits: {} {}: {}|{:06X} doesn't exist, skipped", a_file, a_where, plugin, trimmed);
			return 0;
		}
		return form->GetFormID();
	}

	// the kit files (Data/SKSE/Plugins/SolidGroundCamping/Kits, any folder below it), A to Z by path: [Mound] (a later
	// file's key replaces an earlier one's, ranges checked) and [FireSpots] (Add and Remove gather from every file,
	// a later Count replaces an earlier one). Read once at kDataLoaded
	void LoadKits()
	{
		auto& m = g_moundShape;
		const std::filesystem::path dir = "Data/SKSE/Plugins/SolidGroundCamping/Kits";
		auto lower = [](std::string a_s) {
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		};
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		if (std::filesystem::is_directory(dir, ec)) {
			for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				const auto name = lower(it->path().filename().string());
				if (it->is_regular_file(ec) && name.ends_with(".ini")) {
					files.push_back(it->path());
				} else if (name.ends_with(".ini.txt")) {
					logger::warn("Kits: {} ends in .txt and is not read: rename it to end in .ini", it->path().generic_string());
				}
			}
		}
		std::ranges::sort(files, [&](const auto& a, const auto& b) { return lower(a.generic_string()) < lower(b.generic_string()); });
		struct Key { const char* name; float* value; float lo, hi; };
		const Key keys[]{
			{ "upperslope", &m.upperSlope, 10.0f, 60.0f }, { "lowerslope", &m.lowerSlope, 10.0f, 70.0f },
			{ "turndepth", &m.turnDepth, 20.0f, 600.0f }, { "turnsoftness", &m.turnSoftness, 0.0f, 200.0f },
			{ "shoulder", &m.shoulder, 16.0f, 200.0f }, { "bulge", &m.bulge, 0.0f, 150.0f }, { "depth", &m.depth, 256.0f, 4096.0f },
		};
		bool moundSet = false, spotsSet = false;
		// [Tent]: one section per shelter, finished when the next section starts or the file ends
		struct PendingTent
		{
			bool open = false;
			std::string name, model, form;
			float width = 0.0f, depth = 0.0f, door = 0.0f, size = 1.0f;
			bool winter = false, bad = false;
		} tent;
		std::string tentFile;
		auto finishTent = [&]() {
			if (!tent.open) {
				return;
			}
			const auto t = tent;
			tent = {};
			const auto where = std::format("{} [Tent] {}", tentFile, t.name.empty() ? std::string("(no Name)") : t.name);
			if (t.bad) {
				logger::warn("Kits: {}: left out (see the lines above)", where);
				return;
			}
			if (t.name.empty()) {
				logger::warn("Kits: {}: a tent needs a Name, left out", where);
				return;
			}
			if (t.name.size() > KIT_NAME_MAX) {
				logger::warn("Kits: {}: its Name is longer than {} letters, left out", where, KIT_NAME_MAX);
				return;
			}
			if (std::ranges::any_of(g_tents, [&](const TentKind& a_k) { return lower(a_k.name) == lower(t.name); })) {
				logger::warn("Kits: {}: there is already a shelter with this name, left out", where);
				return;
			}
			// its settings keys (size, opening) come from its letters and digits: a name that gives the same as an earlier
			// kit tent's ("Scout Tent", "Scout-Tent") would share them
			if (const auto key = KitTentKeyName(t.name); key.empty() ||
				std::any_of(g_tents.begin() + (std::min<std::size_t>)(4, g_tents.size()), g_tents.end(), [&](const TentKind& a_k) { return KitTentKeyName(a_k.name) == key; })) {
				logger::warn("Kits: {}: its name is too close to another kit tent's (only letters and digits count), left out", where);
				return;
			}
			TentKind k;
			k.name = t.name;
			k.nameLower = lower(t.name);
			k.kit = true;
			k.winter = t.winter;
			k.door = t.door;
			k.size = t.size;
			if (!t.form.empty()) {
				// a static record of the tent: its model and its footprint from the record
				const auto id = KitForm(t.form, tentFile, "[Tent] Form");
				auto bound = id ? RE::TESForm::LookupByID<RE::TESBoundObject>(id) : nullptr;
				auto model = bound ? bound->As<RE::TESModel>() : nullptr;
				if (!model || !model->GetModel() || !*model->GetModel()) {
					logger::warn("Kits: {}: Form {} has no model, left out", where, t.form);
					return;
				}
				k.model = model->GetModel();
				k.bounds = bound->boundData;
				k.hasBounds = bound->boundData.boundMax.x > bound->boundData.boundMin.x && bound->boundData.boundMax.y > bound->boundData.boundMin.y;
			} else {
				if (t.model.empty()) {
					logger::warn("Kits: {}: give Model (with Width and Depth) or Form, left out", where);
					return;
				}
				k.model = t.model;
				if (t.width > 0.0f && t.depth > 0.0f) {
					// the footprint round the model's origin: Width across, Depth along its front
					k.bounds.boundMin = { static_cast<std::int16_t>(-0.5f * t.width), static_cast<std::int16_t>(-0.5f * t.depth), 0 };
					k.bounds.boundMax = { static_cast<std::int16_t>(0.5f * t.width), static_cast<std::int16_t>(0.5f * t.depth), 0 };
					k.hasBounds = true;
				}
			}
			if (!k.hasBounds) {
				logger::warn("Kits: {}: its size is unknown (Width and Depth, or a Form with bounds): it may not fit its spot", where);
			}
			std::ranges::replace(k.model, '/', '\\');
			if (k.model.size() > 7 && lower(k.model.substr(0, 7)) == "meshes\\") {
				k.model = k.model.substr(7);  // model paths start inside Meshes
			}
			g_tents.push_back(k);
			logger::info("Kits: shelter {} added ({}{})", k.name, k.model, k.winter ? ", a winter tent" : "");
		};
		// [Piece]: one section per piece, finished like a [Tent]
		struct PendingPiece
		{
			bool open = false, bad = false;
			KitPiece p;
			std::string form;
		} piece;
		std::string pieceFile;
		auto finishPiece = [&]() {
			if (!piece.open) {
				return;
			}
			auto k = piece;
			piece = {};
			const auto where = std::format("{} [Piece] {}", pieceFile, k.p.name.empty() ? std::string("(no Name)") : k.p.name);
			if (k.bad) {
				logger::warn("Kits: {}: left out (see the lines above)", where);
				return;
			}
			if (k.p.name.empty()) {
				logger::warn("Kits: {}: a piece needs a Name, left out", where);
				return;
			}
			if (k.form.empty()) {
				logger::warn("Kits: {}: a piece needs a Form (as Skyrim.esm|0x0727A1), left out", where);
				return;
			}
			const auto id = KitForm(k.form, pieceFile, "[Piece] Form");
			auto form = id ? RE::TESForm::LookupByID(id) : nullptr;
			if (!form) {
				return;  // (KitForm said why)
			}
			// no containers: a camp piece is taken away at pack-up, and what a chest held would go with it
			if (!form->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Furniture, RE::FormType::Activator)) {
				logger::warn("Kits: {}: {} is not furniture, a static or an activator, left out (containers are not supported)", where, k.form);
				return;
			}
			k.p.form = id;
			g_kitPieces.push_back(k.p);
			logger::info("Kits: piece {} added ({:08X}, {:.0f} along and {:.0f} across from the fire, turned {:.0f}, {})", k.p.name, id, k.p.along, k.p.across,
				k.p.turn, k.p.mound ? "on a mound of its own" : "on the ground as it is");
		};
		std::vector<RE::FormID> added;
		std::vector<RE::FormID> removed;
		for (const auto& file : files) {
			const auto fname = file.filename().string();
			std::ifstream in(file);
			std::string line, section;
			bool firstLine = true;
			while (std::getline(in, line)) {
				// a file saved as "UTF-8 with BOM" (Notepad's choice): the mark before its first section went with it
				if (firstLine && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
					line.erase(0, 3);
				}
				firstLine = false;
				if (const auto c = line.find(';'); c != std::string::npos) {
					line.erase(c);
				}
				// (a value keeps its inner spaces: tent names and model paths have them)
				auto trim = [](std::string a_s) {
					const auto b = a_s.find_first_not_of(" \t\r\n");
					const auto e = a_s.find_last_not_of(" \t\r\n");
					return b == std::string::npos ? std::string{} : a_s.substr(b, e - b + 1);
				};
				const auto rawEq = line.find('=');
				const std::string rawValue = rawEq == std::string::npos ? std::string{} : trim(line.substr(rawEq + 1));
				std::erase_if(line, [](unsigned char c) { return std::isspace(c); });
				if (line.empty()) {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = lower(line.substr(1, line.size() - 2));
					finishTent();
					finishPiece();
					if (section == "tent") {
						tent.open = true;
						tentFile = fname;
					} else if (section == "piece") {
						piece.open = true;
						pieceFile = fname;
					} else if (section != "mound" && section != "firespots") {
						// a misspelled section would otherwise be skipped without a word (the Kit Guide's Common traps)
						logger::warn("Kits: {} [{}]: not a kit section ([Mound], [FireSpots], [Tent] or [Piece]), its lines are skipped", fname, section);
					}
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				const auto name = lower(line.substr(0, eq));
				const auto value = line.substr(eq + 1);
				if (section == "mound") {
					const auto it = std::ranges::find_if(keys, [&](const Key& k) { return name == k.name; });
					if (it == std::end(keys)) {
						logger::warn("Kits: {} [Mound]: unknown key {}, skipped", fname, name);
						continue;
					}
					float v = 0.0f;
					try {
						v = std::stof(value);
					} catch (...) {
						logger::warn("Kits: {} [Mound] {}: not a number, skipped", fname, name);
						continue;
					}
					if (!std::isfinite(v) || v < it->lo || v > it->hi) {
						const float c = std::isfinite(v) ? std::clamp(v, it->lo, it->hi) : *it->value;
						logger::warn("Kits: {} [Mound] {} = {} is outside {} to {}: {} used", fname, name, v, it->lo, it->hi, c);
						v = c;
					}
					*it->value = v;
					moundSet = true;
				} else if (section == "tent") {
					auto number = [&](float& a_out, float a_lo, float a_hi) {
						try {
							const float v = std::stof(value);
							if (!std::isfinite(v)) {
								throw std::invalid_argument("");  // (nan, inf: not a number here)
							}
							a_out = std::clamp(v, a_lo, a_hi);
							if (v != a_out) {
								logger::warn("Kits: {} [Tent] {} = {} is outside {} to {}: {} used", fname, name, v, a_lo, a_hi, a_out);
							}
						} catch (...) {
							logger::warn("Kits: {} [Tent] {}: not a number", fname, name);
							tent.bad = true;
						}
					};
					if (name == "name") {
						tent.name = rawValue;
					} else if (name == "model") {
						tent.model = rawValue;
					} else if (name == "form") {
						tent.form = rawValue;
					} else if (name == "width") {
						number(tent.width, 20.0f, 2000.0f);
					} else if (name == "depth") {
						number(tent.depth, 20.0f, 2000.0f);
					} else if (name == "size") {
						number(tent.size, 0.5f, 2.0f);
					} else if (name == "winter") {
						const auto v = lower(value);
						tent.winter = v == "yes" || v == "true" || v == "1";
					} else if (name == "door") {
						const auto v = lower(value);
						if (v == "front") {
							tent.door = 0.0f;
						} else if (v == "left") {
							tent.door = 90.0f;  // (as camp.ini's tentNdoor: degrees from the model's front; to check in game)
						} else if (v == "right") {
							tent.door = -90.0f;
						} else if (v == "back") {
							tent.door = 180.0f;
						} else {
							number(tent.door, -360.0f, 360.0f);
						}
					} else {
						logger::warn("Kits: {} [Tent]: unknown key {}, skipped", fname, name);
					}
				} else if (section == "piece") {
					auto number = [&](float& a_out) {
						try {
							a_out = std::stof(value);
							if (!std::isfinite(a_out)) {
								throw std::invalid_argument("");
							}
						} catch (...) {
							logger::warn("Kits: {} [Piece] {}: not a number", fname, name);
							piece.bad = true;
						}
					};
					if (name == "name") {
						piece.p.name = rawValue;
					} else if (name == "form") {
						piece.form = rawValue;
					} else if (name == "along") {
						number(piece.p.along);
						piece.p.along = std::clamp(piece.p.along, -7000.0f, 7000.0f);  // (packing up reaches 8000 out)
					} else if (name == "across") {
						number(piece.p.across);
						piece.p.across = std::clamp(piece.p.across, -7000.0f, 7000.0f);
					} else if (name == "turn") {
						number(piece.p.turn);
						piece.p.turn = std::fmod(piece.p.turn, 360.0f);  // (a huge number overflowed when turned into an angle)
					} else if (name == "mound") {
						const auto v = lower(value);
						piece.p.mound = !(v == "no" || v == "false" || v == "0");
					} else {
						logger::warn("Kits: {} [Piece]: unknown key {}, skipped", fname, name);
					}
				} else if (section == "firespots") {
					spotsSet = true;
					if (name == "count") {
						try {
							const int n = std::stoi(value);
							g_kitFireSpotCount = std::clamp(n, 0, 12);
							if (n != g_kitFireSpotCount) {
								logger::warn("Kits: {} [FireSpots] Count = {} is outside 0 to 12: {} used", fname, n, g_kitFireSpotCount);
							}
						} catch (...) {
							logger::warn("Kits: {} [FireSpots] Count: not a number, skipped", fname);
						}
						continue;
					}
					if (name != "add" && name != "remove") {
						logger::warn("Kits: {} [FireSpots]: unknown key {}, skipped", fname, name);
						continue;
					}
					std::stringstream list(rawValue);
					std::string item;
					while (std::getline(list, item, ',')) {
						{
							const auto b = item.find_first_not_of(" \t");
							const auto e = item.find_last_not_of(" \t");
							item = b == std::string::npos ? std::string{} : item.substr(b, e - b + 1);
						}
						if (item.empty()) {
							continue;
						}
						if (name == "remove") {
							// ours by name (all: every one of ours); Camping's own two spots are never touched
							const auto low = lower(item);
							const RE::FormID which = low == "warmhandscrouch" ? WARM_CROUCH : low == "warmhandsstand" ? WARM_STAND : low == "sitcrosslegged" ? SIT_GROUND : 0;
							if (low == "all") {
								removed.insert(removed.end(), { WARM_CROUCH, SIT_GROUND, WARM_STAND });
							} else if (which) {
								removed.push_back(which);
							} else {
								logger::warn("Kits: {} [FireSpots] Remove: {} is not one of ours (WarmHandsCrouch, WarmHandsStand, SitCrossLegged or All)", fname, item);
							}
							continue;
						}
						const auto id = KitForm(item, fname, "[FireSpots] Add");
						auto form = id ? RE::TESForm::LookupByID(id) : nullptr;
						if (!form) {
							continue;
						}
						if (!form->Is(RE::FormType::IdleMarker, RE::FormType::Furniture)) {
							logger::warn("Kits: {} [FireSpots] Add: {} is not an idle marker or furniture, skipped", fname, item);
							continue;
						}
						added.push_back(id);
					}
				}
			}
			finishTent();  // a [Tent] or [Piece] at the end of the file
			finishPiece();
			logger::info("Kits: read {}", file.generic_string());
		}
		// the mound's shape
		if (m.lowerSlope < m.upperSlope) {
			logger::warn("Kits: [Mound] LowerSlope {} is gentler than UpperSlope {}: LowerSlope set to {}", m.lowerSlope, m.upperSlope, m.upperSlope);
			m.lowerSlope = m.upperSlope;
		}
		constexpr float deg = std::numbers::pi_v<float> / 180.0f;
		m.g1 = std::tan(m.upperSlope * deg);
		m.g2 = std::tan(m.lowerSlope * deg);
		m.inset = m.shoulder * 18.0f / 64.0f;  // our shoulder starts 18 of its 64 inside the top's edge
		m.reach = 0.0f;
		while (m.reach < 20000.0f && MoundDrop(m.reach, 1.0f) < m.depth) {
			m.reach += 5.0f;
		}
		if (moundSet) {
			logger::info("Kits: mound shape: slopes {:.0f} then {:.0f} degrees, turning {:.0f} down over {:.0f}, shoulder {:.0f}, bulge {:.0f}, depth {:.0f} (sides reach {:.0f} out)",
				m.upperSlope, m.lowerSlope, m.turnDepth, m.turnSoftness, m.shoulder, m.bulge, m.depth, m.reach);
		}
		// the spots round the fire: ours less the removed ones, then the kits' (each kind once), in turn
		std::erase_if(g_fireSpotKinds, [&](RE::FormID a_id) { return std::ranges::find(removed, a_id) != removed.end(); });
		for (const auto id : added) {
			if (std::ranges::find(g_fireSpotKinds, id) == g_fireSpotKinds.end()) {
				g_fireSpotKinds.push_back(id);
			}
			if (std::ranges::find(g_kitSpotKinds, id) == g_kitSpotKinds.end() && id != WARM_CROUCH && id != WARM_STAND && id != SIT_GROUND) {
				g_kitSpotKinds.push_back(id);
			}
		}
		if (spotsSet) {
			std::string kinds;
			for (const auto id : g_fireSpotKinds) {
				kinds += std::format("{}{:08X}", kinds.empty() ? "" : ", ", id);
			}
			logger::info("Kits: spots round the fire: {} kinds in turn ({}){}", g_fireSpotKinds.size(), kinds.empty() ? "none" : kinds,
				g_kitFireSpotCount >= 0 ? std::format(", {} wanted", g_kitFireSpotCount) : std::string{});
		}
	}

	// a camp mound read back from its reference (FoundationBase's models: 256 across and 25.6 x length long at scale 1,
	// top at the origin): its top's centre, heading, half sizes and scale, as LayCampFoundation laid it
	struct MoundShape
	{
		RE::NiPoint3 c;
		float h, hs, hl, scale;
	};
	bool ReadMound(RE::TESObjectREFR* a_ref, MoundShape& a_out)
	{
		auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base) {
			return false;
		}
		for (int len = 10; len <= 40; ++len) {
			if (IsCampBase(base, FoundationID(len, false)) || IsCampBase(base, FoundationID(len, true))) {
				const float sc = a_ref->GetScale();
				a_out = { a_ref->GetPosition(), a_ref->GetAngleZ(), 128.0f * sc, 12.8f * len * sc, sc };
				return true;
			}
		}
		return false;
	}
	// how far a mound's outline bulges out at a point of its edge (local x, y on its own axes), as CampFoundation.nif is
	// made (mound.cpp): a slow, irregular swell by the angle round the top (as LayCampFoundation's moundBulge)
	float MoundBulgeAt(const MoundShape& a_md, float a_x, float a_y)
	{
		const float a = std::atan2(a_y / a_md.hl, a_x / a_md.hs);
		const float f = (0.55f * std::sin(2 * a + 0.9f) + 0.35f * std::sin(3 * a + 2.3f)) / 0.9f;  // (as the models since 2026-10-31: the quicker wobbles were dropped, the 5 and 7 times round too, which squeezed into the short sides as streaks)
		return a_md.scale * g_moundShape.bulge * std::clamp(0.5f + 0.5f * f, 0.0f, 1.0f);
	}
	// the height of the mounds' surface at a spot (the highest of them there), or -max where none reaches
	float MoundSurfaceAt(const std::vector<MoundShape>& a_mounds, float a_x, float a_y)
	{
		float best = -std::numeric_limits<float>::max();
		for (const auto& md : a_mounds) {
			const float dx = a_x - md.c.x, dy = a_y - md.c.y;
			const float x = dx * std::cos(md.h) - dy * std::sin(md.h), y = dx * std::sin(md.h) + dy * std::cos(md.h);
			const float ex = (std::max)(0.0f, std::abs(x) - md.hs), ey = (std::max)(0.0f, std::abs(y) - md.hl);
			const float e = (std::max)(0.0f, std::sqrt(ex * ex + ey * ey) - MoundBulgeAt(md, x, y));
			if (e > (g_moundShape.reach + 60.0f) * md.scale) {
				continue;  // past the foot of its sides
			}
			best = (std::max)(best, md.c.z - MoundDrop(e, md.scale));
		}
		return best;
	}
	// anyone standing inside a camp's earth mound (it came up round them as the camp was made, or the game put a
	// follower on the ground under it to catch up with you) is lifted onto its surface. Done when a camp is made and
	// every second while you are outdoors (the mounds are the ones whose texture we lay: g_moundTexture)
	// (a_justMade: the camp being made: its mounds are still hidden while they settle, so hidden ones count too.
	// a_pieces: that camp's pieces (the tent, the seat, the fire...): someone the camp was pitched on top of is moved
	// out of them, to a clear spot beside you)
	void LiftActorsOutOfMounds(bool a_justMade = false, const std::vector<GroundBox>* a_pieces = nullptr)
	{
		auto player = Player();
		if (!player || g_moundTexture.empty()) {
			return;
		}
		const auto me = player->GetPosition();
		std::vector<MoundShape> mounds;
		for (const auto& [id, t] : g_moundTexture) {
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			MoundShape md;
			// (pieces are persistent now: a far camp's mound stays in memory, so only this worldspace's count)
			if (ref && ref->GetWorldspace() == player->GetWorldspace() && (a_justMade || !ref->IsDisabled()) && !ref->IsDeleted() && !ref->IsMarkedForDeletion() && ref->GetPosition().GetDistance(me) < 6000.0f &&
				ReadMound(ref, md)) {
				mounds.push_back(md);
			}
		}
		if (mounds.empty() && !a_pieces) {
			return;
		}
		auto inPiece = [&](float a_x, float a_y) {
			const GroundBox me{ a_x, a_y, 1.0f, 0.0f, 45.0f, 45.0f };  // a person, or a wolf turned any way, and a step more
			return a_pieces && std::ranges::any_of(*a_pieces, [&](const GroundBox& a_box) { return BoxesOverlap(me, a_box); });
		};
		// beside you (you pitched the camp in front of you): your right, your left, behind you..., clear of the pieces
		auto clearSpot = [&](RE::NiPoint3& a_out) {
			const auto p = player->GetPosition();
			const float h = player->GetAngleZ();
			for (const float turn : { 90.0f, -90.0f, 180.0f, 135.0f, -135.0f, 45.0f, -45.0f }) {
				for (const float dist : { 80.0f, 140.0f }) {
					const float a = h + turn * std::numbers::pi_v<float> / 180.0f;
					const float x = p.x + dist * std::sin(a), y = p.y + dist * std::cos(a);
					if (!inPiece(x, y)) {
						a_out = { x, y, (std::max)(p.z, MoundSurfaceAt(mounds, x, y) + 4.0f) };
						return true;
					}
				}
			}
			return false;
		};
		auto lift = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded() || a_actor->GetWorldspace() != player->GetWorldspace()) {
				return;
			}
			const auto at = a_actor->GetPosition();
			// later (not as the camp is made) only someone well under the ground is lifted: the real slopes are lumpy and
			// can lie a little under the estimate, and you are never put under a camp by the game
			if (!a_justMade && a_actor == player) {
				return;
			}
			if (a_pieces && a_actor != player && at.GetDistance(player->GetPosition()) < 1500.0f) {
				logger::info("Camp: {} at ({:.0f}, {:.0f}, {:.0f}) as the camp was made{}", a_actor->GetDisplayFullName(), at.x, at.y, at.z,
					inPiece(at.x, at.y) ? ", where a piece now stands" : "");
			}
			if (a_actor != player && at.GetDistance(player->GetPosition()) < 1500.0f && inPiece(at.x, at.y)) {
				if (RE::NiPoint3 to; clearSpot(to)) {
					a_actor->SetPosition(to, true);
					logger::info("Camp: the camp was pitched on {}: moved beside you", a_actor->GetDisplayFullName());
					return;
				}
			}
			const float surf = MoundSurfaceAt(mounds, at.x, at.y);
			// well inside it (not just a foot in its slope), and not far below it (a cave or a cellar under the hill)
			if (surf != -std::numeric_limits<float>::max() && at.z < surf - (a_justMade ? 8.0f : 50.0f) && surf - at.z < 1600.0f) {
				a_actor->SetPosition({ at.x, at.y, surf + 4.0f }, true);
				logger::info("Camp: {} stood inside a camp mound: lifted onto it", a_actor == player ? "you" : a_actor->GetDisplayFullName());
			}
		};
		lift(player);
		if (auto lists = RE::ProcessLists::GetSingleton()) {
			for (const auto& h : lists->highActorHandles) {
				if (auto a = h.get(); a && a.get() != player) {
					lift(a.get());
				}
			}
		}
	}

	float WaterHeightAt(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_at);

	// spots round the fire for a big party: firespots (camp.ini, default 6) more besides Camping's own two, in a ring
	// facing the flames, crouching and standing to warm hands and sitting on the ground in turn; none on a camp piece,
	// on another spot or off the mound's top. Followers use them as they use Camping's (its follower when you sit on
	// its seat, the rest of your party while you stand about), and so can mods that let the player use idle markers
	void PlaceFireSpots(RE::TESObjectREFR* a_campfire)
	{
		const auto settings = CampSettings();
		// the player's own firespots line wins over a kit's Count, which wins over our 6
		const int wanted = g_fireSpotKinds.empty() ? 0 :
			std::clamp(static_cast<int>(CampSetting(settings, "firespots", g_kitFireSpotCount >= 0 ? static_cast<float>(g_kitFireSpotCount) : 6.0f)), 0, 12);
		auto tes = RE::TES::GetSingleton();
		if (!a_campfire || !tes || wanted == 0) {
			return;
		}
		const auto fire = a_campfire->GetPosition();
		const float reach = CampSetting(settings, "firespotreach", 90.0f);
		auto shelter = CampShelter(a_campfire);
		std::vector<GroundBox> pieces;
		std::vector<RE::NiPoint3> spots;  // the spots there are already (Camping's), and ours as they go down
		tes->ForEachReferenceInRange(a_campfire, 500.0f, [&](RE::TESObjectREFR* a_ref) {
			auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || a_ref == a_campfire || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || !a_ref->IsDynamicForm()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (base->Is(RE::FormType::IdleMarker) || IsFireSpot(base)) {
				spots.push_back(a_ref->GetPosition());
			} else if (!IsFoundation(base) && !IsCampNavCut(base) && !IsCampGrass(base) && !IsCampRug(base) &&
					   base->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Furniture, RE::FormType::Activator, RE::FormType::Container)) {
				pieces.push_back(BoxOf(a_ref, shelter, 12.0f));
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		// the mounds' top here (the spots stand on it; GroundZ looks through our mounds to the hill under them)
		std::vector<MoundShape> mounds;
		for (const auto& [id, t] : g_moundTexture) {
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			MoundShape md;
			if (ref && !ref->IsDeleted() && !ref->IsMarkedForDeletion() && ref->GetPosition().GetDistance(fire) < 3000.0f && ReadMound(ref, md)) {
				mounds.push_back(md);
			}
		}
		float fireGround = fire.z;
		GroundZ(fire, fireGround, shelter, nullptr, 300.0f);
		const float top = (std::max)(fireGround, MoundSurfaceAt(mounds, fire.x, fire.y));
		const auto& KINDS = g_fireSpotKinds;  // ours (crouching, sitting, standing) and the kits', less the ones a kit removed
		std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x2545F491u);
		const float start = std::uniform_real_distribution<float>(0.0f, 6.2832f)(rng);
		int placed = 0;
		for (int i = 0; i < 36 && placed < wanted; ++i) {
			// round the ring in 10-degree steps from a random start, taking every free place
			const float a = start + i * std::numbers::pi_v<float> / 18.0f;
			const RE::NiPoint3 p{ fire.x + reach * std::sin(a), fire.y + reach * std::cos(a), top };
			const GroundBox me{ p.x, p.y, 1.0f, 0.0f, 28.0f, 28.0f };  // a person crouched or sitting
			if (std::ranges::any_of(pieces, [&](const GroundBox& a_box) { return BoxesOverlap(me, a_box); }) ||
				std::ranges::any_of(spots, [&](const RE::NiPoint3& a_s) { return std::hypot(a_s.x - p.x, a_s.y - p.y) < 60.0f; })) {
				continue;
			}
			// on the camp's level top, not out on a mound's slope or over the edge
			float ground = p.z;
			const float surf = (std::max)(GroundZ(p, ground, shelter, nullptr, 300.0f) ? ground : -std::numeric_limits<float>::max(), MoundSurfaceAt(mounds, p.x, p.y));
			if (surf == -std::numeric_limits<float>::max() || std::abs(surf - top) > 12.0f) {
				continue;
			}
			auto base = RE::TESForm::LookupByID<RE::TESBoundObject>(KINDS[placed % KINDS.size()]);
			auto spot = base ? PlaceOwned(a_campfire, base, 0, PieceRole::kSpot).get() : nullptr;
			if (!spot) {
				continue;
			}
			MoveRef(spot, { p.x, p.y, surf }, { 0.0f, 0.0f, std::atan2(fire.x - p.x, fire.y - p.y) });  // facing the fire
			g_campSpots.push_back(spot->GetFormID());  // packed up by reference, whatever its kind
			spots.push_back(p);
			++placed;
		}
		logger::info("Camp: {} more spots round the fire to warm hands or sit ({} wanted)", placed, wanted);
	}

	// the camp being packed up: followers standing on its mounds would drop when the mounds vanish, so first they are
	// moved off them, onto the real ground behind you (out of view: you face the camp as you pack it up), a step apart
	void MoveFollowersOffMounds(std::uint32_t a_camp)
	{
		auto player = Player();
		if (!player || g_moundTexture.empty()) {
			return;
		}
		const auto me = player->GetPosition();
		std::vector<MoundShape> mounds;
		for (const auto& [id, t] : g_moundTexture) {
			// (only the mounds of the camp being packed up: another camp's stay)
			if (const auto it = g_campOwned.find(id); it == g_campOwned.end() || it->second.camp != a_camp) {
				continue;
			}
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			MoundShape md;
			if (ref && ref->GetWorldspace() == player->GetWorldspace() && !ref->IsDeleted() && ref->GetPosition().GetDistance(me) < 6000.0f && ReadMound(ref, md)) {
				mounds.push_back(md);
			}
		}
		if (mounds.empty()) {
			return;
		}
		// the real ground at a spot (GroundZ looks through our mounds), and whether a mound covers it
		auto offMound = [&](float a_x, float a_y, float& a_z) {
			if (!GroundZ({ a_x, a_y, me.z }, a_z, nullptr, nullptr, 600.0f)) {
				return false;
			}
			return MoundSurfaceAt(mounds, a_x, a_y) < a_z + 5.0f;
		};
		// the real ground under you: a follower's spot must be about as high, so it is the same ground you stand on, not a
		// rock top or a ledge above or below
		float myGround = me.z;
		GroundZ(me, myGround, nullptr, nullptr, 600.0f);
		auto tes = RE::TES::GetSingleton();
		// a safe spot: off the mounds, near your own ground's height, out of the water, and level all round (no drop
		// within two steps, so nobody is set down at a ledge or a cliff edge and walks off it)
		auto safe = [&](float a_x, float a_y, float& a_z) {
			if (!offMound(a_x, a_y, a_z) || std::abs(a_z - myGround) > 120.0f) {
				return false;
			}
			if (auto cell = tes ? tes->GetCell({ a_x, a_y, a_z }) : nullptr) {
				if (const float water = WaterHeightAt(cell, { a_x, a_y, a_z }); water > a_z - 5.0f && water < a_z + 1000.0f) {
					return false;
				}
			}
			for (int k = 0; k < 12; ++k) {
				const float a = k * std::numbers::pi_v<float> / 6.0f;
				for (const float d : { 80.0f, 160.0f }) {
					float g = 0.0f;
					if (!GroundZ({ a_x + d * std::sin(a), a_y + d * std::cos(a), a_z }, g, nullptr, nullptr, 600.0f) || g < a_z - 0.6f * d) {
						return false;  // falls away steeper than about 30 degrees
					}
				}
			}
			return true;
		};
		std::vector<RE::NiPoint3> taken;
		// behind you first (out of view: you face the camp as you pack it up), then anywhere round you
		auto spotBehind = [&](RE::NiPoint3& a_out) {
			const float h = player->GetAngleZ();
			for (const bool anywhere : { false, true }) {
				for (const float dist : { 250.0f, 350.0f, 450.0f, 600.0f, 800.0f }) {
					for (float turn = anywhere ? 0.0f : 120.0f; turn <= (anywhere ? 359.0f : 240.0f); turn += anywhere ? 30.0f : 20.0f) {
						const float a = h + turn * std::numbers::pi_v<float> / 180.0f;
						const float x = me.x + dist * std::sin(a), y = me.y + dist * std::cos(a);
						float z = 0.0f;
						if (!safe(x, y, z)) {
							continue;
						}
						const RE::NiPoint3 p{ x, y, z + 2.0f };
						if (std::ranges::any_of(taken, [&](const RE::NiPoint3& a_t) { return a_t.GetDistance(p) < 90.0f; })) {
							continue;
						}
						a_out = p;
						return true;
					}
				}
			}
			return false;
		};
		// you too: on a mound well above the ground (a tall one on a slope) you would drop as it goes, so you are set
		// down just above the real ground under you first, a short step down instead of a fall (only when the real
		// ground under you was found: a failed look left it at 0, and you were set down at height 20)
		if (float ground = 0.0f; GroundZ({ me.x, me.y, me.z }, ground, nullptr, nullptr, 600.0f)) {
			if (MoundSurfaceAt(mounds, me.x, me.y) >= ground + 5.0f && me.z - ground > CampSetting(CampSettings(), "packdropmax", 40.0f)) {
				const float lift = CampSetting(CampSettings(), "packdropto", 20.0f);
				player->SetPosition({ me.x, me.y, ground + lift }, true);
				logger::info("Camp: you stood {:.0f} above the ground on the camp's mound: set down just above it", me.z - ground);
			}
		}
		auto lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}
		for (const auto& h : lists->highActorHandles) {
			auto a = h.get();
			if (!a || a.get() == player || !a->IsPlayerTeammate() || a->IsDead() || !a->Is3DLoaded() || a->GetPosition().GetDistance(me) > 3000.0f) {
				continue;
			}
			const auto at = a->GetPosition();
			float ground = 0.0f;
			// (the real ground under them not found: left where they stand, never set down at a height of 0)
			if (!GroundZ({ at.x, at.y, at.z }, ground, nullptr, nullptr, 600.0f) || MoundSurfaceAt(mounds, at.x, at.y) < ground + 5.0f ||
				at.z < ground + 10.0f) {
				continue;  // on the ground already, not on a mound
			}
			if (RE::NiPoint3 to; spotBehind(to)) {
				a->SetPosition(to, true);
				taken.push_back(to);
				logger::info("Camp: {} moved off the camp's mound before it goes, to safe ground near you", a->GetDisplayFullName());
			} else {
				// no safe ground near: set down where they stand, on the real ground under them (no fall), never
				// somewhere unknown
				a->SetPosition({ at.x, at.y, ground + 2.0f }, true);
				logger::info("Camp: {} set down on the ground under the mound (no safe spot near you)", a->GetDisplayFullName());
			}
		}
	}

