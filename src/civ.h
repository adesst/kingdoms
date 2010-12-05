#ifndef CIV_H
#define CIV_H

#include <stdlib.h>

#include <vector>
#include <string>
#include <list>
#include <set>
#include <map>

#include "coord.h"
#include "color.h"
#include "buf2d.h"

#include "unit_configuration.h"
#include "resource_configuration.h"
#include "city.h"
#include "unit.h"
#include "map.h"
#include "fog_of_war.h"
#include "advance.h"

enum msg_type {
	msg_new_unit,
	msg_civ_discovery,
	msg_new_advance,
	msg_new_city_improv,
};

struct msg {
	msg_type type;
	union {
		struct {
			city* building_city;
			int prod_id;
		} city_prod_data;
		int discovered_civ_id;
		unsigned int new_advance_id;
	} msg_data;
};

enum relationship {
	relationship_unknown,
	relationship_peace,
	relationship_war,
};

class civilization {
	public:
		civilization(std::string name, unsigned int civid, const color& c_, map* m_, bool ai_);
		~civilization();
		unit* add_unit(int uid, int x, int y, const unit_configuration& uconf);
		void remove_unit(unit* u);
		int try_move_unit(unit* u, int chx, int chy);
		void refill_moves(const unit_configuration_map& uconfmap);
		void increment_resources(const unit_configuration_map& uconfmap,
				const advance_map& amap,
				const city_improv_map& cimap);
		char fog_at(int x, int y) const;
		city* add_city(std::string name, int x, int y);
		void remove_city(city* c);
		void add_message(const msg& m);
		relationship get_relationship_to_civ(unsigned int civid) const;
		void set_relationship_to_civ(unsigned int civid, relationship val);
		bool discover(unsigned int civid);
		void undiscover(unsigned int civid);
		void set_war(unsigned int civid);
		void set_peace(unsigned int civid);
		std::vector<unsigned int> check_discoveries(int x, int y, int radius);
		bool unit_discovered(const unit_configuration& uconf) const;
		bool improv_discovered(const city_improvement& uconf) const;
		bool can_move_to(int x, int y) const;
		bool blocked_by_land(int x, int y) const;
		int get_known_land_owner(int x, int y) const;
		void eliminate();
		void set_map(map* m_);
		std::string civname;
		const unsigned int civ_id;
		color col;
		std::list<unit*> units;
		std::list<city*> cities;
		map* m;
		fog_of_war fog;
		int gold;
		int science;
		std::list<msg> messages;
		int alloc_gold;
		int alloc_science;
		unsigned int research_goal_id;
		std::set<unsigned int> researched_advances;
		bool ai;
	private:
		void reveal_land(int x, int y, int r);
		std::vector<relationship> relationships;
		buf2d<int> known_land_map;
};


void total_resources(const city& c, const map& m,
		int* food, int* prod, int* comm);
void set_default_city_production(city* c, 
		const unit_configuration_map& uconfmap);
void combat(unit* u1, unit* u2);
bool can_attack(const unit& u1, const unit& u2);
bool terrain_allowed(const map& m, const unit& u, int x, int y);

#endif

