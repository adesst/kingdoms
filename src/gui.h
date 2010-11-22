#ifndef GUI_H
#define GUI_H

#include <list>
#include <vector>
#include <map>

#include "SDL/SDL.h"
#include "SDL/SDL_image.h"
#include "SDL/SDL_ttf.h"

#include "color.h"
#include "utils.h"
#include "sdl-utils.h"
#include "buf2d.h"
#include "civ.h"
#include "rect.h"

struct camera {
	int cam_x;
	int cam_y;
};

typedef std::map<std::pair<int, color>, SDL_Surface*> UnitImageMap;

struct gui_resources {
	gui_resources(const TTF_Font& f);
	std::vector<SDL_Surface*> terrains;
	std::vector<SDL_Surface*> plain_unit_images;
	UnitImageMap unit_images;
	const TTF_Font& font;
	std::vector<SDL_Surface*> city_images;
};

class gui_data {
	public:
		gui_data(map& mm, round& rr);
		map& m;
		round& r;
		unit* current_unit;
};

class main_window {
	public:
		main_window(SDL_Surface* screen_, int x, int y, gui_data& data_, gui_resources& res_);
		~main_window();
		void set_current_unit(const unit* u);
		int draw();
		int process(int ms);
		int handle_input(const SDL_Event& ev, std::list<unit*>::iterator& current_unit_it, city** c);
	private:
		int draw_main_map();
		int draw_sidebar() const;
		int draw_unit(const unit& u);
		color get_minimap_color(int x, int y) const;
		int draw_minimap() const;
		int draw_civ_info() const;
		int draw_unit_info(const unit* current_unit) const;
		int draw_eot() const;
		int clear_sidebar() const;
		int clear_main_map() const;
		int draw_tile(const SDL_Surface* surf, int x, int y) const;
		int draw_city(const city& c) const;
		int show_terrain_image(int x, int y, int xpos, int ypos, bool shade) const;
		int handle_mousemotion(int x, int y);
		int try_move_camera(bool left, bool right, bool up, bool down);
		void center_camera_to_unit(unit* u);
		int try_center_camera_to_unit(unit* u);
		void numpad_to_move(SDLKey k, int* chx, int* chy) const;
		int handle_keydown(SDLKey k, SDLMod mod, std::list<unit*>::iterator& current_unit_it, city** c);
		SDL_Surface* screen;
		const int screen_w;
		const int screen_h;
		gui_data& data;
		gui_resources& res;
		const int tile_w;
		const int tile_h;
		const int cam_total_tiles_x;
		const int cam_total_tiles_y;
		const int sidebar_size;
		camera cam;
		const unit* current_unit;
		const unit* blink_unit;
		int timer;
};

#define CALL_MEMBER_FUN(object,ptrToMember) ((object).*(ptrToMember))

template <typename T>
class button {
	public:
		button(const rect& dim_, SDL_Surface* surf_, int(T::*)());
		int draw(SDL_Surface* screen) const;
		rect dim;
		SDL_Surface* surf;
		int (T::* onclick)();
};

class city_window {
	typedef int(city_window::*city_window_fun)();
	public:
		city_window(SDL_Surface* screen_, int x, int y, gui_data& data_, gui_resources& res_, city* c_);
		~city_window();
		int handle_input(const SDL_Event& ev);
		int draw();
	private:
		int handle_keydown(SDLKey k, SDLMod mod);
		int handle_mousedown(const SDL_Event& ev);
		int on_exit();
		SDL_Surface* screen;
		const int screen_w;
		const int screen_h;
		gui_data& data;
		gui_resources& res;
		city* c;
		std::list<button<city_window>*> buttons;
		SDL_Surface* label_surf;
		SDL_Surface* button_surf;
};

class gui
{
	public:
		gui(int x, int y, map& mm, round& rr,
				const std::vector<const char*>& terrain_files,
				const std::vector<const char*>& unit_files,
				const char* cityfile,
				const TTF_Font& font_);
		~gui();
		int display(const unit* current_unit);
		int handle_input(const SDL_Event& ev, std::list<unit*>::iterator& current_unit);
		int process(int ms, const unit* u);
	private:
		void show_city_window(city* c);
		const int screen_w;
		const int screen_h;
		gui_data data;
		gui_resources res;
		SDL_Surface* screen;
		main_window mw;;
		city_window* cw;
};

#endif

