#include "tile_editing.h"
#include "tile_util.h"
#include "tile_default.h"
#include "tile_editor_widget.h"
#include "level_forcer.h"
#include "gui.h"
#include "syllabic.h"

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>

#include <mutex>
#include <map>
#include <unordered_map>
#include <fstream>
#include <functional>
#include <thread>

#include <boost/assign.hpp> 
#include <pugixml.hpp>


#define WINDOW_BASE_TITLE "Tile Editor"

#define BUTTON_CLASS(NAME, LABEL) \
class NAME : public Fl_Button { \
public: \
	NAME(int x, int y, int w, int h) : Fl_Button(x,y,w,h,LABEL) {} \
	virtual int handle(int evt) override; \
}

//OPT add simulation widget to the overview page, should simulate a chosen area's chunks
static Fl_Window* window = nullptr;
static std::shared_ptr<StaticChunkPatch> tp;	
static std::shared_ptr<LevelForcer> level_forcer;
static std::shared_ptr<Seeder> seeder;
static std::shared_ptr<DerandomizePatch> dp;
static std::shared_ptr<GameHooks> gh;
static std::thread worker_thread;
static std::function<void(bool)> display_cb;
static bool initialized = false;

struct NF_CheckButton : public Fl_Check_Button {
public:
	NF_CheckButton(int x, int y, int w, int h, const char* L) : 
		Fl_Check_Button(x,y,w,h,L)
	{}

	virtual int handle(int evt) {
		if(evt == FL_FOCUS)
			return 0;
		else
			return Fl_Check_Button::handle(evt);
	}
};

typedef struct area_grouping {
	std::vector<std::string> areas;

	area_grouping(std::string a, std::string b="", std::string c="", std::string d="", std::string e="", std::string f="") {
		areas.push_back(a);
		if(!b.empty())
			areas.push_back(b);
		if(!c.empty())
			areas.push_back(c);
		if(!d.empty())
			areas.push_back(d);
		if(!e.empty())
			areas.push_back(e);
		if(!f.empty())
			areas.push_back(f);
	}

	area_grouping() {}
} area_grouping;

typedef std::vector<std::pair<std::string, std::string>> area_map;
typedef std::vector<std::pair<std::string, area_grouping>> grouping_map;

static area_map area_lookup = boost::assign::map_list_of
			("Default (Read-Only)", "%")
			("Tut-1", "Tutorial-1")
			("Tut-2", "Tutorial-2")
			("Tut-3", "Tutorial-3")
			("1-1", "Mines-1")
			("1-2", "Mines-2")
			("1-3", "Mines-3")
			("1-4", "Mines-4")
			("2-1", "Jungle-5")
			("2-2", "Jungle-6")
			("2-3", "Jungle-7")
			("2-4", "Jungle-8")
			("3-1", "IceCaves-9")
			("3-2", "IceCaves-10")
			("3-3", "IceCaves-11")
			("3-4", "IceCaves-12")
			("4-1", "Temple-13")
			("4-2", "Temple-14")
			("4-3", "Temple-15")
			("Olmec (4-4)", "TempleOlmec")
			("5-1", "Hell-17")
			("5-2", "Hell-18")
			("5-3", "Hell-19")
			("Worm", "Worm")
			("Black Market", "JungleBlackMarket")
			("Haunted Castle", "JungleHauntedCastle");
			//("Yeti Level", "IceCavesYeti")
			//("The Mothership", "IceCavesSpaceship");

static grouping_map grouping = boost::assign::map_list_of
	("Default (Read-Only)", area_grouping("Default (Read-Only)"))
	("Tutorial", area_grouping("Tut-1", "Tut-2", "Tut-3"))
	("Mines", area_grouping("1-1", "1-2", "1-3", "1-4"))
	("Jungle", area_grouping("2-1", "2-2", "2-3", "2-4"))
	("Ice Caves", area_grouping("3-1", "3-2", "3-3", "3-4"))
	("Temple", area_grouping("4-1", "4-2", "4-3"))
	("Olmec (4-4)", area_grouping("Olmec (4-4)"))
	("Hell", area_grouping("5-1", "5-2", "5-3"))
	("Black Market", area_grouping("Black Market"))
	("Haunted Castle", area_grouping("Haunted Castle"))
	("Worm", area_grouping("Worm"));
	//("Yeti Level", area_grouping("Yeti Level"))
	//("The Mothership", area_grouping("The Mothership"));

static void area_order(std::vector<std::string>& areas) {
	for(auto&& p : grouping) {
		for(const std::string& s : p.second.areas) {
			areas.push_back(s);
		}
	}
}

static std::map<std::string, std::function<void()>> level_force_oper = boost::assign::map_list_of
	("1-1",			std::function<void()>([](){level_forcer->force(1);}))
	("1-2",			std::function<void()>([](){level_forcer->force(2);}))
	("1-3",		    std::function<void()>([](){level_forcer->force(3);}))
	("1-4",		    std::function<void()>([](){level_forcer->force(4);}))
	("2-1",			std::function<void()>([](){level_forcer->force(5);}))
	("2-2",			std::function<void()>([](){level_forcer->force(6);}))
	("2-3",			std::function<void()>([](){level_forcer->force(7);}))
	("2-4",			std::function<void()>([](){level_forcer->force(8);}))
	("3-1",			std::function<void()>([](){level_forcer->force(9);}))
	("3-2",			std::function<void()>([](){level_forcer->force(10);}))
	("3-3",			std::function<void()>([](){level_forcer->force(11);}))
	("3-4",			std::function<void()>([](){level_forcer->force(12);}))
	("4-1",			std::function<void()>([](){level_forcer->force(13);}))
	("4-2",			std::function<void()>([](){level_forcer->force(14);}))
	("4-3",			std::function<void()>([](){level_forcer->force(15);}))
	("Olmec (4-4)", std::function<void()>([](){level_forcer->force(16);}))
	("5-1",			std::function<void()>([](){level_forcer->force(17);}))
	("5-2",			std::function<void()>([](){level_forcer->force(18);}))
	("5-3",			std::function<void()>([](){level_forcer->force(19);}))
	("Black Market", std::function<void()>([](){level_forcer->force(5, LF_BLACK_MARKET);}))
	("Haunted Castle", std::function<void()>([](){level_forcer->force(5, LF_HAUNTED_MANSION);}))
	("Worm",		std::function<void()>([](){level_forcer->force(5, LF_WORM);}));
	//("Yeti Level",  std::function<void()>([](){level_forcer->force(9, LF_YETI);}))
	//("The Mothership", std::function<void()>([](){level_forcer->force(12, LF_MOTHERSHIP);}));

static std::string current_game_level() {
	Address game;
	dp->spel->read_mem(dp->game_ptr(), &game, sizeof(Address));

	bool black_market;
	dp->spel->read_mem(game + gh->blackmkt_offset(), &black_market, 1);
	if(black_market)
		return "Black Market";

	bool worm;
	dp->spel->read_mem(game + gh->worm_offset(), &worm, 1);
	if(worm)
		return "Worm";
	
	bool haunted_mansion;
	dp->spel->read_mem(game + gh->haunted_mansion_offset(), &haunted_mansion, 1);
	if(haunted_mansion)
		return "Haunted Castle";

	bool mothership;
	dp->spel->read_mem(game + gh->mothership_offset(), &mothership, 1);
	if(mothership)
		return "The Mothership";

	int level = dp->current_level();
	switch(level) {
	case 16:
		return "Olmec (4-4)";
	default:
		return std::to_string(1 + (level-1) / 4) + "-" + std::to_string(((level - 1) % 4) + 1);
	}
}

static std::string area_group(const std::string& area) {
	for(auto&& p : grouping) {
		if(std::find(p.second.areas.begin(), p.second.areas.end(), area) != p.second.areas.end())
			return p.first;
	}
	return "";
}

template <typename KeyType, typename ValueType>
static const std::pair<KeyType, ValueType>& mget(const std::vector<std::pair<KeyType, ValueType>>& m, const KeyType& key) {
	for(const std::pair<KeyType, ValueType>& p : m) {
		if(p.first == key)
			return p;
	}
	
	throw std::runtime_error("Key not found.");
}

struct AreaControls {
	std::string group;
	std::map<std::string, Fl_Button*> btns;
	
	AreaControls(const std::string& group, const std::map<std::string, Fl_Button*>& btns, Fl_Box* data) : 
		group(group),
		btns(btns)
	{}

	//updates widgets with correct data for this area
	void update() {
		for(auto&& btn : btns) {
			Fl_Button* edit_btn = btn.second;

			const std::string& rarea = mget(area_lookup, btn.first).second;
			
			//% -> default read-only
			if(rarea != "%") {
				std::vector<Chunk*> chunks = tp->query_chunks(rarea);
		
				if(!tp->valid() || chunks.empty()) {
					edit_btn->deactivate();
					continue;
				}
			}

			edit_btn->activate();
		}
	}
};

struct SeedInput : public Fl_Input {
private:
	std::function<void(std::string)> seed_update;

public:
	SeedInput(int x, int y, int w, int h, decltype(seed_update) update) : Fl_Input(x,y,w,h, "Level Seed: "), seed_update(update) {
		this->textfont(4);
	}

	void update() {
		seed_update(std::string(this->value()));
	}

	virtual int handle(int evt) override {
		if(evt == 0xC || evt == FL_KEYBOARD) {
			update();
		}
		else if(evt == FL_FOCUS) {
			return 0;
		}

		return Fl_Input::handle(evt);
	}
};

struct SeedRandomize : public Fl_Button {
	SeedInput* input;
	
	SeedRandomize(SeedInput* input, int x, int y, int w, int h) : Fl_Button(x, y, w, h, "Randomize"), input(input) {}

	virtual int handle(int evt) override {
		if(evt == 2) {
			std::string current = input->value();
			std::string prefix = "";
			auto prefp = current.find(":");
			if(prefp != std::string::npos) {
				prefix = current.substr(0, prefp);
			}
			else {
				prefix = current;
			}

			input->value((prefix + ":" + Syllabic::MakePhoneticString(2)).c_str());
			input->update();
		}
		else if(evt == FL_FOCUS) {
			return 0;
		}

		return Fl_Button::handle(evt);
	}
};

static std::map<std::string, AreaControls*> controls;

namespace TileEditing {
	static std::string prior_nondefault_editor;
	static std::string current_area_editor;
	static std::map<std::string, EditorWidget*> editors;
	static Fl_Check_Button* flcb_force;
	static std::mutex mut_level_seeds;
	static std::map<std::string, std::string> level_seeds;

	static SeedInput* input_seed;
	static SeedRandomize* btn_randomize;
	static Fl_Button* open_file;

	static void SetCurrentEditor(const std::string& area);

	namespace IO 
	{
		static std::string current_file;	
		static bool unsaved_changes = false;
		
		static void SetActiveFile(const std::string& file) {
			current_file = file;

			if(file == "")
				window->label(WINDOW_BASE_TITLE);
			else
				window->copy_label((std::string(WINDOW_BASE_TITLE " - ") + TileUtil::GetBaseFilename(current_file)).c_str());
			
			tp->apply_chunks(); //apply chunks upon setting active file to guarantee newly active file is not unapplied
		}

		static void InitializeEmptySeeds() {
			srand((unsigned int)time(0));
			mut_level_seeds.lock();
			for(auto&& seed : level_seeds) {
				if(seed.second.empty()) {
					seed.second = std::string(":") + Syllabic::MakePhoneticString(2);
				}
			}
			mut_level_seeds.unlock();
		}

		static void NewFile() {
			mut_level_seeds.lock();
			for(auto&& area : area_lookup) {
				level_seeds[area.first] = std::string();
			}
			mut_level_seeds.unlock();

			TileDefault::SetToDefault(tp->get_chunks());

			InitializeEmptySeeds();

			IO::SetActiveFile("");
			unsaved_changes = false;
		}

		static std::string SafeXMLName(const std::string& level) {
			return std::string("s") + std::to_string(std::hash<std::string>()(level));
		}

		static std::string AreaName(const std::string& safe) {
			for(auto&& level : area_lookup) {
				if(SafeXMLName(level.first) == safe) {
					return level.first;
				}
			}
			return "";
		}

		static void EncodeToFile() {
			pugi::xml_document xmld;
			pugi::xml_node seeds = xmld.append_child("seeds");

			mut_level_seeds.lock();
			for(auto&& seed : level_seeds) {
				if(!seed.second.empty() && !editors[seed.first]->read_only) {
					pugi::xml_node level = seeds.append_child(SafeXMLName(seed.first).c_str());
					pugi::xml_node data = level.append_child(pugi::xml_node_type::node_pcdata);
					data.set_value(seed.second.c_str());
				}
			}
			mut_level_seeds.unlock();

			pugi::xml_node node = xmld.append_child("chunks");
			if(!node) { 
				throw std::runtime_error("Failed to create chunks node.");	
			}

			for(SingleChunk* c : tp->root_chunks()) {
				pugi::xml_node cnkn = node.append_child(c->get_name().c_str());
				pugi::xml_node data = cnkn.append_child(pugi::xml_node_type::node_pcdata);
				data.set_value(static_cast<SingleChunk*>(c)->get_data().c_str());
			}

			if(!xmld.save_file(current_file.c_str())) {
				throw std::runtime_error("Failed to write document.");
			}

			SetActiveFile(current_file);

			unsaved_changes = false;
		}

		static void LoadFile(const std::string& file) {
			std::ifstream fst(file, std::ios::in);
			if(!fst.is_open()) {
				//create file if not exists
				std::ofstream ofs(file, std::ios::out);
				if(!ofs.is_open()) {
					throw std::runtime_error("Failed to create file.");
				}
				else {
					//clear undos
					for(auto&& ep : editors) {
						ep.second->clear_state();
					}

					SetActiveFile(file);
					ofs.close();
				}
			}
			else {
				fst.close();
				pugi::xml_document xmld;
				if(!xmld.load_file(file.c_str())) {
					throw std::runtime_error("XML Parser failed to load file.");
				}

				//clear undos
				for(auto&& ep : editors) {
					ep.second->clear_state();
				}

				request_soft_seed_lock();

				mut_level_seeds.lock();
				for(auto&& level : level_seeds) {
					level.second = "";
				}

				pugi::xml_node seeds = xmld.child("seeds");
				if(!seeds.empty()) {
					for(pugi::xml_node child : seeds.children()) {
						if(std::distance(child.children().begin(), child.children().end()) == 0) {
							mut_level_seeds.unlock();
							throw std::runtime_error(std::string("Invalid seed format for ") + child.name());
						}

						pugi::xml_node data = *child.children().begin();
						if(data.type() != pugi::xml_node_type::node_pcdata) {
							mut_level_seeds.unlock();
							throw std::runtime_error(std::string("Invalid seed format, expected node_pcdata as child of ") + child.name());
						}

						std::string area = AreaName(child.name());
						if(!area.empty() && level_seeds.find(area) != level_seeds.end()) {
							level_seeds[area] = data.value();
						
							if(area == current_area_editor) {
								input_seed->value(data.value());
							}
						}
						else {
							DBG_EXPR(std::cout << "[TileEditing] Warning: Level seeds contained unknown level: " << child.name() << std::endl);
						}
					}
				}
				mut_level_seeds.unlock();

				InitializeEmptySeeds();

				std::vector<SingleChunk*> scs = tp->root_chunks();
				pugi::xml_node chunks = xmld.child("chunks");
				if(chunks.empty()) {
					throw std::runtime_error("Invalid format.");
				}

				for(pugi::xml_node cnk : chunks) {
					if(std::distance(cnk.children().begin(), cnk.children().end()) != 1)
						throw std::runtime_error("Invalid format.");

					pugi::xml_node data = *(cnk.children().begin());
					if(data.type() != pugi::xml_node_type::node_pcdata)  {
						throw std::runtime_error("Invalid node format, expected node_pcdata.");
					}

					std::string str(cnk.name());
					std::string val(data.value());
					for(SingleChunk* sc : scs) {
						if(sc->get_name() == str) {
							if(sc->get_data().size() == val.size()) {
								sc->set_data(val);
							}
							break;
						}
					}
				}

				SetActiveFile(file);
			}
		}

		static void SaveAs() {
			try {
				std::string file = TileUtil::QueryTileFile(true);
				try {
					IO::SetActiveFile(file);
					IO::EncodeToFile();
				}
				catch(std::exception& e) {
					MessageBox(NULL, (std::string("Error saving file: ") + e.what()).c_str(), "Error", MB_OK);
				}
			}
			catch(std::exception&) {}
		}

		static void status_handler(unsigned state) {
			if(state == STATE_CHUNK_WRITE || state == STATE_CHUNK_PASTE || state == STATE_RESERVED1) {
				window->copy_label((std::string(WINDOW_BASE_TITLE " - ") + current_file + "*").c_str());
				unsaved_changes = true;
			}
			else if(state == STATE_CHUNK_APPLY) {
				try {
					if(!current_file.empty()) {
						IO::EncodeToFile();
						window->copy_label((std::string(WINDOW_BASE_TITLE " - ") + current_file).c_str());
					}
					else {
						IO::SaveAs();
					}
				}
				catch(std::exception& e) {
					MessageBox(NULL, (std::string("Error saving file: ") + e.what()).c_str(), "Error", MB_OK);
				}
			}
			else if(state == STATE_REQ_OPEN) {
				open_file->handle(2);
			}
			else if(state == STATE_REQ_TAB || state == STATE_REQ_TAB_REVERSE) {
				std::vector<std::string> areas;
				area_order(areas);

				auto p = std::find(areas.begin(), areas.end(), current_area_editor);

				if(p != areas.end()) {
					if(state == STATE_REQ_TAB) {
						if(p+1 == areas.end())
							return;

						SetCurrentEditor(*(p+1));
					}
					else {
						if(p == areas.begin())
							return;
						
						SetCurrentEditor(*(p-1));
					}
				}
			}
			else if(state == STATE_REQ_RANDOMIZE) {
				btn_randomize->handle(2);
			}
			else if(state == STATE_REQ_DEFAULT_SWAP) {
				if(mget(area_lookup, current_area_editor).second == "%") {
					SetCurrentEditor(prior_nondefault_editor);
				}
				else {
					SetCurrentEditor("Default (Read-Only)");
				}
			}
		}
		
		static void _singlechunks(Chunk* c, std::function<void(Chunk*)> cb) {
			if(c->type() == ChunkType::Group) {
				for(Chunk* ck : static_cast<GroupChunk*>(c)->get_chunks()) {
					_singlechunks(ck, cb);
				}
			}
			else {
				cb(c);
			}
		}

		static const std::string& CurrentFile() {
			return current_file;
		}
	}

	static void ForceCurrentLevel(bool r) {
		if(r) {
			//if default levels, no operation required, just setting checkbox
			if(mget(area_lookup, current_area_editor).second == "%") {
				level_forcer->set_enabled(true);
				return;
			}

			auto oper = level_force_oper[current_area_editor];
			if(oper) {
				oper();
				level_forcer->set_enabled(true);
			}
			else {
				MessageBox(NULL, ("Level '" + current_area_editor + "' cannot be forced.").c_str(), "Level Force", MB_OK);
				level_forcer->set_enabled(false);
				flcb_force->value(0);
				flcb_force->redraw();
			}
		}
		else
			level_forcer->set_enabled(false);
	}
	
	static void SetCurrentEditor(const std::string& area) {
		if(area == current_area_editor)
			return;
		
		//check if not default
		if(mget(area_lookup, area).second != "%") {
			prior_nondefault_editor = area;
		}

		if(!current_area_editor.empty()) {
			controls[area_group(current_area_editor)]->btns[current_area_editor]->activate();
		}

		controls[area_group(area)]->btns[area]->deactivate();
		input_seed->value(level_seeds[area].c_str());

		auto ed = editors[area];
		
		if(ed) {
			if(current_area_editor != "") {
				EditorWidget* last = editors[current_area_editor];
				EditorWidget* curr = editors[area];
				curr->shift_down = last->shift_down;
				curr->alt_down = last->alt_down;
				curr->ctrl_down = last->ctrl_down;
				std::memcpy(curr->mouse_down, last->mouse_down, sizeof(curr->mouse_down));
				window->remove(last);
			}
			
			EditorWidget* ew = editors[current_area_editor = area];
			window->add(ew);
			window->add(ew->sidebar_scrollbar);

			ForceCurrentLevel(!!flcb_force->value());

			ew->take_focus();
			window->redraw();
		}
		else {
			throw std::runtime_error("No such area editor");
		}
	}

	void DisplayStateCallback(std::function<void(bool)> cb) {
		display_cb = cb;
	}

	BUTTON_CLASS(RevertButton, "New File");
	int RevertButton::handle(int evt) {
		if(evt == 2) {			
			//clear undos
			for(auto&& ep : editors) {
				ep.second->clear_state();
			}

			IO::NewFile();
			::window->redraw();
		}
		else if(evt == FL_FOCUS)
			return 0;
		return Fl_Button::handle(evt);
	}

	BUTTON_CLASS(SaveButton, "Save As..");
	int SaveButton::handle(int evt) {
		if(evt == 2) {
			IO::SaveAs();
			::window->redraw();
		}
		else if(evt == FL_FOCUS)
			return 0;
		return Fl_Button::handle(evt);
	}
	
	BUTTON_CLASS(LoadButton, "Load File");
	int LoadButton::handle(int evt) {
		if(evt == 2) {
			try {
				std::string file = TileUtil::QueryTileFile(false);
				try {
					IO::LoadFile(file);
					::window->redraw();
				}
				catch(std::exception& e) {
					MessageBox(NULL, (std::string("Error loading file: ") + e.what()).c_str(), "Error", MB_OK);
				}
			}
			catch(std::exception&) {}
		}
		else if(evt == FL_FOCUS)
			return 0;

		return Fl_Button::handle(evt);	
	}
	
	struct AreaButton : public Fl_Button {
		std::string area;

		AreaButton(int x, int y, int w, int h, const std::string& narea) : 
			Fl_Button(x,y,w,h),
			area(narea)
		{
			this->copy_label(narea.c_str());
		}

		virtual int handle(int evt) override {
			if(evt == 2) {
				SetCurrentEditor(area);
			}
			else if(evt == FL_FOCUS)
				return 0;
			return Fl_Button::handle(evt);
		}
	};

	BUTTON_CLASS(ClearButton, "Clear Level");
	int ClearButton::handle(int evt) {
		if(evt == 2) {
			if(!current_area_editor.empty()) {
				editors[current_area_editor]->clear_chunks();
				parent()->redraw();
			}
		}
		else if(evt == FL_FOCUS)
			return 0;

		return Fl_Button::handle(evt);
	}

	//lays out the UI widgets and stores them to an AreaControls object.
	static void create_controls(int x, int y, const std::string& group) {
		const std::vector<std::string>& areas = mget(grouping, group).second.areas;
		
		int p = 0;
		auto make = [&](const std::string& narea) -> Fl_Button* {
			if(areas.size() == 4 || areas.size() == 3) {
				switch(p++) {
				case 0:
					return new AreaButton(x, y, 75, 18, narea);
				case 1:
					return new AreaButton(x+75, y, 75, 18,narea);
				case 2:
					return new AreaButton(x, y+20, 75, 18, narea);
				case 3:
					return new AreaButton(x+75, y+20, 75, 18, narea);
				default:
					return new AreaButton(x, y, 75, 18, narea); //invalid formatting
				}
			}
			else {
				p++;
				return new AreaButton(x, y, 150, 20, narea);
			}
		};

		std::map<std::string, Fl_Button*> buttons;
		//TODO... position the buttons according to their group / group size
		for(const std::string& narea : areas) {
			buttons[narea] = make(narea);
		}
		
		Fl_Box* data = new Fl_Box(x+160, y, 1, 25, "");
		data->align(FL_ALIGN_RIGHT);
		
		controls[group] = new AreaControls(group, buttons, data);
		controls[group]->update();
	}

	//worker thread: currently used for setting level seed
	static void construct_worker_thread() {
		worker_thread = std::thread([=]() {
			while(true) {
				if(tp->is_active()) {
					std::string area = current_game_level();

					mut_level_seeds.lock();
					auto it = level_seeds.find(area);
					if(it != level_seeds.end()) {
						if(seeder->get_seed() != it->second) {
							seeder->seed(it->second);
						}
					}
					mut_level_seeds.unlock();
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		});
	}


	struct TileEditingWindow : public Fl_Double_Window {
		TileEditingWindow(int x, int y, const char* L) : Fl_Double_Window(x, y, L) {}

		virtual int handle(int evt) override {
			//make sure we don't have lingering key-presses on unfocus
			if(evt == FL_UNFOCUS) {
				for(auto&& e : editors) {
					EditorWidget* editor = e.second;
					editor->alt_down = false;
					editor->shift_down = false;
					editor->ctrl_down = false;
				}
				return 1;
			}
			return Fl_Double_Window::handle(evt);
		}
	};

	static std::string construct_window() {
		Fl_Window* cons = new TileEditingWindow(780, 480, WINDOW_BASE_TITLE);
		window = cons;

		cons->begin();

		int y = 5;
		for(auto it = grouping.begin(); it != grouping.end(); ++it) {
			create_controls(5, y, it->first);
			if(it->second.areas.size() == 4 || it->second.areas.size() == 3)
				y += 43;
			else {
				y += 22;
				
				if(it + 1 != grouping.end() && ((it+1)->second.areas.size() == 4 || (it+1)->second.areas.size() == 3))
					y += 2;
			}
		}

		new SaveButton(5, y += 8, 150, 25);
		open_file = new LoadButton(5, y += 30, 150, 25);
		new RevertButton(5, y += 30, 150, 25);

		new ClearButton(165, 425, 135, 25);
		flcb_force = new NF_CheckButton(165, 455, 100, 20, "Force level to game");
		flcb_force->value(0);
		
		if(!level_forcer->valid()) {
			flcb_force->deactivate();
		}

		flcb_force->callback([](Fl_Widget* cbox) {
			ForceCurrentLevel(!!static_cast<Fl_Check_Button*>(cbox)->value());
		});

		
		input_seed = new SeedInput(400, 425, 170, 25, [=](std::string seed) {
			if(!current_area_editor.empty()) {
				IO::status_handler(STATE_RESERVED1);
				level_seeds[current_area_editor] = seed;
			}
		});

		btn_randomize = new SeedRandomize(input_seed, 400, 455, 120, 20);

		cons->end();

		std::string valid_editor;
		for(auto&& area : area_lookup) {
			EditorScrollbar* es = new EditorScrollbar(690, 5, 15, 420);
			std::vector<Chunk*> chunks = tp->query_chunks(area.second);

			// % -> default read-only
			if(area.second == "%" || !chunks.empty()) {
				if(valid_editor.empty() || valid_editor.find("Tut") == 0 || valid_editor == "Default (Read-Only)") {
					valid_editor = area.first;
				}

				//special handling for worm editor construction
				EditorWidget* ew;
				if(area.first == "Worm") {
					std::vector<Chunk*> edited;
					for(size_t i = 0; i < chunks.size(); i++) {
						if(i % 4 < 2) {
							edited.push_back(chunks[i]);
						}
					}
					ew = new EditorWidget(tp, 165, 5, 545 + 90, 420, es, edited, true);
				}
				else if(area.second == "%") { //default read-only
					std::vector<Chunk*> relevant;
					for(SingleChunk* c : tp->tile_patch()->root_chunks()) {
						if(c->get_width() == CHUNK_WIDTH && c->get_height() == CHUNK_HEIGHT)
							relevant.push_back(c);
					}
					ew = new EditorWidget(tp, 165, 5, 545 + 90, 420, es, relevant, false, true);
				}
				else {
					ew = new EditorWidget(tp, 165, 5, 545 + 90, 420, es, chunks);
				}
				es->set_parent_editor(ew);
				ew->status_callback(IO::status_handler);
				editors[area.first] = ew;
			}
		}

		return valid_editor;
	}

	bool Initialize(std::shared_ptr<DerandomizePatch> dp, 
			std::shared_ptr<GameHooks> gh, 
			std::shared_ptr<Seeder> seeder, 
			std::shared_ptr<StaticChunkPatch> scp) 
	{
		tp = scp;
		::seeder = seeder;
		::dp = dp;
		::gh = gh;

		level_forcer = std::make_shared<LevelForcer>(dp, gh);

		std::string first_editor = construct_window();
		window->callback([](Fl_Widget* widget) {
			display_cb(false);
			flcb_force->value(0);
			level_forcer->set_enabled(false);
			window->hide();
		});

		if(!tp->valid()) {
			return false;
		}

		if(!level_forcer->valid()) {
			return false;
		}

		if(!scp->valid()) {
			return false;
		}

		construct_worker_thread();
		IO::NewFile();

		if(!first_editor.empty()) {
			SetCurrentEditor(first_editor);
			initialized = true;
			return true;
		}
		else {
			return false;
		}
	}

	void ShowUI() {
		if(window) {
			window->show();
			display_cb(true);
		}
	}

	void HideUI() {
		if(window) {
			window->hide();
			display_cb(false);
		}
	}

	bool Visible() {
		return window->visible() != 0;
	}

	bool Valid() {
		return initialized;
	}

	std::shared_ptr<::Patch> GetPatch() {
		return tp;
	}
}