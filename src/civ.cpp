#include <string.h>
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include "civ.h"
#include "map-astar.h"

#define SCIENCE_DISCOVERY_DURATION_COEFFICIENT 4

civilization::civilization(std::string name, unsigned int civid, 
		const color& c_, map* m_,
		const std::vector<std::string>::iterator& names_start,
		const std::vector<std::string>::iterator& names_end,
		const city_improv_map* cimap_,
		const government* gov_, bool minor_civ_)
	: civname(name),
	civ_id(civid),
	col(c_),
	m(m_),
	fog(fog_of_war(m_)),
	gold(0),
	science(0),
	alloc_gold(5),
	alloc_science(5),
	research_goal_id(0),
	gov(gov_),
	relationships(civid + 1, relationship_unknown),
	known_land_map(buf2d<int>(0, 0, -1)),
	curr_city_name_index(0),
	next_city_id(1),
	next_unit_id(1),
	points(0),
	cross_oceans(false),
	anarchy_period(0),
	minor_civ(minor_civ_),
	cimap(cimap_)
{
	for(std::vector<std::string>::const_iterator it = names_start;
			it != names_end;
			++it) {
		city_names.push_back(*it);
	}
	relationships[civid] = relationship_peace;
	if(m) {
		fog = fog_of_war(m);
		known_land_map = buf2d<int>(m->size_x(),
				m->size_y(), -1);
	}
}

civilization::civilization()
	: civ_id(1337),
	m(NULL)
{
}

civilization::~civilization()
{
	for(std::map<unsigned int, unit*>::iterator it = units.begin();
			it != units.end();
			++it) {
		delete it->second;
	}
	for(std::map<unsigned int, city*>::iterator cit = cities.begin();
			cit != cities.end();
			++cit) {
		delete cit->second;
	}
}

void civilization::reveal_land(int x, int y, int r)
{
	for(int i = x - r; i <= x + r; i++) {
		for(int j = y - r; j <= y + r; j++) {
			int di = m->wrap_x(i);
			int dj = m->wrap_y(j);
			known_land_map.set(di, dj, m->get_land_owner(di, dj));
		}
	}
}

unit* civilization::add_unit(int uid, int x, int y, 
		const unit_configuration& uconf,
		unsigned int road_moves)
{
	unit* u = new unit(next_unit_id, uid, x, y, civ_id, uconf, road_moves);
	units.insert(std::make_pair(next_unit_id++, u));
	built_units[uid]++;
	m->add_unit(u);
	fog.reveal(x, y, 1);
	reveal_land(x, y, 1);
	return u;
}

void civilization::remove_unit(unit* u)
{
	std::map<unsigned int, unit*>::iterator uit = units.find(u->unit_id);
	if(uit != units.end()) {
		if(u->carried()) {
			u->unload();
		}
		else {
			fog.shade(u->xpos, u->ypos, 1);
		}
		std::list<unit*>::iterator it = u->carried_units.begin();
		while(it != u->carried_units.end()) {
			remove_unit(*it);
			it = u->carried_units.begin();
		}
		m->remove_unit(u);
		lost_units[uit->second->uconf_id]++;
		units.erase(uit);
		delete u;
	}
}

void civilization::eliminate()
{
	std::map<unsigned int, unit*>::iterator uit;
	while((uit = units.begin()) != units.end()) {
		remove_unit(uit->second);
	}
	std::map<unsigned int, city*>::iterator cit;
	while((cit = cities.begin()) != cities.end()) {
		remove_city(cit->second, true);
	}
	m->remove_civ_land(civ_id);
}

bool civilization::eliminated() const
{
	if(minor_civ)
		return units.empty();
	else
		return units.empty() && cities.empty();
}

void civilization::refill_moves(const unit_configuration_map& uconfmap)
{
	for(std::map<unsigned int, unit*>::iterator uit = units.begin();
		uit != units.end();
		++uit) {
		improvement_type i = improv_none;
		unit* u = uit->second;
		u->new_round(i);
		if(i != improv_none) {
			m->try_improve_terrain(u->xpos,
					u->ypos, civ_id, i);
		}
	}
}

void civilization::add_message(const msg& m)
{
	messages.push_back(m);
}

msg new_unit_msg(unit* u, city* c)
{
	msg m;
	m.type = msg_new_unit;
	m.msg_data.city_prod_data.building_city_id = c->city_id;
	m.msg_data.city_prod_data.prod_id = u->uconf_id;
	m.msg_data.city_prod_data.unit_id = u->unit_id;
	return m;
}

msg new_advance_discovered(unsigned int adv_id)
{
	msg m;
	m.type = msg_new_advance;
	m.msg_data.new_advance_id = adv_id;
	return m;
}

msg discovered_civ(int civid)
{
	msg m;
	m.type = msg_civ_discovery;
	m.msg_data.discovered_civ_id = civid;
	return m;
}

msg new_improv_msg(city* c, unsigned int ciid)
{
	msg m;
	m.type = msg_new_city_improv;
	m.msg_data.city_prod_data.building_city_id = c->city_id;
	m.msg_data.city_prod_data.prod_id = ciid;
	return m;
}

msg unit_disbanded(unsigned int uit)
{
	msg m;
	m.type = msg_unit_disbanded;
	m.msg_data.disbanded_unit_id = uit;
	return m;
}

msg new_relationship(int civid, relationship val)
{
	msg m;
	m.type = msg_new_relationship;
	m.msg_data.relationship_data.other_civ_id = civid;
	m.msg_data.relationship_data.new_relationship = val;
	return m;
}

msg anarchy_over()
{
	msg m;
	m.type = msg_anarchy_over;
	return m;
}

int civilization::get_national_income() const
{
	return national_income;
}

int civilization::get_national_science() const
{
	return national_science;
}

int civilization::get_military_expenses() const
{
	return military_expenses;
}

void civilization::update_military_expenses()
{
	military_expenses = 0;
	int free_units_togo = gov->free_units + cities.size() * gov->city_units;
	for(std::map<unsigned int, unit*>::const_iterator it = units.begin();
			it != units.end(); ++it) {
		if(it->second->uconf->max_strength > 0) {
			if(free_units_togo > 0)
				free_units_togo--;
			else
				military_expenses += gov->unit_cost;
		}
	}
}

void civilization::update_national_income_and_science()
{
	national_income = 0;
	national_science = 0;
	for(std::map<unsigned int, city*>::iterator cit = cities.begin();
			cit != cities.end();
			++cit) {
		int add_gold, add_science;
		calculate_total_city_commerce(*cit->second, &add_gold, &add_science);
		national_income += add_gold;
		national_science += add_science;
	}
}

void civilization::calculate_total_city_commerce(const city& c,
		int* add_gold, int* add_science) const
{
	int food, prod, comm;
	total_resources(c, &food, &prod, &comm);
	float alloc_gold_f = alloc_gold / 10.0f;
	float alloc_science_f = alloc_science / 10.0f;
	*add_gold = comm * alloc_gold_f;
	*add_science = comm * alloc_science_f;
	for(std::set<unsigned int>::const_iterator cit = c.built_improvements.begin();
			cit != c.built_improvements.end();
			++cit) {
		city_improv_map::const_iterator ciit = cimap->find(*cit);
		if(ciit != cimap->end()) {
			if(ciit->second.comm_bonus && alloc_gold) {
				*add_gold += (comm * alloc_gold_f) * (ciit->second.comm_bonus / 100.0f);
			}
			if(ciit->second.science_bonus && alloc_science) {
				*add_science += (comm * alloc_science_f) * (ciit->second.science_bonus / 100.0f);
			}
		}
	}
}

void civilization::add_gold(int i)
{
	gold += i;
}

void civilization::increment_resources(const unit_configuration_map& uconfmap,
		const advance_map& amap,
		unsigned int road_moves, unsigned int food_eaten_per_citizen)
{
	if(minor_civ)
		return;

	if(anarchy_period) {
		anarchy_period--;
		if(!anarchy_period) {
			add_message(anarchy_over());
		}
		return;
	}

	for(std::map<unsigned int, city*>::iterator cit = cities.begin();
			cit != cities.end();
			++cit) {
		int food, prod, comm;
		city* this_city = cit->second;
		total_resources(*this_city, &food, &prod, &comm);
		this_city->stored_food += food - this_city->get_city_size() * food_eaten_per_citizen;
		this_city->stored_prod += prod;
		if(this_city->production.current_production_id > -1) {
			if(this_city->production.producing_unit) {
				unit_configuration_map::const_iterator prod_unit = uconfmap.find(this_city->production.current_production_id);
				if(prod_unit != uconfmap.end()) {
					if((int)prod_unit->second.production_cost <= this_city->stored_prod) {
						if((int)prod_unit->second.population_cost < this_city->get_city_size()) {
							for(unsigned int i = 0; i < prod_unit->second.population_cost; i++)
								this_city->decrement_city_size();
							unit* u = add_unit(this_city->production.current_production_id, 
									this_city->xpos, this_city->ypos, prod_unit->second,
									road_moves);
							if(this_city->has_barracks(*cimap))
								u->veteran = true;
							this_city->stored_prod -= prod_unit->second.production_cost;
							add_message(new_unit_msg(u, this_city));
						}
					}
				}
			}
			else {
				city_improv_map::const_iterator prod_improv = cimap->find(this_city->production.current_production_id);
				if(prod_improv != cimap->end()) {
					if((int)prod_improv->second.cost <= this_city->stored_prod) {
						if(this_city->built_improvements.find(prod_improv->first) ==
								this_city->built_improvements.end()) {
							this_city->built_improvements.insert(prod_improv->first);
							this_city->stored_prod -= prod_improv->second.cost;
							if(prod_improv->second.palace) {
								destroy_old_palace(this_city);
							}
						}
						add_message(new_improv_msg(this_city, prod_improv->first));
						this_city->production.current_production_id = -1;
					}
				}
			}
		}

		for(std::set<unsigned int>::const_iterator ciit = this_city->built_improvements.begin();
				ciit != this_city->built_improvements.end();
				++ciit) {
			city_improv_map::const_iterator cnit = cimap->find(*ciit);
			if(cnit != cimap->end())
				this_city->accum_culture += cnit->second.culture;
		}
	}

	update_national_income_and_science();
	update_military_expenses();
	gold += national_income - military_expenses;
	{
		while(gold < 0) {
			std::map<unsigned int, unit*>::iterator uit = units.begin();
			while(uit != units.end() && uit->second->uconf->max_strength <= 0)
				uit++;
			if(uit == units.end())
				break;
			gold += gov->unit_cost;
			add_message(unit_disbanded(uit->second->unit_id));
			remove_unit(uit->second);
		}
	}
	science += national_science;
	advance_map::const_iterator adv = amap.find(research_goal_id);
	if(adv == amap.end()) {
		if(researched_advances.empty()) {
			add_message(new_advance_discovered(0));
			update_ocean_crossing(uconfmap, amap, 0);
		}
		setup_default_research_goal(amap);
	}
	else if(adv->second.cost * SCIENCE_DISCOVERY_DURATION_COEFFICIENT <= science) {
		science -= adv->second.cost;
		add_message(new_advance_discovered(research_goal_id));
		researched_advances.insert(research_goal_id);
		update_ocean_crossing(uconfmap, amap, research_goal_id);
		setup_default_research_goal(amap);
	}
}

bool civilization::allowed_research_goal(const advance_map::const_iterator& it) const
{
	if(researched_advances.find(it->first) == researched_advances.end()) {
		for(int i = 0; i < max_num_needed_advances; i++) {
			if(it->second.needed_advances[i] == 0)
				continue;
			if(researched_advances.find(it->second.needed_advances[i]) ==
					researched_advances.end()) {
				return false;
			}
		}
		return true;
	}
	else {
		return false;
	}
}

void civilization::setup_default_research_goal(const advance_map& amap)
{
	research_goal_id = 0;
	for(advance_map::const_iterator it = amap.begin();
			it != amap.end();
			++it) {
		if(allowed_research_goal(it)) {
			research_goal_id = it->first;
			return;
		}
	}
}

char civilization::fog_at(int x, int y) const
{
	return fog.get_value(m->wrap_x(x), m->wrap_y(y));
}

city* civilization::add_city(int x, int y)
{
	city* c = new city(city_names[curr_city_name_index++], x, y, civ_id,
			next_city_id);
	if(curr_city_name_index >= city_names.size()) {
		for(unsigned int i = 0; i < city_names.size(); i++) {
			city_names[i] = std::string("New ") + city_names[i];
		}
		curr_city_name_index = 0;
	}
	m->add_city(c, x, y);
	add_city(c);
	update_city_resource_workers(c);
	return c;
}

void civilization::update_city_resource_workers(city* c)
{
	c->clear_resource_workers();
	while(c->add_resource_worker(next_good_resource_spot(c)))
		;
}

void civilization::add_city(city* c)
{
	fog.reveal(c->xpos, c->ypos, 2);
	reveal_land(c->xpos, c->ypos, 2);
	fog.shade(c->xpos, c->ypos, 2);
	fog.reveal(c->xpos, c->ypos, 1);
	c->set_city_id(next_city_id);
	c->set_civ_id(civ_id);
	cities.insert(std::make_pair(next_city_id++, c));
}

void civilization::remove_city(city* c, bool del)
{
	std::map<unsigned int, city*>::iterator cit = cities.find(c->city_id);
	if(cit != cities.end()) {
		fog.shade(c->xpos, c->ypos, 1);
		cities.erase(cit);
		if(del) {
			m->remove_city(c);
			delete c;
		}
	}
}

relationship civilization::get_relationship_to_civ(unsigned int civid) const
{
	if(civid == civ_id)
		return relationship_peace;
	if(relationships.size() <= civid)
		return relationship_unknown;
	return relationships[civid];
}

void civilization::set_relationship_to_civ(unsigned int civid, relationship val)
{
	if(civid == civ_id)
		return;
	if(relationships.size() <= civid) {
		relationships.resize(civid + 1, relationship_unknown);
	}
	if(val != relationships[civid])
		add_message(new_relationship(civid, val));
	relationships[civid] = val;
}

bool civilization::discover(unsigned int civid)
{
	if(civid != civ_id && get_relationship_to_civ(civid) == relationship_unknown) {
		add_message(discovered_civ(civid));
		if(minor_civ)
			set_relationship_to_civ(civid, relationship_war);
		else
			set_relationship_to_civ(civid, relationship_peace);
		return 1;
	}
	return 0;
}

void civilization::undiscover(unsigned int civid)
{
	if(civid != civ_id) {
		set_relationship_to_civ(civid, relationship_unknown);
	}
}

void civilization::set_war(unsigned int civid)
{
	if(civid != civ_id) {
		set_relationship_to_civ(civid, relationship_war);
	}
}

void civilization::set_peace(unsigned int civid)
{
	if(civid != civ_id) {
		set_relationship_to_civ(civid, relationship_peace);
	}
}

std::vector<unsigned int> civilization::check_discoveries(int x, int y, int radius)
{
	std::vector<unsigned int> discs;
	for(int i = x - radius; i <= x + radius; i++) {
		for(int j = y - radius; j <= y + radius; j++) {
			int owner = m->get_spot_owner(i, j);
			if(owner != -1 && owner != (int)civ_id) {
				discover(owner);
				discs.push_back(owner);
			}
		}
	}
	return discs;
}

bool civilization::improv_discovered(const city_improvement& uconf) const
{
	return researched_advances.find(uconf.needed_advance) != researched_advances.end() || 
			uconf.needed_advance == 0;
}

bool civilization::unit_discovered(const unit_configuration& uconf) const
{
	return researched_advances.find(uconf.needed_advance) != researched_advances.end() || 
			uconf.needed_advance == 0;
}

bool civilization::advance_discovered(unsigned int adv_id) const
{
	return adv_id == 0 ||
		researched_advances.find(adv_id) != researched_advances.end();
}

void civilization::set_map(map* m_)
{
	if(m_ && !m) {
		m = m_;
		fog = fog_of_war(m);
		known_land_map = buf2d<int>(m->size_x(),
				m->size_y(), -1);
	}
}

bool civilization::move_acceptable_by_land_and_units(int x, int y) const
{
	const std::list<unit*>& units = m->units_on_spot(x, y);
	int land_owner = m->get_land_owner(x, y);
	int unit_owner = -1;
	if(!units.empty())
		unit_owner = units.front()->civ_id;
	bool acceptable_unit_owner = unit_owner == -1 ||
		unit_owner == (int)civ_id;
	if(!acceptable_unit_owner)
		return false;
	bool acceptable_land_owner = land_owner == -1 ||
		land_owner == (int)civ_id ||
		get_relationship_to_civ(land_owner) != relationship_peace;
	if(!acceptable_land_owner)
		return false;
	return true;
}

bool civilization::can_move_unit(const unit* u, int chx, int chy) const
{
	if((!u->num_moves() && !u->num_road_moves()) || !(chx || chy)) {
		return false;
	}
	int newx = u->xpos + chx;
	int newy = u->ypos + chy;
	if(!m->terrain_allowed(*u, newx, newy)) {
		return false;
	}
	if(!move_acceptable_by_land_and_units(newx, newy)) {
		return false;
	}
	return true;
}

void civilization::move_unit(unit* u, int chx, int chy, bool fought)
{
	int newx = m->wrap_x(u->xpos + chx);
	int newy = m->wrap_y(u->ypos + chy);
	if(u->carried()) {
		unload_unit(u);
	}
	m->remove_unit(u);
	fog.shade(u->xpos, u->ypos, 1);
	u->move_to(newx, newy, 
			!fought && m->road_between(u->xpos, u->ypos, newx, newy));
	fog.reveal(u->xpos, u->ypos, 1);
	reveal_land(u->xpos, u->ypos, 1);
	m->add_unit(u);
	for(std::list<unit*>::iterator it = u->carried_units.begin();
			it != u->carried_units.end();
			++it) {
		(*it)->xpos = u->xpos;
		(*it)->ypos = u->ypos;
	}
}
int civilization::get_known_land_owner(int x, int y) const
{
	const int* v = known_land_map.get(m->wrap_x(x), m->wrap_y(y));
	if(!v)
		return -1;
	return *v;
}

bool civilization::blocked_by_land(int x, int y) const
{
	int land_owner = get_known_land_owner(x, y);
	if(land_owner < 0 || land_owner == (int)civ_id) {
		return false;
	}
	else {
		return get_relationship_to_civ(land_owner) == relationship_peace;
	}
}

void civilization::set_government(const government* g)
{
	gov = g;
}

void civilization::set_city_improvement_map(const city_improv_map* cimap_)
{
	cimap = cimap_;
}

void civilization::destroy_old_palace(const city* c)
{
	// go through all cities
	for(std::map<unsigned int, city*>::iterator it = cities.begin();
			it != cities.end();
			++it) {
		if(it->second != c) {
			// all improvements in the city
			for(std::set<unsigned int>::iterator cit = it->second->built_improvements.begin();
					cit != it->second->built_improvements.end();
					++cit) {
				// see if this improvement is the palace
				city_improv_map::const_iterator ciit = cimap->find(*cit);
				if(ciit != cimap->end()) {
					if(ciit->second.palace) {
						it->second->built_improvements.erase(cit);
						return;
					}
				}
			}
		}
	}
}

bool civilization::can_build_unit(const unit_configuration& uc, const city& c) const
{
	if(!unit_discovered(uc))
		return false;
	if(uc.is_water_unit() && !m->connected_to_sea(c.xpos, c.ypos))
		return false;
	for(unsigned int i = 0; i < max_num_unit_needed_resources; i++) {
		if(uc.needed_resources[i] != 0) {
			resource_map::const_iterator it = m->rmap.find(uc.needed_resources[i]);
			if(it != m->rmap.end()) {
				if(researched_advances.find(it->second.needed_advance) ==
						researched_advances.end())
					return false;
				if(!has_access_to_resource(c, uc.needed_resources[i]))
					return false;
			}
			else {
				return false;
			}
		}
	}
	return true;
}

class check_resource {
	public:
		check_resource(const map& m_, unsigned int civ_id_, unsigned int res_id_)
			: m(m_), civ_id(civ_id_), res_id(res_id_) { }
		bool operator()(const coord& c)
		{
			return m.get_resource(c.x, c.y) == res_id &&
				m.get_land_owner(c.x, c.y) == (int)civ_id;
		}
	private:
		const map& m;
		unsigned int civ_id;
		unsigned int res_id;
};

bool civilization::has_access_to_resource(const city& c, unsigned int res_id) const
{
	check_resource cr(*m, civ_id, res_id);
	std::list<coord> path_to_resource = map_along_roads(coord(c.xpos, c.ypos),
			*this, true, true, cr);
	return !path_to_resource.empty();
}

bool civilization::can_build_improvement(const city_improvement& ci, const city& c) const
{
	if(!improv_discovered(ci))
		return false;
	if(c.built_improvements.find(ci.improv_id) != c.built_improvements.end())
		return false;
	return true;
}

bool civilization::can_load_unit(unit* loadee, unit* loader) const
{
	return loadee->can_load_at(loader);
}

void civilization::load_unit(unit* loadee, unit* loader)
{
	// NOTE: load_at must be the last call, because it may change the
	// unit position. The unit must be removed at the map from the
	// current position.
	fog.shade(loadee->xpos, loadee->ypos, 1);
	m->remove_unit(loadee);
	loadee->load_at(loader);
}

void civilization::unload_unit(unit* unloadee)
{
	unloadee->unload();
	m->add_unit(unloadee);
	fog.reveal(unloadee->xpos, unloadee->ypos, 1);
	reveal_land(unloadee->xpos, unloadee->ypos, 1);
}

const std::map<unsigned int, int>& civilization::get_built_units() const
{
	return built_units;
}

const std::map<unsigned int, int>& civilization::get_lost_units() const
{
	return lost_units;
}

void civilization::add_points(unsigned int num)
{
	points += num;
}

void civilization::reset_points()
{
	points = 0;
}

int civilization::get_points() const
{
	return points;
}

void civilization::update_ocean_crossing(const unit_configuration_map& uconfmap,
		const advance_map& amap, int adv_id)
{
	// NOTE: if units are ever obsoleted, it has to be checked here too.
	if(!cross_oceans) {
		for(unit_configuration_map::const_iterator uit = uconfmap.begin();
				uit != uconfmap.end();
				++uit) {
			if((int)uit->second.needed_advance == adv_id && 
				uit->second.ocean_unit) {
				cross_oceans = true;
				return;
			}
		}
	}
}

bool civilization::can_cross_oceans() const
{
	return cross_oceans;
}

void civilization::total_resources(const city& c,
		int* food, int* prod, int* comm) const
{
	*food = 0; *prod = 0; *comm = 0;
	const std::list<coord>& resource_coords = c.get_resource_coords();
	for(std::list<coord>::const_iterator it = resource_coords.begin();
			it != resource_coords.end();
			++it) {
		int f, p, cm;
		m->get_resources_on_spot(c.xpos + it->x,
				c.ypos + it->y, &f, &p, &cm,
				&researched_advances, gov->production_cap);
		*food += f;
		*prod += p;
		*comm += cm;
	}
}

coord civilization::next_good_resource_spot(const city* c) const
{
	int curr_food, curr_prod, curr_comm;
	total_resources(*c, &curr_food, &curr_prod, &curr_comm);
	int req_food = c->get_city_size() * 2 + 2 - curr_food;
	coord ret(0, 0);
	int opt_food = -1;
	int opt_prod = -1;
	int opt_comm = -1;
	for(int i = -2; i <= 2; i++) {
		for(int j = -2; j <= 2; j++) {
			if(abs(i) == 2 && abs(j) == 2)
				continue;
			if(!i && !j)
				continue;
			if(std::find(c->get_resource_coords().begin(),
					c->get_resource_coords().end(),
					coord(i, j)) != c->get_resource_coords().end())
				continue;
			int terr = m->get_data(c->xpos + i, c->ypos + j);
			if(terr == -1)
				continue;
			if(m->get_land_owner(c->xpos + i, c->ypos + j) != (int)c->civ_id)
				continue;
			if(!can_add_resource_worker(coord(c->xpos + i, c->ypos + j)))
				continue;
			int tf, tp, tc;
			m->get_resources_on_spot(c->xpos + i, c->ypos + j, &tf, &tp, &tc,
					&researched_advances, gov->production_cap);
			if((tf >= opt_food && opt_food < req_food) || 
				 (tf >= req_food &&
				 (tp > opt_prod || 
				  (tp == opt_prod && 
				   (tc > opt_comm || tf > opt_food))))) {
				ret.x = i;
				ret.y = j;
				opt_food = tf;
				opt_prod = tp;
				opt_comm = tc;
			}
		}
	}
	return ret;
}

bool civilization::can_add_resource_worker(const coord& c) const
{
	return resource_workers_map.find(c) == resource_workers_map.end();
}

void civilization::update_resource_worker_map()
{
	resource_workers_map.clear();
	std::vector<city*> updateable_cities;
	do {
		updateable_cities.clear();
		for(std::map<unsigned int, city*>::const_iterator it = cities.begin();
				it != cities.end();
				++it) {
			resource_workers_map.insert(std::make_pair(coord(it->second->xpos,
							it->second->ypos),
						it->first));
		}

		for(std::map<unsigned int, city*>::const_iterator it = cities.begin();
				it != cities.end();
				++it) {
			const std::list<coord>& coords = it->second->get_resource_coords();
			for(std::list<coord>::const_iterator cit = coords.begin();
					cit != coords.end();
					++cit) {
				coord cp = coord(cit->x + it->second->xpos,
						cit->y + it->second->ypos);
				std::pair<std::map<coord, unsigned int>::iterator, bool> res =
					resource_workers_map.insert(std::make_pair(cp, it->first));
				if(res.second == false && res.first->second != it->first) {
					printf("Conflict %d <=> %d at (%d, %d)\n",
							it->first, res.first->second, cp.x, cp.y);
					updateable_cities.push_back(it->second);
					break;
				}
			}
		}

		if(!updateable_cities.empty()) {
			for(std::vector<city*>::iterator it = updateable_cities.begin();
					it != updateable_cities.end();
					++it) {
				update_city_resource_workers(*it);
			}
		}
	} while (!updateable_cities.empty());
}

void civilization::set_anarchy_period(unsigned int num)
{
	if(num == 0) {
		add_message(anarchy_over());
	}
	else {
		anarchy_period = num;
	}
}

bool civilization::is_minor_civ() const
{
	return minor_civ;
}

bool civilization::set_commerce_allocation(unsigned int a_gold, unsigned int a_science)
{
	if(a_gold + a_science > 10)
		return false;
	alloc_gold = a_gold;
	alloc_science = a_science;
	update_national_income_and_science();
	update_military_expenses();
	return true;
}

