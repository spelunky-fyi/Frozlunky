#pragma once

#include "patches.h"
#include "derandom.h"
#include "game_hooks.h"
#include <thread>
#include <atomic>

#define LF_BLACK_MARKET (1 << 0)
#define LF_CITY_OF_GOLD (1 << 1)
#define LF_MOTHERSHIP   (1 << 2)
#define LF_WORM			(1 << 3)
#define LF_YETI			(1 << 4)
#define LF_HAUNTED_MANSION (1 << 5)

class LevelForcer {
private:
	std::shared_ptr<Spelunky> spel;
	std::shared_ptr<DerandomizePatch> dp;
	std::shared_ptr<GameHooks> gh;

	std::atomic<int> lvl;
	std::atomic<unsigned> flags;
	
	std::atomic<bool> active;
	std::thread worker_thread;

	bool is_valid;

private:
	void create_worker();

public:
	~LevelForcer();
	LevelForcer(std::shared_ptr<DerandomizePatch> dp, std::shared_ptr<GameHooks> gh);

	void force(int lvl, unsigned flags=0);

	bool valid();

	bool enabled();
	void set_enabled(bool enabled);
};