// Part of the shared camp code: included last of the camp parts, inside the including file's anonymous namespace (not
// compiled on its own). Which of the two mods runs the camps, the camp's place in the save, and turning it all on.
// Free Cooking and Solid Ground Camping (CC Camping Improved) carry the same camp code. When both are installed, the
// newer camp code runs the camps and the other mod's camp part stays off (Free Cooking's cooking always runs); on equal
// versions Free Cooking runs them (CAMP_WINS_TIES). So an update of either mod alone brings the newer camps.
// The including file defines, before the camp parts: CAMP_PLUGIN (this mod's esp with the camp records), CAMP_OTHER_PLUGIN
// (the other mod's esp: the same records under the same local form IDs), CAMP_INI, CAMP_OTHER_DLL and CAMP_WINS_TIES.

	// raise by 1 whenever the camp code changes in a release, in both mods alike
	constexpr int CAMP_CODE_VERSION = 7;  // 2: the timers wait while a loading screen is up or the player's cell is not attached;
	                                      //    plugin lookups kept, loaded objects checked in one batch, lighter ground checks
	                                      //    a camp the other mod put up (mod added mid-game) is known as ours (IsCampBase)
	                                      // 3: loading a save or starting a new game drops the previous game's camp jobs and puts
	                                      //    back what they had changed (CampLoadReset)
	                                      // 4: owned camp pieces (OWND, PEND), the Change tent list (TSWP), unloaded pieces kept
	                                      //    in the save
	                                      // 5: camp state only on the main thread (OnMain), persistent pieces with camp numbers (OWN2,
	                                      //    CNUM), a damaged camp record set aside whole, fire parts known by kind
	                                      // 6: a camp touches only what it recorded: Camping's pieces noted at pitch (CPNG),
	                                      //    dungeon camps own their pieces (numbers with DUNGEON_CAMP set)
	                                      // 7: one ownership gate: every piece has a role (ROLE), menus and watchers carry their
	                                      //    exact shelter, a shelter no record names is never packed up or refunded

	bool g_campActive = true;  // this mod runs the camps (decided once the game's data is loaded)

	using CampVersionFn = int (*)();
	using CampStateFn = std::uint32_t (*)(std::uint8_t*, std::uint32_t, int);

	REX::W32::HMODULE OtherCampModule() { return REX::W32::GetModuleHandleA(CAMP_OTHER_DLL); }

	// which mod runs the camps: the other one only if its camp code is newer (or as new, when it wins ties)
	void DecideCampRunner()
	{
		g_campActive = true;
		auto other = OtherCampModule();
		if (!other) {
			logger::info("Camp: this mod runs the camps (camp code {}; {} not installed)", CAMP_CODE_VERSION, CAMP_OTHER_DLL);
			return;
		}
		auto fn = reinterpret_cast<CampVersionFn>(REX::W32::GetProcAddress(other, "Camp_CodeVersion"));
		const int theirs = fn ? fn() : 0;
		g_campActive = CAMP_CODE_VERSION > theirs || (CAMP_CODE_VERSION == theirs && CAMP_WINS_TIES);
		logger::info("Camp: camp code {} here, {} in {}: {} runs the camps", CAMP_CODE_VERSION, theirs, CAMP_OTHER_DLL,
			g_campActive ? "this mod" : "the other mod");
	}

	// ---- the camp in the save ----
	// Both mods keep the camp's state in their own save data, in the same form ('CAMP': a counter, then records), so
	// whichever runs the camps after an update finds a standing camp as it was. The running mod writes its live state;
	// the other writes a copy fetched from it. On loading, the running mod takes the newer of the two (higher counter).

	std::vector<std::uint8_t> g_campLoaded;  // the state read from this mod's save data (form IDs already resolved)
	std::uint32_t g_campSaveCount = 0;       // how many times the running mod has saved the camp state

	constexpr std::uint32_t CAMP_RECORD = 'CAMP';
	constexpr std::uint32_t CAMP_RECORD_VERSION = 1;
	constexpr std::uint32_t CAMP_FAVORITE = 'SFAV';
	constexpr std::uint32_t CAMP_PLANTS_HIDDEN = 'HIDE';
	constexpr std::uint32_t CAMP_PLANTS_TO_SHOW = 'SHOW';
	constexpr std::uint32_t CAMP_MOUND_TEXTURES = 'MTEX';
	constexpr std::uint32_t CAMP_BURNING_FIRES = 'FIRE';
	constexpr std::uint32_t CAMP_TENT = 'TENT';
	constexpr std::uint32_t CAMP_MOUND_LAND = 'MLND';
	constexpr std::uint32_t CAMP_HINTS = 'HINT';  // the one-time tutorials shown (bits: Basics.inl)
	constexpr std::uint32_t CAMP_TENT_NAMES = 'TNTN';  // a kit tent chosen or standing, by name (TENT keeps a stand-in number)
	constexpr std::uint32_t CAMP_TENT_DOOR = 'TDOR';  // the opening the standing shelter was put up with (GroundAndTents.inl g_tentDoorNow)
	constexpr std::uint32_t CAMP_SPOTS = 'SPOT';  // the spots placed round the fire, by reference (Basics.inl g_campSpots)
	constexpr std::uint32_t CAMP_OWNED = 'OWND';  // older saves: every piece the standing camp placed, reference and base (read only)
	constexpr std::uint32_t CAMP_OWNED_2 = 'OWN2';  // every piece an outdoor camp placed: reference, base, camp number (Basics.inl g_campOwned)
	constexpr std::uint32_t CAMP_CAMPING_PIECES = 'CPN2';  // Camping's own pieces of a camp: reference, base, camp number (Basics.inl g_campingPieces; 'CPNG' of a test build had no base: skipped)
	constexpr std::uint32_t CAMP_NUMBER = 'CNUM';  // the newest outdoor camp's number (Basics.inl g_campNumber)
	constexpr std::uint32_t CAMP_PENDING_2 = 'PND2';  // the same with the game day each started waiting (Basics.inl PENDING_DAYS)
	constexpr std::uint32_t CAMP_PENDING = 'PEND';  // pieces of camps packed up while their area was away (g_campPendingDelete)
	constexpr std::uint32_t CAMP_TENT_SWAP = 'TSWP';  // shelter and bedroll hidden for a moment by Change tent (GroundAndTents.inl)
	constexpr std::uint32_t CAMP_ROLES = 'ROLE';  // each recorded piece's role (Basics.inl PieceRole): reference, which list (0 ours, 1 Camping's), role
	constexpr std::uint32_t CAMP_PLANTS_CAMP = 'HCMP';  // the camp whose ground the hidden plants were cleared for (g_hiddenPlantsCamp)

	struct CampWriter
	{
		std::vector<std::uint8_t> bytes;
		template <class T>
		void Put(const T& a_value)
		{
			const auto p = reinterpret_cast<const std::uint8_t*>(&a_value);
			bytes.insert(bytes.end(), p, p + sizeof(T));
		}
		// a record: its type, then its length, then its data
		template <class F>
		void Record(std::uint32_t a_type, F&& a_write)
		{
			Put(a_type);
			const auto at = bytes.size();
			Put(std::uint32_t{ 0 });
			a_write();
			const auto length = static_cast<std::uint32_t>(bytes.size() - at - sizeof(std::uint32_t));
			std::memcpy(bytes.data() + at, &length, sizeof(length));
		}
	};

	struct CampReader
	{
		const std::uint8_t* at;
		const std::uint8_t* end;
		template <class T>
		bool Get(T& a_value)
		{
			if (static_cast<std::size_t>(end - at) < sizeof(T)) {
				return false;
			}
			std::memcpy(&a_value, at, sizeof(T));
			at += sizeof(T);
			return true;
		}
	};

	// the live camp state as bytes: the save counter, then one record per part (unknown records are skipped on reading,
	// so a newer camp code can add records an older one ignores)
	std::vector<std::uint8_t> CampStateBytes(std::uint32_t a_count, bool a_live = true)
	{
		CampWriter w;
		w.Put(a_count);
		w.Record(CAMP_FAVORITE, [&]() { w.Put(static_cast<std::uint8_t>(g_suppliesFavorite ? 1 : 0)); });
		for (const auto& [type, list] : { std::pair{ CAMP_PLANTS_HIDDEN, &g_hiddenPlants }, { CAMP_PLANTS_TO_SHOW, &g_plantsToShow } }) {
			w.Record(type, [&]() {
				w.Put(static_cast<std::uint32_t>(list->size()));
				for (auto id : *list) {
					w.Put(id);
				}
			});
		}
		w.Record(CAMP_MOUND_TEXTURES, [&]() {
			std::vector<std::pair<RE::FormID, std::array<float, 3>>> live;
			for (const auto& [id, t] : g_moundTexture) {
				auto ref = a_live ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr;
				if (!a_live || !ref || (!ref->IsDeleted() && !ref->IsMarkedForDeletion())) {  // (not found: its area is away, kept)
					live.emplace_back(id, t);  // (mounds of camps packed up are forgotten as they go: DeleteCampRef)
				}
			}
			w.Put(static_cast<std::uint32_t>(live.size()));
			for (const auto& [id, t] : live) {
				w.Put(id);
				w.Put(t);
			}
		});
		w.Record(CAMP_MOUND_LAND, [&]() {
			std::vector<std::pair<RE::FormID, RE::FormID>> live;
			for (const auto& [id, land] : g_moundLand) {
				auto ref = a_live ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr;
				if (!a_live || !ref || (!ref->IsDeleted() && !ref->IsMarkedForDeletion())) {  // (not found: its area is away, kept)
					live.emplace_back(id, land);
				}
			}
			w.Put(static_cast<std::uint32_t>(live.size()));
			for (const auto& [id, land] : live) {
				w.Put(id);
				w.Put(land);
			}
		});
		w.Record(CAMP_BURNING_FIRES, [&]() {
			w.Put(static_cast<std::uint32_t>(g_burningFires.size()));
			for (const auto& [fire, lit] : g_burningFires) {
				w.Put(fire);
				w.Put(lit);
			}
		});
		// our tents by number; a kit tent as our nearest one (the fur tent for a winter tent, else the hide tent), its
		// name in TNTN: code without that kit, or older code, still finds a tent of ours
		auto standIn = [](int a_tent) { return a_tent < 4 ? a_tent : (a_tent < TentCount() && g_tents[a_tent].winter ? WINTER_TENT : 1); };
		const int choice = g_tentChoice.load(), standing = g_campTent.load();
		w.Record(CAMP_TENT, [&]() {
			w.Put(static_cast<std::int32_t>(standIn(choice)));
			w.Put(static_cast<std::int32_t>(standing < 0 ? -1 : standIn(standing)));
		});
		w.Record(CAMP_TENT_NAMES, [&]() {
			auto putName = [&](int a_tent) {
				const std::string name = a_tent >= 4 && a_tent < TentCount() ? g_tents[a_tent].name : std::string{};
				w.Put(static_cast<std::uint32_t>(name.size()));
				w.bytes.insert(w.bytes.end(), name.begin(), name.end());
			};
			putName(choice);
			putName(standing);
		});
		w.Record(CAMP_HINTS, [&]() { w.Put(static_cast<std::uint8_t>(g_hintsShown.load())); });
		w.Record(CAMP_TENT_DOOR, [&]() { w.Put(g_tentDoorNow); });
		w.Record(CAMP_SPOTS, [&]() {
			std::vector<RE::FormID> live;
			for (const auto id : g_campSpots) {
				auto ref = a_live ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr;
				if (!a_live || !ref || (!ref->IsDeleted() && !ref->IsMarkedForDeletion())) {  // (not found: its area is away, kept)
					live.push_back(id);
				}
			}
			w.Put(static_cast<std::uint32_t>(live.size()));
			for (const auto id : live) {
				w.Put(id);
			}
		});
		w.Record(CAMP_TENT_SWAP, [&]() {
			std::scoped_lock lock(g_tentSwapLock);
			// (with the ones still waiting for their area from an earlier load: a second save kept them)
			w.Put(static_cast<std::uint32_t>(g_tentSwapRefs.size() + g_enableWhenLoaded.size()));
			for (const auto id : g_tentSwapRefs) {
				w.Put(id);
			}
			for (const auto id : g_enableWhenLoaded) {
				w.Put(id);
			}
		});
		w.Record(CAMP_OWNED_2, [&]() {
			w.Put(static_cast<std::uint32_t>(g_campOwned.size()));
			for (const auto& [id, piece] : g_campOwned) {
				w.Put(id);
				w.Put(piece.base);
				w.Put(piece.camp);
			}
		});
		w.Record(CAMP_PENDING_2, [&]() {
			w.Put(static_cast<std::uint32_t>(g_campPendingDelete.size()));
			for (const auto& [id, piece] : g_campPendingDelete) {
				w.Put(id);
				w.Put(piece.base);
				w.Put(piece.since);
			}
		});
		w.Record(CAMP_CAMPING_PIECES, [&]() {
			w.Put(static_cast<std::uint32_t>(g_campingPieces.size()));
			for (const auto& [id, piece] : g_campingPieces) {
				w.Put(id);
				w.Put(piece.base);
				w.Put(piece.camp);
			}
		});
		w.Record(CAMP_NUMBER, [&]() {
			w.Put(g_campNumber);
			w.Put(g_campShelterId);
		});
		// (a record of its own: OWN2 and CPN2 stay as they were, so an older camp code reading this save still finds every
		// piece; it skips this record)
		w.Record(CAMP_ROLES, [&]() {
			w.Put(static_cast<std::uint32_t>(g_campOwned.size() + g_campingPieces.size()));
			for (const auto& [which, list] : { std::pair{ std::uint8_t{ 0 }, &g_campOwned }, { std::uint8_t{ 1 }, &g_campingPieces } }) {
				for (const auto& [id, piece] : *list) {
					w.Put(id);
					w.Put(which);
					w.Put(static_cast<std::uint8_t>(piece.role));
				}
			}
		});
		w.Record(CAMP_PLANTS_CAMP, [&]() { w.Put(g_hiddenPlantsCamp); });
		return w.bytes;
	}

	bool g_tentFellBack = false;  // the loaded camp's kit tent is gone: its shelter is refitted to ours after the load
	// reads camp state bytes; a_resolve turns a saved form ID into this session's (or returns false to drop it).
	// a_apply: put it into the live state; otherwise only check and resolve it (the result is returned as bytes)
	template <class R>
	std::vector<std::uint8_t> ReadCampState(const std::vector<std::uint8_t>& a_bytes, R&& a_resolve, bool a_apply)
	{
		CampReader r{ a_bytes.data(), a_bytes.data() + a_bytes.size() };
		std::uint32_t count = 0;
		if (!r.Get(count)) {
			return {};
		}
		bool favorite = false;
		std::uint8_t hintsShown = 0;
		std::vector<RE::FormID> hidden, toShow, campSpots;
		std::unordered_map<RE::FormID, OwnedPiece> owned;
		std::unordered_map<RE::FormID, PendingPiece> pending;
		std::unordered_map<RE::FormID, OwnedPiece> campingPieces;
		std::uint32_t campNumber = 0;
		RE::FormID campShelter = 0;
		std::unordered_map<RE::FormID, std::uint8_t> rolesOwned, rolesCamping;
		std::uint32_t plantsCamp = 0;
		std::vector<RE::FormID> swapShown;  // hidden by a Change tent cut short by the save: shown again
		// a record shorter than it says, or a list shorter than its count (a damaged save): the whole saved camp is set aside,
		// never a part of it, since a half-read list leaves pieces nobody takes away
		bool cut = false;
		// a list's count against the bytes its record has left: false (and the camp data marked damaged) if they can't hold it
		auto fits = [&cut](const CampReader& a_rec, std::uint32_t a_count, std::size_t a_each) {
			if (static_cast<std::size_t>(a_rec.end - a_rec.at) / a_each < a_count) {
				cut = true;
				return false;
			}
			return true;
		};
		std::unordered_map<RE::FormID, std::array<float, 3>> mounds;
		std::unordered_map<RE::FormID, RE::FormID> moundLand;
		std::unordered_map<RE::FormID, float> fires;
		std::int32_t choice = 0, camp = -1;
		float tentDoor = std::numeric_limits<float>::quiet_NaN();
		std::string choiceName, campName;
		std::uint32_t type = 0, length = 0;
		while (r.end - r.at >= 8 && r.Get(type) && r.Get(length)) {  // (a header cut in two is caught below)
			if (static_cast<std::size_t>(r.end - r.at) < length) {
				cut = true;
				break;
			}
			CampReader rec{ r.at, r.at + length };
			r.at += length;
			if (type == CAMP_FAVORITE) {
				std::uint8_t f = 0;
				if (!rec.Get(f)) {
					cut = true;  // (every save read checks it got its value)
				}
				favorite = f != 0;
			} else if (type == CAMP_PLANTS_HIDDEN || type == CAMP_PLANTS_TO_SHOW) {
				auto& list = type == CAMP_PLANTS_HIDDEN ? hidden : toShow;
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0;
					if (!rec.Get(id)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now)) {
						list.push_back(now);
					}
				}
			} else if (type == CAMP_MOUND_TEXTURES) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID) + sizeof(std::array<float, 3>))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0;
					std::array<float, 3> t{};
					if (!rec.Get(id) || !rec.Get(t)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now)) {
						mounds[now] = t;
					}
				}
			} else if (type == CAMP_MOUND_LAND) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, 2 * sizeof(RE::FormID))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0, land = 0, landNow = 0;
					if (!rec.Get(id) || !rec.Get(land)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now) && a_resolve(land, landNow)) {
						moundLand[now] = landNow;
					}
				}
			} else if (type == CAMP_BURNING_FIRES) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID) + sizeof(float))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0;
					float lit = 0.0f;
					if (!rec.Get(id) || !rec.Get(lit)) {
						cut = true;
						break;
					}
					if (std::isfinite(lit) && a_resolve(id, now)) {
						fires[now] = lit;
					}
				}
			} else if (type == CAMP_TENT) {
				if (!rec.Get(choice) || !rec.Get(camp)) {
					cut = true;
				}
			} else if (type == CAMP_HINTS) {
				if (!rec.Get(hintsShown)) {
					cut = true;
				}
			} else if (type == CAMP_TENT_DOOR) {
				if (float d = 0.0f; !rec.Get(d)) {
					cut = true;  // (a cut value; a NaN read whole is the saved "no door yet")
				} else if (std::isfinite(d) && std::abs(d) < 100.0f) {
					tentDoor = d;
				}
			} else if (type == CAMP_TENT_NAMES) {
				for (auto name : { &choiceName, &campName }) {
					std::uint32_t n = 0;
					if (!rec.Get(n) || n > KIT_NAME_MAX || static_cast<std::size_t>(rec.end - rec.at) < n) {
						cut = true;  // (a name cut short: the whole state is refused, not a name silently lost)
						break;
					}
					name->assign(reinterpret_cast<const char*>(rec.at), n);
					rec.at += n;
				}
			} else if (type == CAMP_TENT_SWAP) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && k < 16 && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0;
					if (!rec.Get(id)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now)) {
						swapShown.push_back(now);
					}
				}
			} else if (type == CAMP_OWNED || type == CAMP_OWNED_2 || type == CAMP_PENDING || type == CAMP_PENDING_2) {
				const bool withCamp = type == CAMP_OWNED_2 || type == CAMP_PENDING_2;  // (PND2: the day, in the same 4 bytes)
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, 2 * sizeof(RE::FormID) + (withCamp ? sizeof(std::uint32_t) : 0))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n; ++k) {
					RE::FormID id = 0, base = 0, now = 0, baseNow = 0;
					std::uint32_t campOf = 0;  // ('OWND' of an older save: no number, camp 0)
					if (!rec.Get(id) || !rec.Get(base) || (withCamp && !rec.Get(campOf))) {
						cut = true;
						break;
					}
					if (a_resolve(id, now) && a_resolve(base, baseNow)) {
						if (type == CAMP_PENDING || type == CAMP_PENDING_2) {
							float since = -1.0f;
							if (type == CAMP_PENDING_2) {
								std::memcpy(&since, &campOf, sizeof(since));
								if (!std::isfinite(since) || since < 0.0f) {
									since = -1.0f;  // (not a day: counted from this load)
								}
							}
							pending[now] = { baseNow, since };
						} else {
							owned[now] = { baseNow, campOf };
						}
					}
				}
			} else if (type == CAMP_CAMPING_PIECES) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, 2 * sizeof(RE::FormID) + sizeof(std::uint32_t))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n; ++k) {
					RE::FormID id = 0, now = 0, base = 0, baseNow = 0;
					std::uint32_t of = 0;
					if (!rec.Get(id) || !rec.Get(base) || !rec.Get(of)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now) && a_resolve(base, baseNow)) {
						campingPieces[now] = { baseNow, of };
					}
				}
			} else if (type == CAMP_NUMBER) {
				RE::FormID shelterSaved = 0;
				if (!rec.Get(campNumber)) {
					cut = true;
				} else if (rec.at == rec.end) {
					// (an older save: the number alone, no shelter field)
				} else if (!rec.Get(shelterSaved)) {
					cut = true;  // (a shelter field cut short)
				} else if (shelterSaved != 0 && !a_resolve(shelterSaved, campShelter)) {
					campShelter = 0;
				}
			} else if (type == CAMP_ROLES) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID) + 2)) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n; ++k) {
					RE::FormID id = 0, now = 0;
					std::uint8_t which = 0, role = 0;
					if (!rec.Get(id) || !rec.Get(which) || !rec.Get(role)) {
						cut = true;
						break;
					}
					if (role <= static_cast<std::uint8_t>(PieceRole::kOther) && a_resolve(id, now)) {
						(which == 0 ? rolesOwned : rolesCamping)[now] = role;
					}
				}
			} else if (type == CAMP_PLANTS_CAMP) {
				if (!rec.Get(plantsCamp)) {
					cut = true;  // (camp 0 would let any camp's watcher show the hidden plants)
				}
			} else if (type == CAMP_SPOTS) {
				std::uint32_t n = 0;
				if (!rec.Get(n) || !fits(rec, n, sizeof(RE::FormID))) {
					cut = true;
					continue;
				}
				for (std::uint32_t k = 0; k < n && rec.at < rec.end; ++k) {
					RE::FormID id = 0, now = 0;
					if (!rec.Get(id)) {
						cut = true;
						break;
					}
					if (a_resolve(id, now)) {
						campSpots.push_back(now);
					}
				}
			}
		}
		// a kit tent by its name; a kit that is gone leaves our stand-in number
		auto byName = [](const std::string& a_name, std::int32_t& a_index) {
			if (a_name.empty()) {
				return;
			}
			for (int i = 4; i < TentCount(); ++i) {
				if (g_tents[i].name == a_name) {
					a_index = i;
					return;
				}
			}
			logger::info("Camp: the {} from a kit is not installed now: our {} instead", a_name, TentNameLower(a_index));
			g_tentFellBack = true;
		};
		byName(choiceName, choice);
		byName(campName, camp);
		if (r.at != r.end) {
			cut = true;  // (bytes left over that make no whole record)
		}
		// every owned piece's camp is the newest or older: a number above it means the number record was lost
		// (a dungeon camp's number has its own range: DUNGEON_CAMP)
		if (std::ranges::any_of(owned, [&](const auto& a_p) { return !IsDungeonCampNumber(a_p.second.camp) && a_p.second.camp > campNumber; }) ||
			std::ranges::any_of(campingPieces, [&](const auto& a_p) { return a_p.second.camp > campNumber; })) {
			cut = true;
		}
		if (cut) {
			logger::warn("Camp: the saved camp data is damaged (cut short): it is not used, so nothing of it is half applied");
			return {};
		}
		// each piece's role. A piece of ours saved without one (a save from before roles), or saved as Other (pieces placed
		// before every placement named its role): its role from the base it was recorded with. The record proves the piece
		// is ours; the base says what it is; a rug is the tent's rug only when it is not one of the camp's listed spots or
		// kit pieces (g_campSpots). Never a piece found by kind or distance (Change tent left an older
		// camp's rug, and the old-save shelter guess refunded another mod's shelter)
		for (auto& [id, piece] : owned) {
			if (auto it = rolesOwned.find(id); it != rolesOwned.end()) {
				piece.role = static_cast<PieceRole>(it->second);
			}
			if (piece.role == PieceRole::kUnknown || piece.role == PieceRole::kOther) {
				const bool listed = std::ranges::find(campSpots, id) != campSpots.end();
				if (listed) {
					piece.role = piece.role == PieceRole::kUnknown ? PieceRole::kSpot : piece.role;
				} else if (!IsDungeonCampNumber(piece.camp) && IsCampRug(RE::TESForm::LookupByID(piece.base))) {
					piece.role = PieceRole::kTentRug;  // (a dungeon camp's mat is no tent's rug)
				} else {
					piece.role = OwnRoleFromBase(piece.base);
				}
			}
		}
		for (auto& [id, piece] : campingPieces) {
			if (auto it = rolesCamping.find(id); it != rolesCamping.end()) {
				piece.role = static_cast<PieceRole>(it->second);
			} else {
				// a camp recorded before roles: Camping's piece's role from its recorded base (Camping places one of each
				// single kind per camp, and the record already proves the piece is that camp's)
				piece.role = CampingRoleOf(RE::TESForm::LookupByID(piece.base));
			}
		}
		// a save from before camps recorded their shelter: the newest camp's shelter is the one recorded for it with the
		// shelter's role (its role from its base above). No guess for a shelter no record names
		if (campShelter == 0) {
			for (const auto& [id, piece] : campingPieces) {
				if (piece.camp == campNumber && piece.role == PieceRole::kShelter) {
					campShelter = id;
					break;
				}
			}
		}
		if (a_apply) {
			g_campOwned = std::move(owned);
			g_campPendingDelete = std::move(pending);
			g_campingPieces = std::move(campingPieces);
			// (after the records are in place: only a piece they name is shown again, never whatever now has that
			// number)
			g_enableWhenLoaded.clear();
			for (const auto id : swapShown) {
				auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (!ref) {
					g_enableWhenLoaded.push_back(id);  // (its area isn't loaded: shown when it loads)
				} else if (ref->IsDisabled() && !ref->IsMarkedForDeletion() && !ref->IsDeleted() && ShowCampPiece(ref)) {
					logger::info("Camp: a shelter piece hidden by Change tent when the game was saved shown again");
				}
			}
			// a dungeon camp's piece that is gone (its piece was placed persistent, so one not found is gone, not away)
			// is forgotten: a camp never packed up does not stay in the save for good
			const auto before = g_campOwned.size();
			std::erase_if(g_campOwned, [](const auto& a_p) {
				return IsDungeonCampNumber(a_p.second.camp) && !SameRef(RE::TESForm::LookupByID<RE::TESObjectREFR>(a_p.first), a_p.second.base);
			});
			if (g_campOwned.size() != before) {
				logger::info("Camp: {} pieces of dungeon camps that are gone forgotten", before - g_campOwned.size());
			}
			// (an older save's list has no days: they start counting now)
			const float today = GameDays();
			for (auto& [id, piece] : g_campPendingDelete) {
				// (and a day still to come, from a damaged save: it would never grow old)
				if (piece.since < 0.0f || piece.since > today + 1.0f) {
					piece.since = today;
				}
			}
			g_campNumber = campNumber;
			g_campShelterId = campShelter;
			g_hiddenPlantsCamp = plantsCamp;
			PublishRecords();
			// mound pieces are hidden for 300 ms while they are fitted (Foundation.inl): a save in that moment kept them
			// hidden for good, since the job that shows them is dropped by the load
			int shown = 0;
			for (const auto& [id, piece] : g_campOwned) {
				auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				if (ref && SameRef(ref, piece.base) && ref->IsDisabled() && IsFoundation(ref->GetBaseObject()) && ShowCampPiece(ref)) {
					++shown;
				}
			}
			if (shown > 0) {
				logger::info("Camp: {} mound pieces hidden while being fitted when the game was saved shown again", shown);
			}
			g_suppliesFavorite = favorite;
			g_hiddenPlants = std::move(hidden);
			g_plantsToShow = std::move(toShow);
			g_moundTexture = std::move(mounds);
			g_moundLand = std::move(moundLand);
			g_burningFires = std::move(fires);
			g_tentChoice = std::clamp(choice, 0, TentCount() - 1);
			g_campTent = std::clamp(camp, -1, TentCount() - 1);
			g_hintsShown = hintsShown;
			g_campSpots = std::move(campSpots);
			g_tentDoorNow = tentDoor;
			g_campSaveCount = count;
			return {};
		}
		// the same state again as bytes, with the resolved form IDs
		const auto keepFavorite = g_suppliesFavorite.load();
		auto keepHidden = std::move(g_hiddenPlants);
		auto keepToShow = std::move(g_plantsToShow);
		auto keepMounds = std::move(g_moundTexture);
		auto keepLand = std::move(g_moundLand);
		auto keepFires = std::move(g_burningFires);
		const int keepChoice = g_tentChoice, keepCamp = g_campTent;
		const std::uint8_t keepHints = g_hintsShown;
		auto keepSpots = std::move(g_campSpots);
		const float keepDoor = g_tentDoorNow;
		auto keepOwned = std::move(g_campOwned);
		auto keepPending = std::move(g_campPendingDelete);
		auto keepCamping = std::move(g_campingPieces);
		const auto keepNumber = g_campNumber;
		const auto keepShelter = g_campShelterId;
		const auto keepPlantsCamp = g_hiddenPlantsCamp;
		g_hiddenPlantsCamp = plantsCamp;
		g_campShelterId = campShelter;
		g_campOwned = std::move(owned);
		g_campPendingDelete = std::move(pending);
		g_campingPieces = std::move(campingPieces);
		g_campNumber = campNumber;
		std::vector<RE::FormID> keepSwap;
		{
			// (the hidden shelter's list too: without it the bytes kept for the load lost it, and a shelter saved hidden stayed
			// hidden)
			std::scoped_lock lock(g_tentSwapLock);
			keepSwap = std::move(g_tentSwapRefs);
			g_tentSwapRefs = std::move(swapShown);
		}
		g_suppliesFavorite = favorite;
		g_hiddenPlants = std::move(hidden);
		g_plantsToShow = std::move(toShow);
		g_moundTexture = std::move(mounds);
		g_moundLand = std::move(moundLand);
		g_burningFires = std::move(fires);
		g_tentChoice = std::clamp(choice, 0, TentCount() - 1);
		g_campTent = std::clamp(camp, -1, TentCount() - 1);
		g_hintsShown = hintsShown;
		g_campSpots = std::move(campSpots);
		g_tentDoorNow = tentDoor;
		auto bytes = CampStateBytes(count, false);
		g_suppliesFavorite = keepFavorite;
		g_hiddenPlants = std::move(keepHidden);
		g_plantsToShow = std::move(keepToShow);
		g_moundTexture = std::move(keepMounds);
		g_moundLand = std::move(keepLand);
		g_burningFires = std::move(keepFires);
		g_tentChoice = keepChoice;
		g_campTent = keepCamp;
		g_hintsShown = keepHints;
		g_campSpots = std::move(keepSpots);
		g_tentDoorNow = keepDoor;
		g_campOwned = std::move(keepOwned);
		g_campPendingDelete = std::move(keepPending);
		g_campingPieces = std::move(keepCamping);
		g_campNumber = keepNumber;
		g_campShelterId = keepShelter;
		g_hiddenPlantsCamp = keepPlantsCamp;
		{
			std::scoped_lock lock(g_tentSwapLock);
			g_tentSwapRefs = std::move(keepSwap);
		}
		return bytes;
	}

	// the other mod's camp state: its live one (a_which 0) or the one read from its save data (1)
	std::vector<std::uint8_t> OtherCampState(int a_which)
	{
		auto other = OtherCampModule();
		auto fn = other ? reinterpret_cast<CampStateFn>(REX::W32::GetProcAddress(other, "Camp_State")) : nullptr;
		if (!fn) {
			return {};
		}
		const auto size = fn(nullptr, 0, a_which);
		std::vector<std::uint8_t> bytes(size);
		if (size == 0 || fn(bytes.data(), size, a_which) != size) {
			return {};
		}
		return bytes;
	}

	std::uint32_t CampCount(const std::vector<std::uint8_t>& a_bytes)
	{
		std::uint32_t count = 0;
		if (a_bytes.size() >= sizeof(count)) {
			std::memcpy(&count, a_bytes.data(), sizeof(count));
		}
		return count;
	}

	// save callback: the camp state as this mod's 'CAMP' record
	void CampSave(SKSE::SerializationInterface* a_intfc)
	{
		std::vector<std::uint8_t> bytes;
		if (g_campActive) {
			bytes = CampStateBytes(++g_campSaveCount);
		} else {
			bytes = OtherCampState(0);
			if (bytes.empty()) {
				bytes = g_campLoaded;  // (the other mod is gone: keep what this save had)
			}
		}
		if (!bytes.empty() && a_intfc->OpenRecord(CAMP_RECORD, CAMP_RECORD_VERSION)) {
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(bytes.size()));
			a_intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
		}
	}

	// load callback, for one record: true if it was the camp's. Older Free Cooking saves kept the camp in separate
	// records ('SFAV', 'HIDE', 'SHOW', 'MTEX', 'FIRE', 'TENT'): those are gathered into the same state (counter 0)
	std::vector<std::uint8_t> g_campLegacy;
	bool CampLoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, std::uint32_t a_length)
	{
		auto resolve = [&](RE::FormID a_id, RE::FormID& a_now) { return a_intfc->ResolveFormID(a_id, a_now); };
		if (a_type == CAMP_RECORD && a_version == CAMP_RECORD_VERSION) {
			std::uint32_t size = 0;
			if (!a_intfc->ReadRecordData(size) || size > a_length) {
				return true;
			}
			std::vector<std::uint8_t> bytes(size);
			if (size && a_intfc->ReadRecordData(bytes.data(), size) == size) {
				g_campLoaded = ReadCampState(bytes, resolve, false);
			}
			return true;
		}
		const bool legacy = a_version == 1 && (a_type == CAMP_FAVORITE || a_type == CAMP_PLANTS_HIDDEN || a_type == CAMP_PLANTS_TO_SHOW ||
												  a_type == CAMP_MOUND_TEXTURES || a_type == CAMP_BURNING_FIRES || a_type == CAMP_TENT);
		if (!legacy || !a_length) {
			return false;
		}
		// the old record's data is the same as the new one's record body
		std::vector<std::uint8_t> body(a_length);
		if (a_intfc->ReadRecordData(body.data(), a_length) != a_length) {
			return true;
		}
		if (g_campLegacy.empty()) {
			g_campLegacy.resize(sizeof(std::uint32_t), 0);  // counter 0
		}
		CampWriter w{ std::move(g_campLegacy) };
		w.Record(a_type, [&]() { w.bytes.insert(w.bytes.end(), body.begin(), body.end()); });
		g_campLegacy = std::move(w.bytes);
		g_campLoaded = ReadCampState(g_campLegacy, resolve, false);
		return true;
	}

	void CampRevert()
	{
		g_suppliesFavorite = false;
		g_hiddenPlants.clear();
		g_plantsToShow.clear();
		g_burningFires.clear();
		g_moundTexture.clear();
		g_moundLand.clear();
		g_tentChoice = 0;
		g_campTent = -1;
		g_hintsShown = 0;
		g_hintPending = 0;
		g_campSpots.clear();
		g_campOwned.clear();
		g_campingPieces.clear();
		g_campNumber = 0;
		g_campShelterId = 0;
		g_hiddenPlantsCamp = 0;
		PublishRecords();
		g_enableWhenLoaded.clear();
		g_campPendingDelete.clear();
		g_tentDoorNow = std::numeric_limits<float>::quiet_NaN();
		g_campLoaded.clear();
		g_campLegacy.clear();
		g_campSaveCount = 0;
		// the cookware flags back to "not upgraded" before every load and new game: the save then puts back its own
		// value, so an upgrade can't carry over from the save loaded before (a save made before Solid Ground Camping
		// was installed has no value for its flag, and a new game keeps whatever the last save left)
		for (auto g : { CookwareGlobal(), OtherCookwareGlobal() }) {
			if (g) {
				g->value = 0.0f;
			}
		}
	}

	// ---- load-save safeguard ----
	// at kPreLoadGame and kNewGame, before anything of the next game runs (both mods call it for their own DLL, whether or
	// not it runs the camps): the camp jobs of the game being left are dropped (DropGameTimers: a pack-up watcher that
	// lost its tent would hand out Camping Supplies in the next game, a new-camp watcher would put a pot by a stranger's
	// fire), what they would have put back is put back now, and the short-lived flags start clean. Only the session's
	// watchers (ground, ice, burning fires, brews) carry on, and they wait out the load (g_loadingSave)
	void CampLoadReset()
	{
		DropGameTimers();
		CampPotCleaner::GetSingleton()->Forget();
		g_dungeonPitching = false;  // (its dropped job never clears it)
		EndFade();                 // controls back and no black screen, if a save loaded mid-fade
		RestoreSilencedSounds();   // Camping's ignite and fire-out sounds
		RestoreCampingWorlds();    // a worldspace taken off Camping's list for a camp on ice
		SetPitchOnIce(false);      // the build prompt's Yes condition back to "any Firewood"
		g_oldCampPacking = false;
		{
			std::scoped_lock lock(g_tentSwapLock);
			g_tentSwapRefs.clear();  // (the loaded save's own list shows its hidden ones again: ReadCampState)
		}
		g_campTentBeforePitch = -2;
		g_pitchLit = true;
		g_dungeonBedSleep = false;
		g_packUpFree = false;
		g_campGround = 0;
		g_shelterSleep = 0;
		g_bedPassThrough = 0;
		g_shelterMenuFor = 0;
		g_hintPending = 0;
	}

	// ---- the cookware upgrade, in both mods' records ----
	// upgraded in either mod counts, and is marked in both (so nothing is lost when the other mod takes over)
	void SyncCookware()
	{
		auto mine = CookwareGlobal();
		auto theirs = OtherCookwareGlobal();
		if ((mine && mine->value >= 1.0f) || (theirs && theirs->value >= 1.0f)) {
			if (mine) {
				mine->value = 1.0f;
			}
			if (theirs) {
				theirs->value = 1.0f;
			}
		}
	}

	// the Camping Supplies' name: "Camping Supplies (Fine)" once upgraded with the cookware (as the forge marks what it
	// improves), its own name otherwise. The item is Camping's, so the name is set again on every load
	void NameCampingSupplies()
	{
		static std::string plain;
		auto supplies = CampingForm(CAMP_SUPPLIES);
		auto full = supplies ? supplies->As<RE::TESFullName>() : nullptr;
		if (!full) {
			return;
		}
		if (plain.empty()) {
			plain = full->GetFullName();
		}
		const std::string name = CampHasCookware() ? TrF("{0} (Fine)", plain) : plain;
		full->fullName = name;
	}

	// after a load, the camp standing in the loaded area as this save has it: the game builds the shelter's and the
	// mounds' models while the save loads, before the camp state is applied here, so the shelter showed Camping's
	// lean-to (the model its record had then) and the mounds kept their plain dirt (found 2026-11-16 loading a save
	// right after starting the game). So the shelter and its bedroll are reloaded with this save's model, refitted to
	// the camp's spot when a kit tent fell back to ours, and the mounds painted again with their kept ground
	// the camp's mounds painted again with their kept ground. a_loadedOnly: only those whose model is in the world now
	// (after a loading screen); otherwise each waits for its model (after a save loads)
	int RepaintCampMounds(bool a_loadedOnly)
	{
		int count = 0;
		for (const auto& [id, t] : g_moundTexture) {
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			// (one whose area is attached but whose model isn't built yet is painted when it is: PaintWithGround waits for it;
			// only a mound, never another reference that took over a deleted mound's number)
			const auto cell = ref ? ref->GetParentCell() : nullptr;
			if (!ref || ref->IsDisabled() || ref->IsMarkedForDeletion() || !IsFoundation(ref->GetBaseObject()) || !IsCampOwned(ref) ||
				(a_loadedOnly && !ref->Get3D() && !(cell && cell->IsAttached()))) {
				continue;
			}
			const bool splash = IsCampBase(ref->GetBaseObject(), GROUND_BLEND);
			RE::TESLandTexture* land = nullptr;
			if (auto it = g_moundLand.find(id); !splash && it != g_moundLand.end()) {
				land = RE::TESForm::LookupByID<RE::TESLandTexture>(it->second);
			}
			PaintWithGround(ref->GetHandle(), splash ? 0.0f : 600.0f, 0, land);
			++count;
		}
		return count;
	}

	void RefreshCampAfterLoad()
	{
		RepaintCampMounds(false);
		auto shelter = CurrentShelter();
		auto tes = RE::TES::GetSingleton();
		if (!shelter || !tes) {
			g_tentFellBack = false;
			return;
		}
		if (g_tentFellBack) {
			// (its spot worked out with our tent's own bounds: close to where the kit tent stood)
			const auto settings = CampSettings();
			RE::NiPoint3 centre;
			float facing = 0.0f;
			TentSlotOf(shelter, centre, facing);
			const auto fit = FitTentToSlot(TentModelNow(), centre, facing, settings);
			g_tentDoorNow = TentDoor(TentModelNow(), settings);
			ScaleCampPiece(shelter, fit.scale);
			MoveRef(shelter, { fit.origin.x, fit.origin.y, shelter->GetPositionZ() }, { 0.0f, 0.0f, fit.rz });
			g_tentFellBack = false;
		}
		std::vector<RE::ObjectRefHandle> reload{ shelter->GetHandle() };
		// (only the bedroll recorded for this camp, by its role: another mod's copy of Camping's bedroll stays put)
		if (auto bed = CampRef(CampOf(shelter), PieceRole::kBedroll); bed && !bed->IsDisabled()) {
			reload.push_back(bed->GetHandle());
		}
		SwapReload(reload);  // (not a pack-up: shown again in 300 ms)
		logger::info("Camp: the loaded camp's shelter shown again as the {}, its mounds painted again", TentNameLower(TentModelNow()));
	}

	// a save was loaded (or a new game started): the running mod takes the newer camp state of the two mods
	void CampGameLoaded()
	{
		if (!g_campActive) {
			return;
		}
		auto mine = g_campLoaded;
		auto theirs = OtherCampState(1);
		const auto& use = !theirs.empty() && (mine.empty() || CampCount(theirs) > CampCount(mine)) ? theirs : mine;
		if (!use.empty()) {
			ReadCampState(use, [](RE::FormID a_id, RE::FormID& a_now) { a_now = a_id; return a_id != 0; }, true);
			logger::info("Camp: state taken from {} (saved {} times)", &use == &theirs ? CAMP_OTHER_DLL : "this mod's save data", CampCount(use));
		}
		SyncCookware();
		UpdateSmithingPerkFlag();
		NameCampingSupplies();
		SetCampTentModel();  // the shelter of this save's camp
		RefreshCampAfterLoad();
	}

	// a new game (also one started with coc from the main menu): no camp state and no cookware upgrade, whatever the
	// save played before it left behind (SKSE's own revert may come after this message)
	void CampNewGame()
	{
		CampRevert();
		logger::info("Camp: new game, no camp state and no cookware upgrade");
		if (!g_campActive) {
			return;
		}
		UpdateSmithingPerkFlag();
		NameCampingSupplies();
		SetCampTentModel();
	}

	// ---- the camp's pot over a cold fire ----
	// a camp's cooking pot (either mod's) with no burning fire under it (a cold fire, laid but not lit)
	bool CampPotUnlit(RE::TESObjectREFR* a_pot)
	{
		// over a cold fire the camp's pot is put away (disabled) and a stand-in takes its place (BlockCampPots, by the
		// camp's record), so a pot you can sit at stands over a burning fire. No search for a burning fire within 250
		// units: it called a pot the layout sliders put further out unlit, and this runs on the
		// menu thread, where the camp record is not read
		auto base = a_pot ? a_pot->GetBaseObject() : nullptr;
		if (!base || !IsCampBase(base, POT_PLACED)) {
			return false;
		}
		const bool unlit = a_pot->IsDisabled();
		logger::info("Camp: cooking pot used, fire {}", unlit ? "not lit" : "burning");
		return unlit;
	}

	// the camp's pot over a cold fire: nothing cooks; the crafting menu closes and you stand up. (Also notes when the
	// one-time tutorials' Tutorial Menu really opens)
	class CampPotGuard : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static CampPotGuard* GetSingleton()
		{
			static CampPotGuard singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->opening && g_hintPending && std::string_view(a_event->menuName.c_str()) == RE::TutorialMenu::MENU_NAME) {
				g_hintsShown |= g_hintPending.load();  // our tutorial is on screen: now it counts as shown (Basics.inl)
				g_hintPending = 0;
				logger::info("Camp: tutorial shown");
			}
			// a loading screen closed (a door, fast travel, coc, a save): the camp's mounds in the world painted again. Their
			// models can be built again on the way back to an area without the load watcher painting them (in-game test
			// 2026-11-16: back from Whiterun the whole mound was its plain grey and the log had no paint lines). Once the
			// world is ready (a timer), so the cell has attached; painting a mound that kept its look changes nothing
			if (a_event && !a_event->opening && std::string_view(a_event->menuName.c_str()) == RE::LoadingMenu::MENU_NAME) {
				After(500, []() {
					if (const int n = RepaintCampMounds(true); n > 0) {
						logger::info("Camp: after a loading screen, {} camp mound(s) in the world painted again", n);
					}
				});
			}
			if (!a_event || !a_event->opening || std::string_view(a_event->menuName.c_str()) != RE::CraftingMenu::MENU_NAME) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// (all of it on the main thread: this menu event comes on the UI thread, and the furniture the player sits at and
			// the camp's records are the main thread's)
			OnMain([]() {
				auto player = Player();
				auto seat = player ? player->GetOccupiedFurniture().get() : nullptr;
				if (!seat) {
					return;
				}
				// sitting down to cook at a camp's fire without the cookware: the one-time hint where the camp's pot comes from
				if (seat->GetBaseObject() == CampingForm(CAMP_FIRE) && !CampHasCookware()) {
					ShowTutorialOnce(HINT_COOKWARE, true);
				}
				// a camp's pot over a cold fire: its menu closes (burnout also closes an open pot's menu: CloseCampPotMenu in
				// BlockCampPots). Only a pot our records name
				if (IsCampBase(seat->GetBaseObject(), POT_PLACED) && IsCampOwned(seat.get()) && CampPotUnlit(seat.get())) {
					CloseCampPotMenu(seat.get());
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Camping Supplies used outdoors: our pieces go up with Camping's camp (indoors the dungeon camp is ours, made
	// whole by the supplies hook)
	class CampSuppliesUse : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static CampSuppliesUse* GetSingleton()
		{
			static CampSuppliesUse singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			auto player = Player();
			if (!a_event || !a_event->equipped || !player || a_event->actor.get() != player) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (const auto supplies = CampingForm(CAMP_SUPPLIES); supplies && a_event->baseObject == supplies->GetFormID()) {
				if (player->GetParentCell() && !Underground(player)) {
					OnMain([]() { WatchNewCamp(); });  // (on the main thread, with the rest of the camp's state)
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// this mod's own forge recipe for the cookware, hidden while the other mod runs the camps (both would show)
	void HideOwnCookwareRecipe()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (auto recipe = data ? data->LookupForm<RE::BGSConstructibleObject>(0xA04, CAMP_PLUGIN) : nullptr) {
			recipe->benchKeyword = nullptr;
			logger::info("Camp: this mod's cookware recipe hidden (the other mod runs the camps)");
		}
	}

	// at kDataLoaded: decide who runs the camps; the runner turns its camp work on. (The timers' player update hook is
	// installed by the including mod either way: both mods' own work uses it.)
	void CampSetup()
	{
		g_gameDataLoaded = true;  // the load order is final: plugin lookups are kept from now on (Core/Common.inl)
		DecideCampRunner();
		if (!g_campActive) {
			HideOwnCookwareRecipe();
			return;
		}
		SetCampTentModel();
		ReadSmithingPerks();
		NoBuildingOnIce();
		ShelterMenuChangeTent();
		DeadlandsIsAPlane();
		WatchBurningFires();
		WatchCampGround();
		// the copy the hooks read is rebuilt after every record change, and once a second too: a piece's area loading or
		// going away changes what can be seen of it without any record changing
		RepeatAlways(1000, []() {
			if (!g_loadingSave) {
				PublishRecords();
			}
			return true;
		});
		if (auto ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(CampPotGuard::GetSingleton());
		}
		if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESEquipEvent>(CampSuppliesUse::GetSingleton());
			// first in line, so it acts before Camping's script handles the same activation (an event sink can't stop a
			// script's OnActivate; it only runs first)
			if (auto source = events->GetEventSource<RE::TESActivateEvent>()) {
				source->PrependEventSink(CampWatcher::GetSingleton());
			}
			events->AddEventSink<RE::TESObjectLoadedEvent>(CampPotCleaner::GetSingleton());
			CampPotCleaner::Start();
			events->AddEventSink<RE::TESCellFullyLoadedEvent>(PlantReturner::GetSingleton());
			events->AddEventSink<RE::TESContainerChangedEvent>(SuppliesReturnWatcher::GetSingleton());
		}
		SuppliesHook::Install();
		BedrollHook::Install();
		PackUpPromptHook::Install();
	}
