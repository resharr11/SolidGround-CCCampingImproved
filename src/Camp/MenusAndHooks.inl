// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Change tent, Camping Supplies hook, pack-up prompt, bedroll, activation watcher.
	// Change tent (the shelter's menu): the camp standing now gets the chosen shelter at once, in the same spot (every
	// shelter fills the same spot, so the mound, the navigation cut and the bedroll stay); the choice is kept for the
	// camps to come. In the cold only the large Nordic tent will do
	// a_shelter: the shelter whose menu was used (carried from its activation). Only the standing camp's own recorded
	// shelter is changed: another mod's Camping shelter used for the menu changed ours
	void ChangeCampTent(int a_model, RE::FormID a_shelter)
	{
		a_model = std::clamp(a_model, 0, TentCount() - 1);
		g_tentChoice = a_model;
		auto shelter = a_shelter ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_shelter) : nullptr;
		if (shelter && !IsCampRole(shelter, g_campNumber, PieceRole::kShelter)) {
			logger::info("Camp: Change tent from shelter {:08X}, which is not the standing camp's: only the choice for the next camp kept", a_shelter);
			shelter = nullptr;
		}
		auto tes = RE::TES::GetSingleton();
		if (!shelter || !tes) {
			RE::SendHUDMessage::ShowHUDMessage(TrF("Your next camp will have the {0}.", Tr(TentNameLower(a_model))).c_str());
			return;
		}
		const auto settings = CampSettings();
		RE::NiPoint3 centre;
		float facing = 0.0f;
		TentSlotOf(shelter, centre, facing);
		int model = a_model;
		if (!g_tents[model].winter && TooColdForLightTent(centre)) {
			model = WINTER_TENT;
			RE::SendHUDMessage::ShowHUDMessage(Tr("It's too cold here for a lighter tent."));
		}
		if (model == TentModelNow()) {
			if (model == a_model) {
				RE::SendHUDMessage::ShowHUDMessage(TrF("Your camp already has the {0}.", Tr(TentNameLower(model))).c_str());
			}
			return;
		}
		g_campTent = model;
		SetCampTentModel();
		const auto fit = FitTentToSlot(model, centre, facing, settings);
		g_tentDoorNow = TentDoor(model, settings);
		ScaleCampPiece(shelter, fit.scale);
		MoveRef(shelter, { fit.origin.x, fit.origin.y, shelter->GetPositionZ() }, { 0.0f, 0.0f, fit.rz });
		std::vector<RE::ObjectRefHandle> reload{ shelter->GetHandle() };
		// the bedroll's look goes with the shelter (the lean-to's ground bedroll, or the fur one in a tent), and the rug
		// under it: fur in the large Nordic tent, straw in the others, none under the lean-to. The camp's own bedroll and
		// tent rug by their records: a kit's rug near the bed is not the tent's
		const auto camp = g_campNumber;
		auto bed = CampRef(camp, PieceRole::kBedroll);
		if (bed && bed->IsMarkedForDeletion()) {
			bed = nullptr;
		}
		ForCampPieces(camp, { PieceRole::kTentRug }, false, [](RE::TESObjectREFR* a_rug) { DeleteCampRef(a_rug); });
		if (bed) {
			reload.push_back(bed->GetHandle());
			auto rugBase = g_tents[model].winter ? FurRugBase() : PluginItem(RUG_STRAW);
			if (model != 0 && rugBase && CampSetting(settings, "rug", 1.0f) != 0.0f) {
				if (auto rug = PlaceOwned(bed, rugBase, camp, PieceRole::kTentRug).get()) {
					auto rugAt = bed->GetPosition();
					rugAt.z -= CampSetting(settings, "bedlift", 1.0f) - 0.5f;
					auto rugAngle = bed->GetAngle();
					rugAngle.z += CampSetting(settings, "rugturn", 90.0f) * std::numbers::pi_v<float> / 180.0f;
					MoveRef(rug, rugAt, rugAngle);
				}
			}
		}
		SwapReload(reload);  // (not a pack-up: shown again in 300 ms)
		if (model == a_model) {
			RE::SendHUDMessage::ShowHUDMessage(TrF("You put up the {0}.", Tr(TentNameLower(model))).c_str());
		}
		logger::info("Camp: shelter changed to the {} ({:.2f} size)", TentName(model), fit.scale);
	}

	// the shelters in pages: a message box grows unwieldy past about 8 buttons, so with kit tents it shows 6 at a time
	// and "More shelters" for the next page. In the cold only winter tents are offered (ours is the fur tent; kits can
	// add more): with no other winter tent there is nothing to choose, so no menu, just a short word why
	constexpr int TENTS_PER_PAGE = 6;
	void ShowTentMenu(int a_page, RE::FormID a_shelter);
	std::vector<int> TentChoicesHere(RE::FormID a_shelter)
	{
		bool cold = false;
		auto shelter = a_shelter ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_shelter) : nullptr;
		if (shelter && IsCampRole(shelter, g_campNumber, PieceRole::kShelter)) {
			RE::NiPoint3 centre;
			float facing = 0.0f;
			TentSlotOf(shelter, centre, facing);
			cold = TooColdForLightTent(centre);
		}
		std::vector<int> list;
		for (int i = 0; i < TentCount(); ++i) {
			if (!cold || g_tents[i].winter) {
				list.push_back(i);
			}
		}
		return list;
	}
	class TentMenu : public RE::IMessageBoxCallback
	{
	public:
		TentMenu(std::vector<int> a_list, int a_page, RE::FormID a_shelter) :
			list(std::move(a_list)), page(a_page), shelter(a_shelter) {}
		void Run(std::uint8_t a_button) override
		{
			const int count = static_cast<int>(list.size());
			const int first = page * TENTS_PER_PAGE, shown = (std::min)(TENTS_PER_PAGE, count - first);
			if (a_button < shown) {
				const int model = list[first + a_button];
				OnMain([model, id = shelter]() { ChangeCampTent(model, id); });
			} else if (count > TENTS_PER_PAGE && a_button == shown) {
				const int next = first + TENTS_PER_PAGE < count ? page + 1 : 0;
				OnMain([next, id = shelter]() { ShowTentMenu(next, id); });
			}
		}
		std::vector<int> list;
		int page;
		RE::FormID shelter;
	};

	void ShowTentMenu(int a_page, RE::FormID a_shelter)
	{
		auto list = TentChoicesHere(a_shelter);
		if (list.size() <= 1) {
			RE::SendHUDMessage::ShowHUDMessage(Tr("It's too cold here for a lighter tent."));
			return;
		}
		auto factory = RE::MessageDataFactoryManager::GetSingleton();
		auto strings = RE::InterfaceStrings::GetSingleton();
		auto creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
		auto box = creator ? creator->Create() : nullptr;
		if (!box) {
			return;
		}
		box->bodyText = TrF("Which shelter do you want? Your camp has the {0} now.", Tr(TentNameLower(TentModelNow()))).c_str();
		const int count = static_cast<int>(list.size());
		const int pages = (count + TENTS_PER_PAGE - 1) / TENTS_PER_PAGE;
		a_page = std::clamp(a_page, 0, pages - 1);
		const int first = a_page * TENTS_PER_PAGE;
		for (int i = first; i < (std::min)(first + TENTS_PER_PAGE, count); ++i) {
			box->buttonText.push_back(Tr(TentName(list[i])));
		}
		if (pages > 1) {
			box->buttonText.push_back(Tr("More shelters"));
		}
		box->buttonText.push_back(Tr("Cancel"));
		box->callback = RE::make_smart<TentMenu>(std::move(list), a_page, a_shelter);
		QueueMessageBox(box);
	}

	// Camping's shelter menu gets a Change tent choice (third, before Cancel: Camping's script acts only on Sleep and
	// Pack up, so it does nothing with it). Its answer is caught on the way to Camping's script (ShelterMenuChoice)
	void ShelterMenuChangeTent()
	{
		auto form = CampingForm(0x813);  // ccQDRSSE002_ShelterOptionMsg
		auto message = form ? form->As<RE::BGSMessage>() : nullptr;
		if (!message) {
			return;
		}
		auto it = message->menuButtons.begin();
		if (it == message->menuButtons.end() || ++it == message->menuButtons.end()) {
			return;  // Sleep, then Pack up
		}
		auto button = new RE::BGSMessage::MESSAGEBOX_BUTTON();
		button->text = Tr("Change tent");
		message->menuButtons.insert_after(it, button);
		logger::info("Camp: the shelter menu offers Change tent");
	}

	// the shelter whose menu is opening (set by its activation, read when its box is shown): carried with the box's
	// answer, so Change tent knows which shelter it was
	std::atomic<RE::FormID> g_shelterMenuFor{ 0 };
	// the shelter menu's Sleep was chosen: Camping's script uses that camp's bedroll next, and that one use of that one
	// bedroll sleeps (instead of reopening the menu). Its form ID, from the published records (this runs on the menu's
	// thread); set by the choice itself, not guessed from timing
	std::atomic<RE::FormID> g_shelterSleep{ 0 };

	class ShelterMenuChoice : public RE::IMessageBoxCallback
	{
	public:
		ShelterMenuChoice(RE::BSTSmartPointer<RE::IMessageBoxCallback> a_inner, RE::FormID a_shelter) :
			inner(std::move(a_inner)), shelter(a_shelter) {}
		void Run(std::uint8_t a_button) override
		{
			if (a_button == 0) {
				const auto piece = PublicPieceOf(shelter);
				g_shelterSleep = piece && piece->role == PieceRole::kShelter ? PublicCampRef(piece->camp, PieceRole::kBedroll) : 0;
			} else if (a_button == 2) {
				OnMain([id = shelter]() { ShowTentMenu(0, id); });
			}
			if (inner) {
				inner->Run(a_button);  // Camping's script still gets its answer (it ignores this one)
			}
		}
		RE::BSTSmartPointer<RE::IMessageBoxCallback> inner;
		RE::FormID shelter;
	};

	void SetPitchOnIce(bool a_onIce);
	void RefuseCampingHere();

	// a camp of ours stands where a new one would go: outdoors, Camping's shelter (or one of our mounds) within campgap;
	// indoors, a dungeon camp's bedroll within dgcampgap
	// 1: a camp of ours stands there (its recorded shelter or one of our mounds). 2: another mod's Camping shelter
	// stands there: not a camp of ours, but in the way all the same (it said
	// "There is already a camp here"). 0: neither
	int CampAlreadyHere(RE::Actor* a_player)
	{
		auto tes = RE::TES::GetSingleton();
		if (!a_player || !tes) {
			return 0;
		}
		const auto settings = CampSettings();
		const auto me = a_player->GetPosition();
		const bool indoors = Underground(a_player);
		const float gap = indoors ? CampSetting(settings, "dgcampgap", 500.0f) : CampSetting(settings, "campgap", 1000.0f);
		auto shelterBase = CampingForm(CAMP_SHELTER);
		int found = 0;
		tes->ForEachReferenceInRange(a_player, gap, [&](RE::TESObjectREFR* a_ref) {
			auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || a_ref->GetPosition().GetDistance(me) > gap) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			// (the published records: this runs in the supplies hook, where the records themselves are not read)
			const auto piece = PublicPieceOf(a_ref->GetFormID());
			if (indoors) {
				if (piece && IsDungeonCampNumber(piece->camp) && piece->role == PieceRole::kBedroll) {
					found = 1;
					return RE::BSContainer::ForEachResult::kStop;
				}
			} else if (piece && !IsDungeonCampNumber(piece->camp) && (piece->role == PieceRole::kShelter || piece->role == PieceRole::kMound)) {
				found = 1;
				return RE::BSContainer::ForEachResult::kStop;
			} else if (base == shelterBase) {
				found = 2;  // (keeps looking: one of ours further on says so)
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return found;
	}

	struct SuppliesHook
	{
		static bool DrinkPotion(RE::Actor* a_self, RE::AlchemyItem* a_potion, RE::ExtraDataList* a_extra)
		{
			auto player = Player();
			auto supplies = CampingForm(CAMP_SUPPLIES);
			if (player && a_self == player && supplies && a_potion == supplies) {
				auto entry = InventoryEntry(player, supplies);
				g_suppliesFavorite = entry && entry->IsFavorited();
				// not in the planes of Oblivion or Sovngarde
				if (InOtherPlane(player)) {
					KeepSupplies("This is no place to make camp.");
					return false;
				}
				// not in a fight (Camping says so itself out of doors, but our dungeon camps and checks come first)
				if (player->IsInCombat()) {
					KeepSupplies("You can't make camp while you're in combat.");
					return false;
				}
				// not on top of a camp already standing: the camps would overlap (camp.ini campgap: how far from the
				// standing camp's tent outdoors; indoors, from a dungeon camp's bedroll). Indoors that needs our camp
				// record, which is only read on the main thread: MakeDungeonCamp asks it there (outdoors this reads the
				// camp records here: the supplies hook was seen running on the main thread, in the player's update)
				if (const int here = Underground(player) ? 0 : CampAlreadyHere(player); here == 1) {
					KeepSupplies("There is already a camp here.");
					return false;
				} else if (here == 2) {
					KeepSupplies("Something built stands in the way here. Find a more open spot to set up camp.");
					return false;
				}
				// in a dungeon: our own small camp (Camping makes none indoors), set up on the game's own thread
				if (InDungeon(player)) {
					if (g_dungeonPitching.exchange(true)) {
						return false;  // (one camp is already going up: this use is ignored, the supplies kept)
					}
					// the menu closes now, as outdoors: the camp is made on the next running frame, and an open inventory
					// pauses the game
					if (auto queue = RE::UIMessageQueue::GetSingleton()) {
						for (const auto name : { RE::FavoritesMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME, RE::TweenMenu::MENU_NAME }) {
							queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kHide, nullptr);
						}
					}
					const int before = static_cast<int>(ItemCount(player, supplies));
					OnMain([before]() { MakeDungeonCamp(before); });
					return false;
				}
				// not on ice, a road or in water (what the camp would stand on is looked at every second: WatchCampGround)
				switch (static_cast<BadGround>(g_campGround.load())) {
				case BadGround::kIce:
					KeepSupplies("You can't make camp on the ice.");
					return false;
				case BadGround::kRoad:
					KeepSupplies("You can't make camp on a road.");
					return false;
				case BadGround::kWater:
					KeepSupplies("You can't make camp in the water.");
					return false;
				case BadGround::kNearRoad:
					KeepSupplies("You can't make camp this close to a road.");
					return false;
				case BadGround::kNearWater:
					KeepSupplies("You can't make camp this close to the water.");
					return false;
				case BadGround::kBuilt:
					KeepSupplies("Something built stands in the way here. Find a more open spot to set up camp.");
					return false;
				case BadGround::kCliff:
					KeepSupplies("You can't make camp this close to a cliff edge.");
					return false;
				case BadGround::kFarm:
					KeepSupplies("You can't make camp on a farm's fields or garden plots.");
					return false;
				default:
					break;
				}
				// no camp where trees or big rocks stand in its way: the player looks for a more open spot
				if (CampObstacles(player) > 0) {
					KeepSupplies("Trees or rocks are in the way here. Find a more open spot to set up camp.");
					return false;
				}
				// the shelter for this camp: the player's choice, or the large Nordic tent in the cold. Only now, once
				// every check passed: set earlier, a refused camp changed the standing camp's shelter model; if
				// Camping's own prompt is cancelled, WatchNewCamp puts the standing one's back (not indoors: Camping
				// builds nothing there, and nothing would put the standing camp's back)
				if (!Underground(player)) {
					int none = -2;
					g_campTentBeforePitch.compare_exchange_strong(none, g_campTent.load());
					g_campTent = TentForPlace(g_tentChoice, TooColdForLightTent(player->GetPosition()));
					SetCampTentModel();
				}
				{
					// no camp on ice (RefuseCampOnIce takes it down), so Camping's fire sounds at the end are silenced;
					// the fire's spot: 150 ahead of the player at 60 degrees to the right
					// (checked on the game's own thread: the ground cannot be read from here)
					OnMain([]() {
						auto p = Player();
						if (!p) {
							return;
						}
						const auto me = p->GetPosition();
						const float fh = p->GetAngleZ() + 60.0f * std::numbers::pi_v<float> / 180.0f;
						const bool ice = OnIce(me) || OnIce({ me.x + 150.0f * std::sin(fh), me.y + 150.0f * std::cos(fh), me.z });
						SetPitchOnIce(ice);
						if (ice) {
							SilenceSoundFor(CampingForm(IGNITE_SOUND), 20000);
							SilenceSoundFor(RE::TESForm::LookupByID(FIRE_LOOP_SOUND), 20000);
							logger::info("Camp: pitching on ice, the fire's sounds silenced");
						}
					});
				}
				// a camp's fire burns 1 Firewood when you light it: every camp goes up with its fire laid cold (Camping's
				// lighting sounds silenced), lit when you want it (ColdCampfire), so a night's sleep wastes no wood
				{
					g_pitchLit = false;
					if (!g_pitchLit) {
						SilenceSoundFor(CampingForm(IGNITE_SOUND), 20000);
						SilenceSoundFor(RE::TESForm::LookupByID(FIRE_LOOP_SOUND), 20000);
					}
				}
				// the old camp is packed up (free: its fire's wood was paid when lit). The shelter is looked up and the
				// watcher started on the main thread, where all the camp's state lives (this hook may run on another)
				OnMain([]() {
					if (auto shelter = CurrentShelter()) {
						WatchCamp(shelter->GetHandle(), true);
					}
				});
				// the menu the supplies were used from closes, as with Camping on its own: left open, the Favorites menu
				// kept the game paused, and Camping's "Build a campsite here?" and its fade with it
				if (auto queue = RE::UIMessageQueue::GetSingleton()) {
					for (const auto name : { RE::FavoritesMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME, RE::TweenMenu::MENU_NAME }) {
						queue->AddMessage(name, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					}
				}
			}
			return original(a_self, a_potion, a_extra);
		}
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
			original = vtbl.write_vfunc(0x10F, DrinkPotion);
		}
		static inline REL::Relocation<decltype(DrinkPotion)> original;
	};

	// Camping's shelter menu (Sleep, Pack up, Cancel) and its "pack up?" prompt (Yes, No). Packing up needs 1 Firewood:
	// the Pack up choice always shows and says so; without wood the prompt only offers No (a condition on its Yes,
	// added when the game loads; hidden buttons keep their numbers, so Camping's script reads the answers right)
	RE::BGSMessage::MESSAGEBOX_BUTTON* MessageButton(RE::FormID a_message, int a_index)
	{
		auto form = CampingForm(a_message);
		auto message = form ? form->As<RE::BGSMessage>() : nullptr;
		if (!message) {
			return nullptr;
		}
		int index = 0;
		for (auto button : message->menuButtons) {
			if (index++ == a_index) {
				return button;
			}
		}
		return nullptr;
	}

	RE::TESConditionItem* g_packUpWood = nullptr;  // the Firewood condition on the "pack up?" prompt's Yes
	std::atomic<bool> g_packUpFree{ false };        // the camp being packed up has no fire (pitched on ice)

	// no camp on ice: the "build a campsite here?" prompt's Yes hides itself while you stand on ice (a condition that
	// always holds, made impossible then), and the prompt says why; No keeps your Camping Supplies
	RE::TESConditionItem* g_buildOnIce = nullptr;
	std::atomic<bool> g_pitchOnIce{ false };

	void NoBuildingOnIce()
	{
		auto yes = MessageButton(0x818, 0);  // ccQDRSSE002_BuildCampPrompt: Yes
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		if (!yes || !firewood) {
			return;
		}
		auto item = new RE::TESConditionItem();
		item->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetItemCount;
		item->data.functionData.params[0] = firewood;
		item->data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo;
		item->data.comparisonValue.f = 0.0f;  // always: any amount of Firewood, none included
		item->data.object = RE::CONDITIONITEMOBJECT::kSelf;
		item->next = yes->conditions.head;
		yes->conditions.head = item;
		g_buildOnIce = item;
	}

	// Camping refuses a camp by its own checks before anything goes up (it says you cannot camp here and keeps your
	// supplies): one of them is that the worldspace is on its list of places to camp. On ice the worldspace is taken
	// off that list for a moment when the supplies are used

	std::vector<std::pair<RE::FormID, RE::FormID>> g_campingWorldsOff;  // (worldspace, list) taken off for a moment

	// the worldspaces taken off Camping's list go back on it (5 s after, or at once when a save loads)
	void RestoreCampingWorlds()
	{
		for (const auto& [worldID, listID] : g_campingWorldsOff) {
			auto l = RE::TESForm::LookupByID<RE::BGSListForm>(listID);
			auto w = RE::TESForm::LookupByID(worldID);
			if (l && w && !l->HasForm(w)) {
				l->forms.push_back(w);
			}
		}
		g_campingWorldsOff.clear();
	}

	void RefuseCampingHere()
	{
		auto form = CampingForm(0x811);  // ccQDRSSE002_CampingAllowedWorldspaces
		auto list = form ? form->As<RE::BGSListForm>() : nullptr;
		auto player = Player();
		auto world = player ? player->GetWorldspace() : nullptr;
		if (!list || !world) {
			return;
		}
		auto& forms = list->forms;
		for (std::uint32_t i = 0; i < forms.size(); ++i) {
			if (forms[i] == world) {
				forms.erase(forms.begin() + i);
				logger::info("Camp: on ice: Camping's own check refuses the camp");
				g_campingWorldsOff.emplace_back(world->GetFormID(), list->GetFormID());
				After(5000, []() { RestoreCampingWorlds(); });
				return;
			}
		}
	}

	void SetPitchOnIce(bool a_onIce)
	{
		g_pitchOnIce = a_onIce;
		if (g_buildOnIce) {
			g_buildOnIce->data.comparisonValue.f = a_onIce ? 1.0e9f : 0.0f;
		}
	}

	void PackUpNeedsFirewood()
	{
		auto yes = MessageButton(0x82B, 0);  // ccQDRSSE002_BreakCampPrompt: Yes
		auto firewood = RE::TESForm::LookupByID(FIREWOOD);
		if (!yes || !firewood) {
			return;
		}
		auto item = new RE::TESConditionItem();
		item->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetItemCount;
		item->data.functionData.params[0] = firewood;
		item->data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo;
		item->data.comparisonValue.f = 1.0f;
		item->data.object = RE::CONDITIONITEMOBJECT::kSelf;
		item->next = yes->conditions.head;
		yes->conditions.head = item;
		g_packUpWood = item;
		logger::info("Camp: packing up needs 1 Firewood");
	}

	// the shelter menu's Pack up choice tells you what it costs, just before the menu opens
	void LabelPackUp(bool a_haveWood, bool a_needsWood = true)
	{
		// a camp without a fire packs up for nothing: its Yes needs at least 0 Firewood then
		if (g_packUpWood) {
			g_packUpWood->data.comparisonValue.f = a_needsWood ? 1.0f : 0.0f;
		}
		g_packUpFree = !a_needsWood;
		static std::string original;
		auto packUp = MessageButton(0x813, 1);  // ccQDRSSE002_ShelterOptionMsg: Pack up
		if (!packUp) {
			return;
		}
		if (original.empty()) {
			original = packUp->text.c_str();
		}
		const auto label = !a_needsWood ? original : TrF(a_haveWood ? "{0} (uses 1 Firewood)" : "{0} (needs 1 Firewood)", original);
		packUp->text = label.c_str();
	}

	// Without wood the "pack up?" prompt would still ask "are you sure?" with only No to pick, so before the message
	// box shows we swap its text: it says you need Firewood and its one button reads OK (still Camping's No)
	std::int64_t NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// the player's Firewood and whether they are indoors, copied on the game's thread: the message box hook runs on the
	// UI thread, where the inventory and the cell can change under it
	std::atomic<std::int32_t> g_firewoodNow{ 0 };
	std::atomic<bool> g_playerIndoorsNow{ false };

	struct PackUpPromptHook
	{
		static RE::UI_MESSAGE_RESULTS ProcessMessage(RE::MessageBoxMenu* a_self, RE::UIMessage& a_message)
		{
			// a new box can reuse the last one's memory, so a box is told apart by its address and its text together
			static RE::MessageBoxData* lastSeen = nullptr;
			static std::string lastText;
			auto player = Player();
			auto firewood = RE::TESForm::LookupByID(FIREWOOD);
			auto form = CampingForm(0x82B);  // ccQDRSSE002_BreakCampPrompt
			auto prompt = form ? form->As<RE::BGSMessage>() : nullptr;
			RE::BSSpinLockGuard locker(RE::MessageBoxMenu::GetQueueLock());
			auto& queue = RE::MessageBoxMenu::GetQueue();
			auto data = queue.empty() ? nullptr : queue.front();
			const std::string nowText = data && data->bodyText.c_str() ? data->bodyText.c_str() : "";
			if (!data) {
				lastSeen = nullptr;  // the box closed
			}
			if (data && (data != lastSeen || nowText != lastText) && player && firewood && prompt) {
				lastSeen = data;
				lastText = nowText;
				RE::BSString body;
				prompt->GetDescription(body, prompt);
				const std::string_view question = body.c_str() ? body.c_str() : "";
				const std::string_view shown = data->bodyText.c_str() ? data->bodyText.c_str() : "";
				// the "pack up?" prompt, known by its own text
				const bool isPrompt = !question.empty() && question == shown;
				logger::info("Camp: message box ({} buttons, populated {}, pack-up prompt {}, firewood {}): {}", data->buttonText.size(),
					a_self->isPopulated, isPrompt, g_firewoodNow.load(), shown.substr(0, 60));
				// the "build a campsite here?" prompt on ice: its Yes is hidden (NoBuildingOnIce); it says why, and No reads OK
				if (auto buildForm = CampingForm(0x818); g_pitchOnIce && buildForm && !data->buttonText.empty()) {
					if (auto build = buildForm->As<RE::BGSMessage>()) {
						RE::BSString buildBody;
						build->GetDescription(buildBody, build);
						if (buildBody.c_str() && shown == buildBody.c_str()) {
							data->bodyText = Tr("You can't make camp on the ice.");
							data->buttonText[data->buttonText.size() - 1] = Tr("OK");
							lastText = data->bodyText.c_str();
						}
					}
				}
				// the shelter menu: its answer passes by us first (Change tent)
				if (auto shelterForm = CampingForm(0x813)) {
					static RE::IMessageBoxCallback* wrapped = nullptr;
					if (auto shelterMsg = shelterForm->As<RE::BGSMessage>(); shelterMsg && data->callback && data->callback.get() != wrapped) {
						RE::BSString shelterBody;
						shelterMsg->GetDescription(shelterBody, shelterMsg);
						if (shelterBody.c_str() && shown == shelterBody.c_str()) {
							if (g_playerIndoorsNow) {
								// indoors (a camp in a dungeon: a bedroll, no tent) there is no tent to change: the button
								// is taken out of this box (Cancel moves up into its place; Camping's script only acts on
								// Sleep and Pack up) and the answer is not caught
								for (auto it = data->buttonText.begin(); it != data->buttonText.end(); ++it) {
									if (it->c_str() && std::string_view(it->c_str()) == Tr("Change tent")) {
										data->buttonText.erase(it);
										break;
									}
								}
							} else {
								auto wrap = RE::make_smart<ShelterMenuChoice>(data->callback, g_shelterMenuFor.load());
								wrapped = wrap.get();
								data->callback = wrap;
							}
						}
					}
				}
				if (isPrompt && !g_packUpFree && g_firewoodNow < 1 && !data->buttonText.empty()) {
					// the box still lists Yes and No here; the game hides Yes when it shows the box (its Firewood condition),
					// so only the text changes and the No button reads OK
					data->bodyText = Tr("You need 1 Firewood to pack up your camp.");
					data->buttonText[data->buttonText.size() - 1] = Tr("OK");
					lastText = data->bodyText.c_str();
				}
			}
			return original(a_self, a_message);
		}
		static void Install()
		{
			RepeatAlways(250, []() {
				auto player = Player();
				auto firewood = RE::TESForm::LookupByID(FIREWOOD);
				g_firewoodNow = player && firewood ? ItemCount(player, firewood) : 0;
				auto cell = player ? player->GetParentCell() : nullptr;
				g_playerIndoorsNow = cell && cell->IsInteriorCell();
				return true;
			});
			REL::Relocation<std::uintptr_t> vtbl{ RE::MessageBoxMenu::VTABLE[0] };
			original = vtbl.write_vfunc(0x4, ProcessMessage);
		}
		static inline REL::Relocation<decltype(ProcessMessage)> original;
	};

	// Camping's bedroll in the tent opens the shelter's menu, like the tent itself (and the lean-to's bed did). Only
	// the player's own use does that; the shelter menu's Sleep also uses the bedroll, and that one sleeps
	// (g_shelterSleep). Whether a bedroll is a camp's is read from the published records, here on the hook's thread: a
	// bedroll no camp recorded (any vanilla fur bedroll, another mod's) goes on as usual at once, never stopped and used
	// again (every fur bedroll was activated twice)
	// a camp bedroll whose shelter is gone or hidden (Change tent's 0.3 s swap, another mod or the console took it): used
	// again once as a plain bedroll, so a broken camp still gives a working bed
	std::atomic<RE::FormID> g_bedPassThrough{ 0 };

	struct BedrollHook
	{
		static bool Activate(RE::TESFurniture* a_self, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			auto player = Player();
			// a dungeon camp's bedroll: its menu (Sleep, Pack up, Cancel) when you use it; Sleep then lies down in it
			if (player && a_activator == player && a_target && a_self && a_self->GetFormID() == DUNGEON_BEDROLL) {
				if (g_dungeonBedSleep.exchange(false)) {
					return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);
				}
				const auto piece = PublicPieceOf(a_target->GetFormID());
				if (!piece || !IsDungeonCampNumber(piece->camp) || piece->role != PieceRole::kBedroll) {
					return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);  // no camp's bedroll: as usual
				}
				OnMain([handle = a_target->GetHandle()]() {
					if (auto b = handle.get(); b && IsDungeonCampBed(b.get())) {
						ShowDungeonBedMenu(b.get());
					}
				});
				return false;
			}
			if (player && a_activator == player && a_target && a_self == CampingForm(0x819)) {
				if (RE::FormID sleepBed = a_target->GetFormID(); g_shelterSleep.compare_exchange_strong(sleepBed, 0)) {
					return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);  // the shelter menu's Sleep
				}
				if (RE::FormID plainBed = a_target->GetFormID(); g_bedPassThrough.compare_exchange_strong(plainBed, 0)) {
					return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);  // its camp has no shelter now
				}
				const auto piece = PublicPieceOf(a_target->GetFormID());
				if (!piece || piece->role != PieceRole::kBedroll) {
					return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);  // no camp's bedroll: as usual
				}
				OnMain([camp = piece->camp, handle = a_target->GetHandle()]() {
					auto p = Player();
					auto shelter = CampRef(camp, PieceRole::kShelter);
					if (p && shelter && !shelter->IsDisabled() && !shelter->IsMarkedForDeletion()) {
						shelter->ActivateRef(p, 0, nullptr, 1, false);  // its own camp's shelter menu
					} else if (auto bed = handle.get(); p && bed && !bed->IsMarkedForDeletion() && IsCampPiece(bed.get()) &&
														 CampOf(bed.get()) == camp && RoleOf(bed.get()) == PieceRole::kBedroll) {
						// (asked again here, when it runs: the camp may have been packed up since the hook queued this)
						const auto id = bed->GetFormID();
						g_bedPassThrough = id;
						bed->ActivateRef(p, 0, nullptr, 1, false);
						// (not left armed when the game did not pass it on: a later use would skip the shelter menu)
						After(1000, [id]() {
							auto unused = id;
							g_bedPassThrough.compare_exchange_strong(unused, 0);
						});
					}
				});
				return false;
			}
			return original(a_self, a_target, a_activator, a_arg3, a_object, a_count);
		}
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::TESFurniture::VTABLE[0] };
			original = vtbl.write_vfunc(0x37, Activate);
		}
		static inline REL::Relocation<decltype(Activate)> original;
	};

	class CampWatcher : public RE::BSTEventSink<RE::TESActivateEvent>
	{
	public:
		static CampWatcher* GetSingleton()
		{
			static CampWatcher singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
		{
			// every door, chair and lever anyone uses comes through here, while the game holds its event lock: only the
			// player's own are looked at
			auto player = Player();
			if (!a_event || !player || a_event->actionRef.get() != player || !a_event->objectActivated) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto shelterBase = CampingForm(CAMP_SHELTER);
			// furniture (a forge among them): the cookware recipe's Smithing perk flag is brought up to date before its menu opens
			if (auto base = a_event->objectActivated->GetBaseObject(); base && base->Is(RE::FormType::Furniture)) {
				UpdateSmithingPerkFlag();
			}
			// a camp's pot over a cold fire (blocked, so using it does nothing): says why (only a stand-in our records
			// name, read on the main thread: another mod's copy of it says nothing)
			if (IsCampBase(a_event->objectActivated->GetBaseObject(), POT_COLD)) {
				OnMain([handle = a_event->objectActivated->GetHandle()]() {
					if (auto c = handle.get(); c && IsCampOwned(c.get())) {
						RE::SendHUDMessage::ShowHUDMessage(Tr("The fire isn't lit. Feed it 1 Firewood to cook."));
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
			// a camp's cold fire (one a camp recorded): asks to feed it 1 Firewood
			// (the record is read on the main thread, where camp state lives)
			if (IsCampBase(a_event->objectActivated->GetBaseObject(), COLD_CAMPFIRE)) {
				const auto handle = a_event->objectActivated->GetHandle();
				OnMain([handle]() {
					if (auto c = handle.get(); c && IsCampOwned(c.get())) {
						AskToLightFire(c.get());
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event && shelterBase && player && a_event->actionRef.get() == player && a_event->objectActivated &&
				a_event->objectActivated->GetBaseObject() == shelterBase) {
				// the shelter's menu: its Sleep choice uses the bedroll, which then sleeps instead of reopening the menu.
				// Packing up needs 1 Firewood: the choice says so, and without wood its prompt offers only No (PackUpNeedsFirewood)
				auto firewood = RE::TESForm::LookupByID(FIREWOOD);
				const bool fire = CampHasFire(a_event->objectActivated.get());
				LabelPackUp(firewood && ItemCount(player, firewood) > 0, false);  // packing up is free (the wood is paid when the fire is lit)
				if (!fire) {
					SilenceSoundFor(RE::TESForm::LookupByID(FIRE_OUT_SOUND), 60000);  // no fire to put out
				}
				g_shelterMenuFor = a_event->objectActivated->GetFormID();
				g_shelterSleep = 0;  // (a Sleep chosen in an earlier menu that never reached its bedroll ends here)
				OnMain([handle = a_event->objectActivated->GetHandle()]() { WatchCamp(handle); });  // (main thread: see above)
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

