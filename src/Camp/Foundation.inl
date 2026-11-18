// Part of the shared camp code (Free Cooking's CookingMenu.cpp, Solid Ground Camping's main.cpp): included in order inside an anonymous namespace (not compiled on its own). The earth mound under the camp (LayCampFoundation).
	// a piece of this camp: one it recorded (placed by us, or Camping's own, noted as the camp went up). Anything else
	// near the camp (another mod's bush, chair, barrel or marker) is never moved onto the mound
	bool IsMoundPiece(RE::TESObjectREFR* a_ref)
	{
		return IsCampPiece(a_ref);
	}

	void LayCampFoundation(RE::TESObjectREFR* a_campfire)
	{
		const auto settings = CampSettings();
		auto tes = RE::TES::GetSingleton();
		if (CampSetting(settings, "foundation", 1.0f) != 1.0f || !tes || !a_campfire) {
			return;
		}
		CompactCamp(a_campfire);
		const auto fire = a_campfire->GetPosition();
		// the block turns to whatever angle needs the least of it (tried every 5 degrees); h is its axes' heading
		float h = a_campfire->GetAngleZ();
		const auto shelter = CampShelter(a_campfire);
		const auto fireCamp = CampOf(a_campfire);
		// the camp's pieces: what Camping and we placed around this fire (not people, animals or dropped items)
		std::vector<RE::TESObjectREFR*> pieces;
		std::vector<std::pair<float, float>> outline{ { fire.x, fire.y } };  // the pieces' corners on the ground
		// the chopping block stands out at the camp's edge: it gets a small mound of its own (joined to the main one)
		// instead of stretching the main one over the empty ground between (CampSetting blockmound)
		RE::TESObjectREFR* choppingBlock = nullptr;
		const bool blockMound = CampSetting(settings, "blockmound", 1.0f) != 0.0f;
		// the tent likewise: the main mound then only has to hold the fire, the pot and the seat, and the tent (with its
		// bedroll and rug) stands on one turned with it, so no empty corners are left where the camp's pieces sit at angles
		const bool tentMound = shelter && CampSetting(settings, "tentmound", 1.0f) != 0.0f;
		const GroundBox tentBox = shelter ? BoxOf(shelter, shelter, 0.0f) : GroundBox{};
		auto inTent = [&](const RE::NiPoint3& a_p) {
			const float dx = a_p.x - tentBox.cx, dy = a_p.y - tentBox.cy;
			return std::abs(dx * tentBox.ux + dy * tentBox.uy) < tentBox.hx && std::abs(-dx * tentBox.uy + dy * tentBox.ux) < tentBox.hy;
		};
		auto extend = [&](float a_x, float a_y) { outline.emplace_back(a_x, a_y); };
		tes->ForEachReferenceInRange(a_campfire, 450.0f, [&](RE::TESObjectREFR* a_ref) {
			auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || !a_ref->IsDynamicForm() || a_ref->IsMarkedForDeletion() || IsFoundation(base) || IsCampNavCut(base) || IsCampGrass(base) ||
				!base->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Furniture, RE::FormType::Light,
					RE::FormType::Activator, RE::FormType::Sound, RE::FormType::IdleMarker, RE::FormType::Container)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			// a kit piece gets a mound of its own below (or none), however far out it stands; another mod's object stays
			// (this fire's camp's pieces only: an older camp's recorded piece standing near is not laid on or moved)
			if (IsPitchKitPiece(a_ref) || (a_ref != a_campfire && a_ref != shelter && (!IsMoundPiece(a_ref) || CampOf(a_ref) != fireCamp))) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			// the camp's own pieces only: a disabled one is the tent or its bedroll, about to show (other disabled
			// things nearby, like the plants the camp hid, are not dynamic)
			pieces.push_back(a_ref);
			const auto pos = a_ref->GetPosition();
			// the fire counts as its ring of stones (its furniture bounds reach out to its sitting spots); its light,
			// sound and markers do not count
			if (a_ref == a_campfire) {
				for (const auto& [x, y] : { std::pair{ 75.0f, 75.0f }, { -75.0f, 75.0f }, { 75.0f, -75.0f }, { -75.0f, -75.0f } }) {
					extend(pos.x + x, pos.y + y);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (base->Is(RE::FormType::Light, RE::FormType::Sound, RE::FormType::IdleMarker) || !base->Is(RE::FormType::Static, RE::FormType::MovableStatic, RE::FormType::Furniture, RE::FormType::Container, RE::FormType::Activator)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (blockMound && IsCampBase(base, BLOCK_PLACED)) {
				choppingBlock = a_ref;
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (tentMound && (a_ref == shelter || inTent(pos))) {
				return RE::BSContainer::ForEachResult::kContinue;  // on the tent's own mound
			}
			const auto& b = PieceBounds(a_ref, shelter);
			const float k = a_ref->GetScale() * (a_ref == shelter ? TentOutline() : 1.0f);  // hides and poles reach past its bounds
			const float rz = a_ref->GetAngleZ();
			if (b.boundMax.x > b.boundMin.x && b.boundMax.y > b.boundMin.y) {
				for (const float x : { static_cast<float>(b.boundMin.x), static_cast<float>(b.boundMax.x) }) {
					for (const float y : { static_cast<float>(b.boundMin.y), static_cast<float>(b.boundMax.y) }) {
						extend(pos.x + k * (x * std::cos(rz) + y * std::sin(rz)), pos.y + k * (-x * std::sin(rz) + y * std::cos(rz)));
					}
				}
			} else if (!tentMound) {
				extend(pos.x, pos.y);  // a marker: only its spot (nothing you see: no ground needed for it with the camp's own mounds)
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (pieces.empty()) {
			return;
		}
		float minS = 0.0f, maxS = 0.0f, minA = 0.0f, maxA = 0.0f;  // along the block's heading h, and across it
		auto measure = [&](float a_h) {
			minS = minA = std::numeric_limits<float>::max();
			maxS = maxA = -std::numeric_limits<float>::max();
			for (const auto& [x, y] : outline) {
				const float dx = x - fire.x, dy = y - fire.y;
				const float along = dx * std::sin(a_h) + dy * std::cos(a_h), across = dx * std::cos(a_h) - dy * std::sin(a_h);
				minS = (std::min)(minS, along); maxS = (std::max)(maxS, along);
				minA = (std::min)(minA, across); maxA = (std::max)(maxA, across);
			}
			return (maxS - minS) * (maxA - minA);
		};
		// every mound of the camp is laid square to the tent (the main one and the chopping block's too), so their
		// textures can run on across them
		const bool squareToTent = shelter && CampSetting(settings, "tentmound", 1.0f) != 0.0f && CampSetting(settings, "blockmound", 1.0f) != 0.0f;
		if (squareToTent) {
			h = shelter->GetAngleZ();
			measure(h);  // (its long side, along or across, is chosen below)
		} else {
			const float start = h;
			float bestArea = measure(start);
			for (int i = 1; i < 18; ++i) {  // a box turned 90 degrees is the same box, so 0 to 85 degrees covers all
				const float tryH = start + i * 5.0f * std::numbers::pi_v<float> / 180.0f;
				if (const float area = measure(tryH); area < bestArea) {
					bestArea = area;
					h = tryH;
				}
			}
			measure(h);
		}
		const float H = h;  // the camp's texture heading
		// trimmed: the pieces' outline and a small rim. The block's long side follows the camp's longer side, and the
		// model whose shape is nearest (never shorter) is used
		const float rim = CampSetting(settings, "foundationmargin", 0.0f);
		const float needS = maxS - minS + rim * 2.0f, needA = maxA - minA + rim * 2.0f;
		const bool longAlong = needS >= needA;
		const float needLong = (std::max)(needS, needA), needShort = (std::min)(needS, needA);
		const int length = std::clamp(static_cast<int>(std::ceil(needLong / needShort * 10.0f - 0.01f)), 10, 40);
		auto base = FoundationBase(length, !longAlong);  // laid a quarter round from H: the turned model
		if (!base) {
			return;
		}
		// a mound laid at a_heading round a_c: its texture as if laid at H, and in step with the world
		auto layTexture = [&](RE::TESObjectREFR* a_mound, const RE::NiPoint3& a_c, float a_heading) {
			const float q = std::round((a_heading - H) / (std::numbers::pi_v<float> / 2.0f));
			const int quarter = ((static_cast<int>(q) % 4) + 4) % 4;
			const float sign = quarter >= 2 ? -1.0f : 1.0f;
			const float tile = (std::max)(16.0f, CampSetting(settings, "moundtile", LandTextureRepeat()));
			const float ou = (a_c.x * std::cos(H) - a_c.y * std::sin(H)) / tile, ov = -(a_c.x * std::sin(H) + a_c.y * std::cos(H)) / tile;
			g_moundTexture[a_mound->GetFormID()] = { sign, ou - std::floor(ou), ov - std::floor(ov) };
		};
		auto quarterTurned = [&](float a_heading) {
			const int quarter = ((static_cast<int>(std::round((a_heading - H) / (std::numbers::pi_v<float> / 2.0f))) % 4) + 4) % 4;
			return (quarter & 1) != 0;
		};
		const float fscale = std::clamp((std::max)(needShort / 256.0f, needLong / (25.6f * length)), 0.2f, 4.0f);
		const float halfShort = 128.0f * fscale, halfLong = 12.8f * length * fscale;
		const float midS = (minS + maxS) * 0.5f, midA = (minA + maxA) * 0.5f;
		const RE::NiPoint3 centre{ fire.x + midS * std::sin(h) + midA * std::cos(h), fire.y + midS * std::cos(h) - midA * std::sin(h), fire.z };
		const float fh = h + (longAlong ? 0.0f : std::numbers::pi_v<float> / 2.0f);
		// its top: level with the highest ground under the camp's own pieces (the tent's corners and middle, the
		// bedroll, each other piece), so none of them is buried on a slope; ground elsewhere under it does not count
		// (a rock at an empty corner just pokes through). No higher than its depth allows above the lowest ground
		float top = -std::numeric_limits<float>::max(), low = std::numeric_limits<float>::max();
		for (auto piece : pieces) {
			const auto pos = piece->GetPosition();
			std::vector<RE::NiPoint3> spots{ pos };
			if (piece == shelter) {
				// the tent's footprint, a little inside its outline
				const auto& b = PieceBounds(piece, shelter);
				const float k = piece->GetScale() * 0.7f, rz = piece->GetAngleZ();
				for (const float x : { static_cast<float>(b.boundMin.x), static_cast<float>(b.boundMax.x) }) {
					for (const float y : { static_cast<float>(b.boundMin.y), static_cast<float>(b.boundMax.y) }) {
						spots.push_back({ pos.x + k * (x * std::cos(rz) + y * std::sin(rz)), pos.y + k * (-x * std::sin(rz) + y * std::cos(rz)), pos.z });
					}
				}
			}
			for (const auto& spot : spots) {
				if (float ground = 0.0f; GroundZ(spot, ground, shelter) && std::abs(ground - fire.z) < 300.0f) {
					top = (std::max)(top, ground);
				}
			}
		}
		for (int i = -2; i <= 2; ++i) {
			for (int j = -2; j <= 2; ++j) {
				const float x = halfShort * i / 2.0f, y = halfLong * j / 2.0f;
				RE::NiPoint3 p{ centre.x + x * std::cos(fh) + y * std::sin(fh), centre.y - x * std::sin(fh) + y * std::cos(fh), fire.z };
				if (float ground = 0.0f; GroundZ(p, ground, shelter)) {
					low = (std::min)(low, ground);
				}
			}
		}
		float fireGround = fire.z;
		GroundZ(fire, fireGround, shelter);
		if (top == -std::numeric_limits<float>::max()) {
			top = fireGround;
		}
		if (low == std::numeric_limits<float>::max()) {
			low = top;
		}
		const float depth = g_moundShape.depth * fscale;  // CampFoundation.nif goes 1536 down at scale 1 (a kit's [Mound] Depth)
		// (raised when the tent and the chopping block get mounds of their own: theirs are a step or two lower)
		// (and as far as the kits' strips go down: each takes two steps; before, a camp with a few kit pieces had strips
		// whose tops sank under flat ground)
		int kitStrips = 0;
		for (const auto& [id, index] : g_pitchKitPieces) {
			kitStrips += index < g_kitPieces.size() && g_kitPieces[index].mound ? 1 : 0;
		}
		const float dropFirst = CampSetting(settings, "mounddrop", 4.0f), dropStep = CampSetting(settings, "mounddropstep", 2.5f);
		const int levels = (tentMound ? 1 : 0) + (blockMound ? 1 : 0) + 2 * kitStrips;
		const float floorZ = (std::min)(top, low + depth * 0.9f) + 2.0f +
			(std::max)((tentMound || blockMound) ? dropFirst + dropStep : 0.0f, kitStrips > 0 ? dropFirst + dropStep * (levels - 1) : 0.0f);
		auto foundation = PlaceOwned(a_campfire, base).get();
		if (!foundation) {
			return;
		}
		// its uneven outline (MoundBulge) one way round or the other, so camps do not all share one shape
		const float meshH = fh + ((a_campfire->GetFormID() & 1u) ? std::numbers::pi_v<float> : 0.0f);
		MoveRef(foundation, { centre.x, centre.y, floorZ }, { 0.0f, 0.0f, meshH });
		ScaleCampPiece(foundation, fscale);
		layTexture(foundation, centre, meshH);
		// low lumps of earth here and there on the top, around the pieces (never under one), so it is not dead flat
		std::vector<RE::TESObjectREFR*> patches;
		if (auto patchBase = PluginItem(DIRT_PATCH); patchBase && CampSetting(settings, "dirtpatches", 14.0f) > 0.0f) {
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()));
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			std::vector<GroundBox> taken;
			for (auto piece : pieces) {
				if (piece->GetBaseObject() && !piece->GetBaseObject()->Is(RE::FormType::Light, RE::FormType::Sound, RE::FormType::IdleMarker)) {
					taken.push_back(piece == a_campfire ? GroundBox{ fire.x, fire.y, 1.0f, 0.0f, 85.0f, 85.0f } : BoxOf(piece, shelter, 8.0f));
				}
			}
			const int wanted = static_cast<int>(CampSetting(settings, "dirtpatches", 14.0f));
			for (int attempt = 0; attempt < wanted * 12 && static_cast<int>(patches.size()) < wanted; ++attempt) {
				const float size = 0.6f + unit(rng) * 0.8f;
				const float reach = 45.0f * size;
				const float x = (unit(rng) * 2.0f - 1.0f) * (halfShort - reach * 0.6f), y = (unit(rng) * 2.0f - 1.0f) * (halfLong - reach * 0.6f);
				// a little into the top: its rim crosses the surface instead of lying flush on it
				const RE::NiPoint3 at{ centre.x + x * std::cos(fh) + y * std::sin(fh), centre.y - x * std::sin(fh) + y * std::cos(fh), floorZ - CampSetting(settings, "patchsink", 1.0f) };
				const GroundBox box{ at.x, at.y, 1.0f, 0.0f, reach, reach };
				if (std::ranges::any_of(taken, [&](const GroundBox& a_box) { return BoxesOverlap(box, a_box); })) {
					continue;
				}
				if (auto patch = PlaceOwned(a_campfire, patchBase).get()) {
					// square to the camp's other mounds (one way round or the other), its texture in step with theirs
					const float patchH = H + (unit(rng) < 0.5f ? 0.0f : std::numbers::pi_v<float>);
					MoveRef(patch, at, { 0.0f, 0.0f, patchH });
					ScaleCampPiece(patch, size);
					layTexture(patch, at, patchH);
					patches.push_back(patch);
					taken.push_back({ at.x, at.y, 1.0f, 0.0f, reach * 0.7f, reach * 0.7f });  // lumps may run into each other a little
				}
			}
		}
		// the outline made uneven: a few smaller mounds, each turned a little, half sunk into the main one's edges, their
		// tops a hair lower (hidden where they overlap it); no straight edge or right-angled corner shows for long
		struct Mound
		{
			RE::NiPoint3 c;
			float h, hs, hl, scale;
		};
		std::vector<Mound> mounds{ { { centre.x, centre.y, floorZ }, meshH, halfShort, halfLong, fscale } };
		std::vector<RE::TESObjectREFR*> extras;
		// a piece's own mound (the tent, the chopping block): its outline and a rim, turned with it, stretched on the side
		// facing the camp until it runs into the main mound, so its top goes on level from the main one's (a hair under
		// it, hidden where they overlap) with no dip between; deep enough to reach the ground under it
		float moundDrop_ = 0.0f;  // (how far below the main top the last mound's top went)
		auto attachMound = [&](RE::TESObjectREFR* a_piece, float a_outline, const char* a_what, bool a_aim = false) {
			const auto& b = PieceBounds(a_piece, shelter);
			const float k = a_piece->GetScale() * a_outline;
			float rz = a_piece->GetAngleZ();
			const auto pos = a_piece->GetPosition();
			float x0 = b.boundMin.x * k - rim, x1 = b.boundMax.x * k + rim, y0 = b.boundMin.y * k - rim, y1 = b.boundMax.y * k + rim;
			if (a_aim) {
				// a kit piece: its strip runs straight at the main mound's middle, whichever way the piece is turned or
				// however far to the side it stands (before, along one of its own axes with its side offset kept, so it
				// could run past the camp). Its own patch is a square round all of it
				float r = 0.0f;
				for (const float x : { static_cast<float>(b.boundMin.x), static_cast<float>(b.boundMax.x) }) {
					for (const float y : { static_cast<float>(b.boundMin.y), static_cast<float>(b.boundMax.y) }) {
						r = (std::max)(r, std::hypot(x, y) * k);
					}
				}
				rz = std::atan2(centre.x - pos.x, centre.y - pos.y);
				x0 = y0 = -r - rim;
				x1 = y1 = r + rim;
			}
			auto world = [&](float a_x, float a_y) {
				return RE::NiPoint3{ pos.x + a_x * std::cos(rz) + a_y * std::sin(rz), pos.y - a_x * std::sin(rz) + a_y * std::cos(rz), floorZ };
			};
			auto inMain = [&](const RE::NiPoint3& a_p) {
				const float dx = a_p.x - centre.x, dy = a_p.y - centre.y;
				const float lx = dx * std::cos(fh) - dy * std::sin(fh), ly = dx * std::sin(fh) + dy * std::cos(fh);
				const float in = 40.0f * fscale;
				return std::abs(lx) < halfShort - in && std::abs(ly) < halfLong - in;
			};
			// its side facing the main mound's middle, pushed out until that side's middle is on the main top
			const float cxl = 0.5f * (x0 + x1), cyl = 0.5f * (y0 + y1);
			const auto mid = world(cxl, cyl);
			const float tx = (centre.x - mid.x) * std::cos(rz) - (centre.y - mid.y) * std::sin(rz);  // towards the main middle, in its own axes
			const float ty = (centre.x - mid.x) * std::sin(rz) + (centre.y - mid.y) * std::cos(rz);
			// (as far as it takes: a kit piece may stand far out, no limit; his call 2026-11-16)
			const float growMax = (std::max)(500.0f, std::hypot(centre.x - mid.x, centre.y - mid.y) + 500.0f);
			for (float grow = 0.0f; grow < growMax; grow += 5.0f) {
				RE::NiPoint3 face{};
				if (std::abs(tx) >= std::abs(ty)) {
					face = tx > 0.0f ? world(x1 + grow, cyl) : world(x0 - grow, cyl);
				} else {
					face = ty > 0.0f ? world(cxl, y1 + grow) : world(cxl, y0 - grow);
				}
				if (inMain(face) || grow + 5.0f >= growMax) {
					if (std::abs(tx) >= std::abs(ty)) {
						(tx > 0.0f ? x1 : x0) += tx > 0.0f ? grow : -grow;
					} else {
						(ty > 0.0f ? y1 : y0) += ty > 0.0f ? grow : -grow;
					}
					break;
				}
			}
			// a long strip is laid as a chain of mounds overlapping a little (the models go no longer than four times their
			// width: one stretched over it would be twice as wide as needed). Each mound's top a little lower than the
			// one before (the main one first), so overlapping tops never flicker; the pieces on them come down with them
			const bool alongY = (y1 - y0) >= (x1 - x0);
			const float L0 = alongY ? y0 : x0, L1 = alongY ? y1 : x1;
			const float W0 = alongY ? x0 : y0, W1 = alongY ? x1 : y1;
			const float shrt = (std::max)(1.0f, 0.5f * (W1 - W0));
			const int count = std::clamp(static_cast<int>(std::ceil((L1 - L0) / (8.0f * shrt) - 0.01f)), 1, 32);
			const float seg = (L1 - L0) / count;
			const float overlap = (std::min)(20.0f, 0.25f * seg);
			const float eh = rz + (alongY ? 0.0f : std::numbers::pi_v<float> / 2.0f);
			float pieceDrop = 0.0f;
			bool pieceFound = false;  // (the segment under the piece found: the last one is only the fallback)
			float stripDrop = 0.0f;
			for (int c = 0; c < count; ++c) {
				const float s0 = L0 + c * seg - (c > 0 ? overlap : 0.0f), s1 = L0 + (c + 1) * seg;
				const float lng = 0.5f * (s1 - s0);
				const float mL = 0.5f * (s0 + s1), mW = 0.5f * (W0 + W1);
				const auto at = alongY ? world(mW, mL) : world(mL, mW);
				// lower than the main top (and than each other) by a clear step, so where they overlap the higher one
				// buries the lower: one visible surface at every spot. Two surfaces at the same height flicker however
				// well their textures match; the camp's floor is raised by as much, so the lower ones stay above the
				// ground Within one strip only neighbours overlap, so its segments take two heights in turn: before,
				// each went a step lower than the last, and a far kit piece's strip of up to 32 ended about 80 below
				// the floor. Each new strip still starts below every one laid before it
				const float step = CampSetting(settings, "mounddropstep", 2.5f);
				if (c == 0) {
					moundDrop_ = moundDrop_ == 0.0f ? CampSetting(settings, "mounddrop", 4.0f) : moundDrop_ + step;
					stripDrop = moundDrop_;
				}
				const float segDrop = stripDrop + ((c % 2) ? step : 0.0f);
				moundDrop_ = (std::max)(moundDrop_, segDrop);
				const RE::NiPoint3 top{ at.x, at.y, floorZ - segDrop };
				const int len = std::clamp(static_cast<int>(std::ceil((std::max)(lng, shrt) / (std::min)(lng, shrt) * 10.0f - 0.01f)), 10, 40);
				// (a short segment, wider than long, turns a quarter round)
				const float segH = eh + (lng >= shrt ? 0.0f : std::numbers::pi_v<float> / 2.0f);
				const float longHalf = (std::max)(lng, shrt), shortHalf = (std::min)(lng, shrt);
				const float sc = std::clamp((std::max)(shortHalf / 128.0f, longHalf / (12.8f * len)), 0.2f, 4.0f);
				// the piece itself stands on the segment holding its own spot
				const float px = -at.x + pos.x, py = -at.y + pos.y;
				if (!pieceFound && (std::abs(px * std::sin(eh) + py * std::cos(eh)) <= lng + 1.0f || c == count - 1)) {
					pieceDrop = segDrop;
					pieceFound = true;
				}
				if (auto extraBase = FoundationBase(len, quarterTurned(segH))) {
					if (auto extra = PlaceOwned(a_campfire, extraBase).get()) {
						MoveRef(extra, top, { 0.0f, 0.0f, segH });
						ScaleCampPiece(extra, sc);
						layTexture(extra, top, segH);
						HideCampPiece(extra);
						extras.push_back(extra);
						mounds.push_back({ top, segH, 128.0f * sc, 12.8f * len * sc, sc });
						logger::info("Camp: the {} on a mound of its own, part {} of {} ({:.0f} by {:.0f}, scale {:.2f})", a_what, c + 1, count, 25.6f * len * sc,
							256.0f * sc, sc);
					}
				}
			}
			return pieceDrop;
		};
		std::unordered_map<RE::TESObjectREFR*, float> sunk;  // pieces standing on a lower mound, and how much lower
		if (choppingBlock && squareToTent) {
			// the block turned square to the tent too (its nearest way round: you chop at most an eighth turned)
			const float quarterTurn = std::numbers::pi_v<float> / 2.0f;
			auto a = choppingBlock->GetAngle();
			a.z = H + std::round((a.z - H) / quarterTurn) * quarterTurn;
			MoveRef(choppingBlock, choppingBlock->GetPosition(), a);
		}
		if (tentMound) {
			// (the tent's own footprint: its poles and guy lines may reach into the hill)
			const float d = attachMound(shelter, CampSetting(settings, "tentmoundoutline", 1.0f), "tent");
			for (auto piece : pieces) {
				if (piece == shelter || inTent(piece->GetPosition())) {
					sunk[piece] = d;
				}
			}
		}
		if (choppingBlock) {
			sunk[choppingBlock] = attachMound(choppingBlock, 1.0f, "chopping block") + 1.0f;  // its foot a little into the mound
		}
		// the kits' pieces: each on a mound of its own joined to the camp's, settled on it with the rest below; a piece
		// whose kit says Mound = no stays on the ground where it was put
		for (const auto& [id, index] : g_pitchKitPieces) {
			auto piece = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			if (!piece || index >= g_kitPieces.size() || !g_kitPieces[index].mound) {
				continue;
			}
			// on ground higher than the strip would be there (a far piece up a slope): it stays on the ground as it is,
			// as with Mound = no, instead of going under (above the lowest strip's top, not 4 below it: on flat ground
			// that test skipped every strip)
			if (float ground = 0.0f; GroundZ(piece->GetPosition(), ground, shelter) && ground > floorZ - dropFirst - dropStep * (levels - 1)) {
				logger::info("Camp: kit piece {} stands on ground higher than its strip of mound would be: no mound for it", g_kitPieces[index].name);
				continue;
			}
			const auto before = extras.size();
			sunk[piece] = attachMound(piece, 1.0f, g_kitPieces[index].name.c_str(), true) + 1.0f;
			pieces.push_back(piece);
			// its strip of mound may reach further from the fire than the camp's own pieces: listed like the piece, so it
			// is packed up with the camp by reference and never taken for a stray when the area loads again
			for (auto i = before; i < extras.size(); ++i) {
				if (extras[i]) {
					g_campSpots.push_back(extras[i]->GetFormID());
				}
			}
		}
		{
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x5BD1E995u);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			std::array<int, 4> edges{ 0, 1, 2, 3 };
			std::shuffle(edges.begin(), edges.end(), rng);
			const int wanted = std::clamp(static_cast<int>(CampSetting(settings, "foundationextras", 0.0f)), 0, 4);
			for (int i = 0; i < wanted; ++i) {
				const int len = 10 + static_cast<int>(unit(rng) * 5.0f);
				auto extraBase = FoundationBase(len);
				if (!extraBase) {
					continue;
				}
				const float sc = fscale * (0.42f + 0.2f * unit(rng));
				const float ehs = 128.0f * sc, ehl = 12.8f * len * sc;
				const float eh = fh + (unit(rng) - 0.5f) * 0.9f;  // up to about 26 degrees off
				// a point on one of the main mound's edges, and out from it so the extra mound overlaps it by about half
				float lx = 0.0f, ly = 0.0f;
				const float slide = (unit(rng) - 0.5f) * 1.2f;
				const float out = 0.45f * (std::min)(ehs, ehl);
				switch (edges[i]) {
				case 0: lx = slide * halfShort; ly = halfLong + out; break;
				case 1: lx = slide * halfShort; ly = -halfLong - out; break;
				case 2: lx = halfShort + out; ly = slide * halfLong; break;
				default: lx = -halfShort - out; ly = slide * halfLong; break;
				}
				const RE::NiPoint3 at{ centre.x + lx * std::cos(fh) + ly * std::sin(fh), centre.y - lx * std::sin(fh) + ly * std::cos(fh), floorZ - 0.6f };
				// only where the ground is about as high as the camp floor: on the downhill side its top would stand up
				// out of the slope as a bald hump
				{
					float lowest = std::numeric_limits<float>::max();
					for (int a = -1; a <= 1; ++a) {
						for (int b = -1; b <= 1; ++b) {
							const float x = a * ehs * 0.8f, y = b * ehl * 0.8f;
							RE::NiPoint3 q{ at.x + x * std::cos(eh) + y * std::sin(eh), at.y - x * std::sin(eh) + y * std::cos(eh), at.z };
							if (float ground = 0.0f; GroundZ(q, ground, shelter)) {
								lowest = (std::min)(lowest, ground);
							}
						}
					}
					if (lowest == std::numeric_limits<float>::max() || at.z - lowest > CampSetting(settings, "extrarise", 15.0f)) {
						continue;
					}
				}
				if (auto extra = PlaceOwned(a_campfire, extraBase).get()) {
					MoveRef(extra, at, { 0.0f, 0.0f, eh });
					ScaleCampPiece(extra, sc);
					HideCampPiece(extra);
					extras.push_back(extra);
					mounds.push_back({ at, eh, ehs, ehl, sc });
				}
			}
		}
		// the mound's side (CampFoundation.nif, at scale 1): gentle near the top, steeper further down (mound.cpp); how far below the top it is a_e out from the top's edge, and how far out it is a_d below the top
		auto moundDrop = [](float a_e, float a_scale) { return MoundDrop(a_e, a_scale); };
		// how far its outline bulges out at a point of its edge (local x, y on the mound's own axes), as CampFoundation.nif
		// is made (mound.cpp): a slow, irregular swell by the angle round the top, up to 70 at scale 1
		auto moundBulge = [](const Mound& a_md, float a_x, float a_y) {
			const float a = std::atan2(a_y / a_md.hl, a_x / a_md.hs);
			const float f = (0.55f * std::sin(2 * a + 0.9f) + 0.35f * std::sin(3 * a + 2.3f)) / 0.9f;  // (as the models since 2026-10-31: the quicker wobbles were dropped, the 5 and 7 times round too, which squeezed into the short sides as streaks)
			return a_md.scale * g_moundShape.bulge * std::clamp(0.5f + 0.5f * f, 0.0f, 1.0f);
		};
		auto moundSpread = [&](float a_d, float a_scale) {
			// how far out the side is a_d below the top: the profile above, turned round (it only ever falls)
			float lo = 0.0f, hi = (g_moundShape.reach + 160.0f) * a_scale;
			for (int i = 0; i < 30; ++i) {
				const float mid = 0.5f * (lo + hi);
				(moundDrop(mid, a_scale) < a_d ? lo : hi) = mid;
			}
			return lo;
		};
		auto underMound = [&](const RE::NiPoint3& a_p, size_t a_except) {
			for (size_t m = 0; m < mounds.size(); ++m) {
				if (m == a_except) {
					continue;
				}
				const auto& md = mounds[m];
				const float dx = a_p.x - md.c.x, dy = a_p.y - md.c.y;
				const float x = dx * std::cos(md.h) - dy * std::sin(md.h), y = dx * std::sin(md.h) + dy * std::cos(md.h);
				const float spread = moundSpread(md.c.z - a_p.z, md.scale) + moundBulge(md, x, y);
				if (std::abs(x) < md.hs + spread - 4.0f && std::abs(y) < md.hl + spread - 4.0f) {
					return true;
				}
			}
			return false;
		};
		// out from a point on a mound's edge (local x, y; outward nx, ny) until its side meets the ground: where, and how far out
		auto meetGround = [&](const Mound& a_md, float a_lx, float a_ly, float a_nx, float a_ny, RE::NiPoint3& a_at, float& a_ground, float& a_out) {
			for (float e = 0.0f; e < g_moundShape.reach + 160.0f; e += 5.0f) {
				const float x = a_lx + a_nx * e, y = a_ly + a_ny * e;
				a_at = { a_md.c.x + x * std::cos(a_md.h) + y * std::sin(a_md.h), a_md.c.y - x * std::sin(a_md.h) + y * std::cos(a_md.h), a_md.c.z };
				if (!GroundZ(a_at, a_ground, shelter)) {
					return false;
				}
				if (a_ground >= a_md.c.z - moundDrop(e - moundBulge(a_md, a_lx, a_ly), a_md.scale) - 1.0f) {
					a_out = e;
					return true;
				}
			}
			return false;
		};
		// plants where the mounds meet the hill, only of the kinds that already grow here (none on rock, sand, snow or ice), so the line between
		// earth heap and hillside is broken up; none on another mound or in a camp piece
		std::vector<RE::TESObjectREFR*> plants;
		if (CampSetting(settings, "foundationplants", 1.0f) != 0.0f) {
			std::vector<RE::TESBoundObject*> kinds;
			tes->ForEachReferenceInRange(a_campfire, 700.0f, [&](RE::TESObjectREFR* a_ref) {
				auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
				// never a plant you can pick (mountain flowers, lavender and the like are trees with something to harvest):
				// a camp must not hand out free ingredients
				auto tree = base ? base->As<RE::TESObjectTREE>() : nullptr;
				if (tree && tree->produceItem) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (base && !a_ref->IsDynamicForm() && IsPlantModel(base) && std::ranges::find(kinds, base) == kinds.end()) {
					const auto& b = base->boundData;
					if ((std::max)(b.boundMax.x - b.boundMin.x, b.boundMax.y - b.boundMin.y) < 220 && b.boundMax.z < 140) {
						kinds.push_back(base);
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::vector<GroundBox> keepOut;
			for (auto piece : pieces) {
				if (piece == a_campfire) {
					keepOut.push_back({ fire.x, fire.y, 1.0f, 0.0f, 80.0f, 80.0f });
				} else if (piece->GetBaseObject() && !piece->GetBaseObject()->Is(RE::FormType::Light, RE::FormType::Sound, RE::FormType::IdleMarker)) {
					keepOut.push_back(BoxOf(piece, shelter, 5.0f));
				}
			}
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x9E3779B9u);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			for (size_t m = 0; m < mounds.size() && !kinds.empty(); ++m) {
				const auto& md = mounds[m];
				for (int side = 0; side < 4; ++side) {
					const float edgeLen = 2.0f * (side < 2 ? md.hs : md.hl);
					for (float t = -edgeLen * 0.5f; t <= edgeLen * 0.5f; t += 40.0f) {
						if (unit(rng) < 0.3f) {
							continue;
						}
						float lx = 0, ly = 0, nx = 0, ny = 0;
						switch (side) {
						case 0: lx = t; ly = md.hl; ny = 1; break;
						case 1: lx = t; ly = -md.hl; ny = -1; break;
						case 2: lx = md.hs; ly = t; nx = 1; break;
						default: lx = -md.hs; ly = t; nx = -1; break;
						}
						// out along the side until the slope meets the ground
						RE::NiPoint3 p{};
						float ground = md.c.z, out = 0.0f;
						if (!meetGround(md, lx, ly, nx, ny, p, ground, out)) {
							continue;
						}
						p.x += (unit(rng) - 0.5f) * 20.0f;
						p.y += (unit(rng) - 0.5f) * 20.0f;
						p.z = ground - 2.0f;
						const GroundBox spot{ p.x, p.y, 1.0f, 0.0f, 25.0f, 25.0f };
						if (underMound(p, m) || std::ranges::any_of(keepOut, [&](const GroundBox& a_box) { return BoxesOverlap(spot, a_box); })) {
							continue;
						}
						auto kind = kinds[static_cast<size_t>(unit(rng) * kinds.size()) % kinds.size()];
						if (auto plant = PlaceOwned(a_campfire, kind).get()) {
							MoveRef(plant, p, { 0.0f, 0.0f, unit(rng) * 6.283f });
							ScaleCampPiece(plant, 0.6f + 0.5f * unit(rng));
							plants.push_back(plant);
						}
					}
				}
			}
			logger::info("Camp: {} more mounds and {} plants round the foundation ({} kinds of plant)", extras.size(), plants.size(), kinds.size());
		}
		// the one ground texture all the camp's mounds wear: what shows round the campfire. (Tried 2026-10-30: the ground
		// round the mounds' foot, weighed by how tall the side rises there; on a camp across two grounds it picked the
		// tall side's and missed the one the eye holds the mound against. One texture can't match two grounds.)
		RE::TESLandTexture* campLand = CampLandTexture(a_campfire);
		// grass on the mounds, as the hill round them has: tufts of the grass that grows on the ground texture the mounds
		// wear (the one they are painted with), scattered over their tops and sides wherever they stand above the hill,
		// clear of the camp's pieces. None where that ground grows no grass (bare dirt, rock, snow). grass = 0 turns it
		// off; grassdensity: tufts per 100 x 100 units; grassmax: at most this many per camp
		std::vector<RE::TESObjectREFR*> tufts;
		if (CampSetting(settings, "grass", 1.0f) != 0.0f) {
			std::vector<RE::TESBoundObject*> kinds;
			if (auto ground = campLand) {
				for (auto grass : ground->textureGrassList) {
					if (auto tuft = GrassTuftFor(grass); tuft && std::ranges::find(kinds, tuft) == kinds.end()) {
						kinds.push_back(tuft);
					}
				}
			}
			std::vector<GroundBox> clear;
			for (auto piece : pieces) {
				if (piece == a_campfire) {
					clear.push_back({ fire.x, fire.y, 1.0f, 0.0f, 85.0f, 85.0f });
				} else if (piece->GetBaseObject() && !piece->GetBaseObject()->Is(RE::FormType::Light, RE::FormType::Sound, RE::FormType::IdleMarker)) {
					clear.push_back(BoxOf(piece, shelter, 15.0f));
				}
			}
			// the mounds' surface at a spot: the highest of them there
			auto surfaceAt = [&](float a_x, float a_y) {
				float best = -std::numeric_limits<float>::max();
				for (const auto& md : mounds) {
					const float dx = a_x - md.c.x, dy = a_y - md.c.y;
					const float x = dx * std::cos(md.h) - dy * std::sin(md.h), y = dx * std::sin(md.h) + dy * std::cos(md.h);
					const float ex = (std::max)(0.0f, std::abs(x) - md.hs), ey = (std::max)(0.0f, std::abs(y) - md.hl);
					const float e = (std::max)(0.0f, std::sqrt(ex * ex + ey * ey) - moundBulge(md, x, y));
					if (e < (g_moundShape.reach + 60.0f) * md.scale) {
						best = (std::max)(best, md.c.z - moundDrop(e, md.scale));
					}
				}
				return best;
			};
			const float density = (std::max)(0.0f, CampSetting(settings, "grassdensity", 5.0f)) / 10000.0f;
			const size_t most = static_cast<size_t>((std::max)(0.0f, CampSetting(settings, "grassmax", 220.0f)));
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x6A09E667u);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			for (const auto& md : mounds) {
				if (kinds.empty() || tufts.size() >= most) {
					break;
				}
				// the mound's top and as far down its sides as they can show (a few hundred units round its top)
				const float reach = 320.0f * md.scale;
				const float ax = md.hs + reach, ay = md.hl + reach;
				const int tries = static_cast<int>(4.0f * ax * ay * density);
				for (int i = 0; i < tries && tufts.size() < most; ++i) {
					const float x = (unit(rng) * 2.0f - 1.0f) * ax, y = (unit(rng) * 2.0f - 1.0f) * ay;
					RE::NiPoint3 p{ md.c.x + x * std::cos(md.h) + y * std::sin(md.h), md.c.y - x * std::sin(md.h) + y * std::cos(md.h), 0.0f };
					p.z = surfaceAt(p.x, p.y);
					float hill = 0.0f;
					if (p.z < md.c.z - 2000.0f || !GroundZ(p, hill, shelter) || p.z < hill + 4.0f) {
						continue;  // under the hill there (the mound doesn't show), or no ground to compare with
					}
					// only where this mound is the one on top (each spot is tried by one mound)
					{
						const float dx = p.x - md.c.x, dy = p.y - md.c.y;
						const float lx = dx * std::cos(md.h) - dy * std::sin(md.h), ly = dx * std::sin(md.h) + dy * std::cos(md.h);
						const float ex = (std::max)(0.0f, std::abs(lx) - md.hs), ey = (std::max)(0.0f, std::abs(ly) - md.hl);
						const float e = (std::max)(0.0f, std::sqrt(ex * ex + ey * ey) - moundBulge(md, lx, ly));
						if (md.c.z - moundDrop(e, md.scale) < p.z - 0.5f) {
							continue;
						}
					}
					const GroundBox spot{ p.x, p.y, 1.0f, 0.0f, 12.0f, 12.0f };
					if (std::ranges::any_of(clear, [&](const GroundBox& a_box) { return BoxesOverlap(spot, a_box); })) {
						continue;
					}
					p.z -= 2.0f;  // its roots a little in the earth
					auto kind = kinds[static_cast<size_t>(unit(rng) * kinds.size()) % kinds.size()];
					if (auto tuft = PlaceOwned(a_campfire, kind).get()) {
						MoveRef(tuft, p, { 0.0f, 0.0f, unit(rng) * 6.283f });
						ScaleCampPiece(tuft, 0.75f + 0.5f * unit(rng));
						tufts.push_back(tuft);
					}
				}
			}
			logger::info("Camp: {} tufts of grass on the mounds ({} kinds)", tufts.size(), kinds.size());
		}
		// the hill's own ground carried up onto the foot of the mounds' sides: soft splashes painted with the ground texture
		// right there (grass, snow, sand, rock), laid on the slope where it meets the hill, so earth heap and hillside blend
		// instead of meeting at a hard line
		std::vector<RE::TESObjectREFR*> blends;
		if (auto blendBase = PluginItem(GROUND_BLEND); blendBase && CampSetting(settings, "groundblend", 1.0f) != 0.0f) {
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x27D4EB2Fu);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			const float step = (std::max)(20.0f, CampSetting(settings, "groundblendstep", 55.0f));
			// how high the ground you see is at a spot: the hill (our own mounds are looked through by GroundZ) or a
			// mound's top or side, whichever is higher
			auto surfaceAt = [&](const RE::NiPoint3& a_q) {
				float top = -std::numeric_limits<float>::max();
				if (float ground = 0.0f; GroundZ(a_q, ground, shelter)) {
					top = ground;
				}
				for (const auto& o : mounds) {
					const float dx = a_q.x - o.c.x, dy = a_q.y - o.c.y;
					const float x = dx * std::cos(o.h) - dy * std::sin(o.h), y = dx * std::sin(o.h) + dy * std::cos(o.h);
					const float ex = (std::max)(0.0f, std::abs(x) - o.hs), ey = (std::max)(0.0f, std::abs(y) - o.hl);
					const float e = std::sqrt(ex * ex + ey * ey) - moundBulge(o, x, y);
					top = (std::max)(top, o.c.z - moundDrop(e, o.scale));
				}
				return top;
			};
			// a splash (about 70 across its middle at scale 1) standing clear of the ground anywhere round its rim
			const float floatLimit = CampSetting(settings, "groundblendfloat", 10.0f);
			auto blendFloats = [&](const RE::NiPoint3& a_at, const RE::NiPoint3& a_up, const RE::NiPoint3& a_across, float a_size) {
				for (int k = 0; k < 12; ++k) {
					const float a = k * std::numbers::pi_v<float> / 6.0f;
					for (const float f : { 0.5f, 1.0f }) {
						const RE::NiPoint3 q = a_at + a_up * (70.0f * a_size * f * std::sin(a)) + a_across * (70.0f * a_size * f * std::cos(a));
						if (q.z - surfaceAt(q) > floatLimit) {
							return true;
						}
					}
				}
				return false;
			};
			int floating = 0;
			for (size_t m = 0; m < mounds.size(); ++m) {
				const auto& md = mounds[m];
				for (int side = 0; side < 4; ++side) {
					const float edgeLen = 2.0f * (side < 2 ? md.hs : md.hl);
					for (float t = -edgeLen * 0.5f + 15.0f * md.scale; t <= edgeLen * 0.5f - 15.0f * md.scale; t += step * (0.8f + 0.4f * unit(rng))) {
						float lx = 0, ly = 0, nx = 0, ny = 0;
						switch (side) {
						case 0: lx = t; ly = md.hl; ny = 1; break;
						case 1: lx = t; ly = -md.hl; ny = -1; break;
						case 2: lx = md.hs; ly = t; nx = 1; break;
						default: lx = -md.hs; ly = t; nx = -1; break;
						}
						RE::NiPoint3 p{};
						float ground = md.c.z, out = 0.0f;
						if (!meetGround(md, lx, ly, nx, ny, p, ground, out)) {
							continue;
						}
						out -= moundBulge(md, lx, ly);  // from the edge of the top as it bulges here
						if (out < 3.0f) {
							continue;  // the hill is as high as the top here: it covers the edge itself
						}
						p.z = md.c.z - moundDrop(out, md.scale);
						if (underMound(p, m)) {
							continue;
						}
						// the slope there: its outward direction in the world, its steepness, and the splash laid flat on it
						const float wx = nx * std::cos(md.h) + ny * std::sin(md.h), wy = -nx * std::sin(md.h) + ny * std::cos(md.h);
						const float u = out / md.scale;
						const float theta = std::atan((moundDrop((u + 1.0f) * md.scale, md.scale) - moundDrop((u - 1.0f) * md.scale, md.scale)) / (2.0f * md.scale));
						const float size = 0.7f + 0.6f * unit(rng);
						const RE::NiPoint3 up{ -wx * std::cos(theta), -wy * std::cos(theta), std::sin(theta) };   // up the slope
						const RE::NiPoint3 normal{ wx * std::sin(theta), wy * std::sin(theta), std::cos(theta) };  // out of it
						const RE::NiPoint3 across = up.Cross(normal);
						// its middle a little up the slope: its lower half runs on under the hill
						const float lift = 0.15f + 0.25f * unit(rng);
						// (flat, it lies on the side only where it touches: on a steep hill the ground falls away below
						// that line faster than the side curves down, and its lower edge stood out into the air like a
						// blade. Smaller till all of it lies on a mound or under the hill, else none here)
						float fit = 0.0f;
						for (const float k : { 1.0f, 0.7f, 0.5f }) {
							const float sz = size * k;
							if (!blendFloats(p + up * (64.0f * sz * lift) + normal * 0.6f, up, across, sz)) {
								fit = sz;
								break;
							}
						}
						if (fit == 0.0f) {
							++floating;
							continue;
						}
						const RE::NiPoint3 at = p + up * (64.0f * fit * lift) + normal * 0.6f;
						// the engine turns a reference by its angles as Rx(-x) Ry(-y) Rz(-z): the angles for these axes
						const float Y = std::asin(std::clamp(normal.x, -1.0f, 1.0f));
						const float X = std::atan2(-normal.y, normal.z);
						const float Z = std::atan2(-up.x, across.x);
						if (auto blend = PlaceOwned(a_campfire, blendBase).get()) {
							MoveRef(blend, at, { -X, -Y, -Z });
							ScaleCampPiece(blend, fit);
							blends.push_back(blend);
							if (blends.size() == 1) {
								const auto handle = blend->GetHandle();
								After(1500, [handle, normal]() {
									auto b = handle.get();
									auto root = b ? b->Get3D() : nullptr;
									if (root) {
										const auto& r = root->world.rotate;
										logger::info("Camp test: ground blend faces ({:.2f}, {:.2f}, {:.2f}), wanted ({:.2f}, {:.2f}, {:.2f})",
											r.entry[0][2], r.entry[1][2], r.entry[2][2], normal.x, normal.y, normal.z);
									}
								});
							}
						}
					}
				}
			}
			logger::info("Camp: {} splashes of the hill's ground on the foot of the mounds ({} left out: they would stand out over a steep hill)", blends.size(), floating);
		}
		// where one mound's top edge runs over another's top, a line shows (their textures are laid on each its own
		// way, and their tops a hair apart): soft splashes of the mound's own ground laid flat along it hide it
		std::vector<RE::TESObjectREFR*> seams;
		if (auto blendBase = PluginItem(GROUND_BLEND); blendBase && mounds.size() > 1 && CampSetting(settings, "seamsplashes", 0.0f) != 0.0f) {
			std::mt19937 rng(static_cast<std::uint32_t>(a_campfire->GetFormID()) ^ 0x632BE5ABu);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			auto onTop = [&](const Mound& a_md, const RE::NiPoint3& a_p) {
				const float dx = a_p.x - a_md.c.x, dy = a_p.y - a_md.c.y;
				const float x = dx * std::cos(a_md.h) - dy * std::sin(a_md.h), y = dx * std::sin(a_md.h) + dy * std::cos(a_md.h);
				return std::abs(x) < a_md.hs - 5.0f && std::abs(y) < a_md.hl - 5.0f;
			};
			for (size_t m = 0; m < mounds.size(); ++m) {
				const auto& md = mounds[m];
				for (int side = 0; side < 4; ++side) {
					const float edgeLen = 2.0f * (side < 2 ? md.hs : md.hl);
					for (float t = -edgeLen * 0.5f; t <= edgeLen * 0.5f; t += 45.0f * (0.8f + 0.4f * unit(rng))) {
						float lx = 0, ly = 0;
						switch (side) {
						case 0: lx = t; ly = md.hl; break;
						case 1: lx = t; ly = -md.hl; break;
						case 2: lx = md.hs; ly = t; break;
						default: lx = -md.hs; ly = t; break;
						}
						const RE::NiPoint3 p{ md.c.x + lx * std::cos(md.h) + ly * std::sin(md.h), md.c.y - lx * std::sin(md.h) + ly * std::cos(md.h), md.c.z };
						for (size_t k = 0; k < mounds.size(); ++k) {
							if (k != m && onTop(mounds[k], p)) {
								const RE::NiPoint3 at{ p.x, p.y, (std::max)(md.c.z, mounds[k].c.z) + 0.3f };
								if (auto splash = PlaceOwned(a_campfire, blendBase).get()) {
									MoveRef(splash, at, { 0.0f, 0.0f, unit(rng) * 6.283f });
									ScaleCampPiece(splash, 0.9f + 0.4f * unit(rng));
									seams.push_back(splash);
								}
								break;
							}
						}
					}
				}
			}
			logger::info("Camp: {} splashes over the seams between the mounds", seams.size());
		}
		// every piece onto the floor, flat: the tent and its bedroll on it, the rest keeping their height above the
		// ground they stood on (the fire's light, sound and flames hang above it, the pot hangs on its stand)
		for (auto piece : pieces) {
			auto at = piece->GetPosition();
			const auto pieceBase = piece->GetBaseObject();
			if (piece == shelter) {
				at.z = floorZ;
			} else if (pieceBase && (pieceBase->GetFormID() == BEDROLL_STATIC || pieceBase == CampingForm(0x819))) {
				at.z = floorZ + CampSetting(settings, "bedlift", 1.0f) + 0.8f;  // on the rug (the straw mat is 0.75 thick)
			} else if (IsCampRug(pieceBase)) {
				at.z = floorZ + 0.2f;
			} else {
				// the fire's own parts (its light, sound and flames hang above it) keep their height above the fire;
				// everything else stands right on the floor
				const float dx = at.x - fire.x, dy = at.y - fire.y;
				if (dx * dx + dy * dy < 30.0f * 30.0f) {
					at.z = floorZ + std::clamp(at.z - fire.z, 0.0f, 150.0f);
				} else {
					at.z = floorZ;
				}
			}
			if (auto it = sunk.find(piece); it != sunk.end()) {
				at.z -= it->second;  // on its own mound, a little lower than the main one
			}
			auto angle = piece->GetAngle();
			angle.x = angle.y = 0.0f;
			MoveRef(piece, at, angle);
		}
		// its collision at its new size (like the tent's), then the ground's texture on it
		HideCampPiece(foundation);
		const auto handle = foundation->GetHandle();
		std::vector<RE::ObjectRefHandle> pieceHandles;
		for (auto piece : pieces) {
			pieceHandles.push_back(piece->GetHandle());
		}
		std::vector<RE::ObjectRefHandle> patchHandles;
		for (auto patch : patches) {
			patchHandles.push_back(patch->GetHandle());
		}
		std::vector<RE::ObjectRefHandle> extraHandles;
		for (auto extra : extras) {
			extraHandles.push_back(extra->GetHandle());
		}
		std::vector<RE::ObjectRefHandle> blendHandles;
		for (auto blend : blends) {
			blendHandles.push_back(blend->GetHandle());  // each painted with the ground right where it lies
		}
		for (auto seam : seams) {
			patchHandles.push_back(seam->GetHandle());  // painted like the mounds and the lumps on them
		}
		// where the camp's pieces stand, now they are settled: anyone the camp was pitched on top of is moved out
		std::vector<GroundBox> pieceBoxes{ { fire.x, fire.y, 1.0f, 0.0f, 60.0f, 60.0f } };
		for (auto piece : pieces) {
			const auto pb = piece->GetBaseObject();
			if (piece != a_campfire && pb && !pb->Is(RE::FormType::Light, RE::FormType::Sound, RE::FormType::IdleMarker) && !IsCampRug(pb)) {
				pieceBoxes.push_back(BoxOf(piece, shelter, 10.0f));
			}
		}
		After(300, [handle, patchHandles, extraHandles, blendHandles, pieceBoxes, campLand]() {
			// the mound comes up round whoever stands on its spot (on a slope, below its floor): you, your
			// followers, anyone near: up onto its top
			LiftActorsOutOfMounds(true, &pieceBoxes);
			// the mounds and the lumps on them take what shows over the whole camp and round it, all the same one (looked
			// up once: each looking round its own middle could pick another, with a hard seam where
			// they overlap: chosen above, from round their foot, and kept in the save for when they are drawn again);
			// splashes take the ground at their own spot
			const RE::FormID landID = campLand ? campLand->GetFormID() : 0;
			if (landID) {
				if (auto r = handle.get()) {
					g_moundLand[r->GetFormID()] = landID;
				}
				for (const auto& list : { &extraHandles, &patchHandles }) {
					for (const auto& h : *list) {
						if (auto r = h.get()) {
							g_moundLand[r->GetFormID()] = landID;
						}
					}
				}
			}
			// (still recorded pieces: a mound packed up meanwhile stays gone)
			if (auto f = handle.get(); f && ShowCampPiece(f.get())) {
				PaintWithGround(handle, 600.0f, 0, campLand);
			}
			for (const auto& extra : extraHandles) {
				if (auto e = extra.get(); e && ShowCampPiece(e.get())) {
					PaintWithGround(extra, 600.0f, 0, campLand);
				}
			}
			for (const auto& patch : patchHandles) {
				PaintWithGround(patch, 600.0f, 0, campLand);  // lumps of the mound itself: its texture, not a stray one under them
			}
			for (const auto& blend : blendHandles) {
				PaintWithGround(blend);
			}
		});
		// test: where the game really draws the foundation and the pieces, against where we placed them
		After(1800, [handle, pieceHandles, fh]() {
			auto f = handle.get();
			auto root = f ? f->Get3D() : nullptr;
			if (!root) {
				return;
			}
			const auto& w = root->world;
			logger::info("Camp test: foundation drawn at ({:.0f}, {:.0f}) scale {:.2f}, its long side towards ({:.2f}, {:.2f}), placed towards ({:.2f}, {:.2f})",
				w.translate.x, w.translate.y, w.scale, w.rotate.entry[0][1], w.rotate.entry[1][1], std::sin(fh), std::cos(fh));
			for (const auto& h : pieceHandles) {
				auto piece = h.get();
				auto model = piece ? piece->Get3D() : nullptr;
				if (!model) {
					continue;
				}
				const auto pos = piece->GetPosition();
				const auto& b = model->worldBound;
				const auto& pw = model->world;
				const float dx = b.center.x - w.translate.x, dy = b.center.y - w.translate.y;
				logger::info("Camp test: {:08X} at ({:.0f}, {:.0f}), drawn around ({:.0f}, {:.0f}) radius {:.0f}, front towards ({:.2f}, {:.2f}) / placed heading ({:.2f}, {:.2f}); on the foundation {:.0f} along, {:.0f} across",
					piece->GetBaseObject() ? piece->GetBaseObject()->GetFormID() : 0, pos.x, pos.y, b.center.x, b.center.y, b.radius,
					pw.rotate.entry[0][1], pw.rotate.entry[1][1], std::sin(piece->GetAngleZ()), std::cos(piece->GetAngleZ()),
					dx * std::sin(fh) + dy * std::cos(fh), dx * std::cos(fh) - dy * std::sin(fh));
			}
		});
		for (auto piece : pieces) {
			if (IsCampBase(piece->GetBaseObject(), BLOCK_PLACED)) {
				const auto d = piece->GetPosition() - centre;
				logger::info("Camp: chopping block {:.0f} along and {:.0f} across the foundation's middle (half sizes {:.0f}, {:.0f})",
					d.x * std::sin(fh) + d.y * std::cos(fh), d.x * std::cos(fh) - d.y * std::sin(fh), halfLong, halfShort);
			}
		}
		logger::info("Camp: foundation under {} pieces, shape {}, {:.2f} scale ({:.0f} by {:.0f}, {:.0f} deep); ground from {:.0f} to {:.0f}, fire at {:.0f}; floor at {:.0f}",
			pieces.size(), length, fscale, halfLong * 2.0f, halfShort * 2.0f, depth, low, top, fireGround, floorZ);
	}

