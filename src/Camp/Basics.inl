// Part of the shared camp code (Free Cooking's CookingMenu.cpp, Solid Ground Camping's main.cpp): included in order inside an anonymous namespace (not compiled on its own). Camping forms, camp piece bases, ground painting, removing camp pieces.
	// Camping (Anniversary Edition): its campfire cooks over an open fire. The keyword lives in Update.esm, so it is
	// always there; the campsite's own records are only looked up when Camping is installed.
	constexpr RE::FormID KEYWORD_CAMPFIRE = 0x314B;   // Update.esm: Camping_CampfireCookingShared
	constexpr const char* CAMPING = "ccqdrsse002-firewood.esl";
	constexpr RE::FormID CAMP_SUPPLIES = 0x801;       // ccQDRSSE002_CampingSupplies (used like a potion)
	constexpr RE::FormID CAMP_SHELTER = 0x803;        // ccQDRSSE002_ShelterAct (activate: break camp)
	constexpr RE::FormID CAMP_FIRE = 0x804;           // ccQDRSSE002_CampfireFurn

	RE::TESForm* CampingForm(RE::FormID a_id)
	{
		return PluginForm(a_id, CAMPING);  // (looked for once: Core/Common.inl)
	}

	// The plugin does the camp and pot work itself: calling Papyrus functions from here (DispatchStaticCall) crashed
	// on 1.6.1170, and natives with object parameters crashed at game start.
	constexpr RE::FormID FIREWOOD = 0x0006F993;
	// a kit tent's name, at most (the save keeps it in TNTN: the loader and the save reader use this same limit)
	constexpr std::size_t KIT_NAME_MAX = 256;
	constexpr RE::FormID POT_PLACED = 0x985;  // FreeCooking.esp: FC_CampCookingPot (the pot set over a fire)
	constexpr RE::FormID POT_COLD = 0xA06;    // FreeCooking.esp: FC_CampCookingPotCold, the pot while its fire is cold
	constexpr RE::FormID STAND = 0x00108183;  // Skyrim.esm: CookingStand01, the iron stand the pot hangs from
	constexpr RE::FormID BEDROLL_STATIC = 0x00094AC6;  // Skyrim.esm: Bedroll01Static, the fur bedroll laid in the tent
	constexpr RE::FormID BLOCK_PLACED = 0x98E;  // FreeCooking.esp: FC_CampChoppingBlock (every camp gets one)

	// (filtered to the one item: GetInventoryCounts with no filter copies the whole inventory into a new map, every
	// entry of it, on each call; CommonLibSSE TESObjectREFR.cpp. Some of these counts run every second)
	std::int32_t ItemCount(RE::TESObjectREFR* a_ref, RE::TESForm* a_item)
	{
		std::int32_t have = 0;
		if (a_ref && a_item) {
			for (const auto& [item, count] : a_ref->GetInventoryCounts([a_item](RE::TESBoundObject& a_object) { return &a_object == a_item; })) {
				if (item == a_item) {
					have = count;
				}
			}
		}
		return have;
	}

	RE::TESBoundObject* PluginItem(RE::FormID a_id)
	{
		auto form = PluginForm(a_id, CAMP_PLUGIN);
		return form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
	}

	// a camp record of either mod. Both esps carry the same camp records under the same local IDs, and a camp put up
	// while the other mod ran the camps (one of the two mods added or removed mid-game) is made of the other mod's
	// records: every "is this one of the camp's pieces?" check asks this, so such a camp can still be lit, used and
	// packed up. New pieces are always placed from this mod's own records (PluginItem)
	bool IsCampBase(const RE::TESForm* a_base, RE::FormID a_id)
	{
		if (!a_base) {
			return false;
		}
		if (a_base == PluginItem(a_id)) {
			return true;
		}
		auto other = PluginForm(a_id, CAMP_OTHER_PLUGIN);
		return other && a_base == other;
	}

	// Camp foundation (test): our own deep square block (Fallout 4 style), scaled to the tent and sunk into the ground
	// with its top level, so the tent, its floor and bedroll sit flat on any slope. It wears the ground's own texture
	RE::TESBoundObject* FoundationBase(int a_length = 10, bool a_turned = false);
	// how much ground one repeat of the land's textures covers: a cell (4096 across, 32 steps) repeats them
	// 32 x fLandTextureTilingMult / 4 times, 24 with the game's default of 3: about 171. Read from the game's own
	// setting (Skyrim.ini [Display]), so the mounds match the land with any value. camp.ini moundtile overrides it
	float LandTextureRepeat()
	{
		float mult = 3.0f;
		for (RE::INISettingCollection* ini : { static_cast<RE::INISettingCollection*>(RE::INIPrefSettingCollection::GetSingleton()), RE::INISettingCollection::GetSingleton() }) {
			if (auto setting = ini ? ini->GetSetting("fLandTextureTilingMult:Display") : nullptr; setting && setting->GetFloat() > 0.0f) {
				mult = setting->GetFloat();
				break;
			}
		}
		return 4096.0f / (32.0f * mult / 4.0f);
	}
	// how each mound's texture is laid (FormID -> its sign and offset): a camp's mounds all show the ground the same way
	// round and in step, so it runs on across them with no seam (PaintWithGround)
	std::unordered_map<RE::FormID, std::array<float, 3>> g_moundTexture;
	// the ground texture each mound (and lump of earth on it) wears: the camp's one, chosen from round its foot when it
	// was laid, kept in the save so it is painted the same when drawn again (MLND)
	std::unordered_map<RE::FormID, RE::FormID> g_moundLand;
	bool IsSkirtPlant(RE::TESObjectREFR* a_ref);
	RE::TESBoundObject* PluginItem(RE::FormID a_id);

	// underground: an interior, or one of the cavern worlds built like the outdoors (Blackreach, the Labyrinthian and
	// Alftand caverns, Frostmere Crypt...: Camping's own list of them, ccQDRSSE002_InteriorAreas)
	bool InCavernWorld(const RE::TESObjectREFR* a_ref)
	{
		auto world = a_ref ? a_ref->GetWorldspace() : nullptr;
		auto list = CampingForm(0x81E) ? CampingForm(0x81E)->As<RE::BGSListForm>() : nullptr;
		return world && list && list->HasForm(world);
	}

	bool Underground(const RE::TESObjectREFR* a_ref)
	{
		auto cell = a_ref ? a_ref->GetParentCell() : nullptr;
		return (cell && cell->IsInteriorCell()) || InCavernWorld(a_ref);
	}

	// Camp cookware: the Camping Supplies upgraded once at a forge (2 Iron Ingots, with any Smithing perk) carry a cooking
	// pot and its iron stand, set over every campfire from then on (FreeCooking.esp: FC_CampCookware, kept in the save)
	constexpr RE::FormID COOKWARE_GLOBAL = 0xA02;  // FC_CampCookware: 1 once upgraded
	constexpr RE::FormID COOKWARE_KIT = 0xA03;     // FC_CampCookwareKit: what the forge recipe makes, taken at once

	RE::TESGlobal* CookwareGlobal()
	{
		return PluginForm<RE::TESGlobal>(COOKWARE_GLOBAL, CAMP_PLUGIN);
	}

	// the same upgrade in the other mod's records (Free Cooking and Solid Ground Camping carry the same camp)
	RE::TESGlobal* OtherCookwareGlobal()
	{
		return PluginForm<RE::TESGlobal>(COOKWARE_GLOBAL, CAMP_OTHER_PLUGIN);
	}

	bool CampHasCookware()
	{
		auto g = CookwareGlobal();
		auto o = OtherCookwareGlobal();
		return (g && g->value >= 1.0f) || (o && o->value >= 1.0f);
	}

	// The cookware recipe asks for any one perk of the Smithing tree (his call 2026-11-16: "any one blacksmith perk"), so
	// a skill-tree overhaul that changes or replaces Steel Smithing still lets the player forge the pot. A recipe can't
	// ask "any perk of a tree", so it reads a flag, SGC_SmithingPerkTaken, kept here: 1 while the player has a perk of
	// the Smithing skill's perk tree as the game holds it (the tree such overhauls rewrite), ranks included. Set when a
	// save loads or a new game starts and whenever the player uses furniture (a forge among them), before its menu opens
	constexpr RE::FormID SMITHING_PERK_FLAG = 0xA5D;  // SGC_SmithingPerkTaken (GLOB)
	std::vector<RE::BGSPerk*> g_smithingPerks;        // read once at kDataLoaded, only read afterwards

	void ReadSmithingPerks()
	{
		g_smithingPerks.clear();
		auto info = RE::ActorValueList::GetActorValueInfo(RE::ActorValue::kSmithing);
		auto root = info ? info->perkTree : nullptr;
		std::vector<RE::BGSSkillPerkTreeNode*> todo;
		std::unordered_set<RE::BGSSkillPerkTreeNode*> seen;
		if (root) {
			todo.push_back(root);
		}
		while (!todo.empty()) {
			auto node = todo.back();
			todo.pop_back();
			if (!node || !seen.insert(node).second) {
				continue;
			}
			for (auto perk = node->perk; perk; perk = perk->nextPerk) {  // a ranked perk's later ranks too
				if (std::find(g_smithingPerks.begin(), g_smithingPerks.end(), perk) != g_smithingPerks.end()) {
					break;
				}
				g_smithingPerks.push_back(perk);
			}
			for (auto child : node->children) {
				todo.push_back(child);
			}
		}
		logger::info("Camp: {} perks in the Smithing tree (the cookware recipe asks for any one)", g_smithingPerks.size());
	}

	void UpdateSmithingPerkFlag()
	{
		auto flag = PluginForm<RE::TESGlobal>(SMITHING_PERK_FLAG, CAMP_PLUGIN);
		auto player = Player();
		if (!flag || !player) {
			return;
		}
		bool taken = false;
		for (auto perk : g_smithingPerks) {
			if (player->HasPerk(perk)) {
				taken = true;
				break;
			}
		}
		flag->value = taken ? 1.0f : 0.0f;
	}

	// ---- one-time tutorial boxes ----
	// Shown once per character, the way vanilla's own first-time tutorials (forging, cooking, smelting...) open: the
	// game's Tutorial Menu with a message record, which pauses the game until closed and sits above the vanilla
	// crafting menu, message boxes and Free Cooking's screen. Not shown when the game's tutorials are turned off.
	// Kept in the save (camp state record 'HINT', one bit each, Setup.inl).
	// - the cookware: the first time you sit down to cook at a camp's fire without the cookware upgrade, so a player who
	//   doesn't smith learns where the camp's pot comes from (CampPotGuard, Setup.inl)
	// - gathering fuel: the first time a dungeon camp's cold fire offers to gather something to burn (an hour passes)
	constexpr RE::FormID HELP_COOKWARE = 0xA59;  // FC_HelpCampCookware / SGC_HelpCampCookware (MESG)
	constexpr RE::FormID HELP_GATHER = 0xA5A;    // FC_HelpGatherFuel / SGC_HelpGatherFuel (MESG)
	constexpr std::uint8_t HINT_COOKWARE = 1, HINT_GATHER = 2;
	std::atomic<std::uint8_t> g_hintsShown{ 0 };
	std::atomic<std::uint8_t> g_hintPending{ 0 };  // asked for; counted as shown only when the menu really opens (CampPotGuard)

	// a_now: at once, over the menu or box that just opened; otherwise at a quiet moment of play
	void ShowTutorialOnce(std::uint8_t a_hint, bool a_now = false, int a_tries = 0)
	{
		if ((g_hintsShown & a_hint) || g_hintPending || (a_hint == HINT_COOKWARE && CampHasCookware())) {
			return;
		}
		const char* what = a_hint == HINT_COOKWARE ? "cookware" : "gathering";
		auto form = PluginForm(a_hint == HINT_COOKWARE ? HELP_COOKWARE : HELP_GATHER, CAMP_PLUGIN);
		auto message = form ? form->As<RE::BGSMessage>() : nullptr;
		auto ui = RE::UI::GetSingleton();
		auto queue = RE::UIMessageQueue::GetSingleton();
		auto player = Player();
		if (!message || !ui || !queue || !player) {
			return;
		}
		// a quiet moment: no menu over the game, not fighting, no save loading (tried again for about a minute)
		if (ui->IsMenuOpen(RE::TutorialMenu::MENU_NAME) || g_loadingSave || (!a_now && (ui->GameIsPaused() || player->IsInCombat()))) {
			if (a_tries < 20) {
				After(3000, [a_hint, a_tries]() { ShowTutorialOnce(a_hint, false, a_tries + 1); });
			}
			return;
		}
		auto setting = RE::INISettingCollection::GetSingleton() ? RE::INISettingCollection::GetSingleton()->GetSetting("bShowTutorials:Interface") : nullptr;
		if (setting && !setting->GetBool()) {
			logger::info("Camp: {} tutorial not shown (the game's tutorials are off)", what);
			return;
		}
		// opened as CommonLibSSE's TutorialMenu::OpenMenu does, but without its once-per-session list: our own flags,
		// kept in the save, make it once per character. And it only counts as shown once the menu has really opened
		// (2026-10-31 research, ChatGPT: OpenMenu marks it shown as soon as it is queued, so one that never appears,
		// e.g. asked for during a fade, would be lost). If it hasn't opened 5 s of play later, it is asked for again
		auto ui_data = RE::UIMessageDataFactory::Create<RE::TESFormUIData>();
		if (!ui_data) {
			return;
		}
		ui_data->data = message;
		g_hintPending = a_hint;
		queue->AddMessage(RE::TutorialMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, ui_data);
		logger::info("Camp: {} tutorial asked for", what);
		After(5000, [a_hint, a_tries]() {
			if (g_hintPending == a_hint) {
				g_hintPending = 0;
				logger::info("Camp: a tutorial didn't open, trying again");
				if (a_tries < 20) {
					ShowTutorialOnce(a_hint, false, a_tries + 1);
				}
			}
		});
	}
	// FC_CampFoundation: 256 across, 256 x a_length / 10 long (10 to 40), its top at its origin, 1536 deep (its sides
	// reach out to 1660). A reference can only be scaled evenly, so each shape is its own model, each also with its
	// texture turned a quarter (a_turned): see FoundationBase's form IDs below
	RE::FormID FoundationID(int a_length, bool a_turned)
	{
		a_length = std::clamp(a_length, 10, 40);
		if (a_length > 20) {
			return (a_turned ? 0xA26 : 0xA12) + (a_length - 21);  // the long ones (2.1 to 4.0)
		}
		if (a_turned) {
			return a_length == 10 ? 0xA07 : 0xA08 + (a_length - 11);  // FC_CampFoundationT*: its texture turned a quarter
		}
		return a_length == 10 ? 0x9F2 : 0x9F5 + (a_length - 11);
	}
	RE::TESBoundObject* FoundationBase(int a_length, bool a_turned)
	{
		return PluginItem(FoundationID(a_length, a_turned));
	}
	// the rug laid under the tent's bedroll: our woven straw mat in the small tents (FreeCooking.esp FC_CampStrawMat),
	// a fur pelt in the large Nordic winter tent (Skyrim.esm PeltFloor01)
	constexpr RE::FormID RUG_STRAW = 0x9FF;
	constexpr RE::FormID RUG_FUR = 0x5C015;       // Skyrim.esm PeltFloor01 (camps pitched before 1.1 have this one)
	constexpr RE::FormID RUG_FUR_OURS = 0xA5C;    // SolidGroundCamping.esp SGC_CampFurRug: PeltFloor01's look, camps only
	constexpr RE::FormID STAND_OURS = 0xA5B;      // SolidGroundCamping.esp SGC_CampPotStand: CookingStand01's look, camps only
	// camps place our own copies of these two base-game statics (his call 2026-11-16), so a modder's replacement model
	// (meshes\SolidGroundCamping\CampFurRug.nif, CampPotStand.nif: ApplyCampModelReplacers) changes only camp copies.
	// Without our plugin's record (an older esp), the base-game one
	RE::TESBoundObject* FurRugBase()
	{
		if (auto ours = PluginItem(RUG_FUR_OURS)) {
			return ours;
		}
		return RE::TESForm::LookupByID<RE::TESObjectSTAT>(RUG_FUR);
	}
	RE::TESBoundObject* StandBase()
	{
		if (auto ours = PluginItem(STAND_OURS)) {
			return ours;
		}
		return RE::TESForm::LookupByID<RE::TESObjectSTAT>(STAND);
	}
	bool IsCampStand(const RE::TESForm* a_base)
	{
		return a_base && (a_base->GetFormID() == STAND || IsCampBase(a_base, STAND_OURS));
	}
	// the tent's bedroll look (Camping's bedroll record in a tent): ours, or a modder's model at our path
	std::string g_campBedrollModel = "Furniture\\Bedroll\\Bedroll01.nif";
	// a modder's replacement models at our own paths (plain mesh replacers): the camp copies take them. Read once at
	// kDataLoaded. Our own pieces (chopping block, straw mat, pots, cold fire) already load their model from our paths,
	// so a file there replaces them with no code
	void ApplyCampModelReplacers()
	{
		auto exists = [](const char* a_path) {
			RE::BSResourceNiBinaryStream stream(std::string("Meshes\\") + a_path);
			return stream.good();
		};
		const std::pair<RE::FormID, const char*> copies[]{ { STAND_OURS, "SolidGroundCamping\\CampPotStand.nif" }, { RUG_FUR_OURS, "SolidGroundCamping\\CampFurRug.nif" } };
		for (const auto& [id, path] : copies) {
			auto form = PluginItem(id);
			auto model = form ? form->As<RE::TESModel>() : nullptr;
			if (model && exists(path)) {
				model->SetModel(path);
				logger::info("Camp: {} found: camps use it", path);
			}
		}
		if (exists("SolidGroundCamping\\CampBedroll.nif")) {
			g_campBedrollModel = "SolidGroundCamping\\CampBedroll.nif";
			logger::info("Camp: SolidGroundCamping\\CampBedroll.nif found: tents use it");
		}
	}
	bool IsCampRug(const RE::TESForm* a_base)
	{
		return a_base && (IsCampBase(a_base, RUG_STRAW) || IsCampBase(a_base, RUG_FUR_OURS) || a_base->GetFormID() == RUG_FUR);
	}
	// an invisible navigation cut round the tent (FreeCooking.esp FC_CampNavCut: only collision, on the L_NAVCUT layer,
	// with the Obstacle flag), so NPCs path round the tent instead of walking into it
	constexpr RE::FormID TENT_NAVCUT = 0xA3B;
	bool IsCampNavCut(const RE::TESForm* a_base)
	{
		return IsCampBase(a_base, TENT_NAVCUT);
	}
	// grass tufts on the mounds: FreeCooking.esp FC_Grass_* (0xA3C on), one per vanilla grass model, copied as statics
	// (FreeCooking\\Grass\\, from tools\\meshgen\\grasstuft.cpp: the grass models with their vertex alpha, the grass
	// system's wind weight, turned off so the blades show whole)
	constexpr RE::FormID GRASS_TUFT_FIRST = 0xA3C;
	constexpr int GRASS_TUFT_COUNT = 29;
	// the spots round a camp's fire where people warm their hands or sit (the game's own markers, so other mods that
	// let the player use idle markers know them): standing and crouching warm-hands markers, and sitting on the ground
	constexpr RE::FormID WARM_STAND = 0x000E8644;  // Skyrim.esm WarmHandsStandIdleMarker
	constexpr RE::FormID WARM_CROUCH = 0x000E8645;  // WarmHandsCrouchIdleMarker
	constexpr RE::FormID SIT_GROUND = 0x000C4EFD;   // SitCrossLeggedMarker (Camping places one of these too)
	// kit files can add their own kinds of spot and drop ours ([FireSpots], LoadKits in PlantsMoundsFollowers.inl): the
	// kinds placed round the fire, in turn, and every kind a camp may have (ours and the kits'), for the pack-up. Set
	// once at kDataLoaded, only read afterwards
	std::vector<RE::FormID> g_fireSpotKinds{ WARM_CROUCH, SIT_GROUND, WARM_STAND };
	std::vector<RE::FormID> g_kitSpotKinds;
	int g_kitFireSpotCount = -1;  // a kit's Count (-1: none set); the player's own firespots line in the ini wins
	// every spot a camp placed round its fire, by reference (kept in the save, record 'SPOT'): packing up takes exactly
	// these, whatever kind they are, so none is left in the save even when the kit that added its kind is gone
	std::vector<RE::FormID> g_campSpots;
	// every piece this mod placed for an outdoor camp, by reference: its base and the number of the camp it belongs to
	// (saved, record 'OWN2'; older saves had 'OWND' without the number: those pieces count as camp 0, taken by any
	// whole pack-up). Placed persistent, like Campfire places its camp gear: such a piece stays in memory wherever it
	// stands, so packing up takes it at once even when its area is far away and unloaded (Full research 2026-11-16:
	// Campfire _Camp_ObjectPlacementThread.psc abForcePersist; a piece kept only by its number in our save data was
	// gone from memory with its area). The pending list (saved, 'PEND') is left only for pieces of older saves, placed
	// before this. Every camp has its own number (g_campNumber, saved): a pack-up takes only its own camp's pieces, so
	// an old camp still being packed up when a new one goes up can't take the new camp's what a recorded piece is for
	// (saved in its own record, 'ROLE'). A camp acts on a piece through its role, never by its kind or where it stands:
	// the tent's rug is the TentRug, not any rug near the bed (Change tent took a kit's rug). kUnknown: an older save's
	// piece, recorded before roles
	enum class PieceRole : std::uint8_t
	{
		kUnknown = 0,
		kShelter,
		kBedroll,
		kBurningFire,
		kLight,
		kCrackle,
		kSeat,
		kMarker,
		kColdFire,
		kPot,
		kPotStandIn,
		kTentRug,
		kSpot,
		kKitPiece,
		kMound,
		kBlock,
		kOther,
	};
	struct OwnedPiece
	{
		RE::FormID    base = 0;
		std::uint32_t camp = 0;  // 0: placed before camps had numbers (an older save)
		PieceRole     role = PieceRole::kUnknown;
	};
	std::unordered_map<RE::FormID, OwnedPiece> g_campOwned;
	// a piece of an older save's camp packed up while its area was away: taken when it loads, if that happens within
	// PENDING_DAYS. Older than that, its number is forgotten, never acted on: the game reuses a number only after millions
	// of new objects, so an entry this old could one day name another mod's object
	struct PendingPiece
	{
		RE::FormID base = 0;
		float      since = -1.0f;  // game days passed when it started waiting (-1: not known yet, set at the next load)
	};
	std::unordered_map<RE::FormID, PendingPiece> g_campPendingDelete;
	constexpr float PENDING_DAYS = 30.0f;
	float GameDays()
	{
		auto calendar = RE::Calendar::GetSingleton();
		return calendar && calendar->gameDaysPassed ? calendar->gameDaysPassed->value : 0.0f;
	}
	std::uint32_t g_campNumber = 0;
	// the newest camp's shelter (saved with the number, 'CNUM'): a pack-up watcher for another shelter is an older camp's,
	// and takes only pieces older than the newest camp
	RE::FormID g_campShelterId = 0;
	// a dungeon camp is being found or built (set when the supplies are used, cleared when it is up or refused): a second
	// use meanwhile is ignored, so two camps never go up at once
	std::atomic_bool g_dungeonPitching = false;
	constexpr std::uint32_t OLDER_CAMPS = 0xFFFFFFFE;
	// pieces hidden for a moment when the game was saved (Change tent's shelter and bedroll) whose area wasn't loaded after
	// the load: shown again when they load (CampPotCleaner), not forgotten
	std::vector<RE::FormID> g_enableWhenLoaded;  // the standing (or newest) outdoor camp's number; 0: none yet in this save
	constexpr std::size_t PENDING_DELETE_MAX = 20000;  // (a safety cap only: with persistent pieces only older saves' pieces wait here)
	// (with its base: a number the game gave to something else since is not ours. Not
	// SameRef: a piece being taken away is still ours while it goes)
	bool IsCampOwned(const RE::TESObjectREFR* a_ref)
	{
		const auto it = a_ref ? g_campOwned.find(a_ref->GetFormID()) : g_campOwned.end();
		const auto base = it != g_campOwned.end() ? a_ref->GetBaseObject() : nullptr;
		return base && base->GetFormID() == it->second.base;
	}
	// still the reference this number was given to: a reference, not deleted or being deleted, with the same base. The
	// game only hands out a freed number again after its counter wraps (about 4 million new objects) or when an older
	// save is loaded, so this is the cheap guard against those (Full research 2026-11-16: TESDataHandler::GetNextID,
	// SkyrimSE.exe 1.7.104 ID 13740)
	bool SameRef(const RE::TESObjectREFR* a_ref, RE::FormID a_base)
	{
		const auto base = a_ref && !a_ref->IsDeleted() && !a_ref->IsMarkedForDeletion() ? a_ref->GetBaseObject() : nullptr;
		return base && base->GetFormID() == a_base;
	}
	// The one rule of the camp's cleanup: a camp moves, hides or takes away only what it recorded when it was made. Never
	// a reference found by its kind or by how close it stands (that took other mods' objects). What it records: every piece it places (g_campOwned) and Camping's own pieces of the camp, noted
	// once as the camp goes up (g_campingPieces)
	// a dungeon camp's number: this bit set (an outdoor camp's never has it), so packing up an outdoor camp, or a new one
	// going up, never takes a dungeon camp's pieces, and a dungeon camp is packed up by its own number
	constexpr std::uint32_t DUNGEON_CAMP = 0x80000000;
	bool IsDungeonCampNumber(std::uint32_t a_camp) { return (a_camp & DUNGEON_CAMP) != 0 && a_camp < OLDER_CAMPS; }
	// a number for a new dungeon camp: one past the highest a dungeon camp still standing has
	std::uint32_t NewDungeonCampNumber()
	{
		std::uint32_t top = 0;
		for (const auto& [id, piece] : g_campOwned) {
			if (IsDungeonCampNumber(piece.camp)) {
				top = (std::max)(top, piece.camp & ~DUNGEON_CAMP);
			}
		}
		return DUNGEON_CAMP | (top + 1);
	}
	// Camping's own pieces of an outdoor camp (its shelter, fire, light, crackle, seat, bedroll, idle markers), by
	// reference, with the camp's number: found once as the camp goes up, as what is new near the fire since the Camping
	// Supplies were used (WatchNewCamp). Saved ('CPNG'). Camping takes them away itself at its pack-up; we only move them
	// into place, hide the burning fire's parts while it is cold, and lay the mound round them
	// (with each piece's base, checked before it is acted on: a number the game gave to something else since is left
	// alone)
	std::unordered_map<RE::FormID, OwnedPiece> g_campingPieces;
	bool IsCampingPiece(const RE::TESObjectREFR* a_ref)
	{
		const auto it = a_ref ? g_campingPieces.find(a_ref->GetFormID()) : g_campingPieces.end();
		return it != g_campingPieces.end() && SameRef(a_ref, it->second.base);
	}
	// a recorded piece of Camping's, when it is still that piece
	RE::TESObjectREFR* CampingRef(RE::FormID a_id, const OwnedPiece& a_piece)
	{
		auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
		return SameRef(ref, a_piece.base) ? ref : nullptr;
	}
	// anything a camp recorded: its own pieces or Camping's
	bool IsCampPiece(const RE::TESObjectREFR* a_ref)
	{
		return IsCampOwned(a_ref) || IsCampingPiece(a_ref);
	}
	// the camp a recorded piece belongs to (0: none recorded)
	std::uint32_t CampOf(const RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return 0;
		}
		if (IsCampOwned(a_ref)) {
			return g_campOwned.find(a_ref->GetFormID())->second.camp;
		}
		if (auto it = g_campingPieces.find(a_ref->GetFormID()); it != g_campingPieces.end() && SameRef(a_ref, it->second.base)) {
			return it->second.camp;
		}
		return 0;
	}
	// ---- the ownership gate (main thread: camp state lives there). Every action on a camp's piece finds it here, by
	// its record: camp number, role, and the base it was recorded with
	// the role a recorded piece has (kUnknown: not recorded, or an older save's)
	PieceRole RoleOf(const RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return PieceRole::kUnknown;
		}
		if (IsCampOwned(a_ref)) {
			return g_campOwned.find(a_ref->GetFormID())->second.role;
		}
		if (auto it = g_campingPieces.find(a_ref->GetFormID()); it != g_campingPieces.end() && SameRef(a_ref, it->second.base)) {
			return it->second.role;
		}
		return PieceRole::kUnknown;
	}
	// this exact reference is camp a_camp's piece in role a_role
	bool IsCampRole(const RE::TESObjectREFR* a_ref, std::uint32_t a_camp, PieceRole a_role)
	{
		return a_ref && a_camp && CampOf(a_ref) == a_camp && RoleOf(a_ref) == a_role;
	}
	// every recorded piece of camp a_camp in one of the roles (none given: every role), still the reference it was
	// recorded as. a_campingToo: Camping's pieces of the camp as well as ours
	template <class F>
	void ForCampPieces(std::uint32_t a_camp, std::initializer_list<PieceRole> a_roles, bool a_campingToo, F&& a_fn)
	{
		auto wanted = [&](PieceRole a_r) { return a_roles.size() == 0 || std::ranges::find(a_roles, a_r) != a_roles.end(); };
		std::vector<RE::TESObjectREFR*> found;
		for (const auto& [id, piece] : g_campOwned) {
			if (piece.camp == a_camp && wanted(piece.role)) {
				if (auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); SameRef(ref, piece.base)) {
					found.push_back(ref);
				}
			}
		}
		if (a_campingToo) {
			for (const auto& [id, piece] : g_campingPieces) {
				if (piece.camp == a_camp && wanted(piece.role)) {
					if (auto ref = CampingRef(id, piece)) {
						found.push_back(ref);
					}
				}
			}
		}
		for (auto ref : found) {  // (collected first: the visitor may change the lists)
			a_fn(ref);
		}
	}
	// the one recorded piece of camp a_camp in role a_role (a shelter, a bedroll, a burning fire), or none
	RE::TESObjectREFR* CampRef(std::uint32_t a_camp, PieceRole a_role)
	{
		RE::TESObjectREFR* one = nullptr;
		if (a_camp) {
			ForCampPieces(a_camp, { a_role }, true, [&](RE::TESObjectREFR* a_ref) {
				if (!one) {
					one = a_ref;
				}
			});
		}
		return one;
	}
	// a fire's own parts: the pieces of the fire's camp in a fire part's role (Camping's light and crackle, our crackle,
	// a cold fire, a burning fire put away), never the fire itself. Each camp has one fire, so its parts are found by role,
	// not by standing near it (the 30-unit search was the same guess by distance the gate replaced).
	// Disabled ones too (the burning fire's parts while it is cold). a_campingOnly: Camping's own pieces only
	std::vector<RE::TESObjectREFR*> FireParts(RE::TESObjectREFR* a_fire, bool a_campingOnly)
	{
		std::vector<RE::TESObjectREFR*> found;
		const auto camp = CampOf(a_fire);
		if (!a_fire || !camp) {
			return found;
		}
		ForCampPieces(camp, { PieceRole::kLight, PieceRole::kCrackle, PieceRole::kColdFire, PieceRole::kBurningFire }, true, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref != a_fire && (!a_campingOnly || IsCampingPiece(a_ref))) {
				found.push_back(a_ref);
			}
		});
		return found;
	}

	// ---- what the records say, published for code on other threads (the bedroll hook, the supplies hook, Free Cooking's
	// question about a pot): they read this copy, never the records themselves (the pot export and the
	// hooks read the camp's state off the main thread). Rebuilt on the main thread after every change to the records
	struct PublicPiece
	{
		RE::FormID    base = 0;
		std::uint32_t camp = 0;
		PieceRole     role = PieceRole::kUnknown;
		bool          hidden = false;  // (disabled when published: readers never read the live flag)
	};
	std::mutex g_publicLock;
	std::unordered_map<RE::FormID, PublicPiece> g_publicPieces;
	void PublishRecords()
	{
		std::unordered_map<RE::FormID, PublicPiece> before;
		{
			std::scoped_lock lock(g_publicLock);
			before = g_publicPieces;
		}
		std::unordered_map<RE::FormID, PublicPiece> all;
		// each record checked against its live reference here, on the main thread: a number now naming another object
		// (another base) is left out of the copy; a piece whose area is not loaded keeps the state it was last seen in
		// (shown when it was never seen: it is not shown as put away only because it is far)
		auto publish = [&](RE::FormID a_id, const OwnedPiece& a_p) {
			bool hidden = false;
			if (auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id)) {
				auto base = ref->GetBaseObject();
				if (!base || base->GetFormID() != a_p.base) {
					return;
				}
				hidden = ref->IsDisabled() || ref->IsMarkedForDeletion() || ref->IsDeleted();
			} else if (const auto it = before.find(a_id); it != before.end() && it->second.base == a_p.base) {
				hidden = it->second.hidden;
			}
			all[a_id] = { a_p.base, a_p.camp, a_p.role, hidden };
		};
		for (const auto& [id, p] : g_campOwned) {
			publish(id, p);
		}
		for (const auto& [id, p] : g_campingPieces) {
			publish(id, p);
		}
		std::scoped_lock lock(g_publicLock);
		g_publicPieces = std::move(all);
	}
	// any thread: the published record of this form ID. Only the copy is read, nothing of the live reference (its base
	// was checked on the main thread when the copy was made; a number is forgotten, and the copy rebuilt, as its piece
	// goes)
	std::optional<PublicPiece> PublicPieceOf(RE::FormID a_id)
	{
		std::scoped_lock lock(g_publicLock);
		const auto it = g_publicPieces.find(a_id);
		if (!a_id || it == g_publicPieces.end()) {
			return std::nullopt;
		}
		return it->second;
	}
	// any thread: the one published piece of a camp in a role (0: none). a_shown: only one not hidden
	RE::FormID PublicCampRef(std::uint32_t a_camp, PieceRole a_role, bool a_shown = false)
	{
		std::scoped_lock lock(g_publicLock);
		for (const auto& [id, p] : g_publicPieces) {
			if (a_camp && p.camp == a_camp && p.role == a_role && (!a_shown || !p.hidden)) {
				return id;
			}
		}
		return 0;
	}

	bool IsFoundation(const RE::TESForm* a_base);
	// the role of a piece of ours, from the base it was recorded with: given at placement when the caller names none, and
	// to the pieces of a save from before roles (the record already proves the piece is ours; its base says what it is).
	// Never the tent rug, a spot or a kit piece: those are told apart by their own records (g_campSpots), not by kind
	PieceRole OwnRoleFromBase(RE::FormID a_base)
	{
		auto base = RE::TESForm::LookupByID(a_base);
		if (!base) {
			return PieceRole::kOther;
		}
		if (IsCampBase(base, POT_PLACED)) {
			return PieceRole::kPot;
		}
		if (IsCampBase(base, POT_COLD)) {
			return PieceRole::kPotStandIn;
		}
		if (IsCampBase(base, 0xA05)) {  // (FreeCooking.esp FC_ColdCampfire)
			return PieceRole::kColdFire;
		}
		if (IsCampBase(base, BLOCK_PLACED)) {
			return PieceRole::kBlock;
		}
		if (a_base == 0x00037E3E) {  // (Skyrim.esm FXFireCampfireLP: a crackle we placed)
			return PieceRole::kCrackle;
		}
		if (a_base == 0x000AF8BA) {  // (Skyrim.esm LightCampFire01: a dungeon fire's light)
			return PieceRole::kLight;
		}
		if (base == CampingForm(CAMP_FIRE)) {
			return PieceRole::kBurningFire;  // (a dungeon camp's fire, or an outdoor fire lit again)
		}
		if (a_base == 0x0003B3CD) {  // (Skyrim.esm Bedroll01F: a dungeon camp's bedroll)
			return PieceRole::kBedroll;
		}
		if (IsFoundation(base)) {
			return PieceRole::kMound;
		}
		return PieceRole::kOther;
	}
	// a piece placed for a camp, recorded with the camp's number, placed persistent so it stays in memory wherever it
	// stands (a dungeon camp's too: a camp left standing is then still found, and forgotten at a load once it is gone)
	RE::NiPointer<RE::TESObjectREFR> PlaceOwned(RE::TESObjectREFR* a_at, RE::TESBoundObject* a_base, std::uint32_t a_camp = 0, PieceRole a_role = PieceRole::kOther)
	{
		const auto camp = a_camp ? a_camp : g_campNumber;
		auto ref = a_at && a_base ? a_at->PlaceObjectAtMe(a_base, true) : RE::NiPointer<RE::TESObjectREFR>();
		if (ref) {
			const auto role = a_role == PieceRole::kOther ? OwnRoleFromBase(a_base->GetFormID()) : a_role;
			g_campOwned[ref->GetFormID()] = { a_base->GetFormID(), camp, role };
			g_campPendingDelete.erase(ref->GetFormID());  // (a new piece with that number: the old one is gone)
			g_campingPieces.erase(ref->GetFormID());
			PublishRecords();
		}
		return ref;
	}
	// a piece placed at a camp's piece (a fire, a pot): it joins that piece's camp. At something no camp recorded: the
	// newest outdoor camp's outdoors; underground, a_camp's (a dungeon camp's fire placed first) or none
	RE::NiPointer<RE::TESObjectREFR> PlaceCampPiece(RE::TESObjectREFR* a_at, RE::TESBoundObject* a_base, std::uint32_t a_camp = 0, PieceRole a_role = PieceRole::kOther)
	{
		auto camp = a_camp ? a_camp : CampOf(a_at);
		if (!camp && a_at && !Underground(a_at)) {
			camp = g_campNumber;
		}
		if (camp) {
			return PlaceOwned(a_at, a_base, camp, a_role);
		}
		return a_at && a_base ? a_at->PlaceObjectAtMe(a_base, false) : RE::NiPointer<RE::TESObjectREFR>();
	}
	// ---- the gate for every action on a piece: hide, show, move, take away. Each acts only on a reference the records name
	// as a camp's piece (ours or Camping's) and still the piece recorded (the same base); anything else is refused and
	// logged, whoever asks (the actions took any reference they were handed, so each new caller was a
	// new chance to touch another mod's object). A record is forgotten only after its piece is taken away
	bool GateOk(const RE::TESObjectREFR* a_ref, const char* a_what)
	{
		if (IsCampPiece(a_ref)) {
			return true;
		}
		if (a_ref) {
			logger::warn("Camp: {:08X} is no camp's recorded piece: not {}", a_ref->GetFormID(), a_what);
		}
		return false;
	}
	bool HideCampPiece(RE::TESObjectREFR* a_ref)
	{
		if (!GateOk(a_ref, "hidden")) {
			return false;
		}
		a_ref->Disable();
		PublishRecords();
		return true;
	}
	bool ShowCampPiece(RE::TESObjectREFR* a_ref)
	{
		if (!GateOk(a_ref, "shown")) {
			return false;
		}
		a_ref->Enable(false);
		PublishRecords();
		return true;
	}
	// a camp's piece resized (the gate's other actions: MoveRef in GroundAndTents.inl checks it too)
	template <class P>
	bool ScaleCampPiece(const P& a_ref, float a_scale)
	{
		RE::TESObjectREFR* ref = a_ref ? &*a_ref : nullptr;
		if (!GateOk(ref, "resized")) {
			return false;
		}
		ref->SetScale(a_scale);
		return true;
	}
	// every list that names this number forgets it
	void ForgetPiece(RE::FormID a_id)
	{
		g_campOwned.erase(a_id);
		g_campPendingDelete.erase(a_id);
		g_campingPieces.erase(a_id);
		std::erase(g_campSpots, a_id);
		g_moundTexture.erase(a_id);
		g_moundLand.erase(a_id);
		PublishRecords();
	}
	// (only after a record proved it: the gate below, or a pending piece's own record in TakePendingDelete)
	void RemoveRecordedRef(RE::TESObjectREFR* a_ref)
	{
		const auto id = a_ref->GetFormID();
		a_ref->Disable();
		a_ref->SetDelete(true);
		ForgetPiece(id);
	}
	// a camp's piece taken away, then forgotten by every list that names it
	bool DeleteCampRef(RE::TESObjectREFR* a_ref)
	{
		if (!GateOk(a_ref, "taken away")) {
			return false;
		}
		RemoveRecordedRef(a_ref);
		return true;
	}
	// a pending piece that has just loaded: taken away now (main thread)
	bool TakePendingDelete(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return false;
		}
		const auto it = g_campPendingDelete.find(a_ref->GetFormID());
		if (it == g_campPendingDelete.end()) {
			return false;
		}
		const bool fresh = it->second.since < 0.0f || GameDays() - it->second.since <= PENDING_DAYS;
		// and nothing that could be someone else's: no container (it may hold things), nothing owned, nothing a quest holds.
		// The pieces that wait here are an older save's, placed before pieces were persistent: a number reused by the game
		// could name another mod's object with the same base, so those are left alone
		const auto base = a_ref->GetBaseObject();
		const bool safe = base && !base->Is(RE::FormType::Container) && !a_ref->GetOwner() &&
		                  !a_ref->extraList.HasType(RE::ExtraDataType::kAliasInstanceArray);
		const bool same = fresh && safe && SameRef(a_ref, it->second.base);
		g_campPendingDelete.erase(it);
		if (!same) {
			return false;  // its number was reused by something else: left alone
		}
		RemoveRecordedRef(a_ref);  // (its pending record, checked just above, is the proof)
		logger::info("Camp: a piece of a camp packed up while its area was away taken now");
		return true;
	}
	// kit pieces ([Piece] in a kit file, LoadKits): furniture, a crafting station or anything else an author adds to every
	// outdoor camp, Along and Across from the fire (the pot's axes: Along out from the fire away from the tent, minus
	// toward it; Across to the side) and turned by Turn. Read once at kDataLoaded, only read afterwards. The pieces a
	// camp gets are kept in g_campSpots (saved, packed up by reference, like the spots round the fire)
	struct KitPiece
	{
		std::string name;
		RE::FormID  form = 0;
		float       along = 0.0f, across = 0.0f, turn = 0.0f;
		bool        mound = true;  // stands on a mound of its own joined to the camp's (no: on the ground as it is)
	};
	std::vector<KitPiece> g_kitPieces;
	// the camp being pitched now: its kit pieces (reference, mound or not), for the mound laid next (not saved)
	std::vector<std::pair<RE::FormID, std::size_t>> g_pitchKitPieces;  // (reference, index in g_kitPieces)
	bool IsPitchKitPiece(const RE::TESObjectREFR* a_ref)
	{
		return a_ref && std::ranges::any_of(g_pitchKitPieces, [&](const auto& a_p) { return a_p.first == a_ref->GetFormID(); });
	}
	bool IsFireSpot(const RE::TESForm* a_base)
	{
		const auto id = a_base ? a_base->GetFormID() : 0;
		return id != 0 && (id == WARM_STAND || id == WARM_CROUCH || id == SIT_GROUND || std::ranges::find(g_kitSpotKinds, id) != g_kitSpotKinds.end());
	}

	bool IsCampGrass(const RE::TESForm* a_base)
	{
		if (!a_base) {
			return false;
		}
		for (int i = 0; i < GRASS_TUFT_COUNT; ++i) {
			if (IsCampBase(a_base, GRASS_TUFT_FIRST + i)) {
				return true;
			}
		}
		return false;
	}
	// a model's file name, lower case ("Landscape\\Grass\\ForestGrass01.nif" -> "forestgrass01.nif")
	std::string ModelFileName(const RE::TESForm* a_form)
	{
		auto model = a_form ? a_form->As<RE::TESModel>() : nullptr;
		std::string path = model && model->GetModel() ? model->GetModel() : "";
		if (const auto slash = path.find_last_of("\\/"); slash != std::string::npos) {
			path = path.substr(slash + 1);
		}
		std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return path;
	}
	// our tuft for a kind of grass (matched by its model's file name), or none for grass we have no copy of
	RE::TESBoundObject* GrassTuftFor(const RE::TESGrass* a_grass)
	{
		static std::unordered_map<std::string, RE::TESBoundObject*> tufts;
		if (tufts.empty()) {
			for (int i = 0; i < GRASS_TUFT_COUNT; ++i) {
				if (auto tuft = PluginItem(GRASS_TUFT_FIRST + i)) {
					tufts[ModelFileName(tuft)] = tuft;
				}
			}
		}
		const auto it = tufts.find(ModelFileName(a_grass));
		return it != tufts.end() ? it->second : nullptr;
	}
	constexpr RE::FormID DIRT_PATCH = 0xA00;  // FreeCooking.esp: FC_CampDirtPatch, a low lump of earth on the foundation
	constexpr RE::FormID GROUND_BLEND = 0xA01;  // FreeCooking.esp: FC_CampGroundBlend, a soft splash of the hill's ground on its slope
	bool IsFoundation(const RE::TESForm* a_base)
	{
		if (!a_base) {
			return false;
		}
		if (IsCampBase(a_base, DIRT_PATCH) || IsCampBase(a_base, GROUND_BLEND)) {
			return true;  // removed with the foundation, and never counted as one of the camp's pieces
		}
		for (int k = 10; k <= 40; ++k) {
			if (IsCampBase(a_base, FoundationID(k, false)) || IsCampBase(a_base, FoundationID(k, true))) {
				return true;
			}
		}
		return false;
	}

	// the ground texture that shows most around a point: the terrain blends up to six textures over its base at each of
	// its points (every 128 units), and the base alone (what the game's own lookup gives) is often not what you see, like
	// gravel under snow. Weighed over the terrain's points within a_radius (at least the nearest one)
	RE::TESLandTexture* VisibleLandTexture(const RE::NiPoint3& a_at, float a_radius)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes) {
			return nullptr;
		}
		std::unordered_map<RE::TESLandTexture*, float> weight;
		auto fallback = RE::TESForm::LookupByID<RE::TESLandTexture>(0x00000C16);  // the default dirt, for quadrants with none
		const float r = (std::max)(a_radius, 64.0f);
		for (float dx = -r; dx <= r; dx += 128.0f) {
			for (float dy = -r; dy <= r; dy += 128.0f) {
				if (dx * dx + dy * dy > r * r + 1.0f) {
					continue;
				}
				const RE::NiPoint3 p{ a_at.x + dx, a_at.y + dy, a_at.z };
				auto cell = tes->GetCell(p);
				auto land = cell ? cell->cellLand : nullptr;
				auto data = land ? land->loadedData : nullptr;
				auto coords = cell && cell->IsExteriorCell() ? cell->GetCoordinates() : nullptr;
				if (!data || !coords) {
					continue;
				}
				const float lx = p.x - coords->cellX * 4096.0f, ly = p.y - coords->cellY * 4096.0f;
				const int qx = lx >= 2048.0f ? 1 : 0, qy = ly >= 2048.0f ? 1 : 0;
				const int quad = qx + 2 * qy;  // 0 south-west, 1 south-east, 2 north-west, 3 north-east
				const int col = std::clamp(static_cast<int>(std::lround((lx - qx * 2048.0f) / 128.0f)), 0, 16);
				const int row = std::clamp(static_cast<int>(std::lround((ly - qy * 2048.0f) / 128.0f)), 0, 16);
				const int v = row * 17 + col;
				// layers lie over the base in order: each shows by its own share, less what the layers above it cover
				float cover = 1.0f;
				for (int layer = 5; layer >= 0; --layer) {
					auto tex = data->quadTextures[quad][layer];
					const float a = std::clamp(static_cast<float>(static_cast<std::uint8_t>(data->percents[quad][v][layer])) / 100.0f, 0.0f, 1.0f);
					if (tex && a > 0.0f) {
						weight[tex] += a * cover;
						cover *= 1.0f - a;
					}
				}
				if (auto base = data->defQuadTextures[quad] ? data->defQuadTextures[quad] : fallback) {
					weight[base] += cover;
				}
			}
		}
		RE::TESLandTexture* best = nullptr;
		float most = 0.0f;
		for (const auto& [tex, w] : weight) {
			if (w > most) {
				most = w;
				best = tex;
			}
		}
		return best ? best : tes->GetLandTexture(a_at);
	}

	// the one ground texture all of a camp's mounds wear: what shows round its campfire
	RE::TESLandTexture* CampLandTexture(RE::TESObjectREFR* a_campfire)
	{
		return a_campfire ? VisibleLandTexture(a_campfire->GetPosition(), 600.0f) : nullptr;
	}

	// paints the foundation with the ground texture that shows around it (snow on snow, dirt on dirt), once its model has
	// loaded; a_radius: how far around to look (0: just the spot it stands on); a_land: paint with this texture instead
	// (all of a camp's mounds take the one texture, so where they overlap there is no seam)
	std::unordered_map<std::string, float> CampSettings();
	float CampSetting(const std::unordered_map<std::string, float>& a_settings, const char* a_key, float a_default);

	void PaintWithGround(RE::ObjectRefHandle a_ref, float a_radius = 0.0f, int a_tries = 0, RE::TESLandTexture* a_land = nullptr)
	{
		auto ref = a_ref.get();
		auto root = ref ? ref->Get3D() : nullptr;
		if (!root) {
			if (a_tries < 20) {
				After(200, [a_ref, a_radius, a_tries, a_land]() { PaintWithGround(a_ref, a_radius, a_tries + 1, a_land); });
			} else if (ref) {
				logger::info("Camp: {:08X} had no model after 4 s, not painted", ref->GetFormID());
			}
			return;
		}
		// test (camp.ini moundpaint=0): the mound keeps its own dirt texture instead of the ground's
		if (CampSetting(CampSettings(), "moundpaint", 1.0f) == 0.0f) {
			return;
		}
		auto tes = RE::TES::GetSingleton();
		auto land = a_land ? a_land : (tes ? VisibleLandTexture(ref->GetPosition(), a_radius) : nullptr);
		auto textures = land ? land->textureSet : nullptr;
		if (!textures) {
			logger::info("Camp: no ground texture found under the foundation; it keeps its dirt");
			return;
		}
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
			auto shader = a_geometry ? skyrim_cast<RE::BSLightingShaderProperty*>(a_geometry->shaderProperty.get()) : nullptr;
			auto material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
			if (material) {
				if (auto copy = static_cast<RE::BSLightingShaderMaterialBase*>(material->Create())) {
					copy->CopyMembers(material);
					copy->ClearTextures();
					copy->OnLoadTextureSet(0, textures);
					// the texture as big in the world whatever the reference's scale (the models are textured from above,
					// one tile per 128 of their own size), so neighbouring mounds of different sizes match
					// (moundtile: how much ground one repeat of the texture covers; the models are textured one repeat per 128 of
					// their own size)
					if (const float k = ref->GetScale() * 128.0f / (std::max)(16.0f, CampSetting(CampSettings(), "moundtile", LandTextureRepeat())); k > 0.01f) {
						// (both entries: the renderer uses one on one frame and the other on the next; setting only the first
						// made the texture jump between two sizes every frame, a very fast flicker)
						for (int b = 0; b < 2; ++b) {
							copy->texCoordScale[b] = { k, k };
							// a mound: turned and shifted to run on from the camp's other mounds
							if (auto it = g_moundTexture.find(ref->GetFormID()); it != g_moundTexture.end()) {
								copy->texCoordScale[b] = { it->second[0] * k, it->second[0] * k };
								copy->texCoordOffset[b] = { it->second[1], it->second[2] };
							}
						}
					}
					// tests (camp.ini): the mound without its shine (moundspecular=0) or without shadows on it (moundshadows=0)
					const auto testSettings = CampSettings();
					if (CampSetting(testSettings, "moundspecular", 1.0f) == 0.0f) {
						copy->specularColorScale = 0.0f;
					}
					if (CampSetting(testSettings, "moundshadows", 1.0f) == 0.0f) {
						shader->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kReceiveShadows, RE::BSShaderProperty::EShaderPropertyFlag::kCastShadows);
					}
					shader->SetMaterial(copy, true);
					shader->SetupGeometry(a_geometry);
					shader->FinishSetupGeometry(a_geometry);
					copy->~BSLightingShaderMaterialBase();
					RE::free(copy);
				}
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		logger::info("Camp: foundation painted with the ground's texture ({})", land->GetFormEditorID());
	}



	// Camping: takes away the pieces of a camp (packing it up, or a new camp replacing it): every piece it placed, by
	// reference, wherever it stands; one whose area isn't loaded (only an older save's) goes when it loads
	// (g_campPendingDelete). a_camp: that camp's number, OLDER_CAMPS (every outdoor camp older than the newest) or
	// ALL_CAMPS (every outdoor camp). A dungeon camp's pieces go only with their own number. Nothing is found by its kind
	// or by where it stands. Camping's own pieces are forgotten (Camping takes them away itself)
	constexpr std::uint32_t ALL_CAMPS = 0xFFFFFFFF;
	void RemoveCampPieces(std::uint32_t a_camp)
	{
		auto takes = [&](std::uint32_t a_of) {
			if (IsDungeonCampNumber(a_of) || IsDungeonCampNumber(a_camp)) {
				return a_of == a_camp;
			}
			if (a_camp == ALL_CAMPS || a_of == 0) {
				return true;  // (0: an older save's piece, placed before camps had numbers)
			}
			return a_camp == OLDER_CAMPS ? a_of < g_campNumber : a_of == a_camp;
		};
		// (each piece is taken through the gate while it is still recorded, and forgotten as it goes: never forgotten
		// first)
		std::vector<RE::TESObjectREFR*> gone;
		std::vector<std::pair<RE::FormID, RE::FormID>> away;  // (number, base): not in memory now (only an older save's)
		std::vector<RE::FormID> stale;                        // its number names something else now, or a dungeon piece gone
		for (const auto& [id, piece] : g_campOwned) {
			if (!takes(piece.camp)) {
				continue;
			}
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
			if (!ref) {
				if (IsDungeonCampNumber(piece.camp)) {
					stale.push_back(id);
				} else {
					away.emplace_back(id, piece.base);
				}
			} else if (SameRef(ref, piece.base)) {
				gone.push_back(ref);
			} else {
				stale.push_back(id);
			}
		}
		std::size_t taken = 0;
		for (auto ref : gone) {
			taken += DeleteCampRef(ref) ? 1 : 0;
		}
		for (const auto& [id, base] : away) {
			g_campOwned.erase(id);
			g_campPendingDelete[id] = { base, GameDays() };
		}
		for (const auto id : stale) {
			g_campOwned.erase(id);
		}
		const std::size_t waiting = away.size();
		std::erase_if(g_campingPieces, [&](const auto& a_p) { return takes(a_p.second.camp); });  // (Camping takes its own away)
		PublishRecords();
		if (g_campPendingDelete.size() > PENDING_DELETE_MAX) {
			logger::warn("Camp: {} pieces of older camps wait for their areas to load", g_campPendingDelete.size());
		}
		// (entries of pieces still waiting for their area are not needed in the other lists: those pieces go when they load)
		for (const auto& [id, baseId] : g_campPendingDelete) {
			std::erase(g_campSpots, id);
			g_moundTexture.erase(id);
			g_moundLand.erase(id);
		}
		// listed spots found deleted are dropped from the list (one not found is only away: its area isn't loaded)
		std::erase_if(g_campSpots, [](RE::FormID a_id) {
			auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
			return ref && (ref->IsDeleted() || ref->IsMarkedForDeletion());
		});
		logger::info("Camp: packed up: {} pieces taken, {} wait for their area to load", taken, waiting);
	}

	// Camping's light for the campfire: placed by Camping at the middle of the flames (the campfire model itself is not
	// centred on its origin)
	constexpr RE::FormID CAMP_LIGHT = 0x000AF8BA;  // Skyrim.esm: LightCampFire01
	RE::TESObjectREFR* CampfireLight(RE::TESObjectREFR* a_campfire)
	{
		auto tes = RE::TES::GetSingleton();
		auto lightBase = RE::TESForm::LookupByID(CAMP_LIGHT);
		RE::TESObjectREFR* found = nullptr;
		if (tes && a_campfire && lightBase) {
			tes->ForEachReferenceInRange(a_campfire, 250.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetBaseObject() == lightBase && a_ref->IsDynamicForm() && !a_ref->IsDisabled()) {
					found = a_ref;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return found;
	}

