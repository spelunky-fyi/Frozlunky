// generated by Fast Light User Interface Designer (fluid) version 1.0302

#ifndef gui_h
#define gui_h

#define WIN32

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <map>
#include <functional>
#include <memory>
#include <iostream>

#include "spelunky.h"
#include "remote_call_patch.h"

#define BUTTON_CLASS(NAME) \
class NAME : public Fl_Button { \
public: \
	NAME(int x, int y, int w, int h, char* L) : Fl_Button(x,y,w,h,L) {} \
	virtual int handle(int evt) override; \
}

BUTTON_CLASS(ToggleButton);
BUTTON_CLASS(SeedChangeButton);
BUTTON_CLASS(InfoButton);
BUTTON_CLASS(DailyButton);
BUTTON_CLASS(FrozboardsButton);
BUTTON_CLASS(DisplayModsButton);
BUTTON_CLASS(NetplayButton);

#undef BUTTON_CLASS


Fl_Window* make_window();
int gui_operate(std::shared_ptr<Spelunky> spelunk, char* icon);
void set_toggle_callback(std::function<void(bool)> callback);
void set_seed_change_callback(std::function<bool(const std::string&)> callback);
void undo_patches();

std::shared_ptr<RemoteCallPatch> CurrentRCP();

#endif
