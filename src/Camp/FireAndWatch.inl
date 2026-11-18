// Part of the shared camp code (Free Cooking's CookingMenu.cpp, Solid Ground Camping's main.cpp): included in order inside an anonymous namespace (not compiled on its own). Fire sounds, the camp pot, watching new camps and pack-ups, obstacles.
	// Camping's fire sounds, silenced for a camp without a fire: the ignite at the end of the pitch and the sizzle of the
	// fire put out at pack up. Their sound records point at a silent sound for a while (Camping plays them by script)
	constexpr RE::FormID IGNITE_SOUND = 0x840;      // ccQDRSSE002_IgniteFireSM (Camping)
	constexpr RE::FormID FIRE_OUT_SOUND = 0x1080CD;  // FXFireOut (Skyrim.esm)
	constexpr RE::FormID SILENCE_SOUND = 0x3E5E9;    // XXXPlaceholderSilenceSD (Skyrim.esm)
	constexpr RE::FormID FIRE_LOOP_SOUND = 0x37E3E;  // FXFireCampfireLP (Skyrim.esm): Camping plays the fire's crackle with it

	std::unordered_map<RE::TESSound*, RE::BGSSoundDescriptorForm*> g_silencedSounds;  // silenced sound -> its own descriptor
	// (a shelter's activation silences from the event's thread while a timer restores on the main one)
	std::recursive_mutex g_silencedSoundsLock;

	void SilenceSound(RE::TESForm* a_sound, bool a_silent)
	{
		std::scoped_lock guard(g_silencedSoundsLock);
		auto& originals = g_silencedSounds;
		auto sound = a_sound ? a_sound->As<RE::TESSound>() : nullptr;
		auto silence = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(SILENCE_SOUND);
		if (!sound || !silence) {
			return;
		}
		if (a_silent) {
			originals.try_emplace(sound, sound->descriptor);
			sound->descriptor = silence;
			logger::info("Camp: sound {:08X} silenced", sound->GetFormID());
		} else if (auto it = originals.find(sound); it != originals.end()) {
			sound->descriptor = it->second;
			originals.erase(it);
			logger::info("Camp: sound {:08X} back to normal", sound->GetFormID());
		}
	}

	// every sound we silenced, back to normal at once (a save load: the timers that would have done it are dropped)
	void RestoreSilencedSounds()
	{
		std::scoped_lock guard(g_silencedSoundsLock);
		for (auto& [sound, descriptor] : g_silencedSounds) {
			sound->descriptor = descriptor;
			logger::info("Camp: sound {:08X} back to normal (save loaded)", sound->GetFormID());
		}
		g_silencedSounds.clear();
	}

	// each sound's latest silencing (main thread only): a restore scheduled by an older one leaves a newer one alone
	std::unordered_map<RE::FormID, int> g_soundGenerations;

	// silent now (on the game's main thread, from any caller), back to normal after a_ms (or sooner, by
	// SilenceSound(sound, false)). The sound record is a game form: it is only changed on the main thread, never from
	// an event's thread. Camping plays these sounds well after the request: at the end of a pitch, or after the
	// shelter's menu and its fade at pack-up
	void SilenceSoundFor(RE::TESForm* a_sound, int a_ms)
	{
		const RE::FormID id = a_sound ? a_sound->GetFormID() : 0;
		if (id == 0) {
			return;
		}
		OnMain([id, a_ms]() {
			SilenceSound(RE::TESForm::LookupByID(id), true);
			const int mine = ++g_soundGenerations[id];
			After(a_ms, [mine, id]() {
				if (g_soundGenerations[id] == mine) {
					SilenceSound(RE::TESForm::LookupByID(id), false);
				}
			});
		});
	}

	// back to normal after a_ms, unless the sound is silenced again meanwhile (main thread)
	void RestoreSoundAfter(RE::FormID a_id, int a_ms)
	{
		const int mine = g_soundGenerations[a_id];
		After(a_ms, [mine, a_id]() {
			if (g_soundGenerations[a_id] == mine) {
				SilenceSound(RE::TESForm::LookupByID(a_id), false);
			}
		});
	}

	void RestoreSuppliesFavorite();

	// what Camping's own pack-up puts away besides the camp's pieces (ccqdrsse002_shelterscript.pex BreakCamp:
	// DisableFastTravelMarkers and StoreNavCutMeshes, 2026-11-17): its two fixed map markers (Tamriel, Solstheim) and the
	// three navmesh cuts it moved to the shelter, fire and seat, which go back to the storage spot each is linked to.
	// Done for a camp refused on ice, which never reaches Camping's pack-up
	void StoreCampingExtras()
	{
		for (const RE::FormID id : { RE::FormID{ 0x831 }, RE::FormID{ 0x832 } }) {
			if (auto marker = CampingForm(id) ? CampingForm(id)->As<RE::TESObjectREFR>() : nullptr; marker && !marker->IsDisabled()) {
				marker->Disable();
			}
		}
		for (const RE::FormID id : { RE::FormID{ 0x83A }, RE::FormID{ 0x83B }, RE::FormID{ 0x83C } }) {
			auto cut = CampingForm(id) ? CampingForm(id)->As<RE::TESObjectREFR>() : nullptr;
			auto store = cut ? cut->GetLinkedRef(nullptr) : nullptr;
			if (store && store->GetParentCell()) {
				cut->MoveTo(store);  // (CommonLib's MoveTo(target): the target's handle, cell and spot, as Papyrus's MoveTo)
			}
		}
	}

	// no camp on ice (floes, glaciers, icebergs): their ice is too uneven and its solid surface lies above what you see.
	// Camping has already put the camp up by now (the ground cannot be read before), so it comes down at once: every
	// piece goes and the Camping Supplies come back
	bool RefuseCampOnIce(RE::TESObjectREFR* a_campfire)
	{
		auto player = Player();
		if (!a_campfire || !player || (!OnIce(a_campfire->GetPosition()) && !OnIce(player->GetPosition()))) {
			return false;
		}
		// the camp's recorded pieces: Camping's (its shelter, fire, seat, bedroll...; some still hidden while the camp goes
		// up) and nothing else (no piece of ours is placed yet)
		// (only by the records: the fire itself is among them, since a pitch whose fire was not recorded stops before this;
		// an unrecorded fire was added here and deleted)
		const auto camp = CampOf(a_campfire);
		if (!camp) {
			return false;
		}
		std::vector<RE::TESObjectREFR*> gone;
		ForCampPieces(camp, {}, true, [&](RE::TESObjectREFR* a_ref) {
			if (IsCampingPiece(a_ref)) {
				gone.push_back(a_ref);
			}
		});
		for (auto ref : gone) {
			DeleteCampRef(ref);
		}
		if (auto supplies = CampingForm(CAMP_SUPPLIES); supplies && supplies->IsBoundObject()) {
			player->AddObjectToContainer(static_cast<RE::TESBoundObject*>(supplies), nullptr, 1, nullptr);
			RestoreSuppliesFavorite();
		}
		StoreCampingExtras();
		RE::SendHUDMessage::ShowHUDMessage(Tr("You can't make camp on the ice."));
		logger::info("Camp: pitched on ice: taken down at once ({} pieces), supplies given back", gone.size());
		return true;
	}

	// the camp of this shelter has its fire burning (no: laid cold, or none). From the published records, by role: this runs
	// in the activation event, off the main thread (never a fire found by its kind near the shelter).
	// A shelter no record names is not ours: counted as having a fire, as before
	bool CampHasFire(RE::TESObjectREFR* a_shelter)
	{
		const auto shelter = PublicPieceOf(a_shelter ? a_shelter->GetFormID() : 0);
		if (!shelter || shelter->role != PieceRole::kShelter) {
			return true;
		}
		// (any of the camp's burning fires shown counts, not the first one found. A camp with no
		// burning fire recorded counts as having one, as before)
		if (PublicCampRef(shelter->camp, PieceRole::kBurningFire, true)) {
			return true;
		}
		return !PublicCampRef(shelter->camp, PieceRole::kBurningFire);
	}

	void LightOrLayCampFire(RE::TESObjectREFR* a_campfire);

	void PlaceKitPieces(RE::TESObjectREFR* a_campfire);  // Placement.inl

	void PlaceCampPot(RE::TESObjectREFR* a_campfire, [[maybe_unused]] RE::TESObjectREFR* a_light)
	{
		struct AtEnd
		{
			RE::TESObjectREFR* fire;
			~AtEnd() { LightOrLayCampFire(fire); }
		} atEnd{ a_campfire };
		// a fire indoors is a dungeon camp's: it is made whole already, with no tent, block or mound
		if (a_campfire && Underground(a_campfire)) {
			return;
		}
		if (RefuseCampOnIce(a_campfire)) {
			return;
		}
		HideCampPlants(a_campfire);
		// Camping sets the fire at the player's height, so on a slope it was buried (only its glow showed) or floated:
		// lift or lower it, with its light, sound and flames, onto the ground under it
		// (its pieces: Camping's recorded ones over the fire, nothing else standing there)
		if (a_campfire) {
			const auto fire = a_campfire->GetPosition();
			if (float ground = 0.0f; GroundZ(fire, ground) && std::abs(ground - fire.z) > 2.0f && std::abs(ground - fire.z) < 150.0f) {
				const float delta = ground - fire.z;
				std::vector<RE::TESObjectREFR*> pieces{ a_campfire };
				for (auto ref : FireParts(a_campfire, true)) {
					if (!ref->IsDisabled()) {
						pieces.push_back(ref);
					}
				}
				for (auto piece : pieces) {
					auto at = piece->GetPosition();
					at.z += delta;
					MoveRef(piece, at, piece->GetAngle());
				}
				logger::info("Camp: campfire moved {:.0f} to the ground ({} pieces)", delta, pieces.size());
			}
		}
		if (!a_campfire) {
			return;
		}
		FitCampTent(a_campfire);
		// test tuning, read again every time a camp is pitched (no rebuild or restart needed); the defaults are the
		// settled spots. shift and side move a piece along and across the fire's own axes, turn rotates it (degrees)
		// (both settings files: the shipped one and the player's own, which the settings page writes)
		const auto layout = CampSettings();
		const float shift = CampSetting(layout, "shift", 160.0f), side = CampSetting(layout, "side", 0.0f),
					turn = CampSetting(layout, "turn", 180.0f), sink = CampSetting(layout, "sink", 0.0f),
					scale = CampSetting(layout, "scale", 1.0f);
		const float blockShift = CampSetting(layout, "blockshift", -80.0f), blockSide = CampSetting(layout, "blockside", 170.0f),
					blockTurn = CampSetting(layout, "blockturn", 90.0f), blockSink = CampSetting(layout, "blocksink", 0.0f);
		const auto angle = a_campfire->GetAngle();
		// Camping sets its box seat at the fire's height, so on a slope it floated or sank: rest it on the ground, at the
		// average height under its middle and corners (the lowest corner buried it too deep on a slope)
		if (auto seatBase = CampingForm(0x825)) {
			// (this camp's own seat, recorded as it went up)
			std::vector<RE::TESObjectREFR*> seats;
			const auto camp = CampOf(a_campfire);
			for (const auto& [id, piece] : g_campingPieces) {
				auto ref = piece.camp == camp ? CampingRef(id, piece) : nullptr;
				if (ref && ref->GetBaseObject() == seatBase && !ref->IsDisabled() && !ref->IsMarkedForDeletion()) {
					seats.push_back(ref);
				}
			}
			for (auto seatRef : seats) {
				auto seat = seatRef->GetPosition();
				const float h = seatRef->GetAngleZ();
				float total = 0.0f;
				int count = 0;
				for (const auto& [x, y] : { std::pair{ 0.0f, 0.0f }, { 22.0f, 17.0f }, { -22.0f, 17.0f }, { 22.0f, -17.0f }, { -22.0f, -17.0f } }) {
					RE::NiPoint3 p{ seat.x + x * std::cos(h) + y * std::sin(h), seat.y - x * std::sin(h) + y * std::cos(h), seat.z };
					if (float ground = 0.0f; GroundZ(p, ground) && std::abs(ground - seat.z) < 150.0f) {
						total += ground;
						++count;
					}
				}
				const float lowest = count > 0 ? total / count : std::numeric_limits<float>::max();
				if (lowest != std::numeric_limits<float>::max() && std::abs(lowest - seat.z) > 1.0f && std::abs(lowest - seat.z) < 150.0f) {
					logger::info("Camp: box seat moved {:.0f} to the ground", lowest - seat.z);
					seat.z = lowest;
					MoveRef(seatRef, seat, seatRef->GetAngle());
				}
			}
		}
		if (auto blockBase = PluginItem(BLOCK_PLACED)) {
			if (auto block = PlaceOwned(a_campfire, blockBase, 0, PieceRole::kBlock)) {
				auto turned = angle;
				turned.z += blockTurn * std::numbers::pi_v<float> / 180.0f;
				// the block's model spreads its stump and log piles over about 240 units: on a slope it lies along the
				// ground (a flat plane fitted to the ground under them), so the stump is neither buried nor floating
				auto at = BesideFire(a_campfire, blockShift, blockSide, 0.0f);
				{
					const float h = turned.z;
					float n = 0, sx = 0, sy = 0, sz = 0, sxx = 0, syy = 0, sxz = 0, syz = 0;
					for (const auto& [x, y] : { std::pair{ 0.0f, 0.0f }, { 0.0f, 125.0f }, { -80.0f, 65.0f }, { 80.0f, 65.0f },
							 { -100.0f, 0.0f }, { 100.0f, 0.0f }, { 0.0f, 160.0f }, { -60.0f, 140.0f }, { 60.0f, 140.0f } }) {
						RE::NiPoint3 p{ at.x + x * std::cos(h) + y * std::sin(h), at.y - x * std::sin(h) + y * std::cos(h), at.z };
						if (float ground = 0.0f; GroundZ(p, ground) && std::abs(ground - at.z) < 120.0f) {
							n += 1; sx += x; sy += y; sz += ground; sxx += x * x; syy += y * y; sxz += x * ground; syz += y * ground;
						}
					}
					if (n >= 4) {
						// least squares: ground = a + b x + c y (the sample spots are near enough balanced to solve x and y apart)
						const float mx = sx / n, my = sy / n, mz = sz / n;
						const float vx = sxx / n - mx * mx, vy = syy / n - my * my;
						const float b = vx > 1.0f ? (sxz / n - mx * mz) / vx : 0.0f;
						const float c = vy > 1.0f ? (syz / n - my * mz) / vy : 0.0f;
						const float limit = 0.35f;  // about 20 degrees
						turned.x = g_pitchSign * std::clamp(-std::atan(c), -limit, limit);
						turned.y = g_rollSign * std::clamp(std::atan(b), -limit, limit);
						at.z = mz - b * mx - c * my - 2.0f;  // the plane under the block's centre, a little into the ground
						logger::info("Camp: chopping block lies on the slope, tilt {:.1f} / {:.1f} degrees", turned.x * 57.3f, turned.y * 57.3f);
					}
				}
				at.z -= blockSink;
				MoveRef(block.get(), at, turned);
			}
		}

		auto potBase = PluginItem(POT_PLACED);
		auto standForm = StandBase();
		if (!potBase || !standForm || !standForm->IsBoundObject() || !CampHasCookware()) {
			PlaceKitPieces(a_campfire);
			LayCampFoundation(a_campfire);
			PlaceTentNavCut(a_campfire);
			PlaceFireSpots(a_campfire);
			LogCampLayout(a_campfire);
			return;
		}
		const auto at = BesideFire(a_campfire, shift, side, sink);
		logger::info("Camp: campfire at ({:.0f}, {:.0f}, {:.0f}), pot at ({:.0f}, {:.0f}, {:.0f})", a_campfire->GetPositionX(),
			a_campfire->GetPositionY(), a_campfire->GetPositionZ(), at.x, at.y, at.z);
		auto potAngle = angle;
		potAngle.z += turn * std::numbers::pi_v<float> / 180.0f;
		for (auto base : { static_cast<RE::TESBoundObject*>(standForm), potBase }) {
			if (auto placed = PlaceOwned(a_campfire, base, 0, base == potBase ? PieceRole::kPot : PieceRole::kOther)) {
				MoveRef(placed.get(), at, potAngle);
				if (scale > 0.0f && scale != 1.0f) {
					ScaleCampPiece(placed, scale);
				}
			}
		}
		// (no notice: crafting the upgrade already said the supplies carry the pot, and the camp's other notices show now)
		PlaceKitPieces(a_campfire);
		LayCampFoundation(a_campfire);
		PlaceTentNavCut(a_campfire);
		PlaceFireSpots(a_campfire);
		LogCampLayout(a_campfire);
	}

	// the Camping campfires standing near the player (form IDs)
	std::vector<RE::FormID> CampfiresNear()
	{
		auto player = Player();
		auto tes = RE::TES::GetSingleton();
		auto fireBase = CampingForm(CAMP_FIRE);
		std::vector<RE::FormID> fires;
		if (player && tes && fireBase) {
			tes->ForEachReferenceInRange(player, 4000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetBaseObject() == fireBase && !a_ref->IsDisabled() && !a_ref->IsMarkedForDeletion()) {
					fires.push_back(a_ref->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return fires;
	}

	// the campfire that wasn't there before (nearest the player if several). Not "the highest form ID": the game
	// reuses the IDs of deleted references, so a new camp can get a lower one than the old camp it replaces
	RE::TESObjectREFR* NewCampfire(const std::vector<RE::FormID>& a_before)
	{
		auto player = Player();
		RE::TESObjectREFR* found = nullptr;
		float best = (std::numeric_limits<float>::max)();
		for (const auto id : CampfiresNear()) {
			if (std::ranges::find(a_before, id) != a_before.end()) {
				continue;
			}
			// (never a dungeon camp's fire, ours, nor one underground: walking into a cave with a lit dungeon camp
			// after a cancelled pitch took it for the new camp's). Nor a fire already recorded as a camp's: travelling
			// back to the standing camp after a cancelled pitch rebuilt it
			if (auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); ref && player && !IsCampPiece(ref) && !Underground(ref)) {
				if (const float d = ref->GetPosition().GetDistance(player->GetPosition()); d < best) {
					best = d;
					found = ref;
				}
			}
		}
		return found;
	}

	// what was placed during play near the player (form IDs): taken when the Camping Supplies are used, so the camp's
	// own pieces are what is new afterwards (RecordCampingPieces)
	std::unordered_set<RE::FormID> PlacedNear()
	{
		auto player = Player();
		auto tes = RE::TES::GetSingleton();
		std::unordered_set<RE::FormID> placed;
		if (player && tes) {
			tes->ForEachReferenceInRange(player, 6000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->IsDynamicForm()) {
					placed.insert(a_ref->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return placed;
	}

	// Camping's own pieces, read from its build script (ccqdrsse002_shelterscript.pex BuildRemainingCamp, 2026-11-17):
	// the shelter, then (after its set-up sounds) bedroll, fire, light, crackle, box seat and five markers, in this order.
	// The dog's marker is the last it places
	constexpr RE::FormID DOG_LAY_MARKER = 0x0010F801;  // Skyrim.esm DogLayingDownIdleMarker
	bool IsCampingKind(const RE::TESForm* a_base)
	{
		if (!a_base) {
			return false;
		}
		const auto id = a_base->GetFormID();
		for (const auto own : { CAMP_SHELTER, CAMP_FIRE, RE::FormID{ 0x819 }, RE::FormID{ 0x825 } }) {  // (shelter, fire, BedrollNoSandbox, Chair)
			if (a_base == CampingForm(own)) {
				return true;
			}
		}
		return id == CAMP_LIGHT || id == 0x00037E3E || id == 0x000C4EFD || id == 0x0004240B || id == 0x0007872B || id == 0x00097539 || id == DOG_LAY_MARKER;
		// (LightCampFire01, FXFireCampfireLP, SitCrossLeggedMarker, warm hands, book reading, TavernDrinkingMarker, dog)
	}
	constexpr std::size_t CAMPING_PIECE_COUNT = 11;

	// a piece of Camping's kind, new near the fire since the supplies were used
	template <class F>
	void ForNewCampingPieces(RE::TESObjectREFR* a_campfire, const std::unordered_set<RE::FormID>& a_before, F&& a_do)
	{
		auto tes = RE::TES::GetSingleton();
		if (!a_campfire || !tes) {
			return;
		}
		tes->ForEachReferenceInRange(a_campfire, 800.0f, [&](RE::TESObjectREFR* a_ref) {
			const auto b = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (b && a_ref->IsDynamicForm() && !a_ref->IsMarkedForDeletion() && !a_before.contains(a_ref->GetFormID()) && !IsCampPiece(a_ref) &&
				!a_ref->extraList.HasType(RE::ExtraDataType::kAliasInstanceArray) && IsCampingKind(b)) {
				a_do(a_ref, b);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	// Camping has placed its last piece (the dog's marker) for the camp at this fire
	bool CampingBuildDone(RE::TESObjectREFR* a_campfire, const std::unordered_set<RE::FormID>& a_before)
	{
		bool done = false;
		ForNewCampingPieces(a_campfire, a_before, [&](RE::TESObjectREFR*, RE::TESForm* a_base) {
			done = done || a_base->GetFormID() == DOG_LAY_MARKER;
		});
		return done;
	}

	// the role of one of Camping's pieces, from its kind (each of Camping's single kinds appears once per camp)
	PieceRole CampingRoleOf(const RE::TESForm* a_base)
	{
		if (!a_base) {
			return PieceRole::kUnknown;
		}
		const auto id = a_base->GetFormID();
		if (a_base == CampingForm(CAMP_SHELTER)) {
			return PieceRole::kShelter;
		}
		if (a_base == CampingForm(CAMP_FIRE)) {
			return PieceRole::kBurningFire;
		}
		if (a_base == CampingForm(0x819)) {
			return PieceRole::kBedroll;
		}
		if (a_base == CampingForm(0x825)) {
			return PieceRole::kSeat;
		}
		if (id == CAMP_LIGHT) {
			return PieceRole::kLight;
		}
		if (id == 0x00037E3E) {
			return PieceRole::kCrackle;
		}
		return PieceRole::kMarker;
	}

	// Camping's pieces of the camp that just went up, recorded with its number, once Camping has placed all of them:
	// exactly the kinds its build script places, new near the fire since the supplies were used ([stated] 2026-11-17
	// chose this over reading Camping's script variables) the new camp can be recorded: among Camping's new pieces at
	// the fire there is exactly one shelter and one burning fire, and that fire is this one. Checked before anything
	// changes (the old camp packed up, the number moved on): a pitch whose fire would not be recorded stops, so no
	// later step acts on a fire the records don't name
	bool NewCampRecordable(RE::TESObjectREFR* a_campfire, const std::unordered_set<RE::FormID>& a_before)
	{
		int shelters = 0, fires = 0;
		bool thisFire = false;
		ForNewCampingPieces(a_campfire, a_before, [&](RE::TESObjectREFR* a_ref, RE::TESForm* a_base) {
			const auto role = CampingRoleOf(a_base);
			shelters += role == PieceRole::kShelter ? 1 : 0;
			fires += role == PieceRole::kBurningFire ? 1 : 0;
			thisFire = thisFire || (role == PieceRole::kBurningFire && a_ref == a_campfire);
		});
		return shelters == 1 && fires == 1 && thisFire;
	}

	void RecordCampingPieces(RE::TESObjectREFR* a_campfire, const std::unordered_set<RE::FormID>& a_before)
	{
		std::erase_if(g_campingPieces, [](const auto& a_p) { return a_p.second.camp == g_campNumber; });  // (a number used again: an older save)
		// each piece's role, from its kind. Camping's build places exactly one shelter, bedroll, fire, light, crackle and
		// seat: two new ones of such a kind mean another mod placed one there meanwhile, and which is Camping's can't be
		// told, so neither is recorded (an unrecorded piece is never touched)
		auto roleOf = [](const RE::TESForm* a_base) { return CampingRoleOf(a_base); };
		std::vector<std::pair<RE::TESObjectREFR*, RE::TESForm*>> found;
		std::unordered_map<PieceRole, int> count;
		ForNewCampingPieces(a_campfire, a_before, [&](RE::TESObjectREFR* a_ref, RE::TESForm* a_base) {
			found.emplace_back(a_ref, a_base);
			++count[roleOf(a_base)];
		});
		std::size_t n = 0;
		for (const auto& [ref, base] : found) {
			const auto role = roleOf(base);
			if (role != PieceRole::kMarker && count[role] > 1) {
				logger::warn("Camp: {} new pieces of one of Camping's single kinds ({:08X}) at the fire: none of them recorded", count[role], base->GetFormID());
				continue;
			}
			g_campingPieces[ref->GetFormID()] = { base->GetFormID(), g_campNumber, role };
			++n;
		}
		PublishRecords();
		logger::info("Camp: {} of Camping's {} pieces recorded for camp {}", n, CAMPING_PIECE_COUNT, g_campNumber);
	}

	// a camp pitched while another stands: the old one is still being packed up (Camping takes it down only after the
	// new one is up). The new camp's pieces wait for that, so packing up the old camp can't take any of them along
	// (the two camps often stand on the same spot) and the new mound isn't laid round the old camp's pieces too
	std::atomic<bool> g_oldCampPacking{ false };

	void NameCampingSupplies();  // (Setup.inl)

	// after the camping supplies are used: wait for the new campfire (the camp goes up behind a fade), then the pot
	void WatchNewCamp()
	{
		static std::atomic<int> generation{ 0 };
		const int mine = ++generation;
		auto before = std::make_shared<const std::vector<RE::FormID>>(CampfiresNear());
		auto nearBefore = std::make_shared<const std::unordered_set<RE::FormID>>(PlacedNear());
		auto tries = std::make_shared<int>(0);
		auto looks = std::make_shared<int>(0);
		auto seenFire = std::make_shared<bool>(false);
		Repeat(1000, [mine, before, nearBefore, tries, looks, seenFire]() {
			if (generation != mine || ++*looks > 25) {
				if (generation == mine && *seenFire) {
					// a new fire stands, but Camping never placed its last piece: nothing recorded (no guess at what is
					// Camping's), and the shelter model chosen for it stays
					g_campTentBeforePitch = -2;
					logger::warn("Camp: Camping's build did not finish in 25 s (its last piece never appeared): the camp is not recorded");
					return false;
				}
				// no new camp went up (Camping's prompt cancelled): the standing camp's shelter model back
				if (generation == mine && g_campTentBeforePitch.load() != -2) {
					g_campTent = g_campTentBeforePitch.load();
					g_campTentBeforePitch = -2;
					SetCampTentModel();
					logger::info("Camp: no new camp went up; the standing camp's shelter kept");
				}
				return false;
			}
			if (auto campfire = NewCampfire(*before)) {
				// wait a little for the fire's light (it goes up right after the campfire), then place anyway
				auto light = CampfireLight(campfire);
				if (!light && ++*tries < 5) {
					return true;
				}
				// Camping places its pieces one after another: recorded once it has placed its last one. A build that never
				// finishes is not recorded at all (given up after 25 looks)
				if (!CampingBuildDone(campfire, *nearBefore)) {
					*seenFire = true;
					return true;
				}
				if (g_oldCampPacking && *looks < 25) {
					return true;  // the old camp first
				}
				// the old camp is replaced only once the new one is up: the new fire has a shelter of its own, new since
				// the supplies were used ([stated] 2026-11-17 OK)
				// (the new shelter is one of the new Camping pieces: not there when the supplies were used, not recorded by any
				// camp. Not "the nearest shelter": a disabled one another mod left there was taken)
				// (exactly one: two new shelters mean another mod placed one meanwhile, and which is Camping's can't be told,
				// so the camp isn't recorded)
				RE::TESObjectREFR* newShelter = nullptr;
				int shelters = 0;
				ForNewCampingPieces(campfire, *nearBefore, [&](RE::TESObjectREFR* a_ref, RE::TESForm* a_base) {
					if (a_base == CampingForm(CAMP_SHELTER)) {
						newShelter = a_ref;
						++shelters;
					}
				});
				if (shelters != 1 || !NewCampRecordable(campfire, *nearBefore)) {
					return true;  // (still not one after 25 looks: given up above, the standing camp kept, nothing of ours placed)
				}
				// a new camp breaks the old one: all of its pieces go (Camping allows one camp)
				RemoveCampPieces(ALL_CAMPS);
				g_campTentBeforePitch = -2;  // (the new camp went up: its shelter stays)
				++g_campNumber;  // the new camp's pieces carry its own number from here on
				// (the shelter checked above, not a second nearest-shelter search: a closer disabled one another mod left there
				// was saved as the camp's)
				g_campShelterId = newShelter->GetFormID();
				RecordCampingPieces(campfire, *nearBefore);
				if (!IsCampRole(campfire, g_campNumber, PieceRole::kBurningFire)) {
					logger::warn("Camp: the new camp's fire was not recorded: nothing of ours placed");  // (cannot happen after the check above)
					return false;
				}
				logger::info("Camp: camp {} goes up", g_campNumber);
				PlaceCampPot(campfire, light);
				return false;
			}
			return true;
		});
	}

	// a pot or chopping block of ours whose campfire is gone (a camp broken far away) is removed when its area loads
	class CampPotCleaner : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		static CampPotCleaner* GetSingleton()
		{
			static CampPotCleaner singleton;
			return &singleton;
		}
		// Everything placed during play (IDs FF...) that loads is only noted here, and one shared task looks at all of them
		// together: a task per object, thousands of them after a save loads in a big load order, holds the screen black
		// while the game drains them (Equip Enchantment Fix NG 1.3.16/1.3.17, Nexus 191783, fixed its load freeze this
		// way). The game also holds its event lock while every mod's handler runs, so this one stays tiny
		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			if (!a_event || !a_event->loaded || a_event->formID < 0xFF000000) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// (any thread may send this: the noted numbers are looked at only on the main thread, by Start's job)
			std::lock_guard guard(lock);
			loaded.push_back(a_event->formID);
			return RE::BSEventNotifyControl::kContinue;
		}
		// one job for the whole session looks at what loaded, on the main thread (set at kDataLoaded with the sink)
		static void Start()
		{
			RepeatAlways(250, []() {
				GetSingleton()->CheckLoaded();
				return true;
			});
		}
		// a save is loading: numbers noted in the game being left mean nothing in the next one
		void Forget()
		{
			std::lock_guard guard(lock);
			loaded.clear();
		}

	private:
		std::mutex lock;
		std::vector<RE::FormID> loaded;  // noted, not yet looked at

		void CheckLoaded()
		{
			std::vector<RE::FormID> ids;
			{
				std::lock_guard guard(lock);
				ids.swap(loaded);
			}
			std::ranges::sort(ids);
			const auto [first, last] = std::ranges::unique(ids);
			ids.erase(first, last);
			// a shelter piece Change tent had hidden when the game was saved, far away then: shown once it is in memory again
			// (looked up with every batch: a hidden piece builds no model, so it sends no load event of its own)
			std::erase_if(g_enableWhenLoaded, [&](RE::FormID a_id) {
				auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
				if (!ref) {
					return false;
				}
				if (ref->IsDisabled() && !ref->IsDeleted() && !ref->IsMarkedForDeletion() && ShowCampPiece(ref)) {  // (a recorded piece only)
					logger::info("Camp: a shelter piece hidden by Change tent when the game was saved shown again now that it loaded");
				}
				return true;
			});
			if (ids.empty()) {
				return;
			}
			// the pieces of camps packed up while their area was away, first: every one in memory now goes, the loaded ones
			// and a disabled one that builds no model and so sends no load event (the pot over a cold fire), before any of
			// this batch is judged a stray beside them
			if (!g_campPendingDelete.empty()) {
				std::vector<RE::TESObjectREFR*> found;
				for (const auto& [id, base] : g_campPendingDelete) {
					if (auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
						found.push_back(ref);
					}
				}
				for (auto ref : found) {
					TakePendingDelete(ref);
				}
			}
			for (const auto id : ids) {
				Check(id);
			}
		}

		// one placed object that loaded: ours without its camp is removed; ours with it gets its look back
		static void Check(RE::FormID a_id)
		{
			{
				auto form = RE::TESForm::LookupByID(a_id);  // (looked up again: it may be gone since it loaded)
				auto pot = form ? form->AsReference() : nullptr;
				auto tes = RE::TES::GetSingleton();
				if (!pot || !tes) {
					return;
				}
				// a piece of a camp packed up while its area was away: it goes now
				if (TakePendingDelete(pot) || pot->IsDisabled()) {
					return;
				}
				const auto base = pot->GetBaseObject();
				// only what a camp recorded is looked at, never anything by its kind (a piece of a camp gone is taken by
				// reference when the camp is packed up, wherever it stands: no "stray" is judged by what stands near it)
				const bool campBedroll = base && base->GetFormID() == BEDROLL_STATIC && IsCampingPiece(pot);  // (Camping's, moved into the tent)
				if (!campBedroll && !(IsCampOwned(pot) && IsFoundation(base))) {
					return;
				}
				// the camp's own fire, by its record (the mounds' texture is looked up round it; an older save's pieces have
				// their roles from their bases from their recorded bases, so no fire is searched for by distance)
				RE::TESObjectREFR* campfire = CampRef(CampOf(pot), PieceRole::kBurningFire);
				if (campBedroll) {
					MakeBedrollSeeThrough(pot->GetHandle());  // its model was loaded again
				} else if (IsFoundation(base)) {
					// its model was loaded again (a save loaded, the area visited again): its ground's texture again
					// (the camp's one texture, as when it was laid; a splash takes its own spot's)
					const bool splash = IsCampBase(base, GROUND_BLEND);
					RE::TESLandTexture* land = nullptr;
					if (auto it = g_moundLand.find(pot->GetFormID()); !splash && it != g_moundLand.end()) {
						land = RE::TESForm::LookupByID<RE::TESLandTexture>(it->second);
					}
					if (!land && !splash) {
						land = CampLandTexture(campfire);  // (a camp laid before its texture was kept)
					}
					PaintWithGround(pot->GetHandle(), splash ? 0.0f : 600.0f, 0, land);
				}
			}
		}
	};

	// Camping Supplies are used up when a camp goes up and given back when it is packed up; a favorited stack would
	// come back unfavorited, so remember the choice and put it back
	std::atomic<bool> g_suppliesFavorite{ false };

	RE::InventoryEntryData* InventoryEntry(RE::TESObjectREFR* a_ref, RE::TESForm* a_item)
	{
		auto changes = a_ref ? a_ref->GetInventoryChanges() : nullptr;
		if (!changes || !changes->entryList) {
			return nullptr;
		}
		for (auto entry : *changes->entryList) {
			if (entry && entry->object == a_item) {
				return entry;
			}
		}
		return nullptr;
	}

	void RestoreSuppliesFavorite()
	{
		auto player = Player();
		auto supplies = CampingForm(CAMP_SUPPLIES);
		if (!g_suppliesFavorite || !player || !supplies) {
			return;
		}
		auto entry = InventoryEntry(player, supplies);
		auto changes = player->GetInventoryChanges();
		if (entry && changes && !entry->IsFavorited()) {
			RE::ExtraDataList* list = entry->extraLists && !entry->extraLists->empty() ? entry->extraLists->front() : nullptr;
			changes->SetFavorite(entry, list);
		}
	}

	// Camping gives the supplies back on its own when it refuses a camp ("You cannot build a campsite in cities and
	// towns"): any time they come back to the player while the stack was a favorite, it is made a favorite again
	class SuppliesReturnWatcher : public RE::BSTEventSink<RE::TESContainerChangedEvent>
	{
	public:
		static SuppliesReturnWatcher* GetSingleton()
		{
			static SuppliesReturnWatcher singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			// every item moved anywhere comes through here, while the game holds its event lock: only what reaches the
			// player is looked at
			if (!a_event || a_event->newContainer != 0x14) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto supplies = CampingForm(CAMP_SUPPLIES);
			if (supplies && g_suppliesFavorite && a_event->baseObj == supplies->GetFormID()) {
				OnMain([]() { RestoreSuppliesFavorite(); });
			}
			// the forge's cookware upgrade: the kit goes into the supplies at once (it is not kept as an item)
			if (auto kit = PluginItem(COOKWARE_KIT); kit && a_event->baseObj == kit->GetFormID()) {
				OnMain([]() {
					auto p = Player();
					auto k = PluginItem(COOKWARE_KIT);
					if (!p || !k) {
						return;
					}
					if (const auto n = ItemCount(p, k); n > 0) {
						p->RemoveItem(k, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
					for (auto g : { CookwareGlobal(), OtherCookwareGlobal() }) {
						if (g) {
							g->value = 1.0f;  // (both mods' records: nothing is lost when the other mod runs the camps)
						}
					}
					NameCampingSupplies();
					RE::SendHUDMessage::ShowHUDMessage(Tr("Your Camping Supplies now carry a cooking pot and its iron stand."));
					logger::info("Camp: Camping Supplies upgraded with cookware");
				});
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Camping: once a camp is being packed up (the player used its shelter, or pitched a new camp elsewhere), watch for
	// its shelter to go; then the fire has burned 1 Firewood, the camping supplies come back and our pot there goes
	// (a_newCampComing: a new camp is going up; until this one is packed up, the new one's pieces wait: g_oldCampPacking)
	void WatchCamp(RE::ObjectRefHandle a_shelter, bool a_newCampComing = false)
	{
		// which camp this shelter is, decided before anything else changes (the watcher's generation, the packing flag):
		// a shelter our records name as a camp's shelter is that camp; the shelter this save's newest camp went up with is
		// that camp (an older save's shelter gets its role from its recorded base at the load; any other shelter is not
		// guessed to be ours). Anything else, or a shelter already gone from memory, is not ours: nothing is given back and
		// nothing taken (a new game refunded another mod's shelter; a lost handle packed up the standing camp)
		const auto watched = a_shelter.get();
		if (!watched) {
			logger::info("Camp: a shelter to watch is already gone: not watched");
			return;
		}
		std::uint32_t camp = 0;
		if (const auto recorded = CampOf(watched.get()); recorded && RoleOf(watched.get()) == PieceRole::kShelter) {
			camp = recorded;
		} else if (g_campShelterId != 0 && watched->GetFormID() == g_campShelterId) {
			camp = g_campNumber;
		} else {
			logger::info("Camp: shelter {:08X} is not our camp's: not watched", watched->GetFormID());
			return;
		}
		static std::atomic<int> generation{ 0 };
		const int mine = ++generation;
		g_oldCampPacking = a_newCampComing;
		// (looked at 4 times a second: Camping takes its camp down with the screen black and fades back in a second
		// later, and our pieces must go in that second too, out of sight; given up after 90 seconds)
		auto looks = std::make_shared<int>(0);
		Repeat(250, [a_shelter, mine, looks, camp]() {
			if (generation != mine || ++*looks > 360) {
				if (generation == mine) {
					g_oldCampPacking = false;  // (given up)
				}
				return false;
			}
			auto shelter = a_shelter.get();
			if (shelter && (!shelter->IsDisabled() || TentSwapPending(shelter.get())) && !shelter->IsMarkedForDeletion() && !shelter->IsDeleted()) {
				return true;  // standing (or only reloading for Change tent)
			}
			// the camp is packed up: the fire-out sound plays again for other fires, once Camping's take-down sounds
			// (tent, hammering, fire out) have all played: they run on well after the tent is gone
			RestoreSoundAfter(FIRE_OUT_SOUND, 20000);
			auto player = Player();
			auto firewood = RE::TESForm::LookupByID(FIREWOOD);
			auto supplies = CampingForm(CAMP_SUPPLIES);
			// packing up is free: the fire's wood was paid when it was lit
			(void)firewood;
			if (player && supplies && supplies->IsBoundObject()) {
				player->AddObjectToContainer(static_cast<RE::TESBoundObject*>(supplies), nullptr, 1, nullptr);
				RestoreSuppliesFavorite();
				RE::SendHUDMessage::ShowHUDMessage(Tr("You pack up your Camping Supplies."));
			}
			// the camp's own pieces, by reference, wherever they stand
			MoveFollowersOffMounds(camp);  // before the mounds go: nobody drops when they vanish
			RemoveCampPieces(camp);
			ShowCampPlants(camp);
			g_oldCampPacking = false;
			return false;
		});
	}

	// the shelter of the camp the player has standing now, if any. Camping's quest script keeps the last camp in a
	// property, which keeps its shelter in memory even far away, so it can be found among the game's placed forms
	// (asking the script directly crashes on this game version)
	RE::TESObjectREFR* CurrentShelter()
	{
		auto shelterBase = CampingForm(CAMP_SHELTER);
		if (!shelterBase) {
			return nullptr;
		}
		// the newest camp's shelter, as it recorded it
		for (const auto& [id, piece] : g_campingPieces) {
			if (piece.camp == g_campNumber && piece.base == shelterBase->GetFormID()) {
				if (auto ref = CampingRef(id, piece); ref && !ref->IsMarkedForDeletion()) {
					return ref;
				}
			}
		}
		// nothing recorded: no camp of ours stands. Not "any Camping shelter in the world": that hid, moved or resized
		// another mod's shelter on load. Camps from before the record: packed up before
		// updating (CHANGELOG)
		return nullptr;
	}

	// Camping: pitching a new camp packs up the one standing, so it costs 1 Firewood like packing up by hand; without
	// Firewood the supplies are not used and the old camp stays
	// Before a camp goes up: tree trunks and big rocks where its pieces would stand. Camping lays the camp out from where
	// the player stands: the fire 150 ahead at 60 degrees to the right, facing away from the player, and the rest around
	// the fire in its own axes (measured from real camps), so each piece's spot is known and checked with its own size.
	// The shelter's spot is the rectangle every shelter is fitted into (FitCampTent: tentwidth across, tentdepth deep,
	// its front tentfiregap past the fire ring), grown by a shelter's own Size above 1; the pot and the chopping block
	// stand where the settings put them. A rock is checked by its own box, turned as it lies, and only where it stands
	// above the ground: a circle round its middle missed a rock whose middle was off to one side and let it stand
	// against the shelter (in-game test 2026-11-16, the lean-to and a kit tent at Size 1.2)
	int CampObstacles(RE::Actor* a_player)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes || !a_player) {
			return 0;
		}
		const auto me = a_player->GetPosition();
		const float fh = a_player->GetAngleZ() + 60.0f * std::numbers::pi_v<float> / 180.0f;  // the fire's heading
		const RE::NiPoint3 fire{ me.x + 150.0f * std::sin(fh), me.y + 150.0f * std::cos(fh), me.z };
		const float sf = std::sin(fh), cf = std::cos(fh);
		// a point in the fire's axes: along its front (shift) and across it (side)
		auto toFire = [&](float a_x, float a_y) {
			const float dx = a_x - fire.x, dy = a_y - fire.y;
			return std::pair{ dx * sf + dy * cf, dx * cf - dy * sf };
		};
		const auto settings = CampSettings();
		struct Piece
		{
			const char* name;
			float shift, side;   // its middle, in the fire's axes
			float radius;        // a round piece (0: the shelter's rectangle)
			float ux = 0.0f, uy = 0.0f, halfDepth = 0.0f, halfWidth = 0.0f;  // the rectangle: its depth along (ux, uy)
		};
		std::vector<Piece> pieces{
			{ "fire", 0.0f, 0.0f, 70.0f },
			{ "box seat", 125.0f, 85.0f, 35.0f },
			{ "chopping block", CampSetting(settings, "blockshift", -80.0f), CampSetting(settings, "blockside", 170.0f), 110.0f },
		};
		if (CampHasCookware()) {
			// (only a camp that gets the pot: without the cookware upgrade nothing stands there)
			pieces.push_back({ "pot", CampSetting(settings, "shift", 160.0f), CampSetting(settings, "side", 0.0f), 50.0f });
		}
		{
			// Camping puts its shelter at (-171, -104) in the fire's axes; ours goes into the spot on that side
			const float len = std::hypot(-171.0f, -104.0f);
			const float ux = -171.0f / len, uy = -104.0f / len;
			const float D = TentSlotDepth(settings), W = TentSlotWidth(settings);
			const int choice = std::clamp(g_tentChoice.load(), 0, TentCount() - 1);
			float grow = 1.0f;
			for (const int model : { choice, TentForPlace(choice, true) }) {
				const auto key = TentSettingKey(model, "size");
				grow = (std::max)(grow, settings.contains(key) ? settings.at(key) : model >= 4 ? g_tents[model].size : 1.0f);
			}
			const float reach = 70.0f + CampSetting(settings, "tentfiregap", 15.0f) + 0.5f * D;
			pieces.push_back({ "shelter", ux * reach, uy * reach, 0.0f, ux, uy, 0.5f * D * grow + 10.0f, 0.5f * W * grow + 10.0f });
		}
		// how far a point (in the fire's axes) is outside a piece (0 or less: on it)
		auto outside = [](const Piece& a_piece, float a_shift, float a_side) {
			const float dx = a_shift - a_piece.shift, dy = a_side - a_piece.side;
			if (a_piece.radius > 0.0f) {
				return std::hypot(dx, dy) - a_piece.radius;
			}
			const float along = dx * a_piece.ux + dy * a_piece.uy, across = dx * a_piece.uy - dy * a_piece.ux;
			return std::hypot((std::max)(0.0f, std::abs(along) - a_piece.halfDepth), (std::max)(0.0f, std::abs(across) - a_piece.halfWidth));
		};
		const auto shelterIndex = pieces.size() - 1;
		// standing on a rock (well above the terrain under the player): the camp's level is that rock top, not the terrain;
		// on the open ground, the terrain (raised everywhere, rocks downhill of the player were missed)
		bool onRock = false;
		if (float under = 0.0f; tes->GetLandHeight(me, under)) {
			onRock = me.z - under > 20.0f;
		}
		int count = 0;
		tes->ForEachReferenceInRange(a_player, 1500.0f, [&](RE::TESObjectREFR* a_ref) {
			auto obj = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!obj || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || a_ref->IsDynamicForm()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			auto model = obj->As<RE::TESModel>();
			std::string path = model && model->GetModel() ? model->GetModel() : "";
			std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (obj->Is(RE::FormType::Tree)) {
				// a tree, not a bush: bushes and shrubs are cleared from the camp anyway
				if (path.find("shrub") != std::string::npos || path.find("bush") != std::string::npos || path.find("plant") != std::string::npos) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float trunk = 45.0f * a_ref->GetScale();
				const auto pos = a_ref->GetPosition();
				const auto [shift, side] = toFire(pos.x, pos.y);
				for (const auto& piece : pieces) {
					if (outside(piece, shift, side) < trunk) {
						++count;
						logger::info("Camp: a tree ({}) where the {} would stand", path, piece.name);
						break;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (!obj->Is(RE::FormType::Static) || (path.find("rock") == std::string::npos && path.find("boulder") == std::string::npos)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const auto& b = obj->boundData;
			auto root = a_ref->Get3D();
			// (its model's place in the world when loaded, else the reference's)
			const float k = root ? root->world.scale : a_ref->GetScale();
			const float half = 0.5f * (std::max)(b.boundMax.x - b.boundMin.x, b.boundMax.y - b.boundMin.y) * k;
			if (half <= 25.0f) {
				return RE::BSContainer::ForEachResult::kContinue;  // small stones are just ground
			}
			const auto rotate = root ? root->world.rotate : RE::NiMatrix3(a_ref->GetAngleX(), a_ref->GetAngleY(), a_ref->GetAngleZ());
			const auto origin = root ? root->world.translate : a_ref->GetPosition();
			// points over the rock's box, a little inside it (rocks are rounder than their boxes), and of those the ones
			// above the ground: what of it a piece would meet
			struct Above
			{
				float shift, side, height;
			};
			std::vector<Above> above;
			float tallest = 0.0f;
			constexpr int N = 6;
			for (int i = 0; i <= N; ++i) {
				for (int j = 0; j <= N; ++j) {
					for (int l = 0; l <= N; ++l) {
						if (i % N && j % N && l % N) {
							continue;  // its surface only
						}
						auto lerp = [](float a_lo, float a_hi, int a_step) {
							const float inset = 0.1f * (a_hi - a_lo);
							return a_lo + inset + (a_hi - a_lo - 2.0f * inset) * static_cast<float>(a_step) / N;
						};
						const RE::NiPoint3 local{ lerp(b.boundMin.x, b.boundMax.x, i), lerp(b.boundMin.y, b.boundMax.y, j), lerp(b.boundMin.z, b.boundMax.z, l) };
						const auto world = origin + rotate * (local * k);
						float land = 0.0f;
						if (!tes->GetLandHeight(world, land)) {
							land = fire.z;
						}
						// above the ground the camp stands on: the terrain, or the rock top you stand on (a camp on a flat
						// outcrop was refused by its own rock)
						if (onRock) {
							land = (std::max)(land, me.z - 10.0f);
						}
						if (const float h = world.z - land; h > 12.0f) {
							const auto [shift, side] = toFire(world.x, world.y);
							above.push_back({ shift, side, h });
							tallest = (std::max)(tallest, h);
						}
					}
				}
			}
			// a big rock (at least about 3 m across and 75 above the ground) stands in any piece's way; a smaller one
			// only where the shelter goes: it pokes through the floor you sleep on
			const bool big = half > 150.0f && tallest > 75.0f;
			for (std::size_t p = 0; p < pieces.size(); ++p) {
				if (!big && p != shelterIndex) {
					continue;
				}
				const bool hit = std::ranges::any_of(above, [&](const Above& a_point) { return outside(pieces[p], a_point.shift, a_point.side) < 0.0f; });
				if (hit) {
					++count;
					logger::info("Camp: a rock ({}, {:.0f} above the ground) where the {} would stand", path, tallest, pieces[p].name);
					break;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		logger::info("Camp: {} trees or big rocks where the camp would stand", count);
		return count;
	}


