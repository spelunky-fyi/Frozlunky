// generated by Fast Light User Interface Designer (fluid) version 1.0302

#include "winheaders.h"

#include "gui.h"

#include "spelunky.h"
#include "patches.h"
#include "seeder.h"
#include "offline.h"
#include "gamedetect.h"
#include "watermark.h"
#include "anticrash.h"
#include "info.h"
#include "derandom.h"
#include "version.h"
#include "oneplayer_only.h"
#include "game_state_detector.h"
#include "updates.h"
#include "daily.h"
#include "remove_daily.h"
#include "custom_hud.h"
#include "patch_group.h"
#include "registry.h"
#include "mods.h"
#include "gui_netplay.h"
#include "syllabic.h"
#include "netplay_connection.h"
#include "frzsave_patch.h"
#include "rc_io.h"

#include "frozboards/session.h"

#include <FL/Fl_Check_Button.H>

#include <boost/assign.hpp>

#include <thread>
#include <memory>
#include <mutex>
#include <sstream>

#define FROZLUNKY_TITLE "Frozlunky " VERSION_STR

#define map_init boost::assign::map_list_of

typedef std::mutex mutex_type;

std::function<void(bool)> toggle_callback;
std::function<bool(const std::string&)> seed_change_callback;
std::thread start_thread;

Fl_Window* window = nullptr;
Fl_Double_Window* info_window = nullptr;
Fl_Check_Button* change_every_level = nullptr;
Fl_Input* input_seed = nullptr;
SeedChangeButton* seed_button = nullptr;
InfoButton* info_button = nullptr;
DailyButton* daily_button = nullptr;

std::shared_ptr<Spelunky> spelunky;
std::shared_ptr<DerandomizePatch> dp;

std::shared_ptr<PatchGroup> patches;
std::shared_ptr<PatchGroup> special_patches;
std::shared_ptr<PatchGroup> all_patches;

std::shared_ptr<Seeder> seeder;
std::shared_ptr<GameChangeDetector> gcd;

std::shared_ptr<DailyInstance> daily;
std::shared_ptr<OnePlayerOnlyPatch> opop;
std::shared_ptr<GameHooks> info_hooks;
std::shared_ptr<CustomHudPatch> chp;

std::shared_ptr<RemoteCallPatch> rcp;

std::mutex daily_mutex;
std::mutex gsd_mutex;
std::shared_ptr<GameStateDetector> gsd;
std::vector<GameStateDetector::bind_id> froz_toggle_binds;

mutex_type state_mutex;
mutex_type gui_info_mutex;

int init_state = 0;
int window_init_state = -10;
bool allow_seed_change = true;
bool daily_available = false;
bool gui_visible = false;

const int states_can_disable_froz[] = {STATE_GAMEOVER_HUD, STATE_MAINMENU, STATE_LOBBY, STATE_TITLE, STATE_INTRO, STATE_CHARSELECT};

//////
// Unsafe
//////

std::shared_ptr<RemoteCallPatch> CurrentRCP() {
	return rcp;
}

/////
//Special Mods
/////

DisplayModsButton* mods_button = nullptr;

void init_special_mods() {
	Mods::Initialize(dp, info_hooks, chp);
	Mods::SetVisibilityChangeCallback([](bool visible) {
		if(!mods_button)
			return;
		
		if(visible || daily) {
			mods_button->deactivate();
		}
		else {
			mods_button->activate();
		}
	});
}


int DisplayModsButton::handle(int evt) {
	if(evt == 2) {
		Mods::ShowModsGUI();
	}

	return Fl_Button::handle(evt);
}


/////////

void undo_patches() {
	if(gsd) {
		int current_state = gsd->current_state();
		auto end = std::end(states_can_disable_froz);

		if(std::find(std::begin(states_can_disable_froz), end, current_state) == end) {
			return;
		}
	}

	//!TODO patches need to cover the mod patches as well
	if(patches) {
		auto group = Mods::ModsGroup();
		if(group) {
			Mods::ModsGroup()->undo();
		}
		if(patches) {
			patches->undo();
		}
		if(chp) {
			chp->undo();
		}
	}
}

bool active_daily() {
	return daily;
}

void hide_gui(Fl_Widget* ignored = nullptr) {
	gui_info_mutex.lock();
	int children = window->children();
	for(int i = 0; i < children; i++) {
		auto child = window->child(i);
		if(child != ignored) {
			child->deactivate();
		}
	}
	gui_visible = false;
	gui_info_mutex.unlock();
	window->redraw();
}

void show_gui(Fl_Widget* ignored = nullptr) {
	gui_info_mutex.lock();
	int children = window->children();
	Fl_Widget* daily = dynamic_cast<Fl_Widget*>(daily_button);
	for(int i = 0; i < children; i++) {
		auto child = window->child(i);
		if(child != ignored && !(!daily_available && child == daily)) {
			child->activate();
		}
	}
	gui_visible = true;
	gui_info_mutex.unlock();
	window->redraw();
}

DWORD __stdcall display_ended_daily_thread_func(void*) {
	MessageBox(NULL, "Today's daily has ended.", "Frozboards", MB_OK);
	return 0;
}

void create_session_and_start() {
	daily_button->deactivate();
	daily_button->label("Loading..");

	Frozboards::Session::CreateSession(info_hooks->steam_id(), [](std::shared_ptr<Frozboards::Session> session, int code) {
		if(session) {
			session->set_invalidate_callback([]() {
				if(daily) {
					daily->force_end();
					show_gui();
					window->label(FROZLUNKY_TITLE);
					daily_button->label("Spd Daily");
					chp->set_text(L"");
				}

				DWORD thread;
				CreateThread(NULL, 0, display_ended_daily_thread_func, NULL, NULL, &thread); 
			});

			daily = std::shared_ptr<DailyInstance>(new DailyInstance(session, spelunky, seeder, dp, chp, patches, Mods::ModsGroup()));
			daily_button->activate();
			daily_button->label("End Daily");
		}
		else {
			daily_button->activate();
			daily_button->label("Spd Daily");
			show_gui();
			window->label(FROZLUNKY_TITLE);

			if(code == SESSION_ERR_FAILED_REQ) {
				MessageBoxA(NULL, "Oops! Frozlunky couldn't connect to Frozboards, try again later.", "Frozboards", MB_OK);
			}
			else if(code == SESSION_ERR_ALREADY_PLAYED) {
				MessageBoxA(NULL, "You have already played today's daily.", "Frozboards", MB_OK);
			}
		}
	});
}

bool attempt_create_daily(std::shared_ptr<Frozboards::Session> session) {
	daily = std::shared_ptr<DailyInstance>(new DailyInstance(session, spelunky, seeder, dp, chp, patches, Mods::ModsGroup()));

	int attempts = 0;
	do {
		daily = std::shared_ptr<DailyInstance>(new DailyInstance(session, spelunky, seeder, dp, chp, patches, Mods::ModsGroup()));
		Sleep(200);
	} while(!(daily->status() == DAILY_WAITING || daily->status() == DAILY_WAITING2) && attempts++ < 10);

	return daily->status() == DAILY_WAITING || daily->status() == DAILY_WAITING2;
}

bool last_cycle_daily = false;
unsigned last_daily_status = DAILY_INVALID;

//returns true if daily in progress
bool daily_gui_update(Fl_Window* window) 
{
	if(active_daily()) 
	{
		if(!last_cycle_daily) {
			hide_gui(daily_button);
		}

		daily->cycle();

		unsigned status = daily->status();
		
		switch(status) {
		case DAILY_INVALID:
			if(last_daily_status == DAILY_WAITING)
				window->label(FROZLUNKY_TITLE);
			daily.reset();
			break;

		case DAILY_WAITING:
		case DAILY_WAITING2:
			if(last_daily_status != DAILY_WAITING) {
				window->label(FROZLUNKY_TITLE " - Start a game");
			}
			break;

		case DAILY_INPROGRESS:
			if(last_daily_status != DAILY_INPROGRESS)
				window->label(FROZLUNKY_TITLE " - Daily in progress");
			break;

		case DAILY_COMPLETED:
			if(last_daily_status != DAILY_COMPLETED) {
				auto session = daily->get_session();

				wchar_t fin[128];
				std::swprintf(fin, sizeof(fin), L"Submitted Score: %.1f (Start a new game for a new attempt)", (float)daily->total_score());
				chp->unlock();
				chp->set_text(fin);

				if(!session->invalidated()) {
					daily.reset();
					if(!attempt_create_daily(session)) {
						daily.reset();
						MessageBox(NULL, "Failed to re-initialize session, the daily has been ended.", "Frozboards", MB_OK);
					}
					else {
						daily->cycle();
					}
				}
				else {
					daily.reset();
				}
			}

			break;
		}
		
		if(daily) {
			daily->cycle();
		}

		last_daily_status = status;
		last_cycle_daily = true;

		return true;
	}
	else 
	{
		//if the daily ends in any fashion we will always end up here once
		if(last_cycle_daily) {
			show_gui();
			window->label(FROZLUNKY_TITLE);
			daily_button->label("Spd Daily");
			chp->set_text(L"");
		}

		last_daily_status = DAILY_INVALID;
		last_cycle_daily = false;
		return false;
	}
}


int DailyButton::handle(int evt) {
	if(evt == 2) {
		//abort daily
		if(daily) {
			daily->force_end();
			if(MessageBox(NULL, "Would you like to view your runs?", "Frozboards", MB_YESNO) == IDYES) {
				ShellExecute(NULL, "open", (std::string(FROZBOARDS_URL "/user.htm?sid=")+info_hooks->steam_id()).c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
		}
		else {
			//if this is accessed then opop is guaranteed to not be null
			if(opop->controller_count() > 1) { 
				MessageBox(NULL, "You currently have multiple players active, please play with only one player.", "Frozboards", MB_OK);
				return Fl_Button::handle(evt);
			}

			this->label("...");
			this->deactivate();

			Mods::HideModsGUI();
			hide_gui(daily_button);

			if(!chp->valid()) {
				MessageBox(NULL, "Custom HUDs are not available (this is not supposed to happen). You will not see your score in-game.", "Frozboards", MB_OK);
			}

			create_session_and_start();
		}
	}

	return Fl_Button::handle(evt);
}

int FrozboardsButton::handle(int evt) {
	if(evt == 2) {
		ShellExecute(NULL, "open", FROZBOARDS_URL, NULL, NULL, SW_SHOWNORMAL);
	}

	
	return Fl_Button::handle(evt);
}


int InfoButton::handle(int evt) {
	if(evt == 2) {
		Fl_Window* window = this->window();

		Fl_Double_Window* info = make_info_window();
		info->callback([](Fl_Widget* wind) {
			if(info_button != nullptr) {
				info_button->activate();
			}

			delete (Fl_Double_Window*)wind;
			info_window = nullptr;
		});
		
		if(info_button != nullptr) {
			info_button->deactivate();
		}

		info->show();
		info_window = info;
	}

	return Fl_Button::handle(evt);
}

void cancel_froz_disable() {
	if(froz_toggle_binds.size() > 0) {
		for(GameStateDetector::bind_id id : froz_toggle_binds) {
			gsd->request_unbind(id);
		}
		froz_toggle_binds.clear();
	}
}

void queue_froz_disable(std::function<void()> disable) {
	cancel_froz_disable();

	//called by gui cycle thread eventually
	auto bind = [disable](int,int,GameStateDetector::bind_id) {
		disable();
		cancel_froz_disable();
		window->redraw();
	};

	for(int allowed_state : states_can_disable_froz) {
		froz_toggle_binds.push_back(gsd->bind(allowed_state, bind, true));
	}
}


int ToggleButton::handle(int evt) 
{
	if(active_daily())
		return Fl_Button::handle(evt);

	if(evt == 2 && spelunky && gsd) 
	{
		Fl_Window* window = this->window();

		//TODO implement all_patches and change these patches checks to all_patches (so we can have special patches too)
		if(patches->is_active()) { //disable frozlunky
			gsd_mutex.lock();
			if(!froz_toggle_binds.empty()) 
			{ //cancel froz disable if queued
				cancel_froz_disable();
				this->label("Disable");
				mods_button->activate();
			}
			else 
			{
				queue_froz_disable([this]() {
					patches->push_state();
					Mods::ModsGroup()->push_state();
					undo_patches();
					if(!patches->is_active()) {
						hide_gui(this);
						this->label("Enable");
					}
					else {
						this->label("(Failed)");
						patches->pop_state();
						Mods::ModsGroup()->pop_state();
					}

					this->window()->redraw();
				});

				this->label("Waiting");

				Mods::HideModsGUI();
				mods_button->deactivate();
			}
			gsd_mutex.unlock();
			window->redraw();
		}
		else { //enable frozlunky
			gsd_mutex.lock();
			if(!froz_toggle_binds.empty()) {
				cancel_froz_disable();
				this->label("Enable");
			}
			else {
				queue_froz_disable([this]() {
					patches->pop_state();
					Mods::ModsGroup()->pop_state();

					seeder->seed(seeder->get_seed());
					show_gui();
					this->label("Disable");
					this->window()->redraw();
				});

				this->label("Waiting");
			}

			window->redraw();
			gsd_mutex.unlock();
		}
	}

	return Fl_Button::handle(evt);
}

int SeedChangeButton::handle(int evt) {
	if(evt == 2 && spelunky && input_seed != nullptr) {
		Fl_Window* window = this->window();
		std::string seed = input_seed->value();
		window->label((std::string(FROZLUNKY_TITLE " - ") + seed).c_str());
		seeder->seed(seed);
		window->redraw();
	}

	return Fl_Button::handle(evt);
}



///////////////////////////////
// NETPLAY
//////////////////////////////

Fl_Button* netplay_button = nullptr;
bool netplay_visible = false;

int NetplayButton::handle(int evt) {
	if(evt == 2) {
		std::cout << "Netplay called!" << " netplay is " << netplay_visible << std::endl; //DEBUG
		if(netplay_visible) {
			NetplayGUI::HideNetplayGUI();
		}
		else {
			NetplayGUI::DisplayNetplayGUI();
		}
		//netplay_visible ? NetplayGUI::HideNetplayGUI() : NetplayGUI::DisplayNetplayGUI();	
	}
	return Fl_Button::handle(evt);
}

void configure_netplay() {
	NetplayGUI::NetplayChangeCallback([](bool val) 
	{
		auto mods = Mods::ModsGroup();

		if(val) {
			mods->push_state();
			mods->undo();
			mods->lock();

			if(NetplayGUI::PlayerID() == 0) {
				hide_gui(input_seed);
				seed_button->activate();
				change_every_level->activate();
				allow_seed_change = true;
			}
			else {
				hide_gui();
				allow_seed_change = false;
			}

			if(!netplay_visible) {
				netplay_button->activate();
			}
		}
		else {
			mods->unlock();
			mods->pop_state();

			allow_seed_change = true;
			show_gui();
		}
	});

	NetplayGUI::VisibilityChangeCallback([](bool visible) {
		netplay_visible = visible;
		if(visible) {
			netplay_button->deactivate();
		}
		else {
			netplay_button->activate();
		}
	});
}


/////////////////////////////



/**
   Creates the window!
*/
Fl_Window* make_window() 
{
  Fl_Window* w;

  { Fl_Window* o = new Fl_Window(256, 120, FROZLUNKY_TITLE);
    w = o;
	window = o;
	o->begin();
    { Fl_Input* o = new Fl_Input(45, 10, 205, 25, "Seed:");
      o->textfont(4);
	  o->deactivate();
	  input_seed = o;
    } // Fl_Input* o
	{
		change_every_level = new Fl_Check_Button(5, 35, 230, 25, "Change seed on game end");
		std::string cel_save = Registry::GetValue("ChangeSeed");
		if(!cel_save.empty()) {
			try {
				change_every_level->value(std::stoi(cel_save));
			}
			catch(std::exception e) {
				change_every_level->value(0);
				Registry::SetValue("ChangeSeed", "0");
			}
		}
		change_every_level->callback([](Fl_Widget* cel_widget) {
			Fl_Check_Button* fcel = dynamic_cast<Fl_Check_Button*>(cel_widget);
			Registry::SetValue("ChangeSeed", std::to_string(fcel->value()));
		});
		change_every_level->deactivate();
	} // Fl_Check_Button* o

	{ (new FrozboardsButton(5, 90, 35, 25, "LBs"))->deactivate();
	} //Fl_Button* o
	{ (mods_button = new DisplayModsButton(45, 90, 70, 25, "Mods"))->deactivate();
	}
	{ (seed_button = new SeedChangeButton(120, 90, 130, 25, "Seed it!"))->deactivate();
    } // Fl_Button* o
	{ (info_button = new InfoButton(5, 60, 35, 25, "?"))->deactivate();
	} // Fl_Button* o
	{ (daily_button = new DailyButton(45, 60, 70, 25, "Loading.."))->deactivate();
	} //Fl_Button* o
	{ (new ToggleButton(185, 60, 65, 25, "Disable"))->deactivate();
	} // Fl_Button* o
	{ (netplay_button = new NetplayButton(120, 60, 60, 25, "Netplay"))->deactivate();
	} // Fl_Button* o
    o->size_range(256, 120, 256, 120);
    o->end();
  } // Fl_Window* o
  return w;
}

//this must be set before showing window
void set_toggle_callback(decltype(toggle_callback) callback) {
	toggle_callback = callback;
}

//this must be set before showing window
void set_seed_change_callback(decltype(seed_change_callback) callback) {
	seed_change_callback = callback;
}

bool is_int(const std::string& str) {
	try {
		std::stoi(str);
		return true;
	}
	catch(std::exception e) {
		return false;
	}
}

void set_guimode_seed(const std::string& seed) {
	if(seeder) {
		seeder->seed(seed);
	}
}


/*
std::string random_str(const char* seed = nullptr) {
	if(seed == nullptr) {
		srand((unsigned int)time(nullptr));
	}
	else {
		srand(std::hash<std::string>()(std::string(seed)));
	}

	std::stringstream str;

	int len = 14;
	while(len > 0) {
		str << (char)((rand()%(0x7A-0x61))+0x61);
		len--;
	}

	return str.str();
}
*/

std::string random_str(const char* seed = nullptr) {
	unsigned iseed;
	if(seed == nullptr) {
		iseed = (unsigned int)time(nullptr);
	}
	else {
		iseed = std::hash<std::string>()(std::string(seed));
	}

	return Syllabic::MakePhoneticString(iseed, 2);
}

DWORD __stdcall loop(void* wind) 
{
	while(true)
	{
		Fl_Window* window = (Fl_Window*)wind;

		state_mutex.lock();

		if(window_init_state != init_state) {
			if(init_state == 0) {
				window->label(FROZLUNKY_TITLE " - Initializing...");
				hide_gui();
				window->redraw();
			}
			else if(init_state == 1) {
				show_gui();
				daily_button->deactivate();
				netplay_button->deactivate();
				window->redraw();
			}
			else {
				MessageBox(NULL, "Failed to initialize Frozlunky, try restarting Spelunky or checking for a Frozlunky update at " UPDATE_DOWNLOAD_URL, "Error", MB_OK);
				if(window) {
					delete window;
				}

				return 0;
			}

			window_init_state = init_state;
		}

		if(init_state == 1 && !daily_gui_update(window) && allow_seed_change) 
		{
			if(change_every_level->value() == 1 && gcd->game_changed()) 
			{
				std::string seed = input_seed->value();
			
				auto rand_mark = seed.find(":");
				if(rand_mark != std::string::npos) {
					seed = seed.substr(0, rand_mark);
				}

				std::string seeder_seed = seeder->get_seed();
				const char* rand_seed = nullptr;
				if(seeder_seed != "") {
					rand_seed = seeder_seed.c_str();
				}
				std::string res = seed + ":" + random_str(rand_seed);
				set_guimode_seed(res);
			}
		
			//this will not get called if a daily is in progress, daily overrides user-level importance
			if(gsd) {
				gsd_mutex.lock();
				gsd->cycle();
				gsd_mutex.unlock();
			}
		}

		//destroy
		if(!spelunky->alive()) {
			delete window;
			exit(0);
			return 0;
		}

		state_mutex.unlock();

		Sleep(32);
	}
	
	return 0;
}

int gui_operate(std::shared_ptr<Spelunky> spelunk, char* icon) 
{
	spelunky = spelunk;

	DBG_EXPR(std::cout << "[GUI] Initializing operations on Spelunky located at base: " << spelunk->base_directory() << std::endl);

	start_thread = std::thread([]() {
		try {
			patches = std::make_shared<PatchGroup>(spelunky);
			
			patches->add("op", std::make_shared<OfflinePatch>(spelunky));
			patches->add("dp", dp = std::make_shared<DerandomizePatch>(spelunky));
			patches->add("wp", std::make_shared<WatermarkPatch>(spelunky));
			patches->add("acp", std::make_shared<AnticrashPatch>(spelunky));
			patches->add("rdp", std::make_shared<RemoveDailyPatch>(spelunky));


#ifdef DEBUG_MODE
#define PATCH_ASSERT(NAME) if(!patches->get(NAME)->valid()) { throw std::exception(#NAME " patch invalid."); }
#else
#define PATCH_ASSERT(NAME) if(!patches->get(NAME)->valid()) { throw std::exception(); }
#endif

			PATCH_ASSERT("dp");
			PATCH_ASSERT("wp");
			PATCH_ASSERT("acp");

#undef PATCH_ASSERT

			opop = std::make_shared<OnePlayerOnlyPatch>(spelunky, dp);

			patches->perform();

			seeder = std::make_shared<Seeder>(dp);
			seeder->on_seed_change([](const std::string& seed) {
				if(!daily && input_seed != nullptr) {
					input_seed->value(seed.c_str());

					window->label((std::string(FROZLUNKY_TITLE " - ") + seed).c_str());
					
					input_seed->redraw();
					window->redraw();
				}
				return true;
			});

			gsd = std::make_shared<GameStateDetector>(dp);
			info_hooks = std::make_shared<GameHooks>(spelunky, dp);
			gcd = std::make_shared<GameChangeDetector>(dp, info_hooks);
			chp = std::make_shared<CustomHudPatch>(dp);
			chp->perform();

			init_special_mods();

			bool allow_netplay = true;
			//change save paths to frzlunky_save.*
			{
				rcp = std::make_shared<RemoteCallPatch>(spelunky);
				rcp->perform();

				std::shared_ptr<FrzSavePatch> fsp = std::make_shared<FrzSavePatch>(spelunky);
				if(!fsp->valid()) {
					DBG_EXPR(std::cout << "[GUI] Warning: Failed to initialize FrzSavePatch" << std::endl);
					allow_netplay = false;
				}
				else {
					fsp->perform();
					
					if(!rcp->enqueue_call(std::make_shared<RCSave>(dp)->make(), []() {
						DBG_EXPR(std::cout << "[GUI] Successfully performed force save." << std::endl);
					}))
					{
						DBG_EXPR(std::cout << "[GUI] Failed to enqueue call to force save." << std::endl);
					}
				}
			}

			state_mutex.lock();
			init_state = 1;
			state_mutex.unlock();

			set_guimode_seed(std::string(":")+random_str()); //init with random seed

			netplay_button->deactivate();
			netplay_button->label("Init..");

			Updates::easy_update_check([=](bool result) {
				if(!result) {
					DailyInstance::Available(spelunky, dp, [](bool available) {
						gui_info_mutex.lock();
						
						daily_available = available;
						if(available && gui_visible) {
							daily_button->label("Spd Daily");
							daily_button->activate();
						}
						else {
							daily_button->label("No Daily");
							daily_button->deactivate();
						}

						gui_info_mutex.unlock();
						window->redraw();
					});


					if(allow_netplay && NetplayConnection::Initialize()) {
						std::cout << "Initialized " << std::endl;

						if(NetplayGUI::Init(spelunky, dp, seeder, info_hooks)) {
							configure_netplay();

							netplay_button->label("Netplay");
							netplay_button->activate();
							window->redraw();
						}
						else {
							netplay_button->label("Failed");
							netplay_button->deactivate();
						}
					}
					else {
						netplay_button->label("Failed");
						netplay_button->deactivate();
					}
				}
				else {
					daily_button->label("Outdated");
					daily_button->deactivate();

					netplay_button->label("Outdated");
					netplay_button->deactivate();
				}
				window->redraw();
			});
		}
		catch(std::exception e) {
			state_mutex.lock();
			init_state = -1;
			state_mutex.unlock();

			std::cout << "Error: " << e.what() << std::endl;
		}
	});

	Fl_Window* gui_window = make_window();
	window = gui_window;

	if(icon != nullptr) {
		window->icon(icon);
	}

	window->show();
	window->callback([](Fl_Widget* wind) {
		undo_patches();
		Mods::HideModsGUI();

		if(info_window != nullptr) {
			delete (Fl_Double_Window*)info_window;
		}

		delete (Fl_Double_Window*)wind;

		seeder.reset();
		patches.reset();
	});

	DWORD thread;
	CreateThread(NULL, 0, loop, window, NULL, &thread);

	Fl::run();
	return 0;
}