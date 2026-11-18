// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Main-thread timers (After / Repeat) and the player update hook that runs them.
	// ---- timers: every delayed or repeating job runs on the game's main thread, from the player's update (one hook,
	// PlayerUpdateHook) instead of a sleeping thread of its own per job (SKSE authors drive
	// periodic work from a main-loop/update hook; a task that re-queues itself can freeze the game). Jobs only advance
	// while the game runs: not in menus or loading screens
	struct TimerJob
	{
		std::chrono::steady_clock::time_point due;
		std::chrono::milliseconds every;  // 0: once
		std::function<bool()> run;        // a repeating job returns false to stop
		bool keep = false;                // a watcher that runs for the whole session (RepeatAlways): kept across save loads
		std::chrono::steady_clock::time_point added = std::chrono::steady_clock::now();  // when it was set (TickTimers)
	};
	std::mutex g_timerLock;
	std::vector<TimerJob> g_timersNew;  // added from any thread, taken into the list on the next tick
	std::vector<TimerJob> g_timers;     // main thread only

	// a_job runs once, a_ms from now
	void After(int a_ms, std::function<void()> a_job)
	{
		std::lock_guard guard(g_timerLock);
		g_timersNew.push_back({ std::chrono::steady_clock::now() + std::chrono::milliseconds(a_ms), std::chrono::milliseconds(0),
			[job = std::move(a_job)]() {
				job();
				return false;
			} });
	}

	// a_job runs on the game's main thread at the next tick of the player's update, from any thread. Everything that
	// reads or changes the camp's state goes through here, never through SKSE's task queue: in normal play SKSE runs
	// its tasks on a worker thread (the per-frame "Post process" job) while the main thread runs the player's update
	// and our timers, so the two touched the same lists at once. Like every timer job it waits while the game is paused
	// or loading, and a save load drops it (a request of the game being left never acts in the next one)
	void OnMain(std::function<void()> a_job)
	{
		After(0, std::move(a_job));
	}

	// a_job runs every a_ms (the first time a_ms from now) for as long as it returns true
	void Repeat(int a_ms, std::function<bool()> a_job)
	{
		std::lock_guard guard(g_timerLock);
		g_timersNew.push_back({ std::chrono::steady_clock::now() + std::chrono::milliseconds(a_ms), std::chrono::milliseconds(a_ms), std::move(a_job) });
	}

	// a watcher for the whole session (started once at kDataLoaded): the only kind of job a save load or new game keeps
	void RepeatAlways(int a_ms, std::function<bool()> a_job)
	{
		std::lock_guard guard(g_timerLock);
		g_timersNew.push_back({ std::chrono::steady_clock::now() + std::chrono::milliseconds(a_ms), std::chrono::milliseconds(a_ms), std::move(a_job), true });
	}

	// a save is loading or a new game starts (kPreLoadGame / kNewGame, on the game's thread): every job of the game being
	// left is dropped, so none of them acts in the next one (a pack-up watcher that lost its tent would hand out Camping
	// Supplies there). Only the session's watchers stay. What a dropped job would have put back is put back by the
	// caller first (CampLoadReset, Setup.inl)
	void DropGameTimers()
	{
		std::lock_guard guard(g_timerLock);
		const auto dropped = std::erase_if(g_timersNew, [](const TimerJob& j) { return !j.keep; }) +
							 std::erase_if(g_timers, [](const TimerJob& j) { return !j.keep; });
		if (dropped > 0) {
			logger::info("Timers: {} job(s) of the previous game dropped", dropped);
		}
	}

	// the world is ready for the jobs: no loading screen up (a save, a door, fast travel, a teleport) and the player's
	// cell attached. A move to another cell is queued and finished over several frames, and the new cell attaches
	// after the loading screen; touching the world before then is what heavy load orders do not forgive (Dungeon Door
	// Warp, src/Warp.cpp, waits for the same before it touches anything after a cell change). The time the world was
	// not ready is not counted (TickTimers)
	bool WorldReady()
	{
		if (auto ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
			return false;
		}
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cell = player ? player->GetParentCell() : nullptr;
		return cell && cell->IsAttached();
	}

	void TickTimers()
	{
		{
			std::lock_guard guard(g_timerLock);
			for (auto& job : g_timersNew) {
				g_timers.push_back(std::move(job));
			}
			g_timersNew.clear();
		}
		if (!WorldReady()) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		// time the game stood still (a menu, the pause menu, a loading screen: the player isn't updated then) doesn't
		// count: every waiting job is moved on by it, so "5 s after waking" is 5 s of running game. Counted on the
		// clock, a job came due during a long menu and ran on its first frame back, before the game's own scripts had
		// caught up (iNeed's rest was read before iNeed had added the night). A job set during the stop
		// is moved on only by the part after it was set: a follower's meal set when the inventory closes, or the
		// listener set at a save load, counts from the game running again (the first ready tick of the session, after
		// the first load's long loading screen, counts too: lastTick starts at the clock's start, so every job set
		// before it is moved on by the wait since it was set)
		static std::chrono::steady_clock::time_point lastTick{};
		if (now - lastTick > std::chrono::milliseconds(500)) {  // (a long hitch counts too; a game under 2 frames a second isn't played)
			for (auto& job : g_timers) {
				const auto stood = now - std::max(lastTick, job.added) - std::chrono::milliseconds(20);  // (less one frame of running game)
				if (stood.count() > 0) {
					job.due += std::chrono::duration_cast<std::chrono::steady_clock::duration>(stood);
				}
				job.added = now;
			}
		}
		lastTick = now;
		// (a job may add others: they wait in g_timersNew for the next tick, so this list is not changed while walked)
		for (auto it = g_timers.begin(); it != g_timers.end();) {
			if (now < it->due) {
				++it;
				continue;
			}
			const bool again = it->run() && it->every.count() > 0;
			if (again) {
				it->due = now + it->every;  // one run per tick at most, however long the game was paused
				++it;
			} else {
				it = g_timers.erase(it);
			}
		}
	}

	struct PlayerUpdateHook
	{
		static void Update(RE::PlayerCharacter* a_this, float a_delta)
		{
			original(a_this, a_delta);
			TickTimers();
		}
		static inline REL::Relocation<decltype(Update)> original;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
			original = vtbl.write_vfunc(0xAD, Update);  // Actor::Update(float)
			logger::info("Timers run from the player's update");
		}
	};

