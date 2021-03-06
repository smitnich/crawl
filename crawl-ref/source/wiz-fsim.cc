/**
 * @file
 * @brief Fight simualtion wizard functions.
**/

#include "AppHdr.h"

#include "wiz-fsim.h"

#include <cerrno>

#include "beam.h"
#include "bitary.h"
#include "coordit.h"
#include "dbg-util.h"
#include "directn.h"
#include "env.h"
#include "fight.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "item_use.h"
#include "jobs.h"
#include "libutil.h"
#include "makeitem.h"
#include "message.h"
#include "melee_attack.h"
#include "mgen_data.h"
#include "mon-clone.h"
#include "mon-death.h"
#include "mon-place.h"
#include "monster.h"
#include "mon-util.h"
#include "options.h"
#include "output.h"
#include "player-equip.h"
#include "player.h"
#include "ranged_attack.h"
#include "skills.h"
#include "species.h"
#include "state.h"
#include "stringutil.h"
#include "throw.h"
#include "unwind.h"
#include "version.h"
#include "wiz-you.h"

#ifdef WIZARD

fight_data null_fight = {0.0, 0, 0, 0.0, 0, 0.0, 0.0};
typedef map<skill_type, int8_t> skill_map;

static const char* _title_line =
    "AvHitDam | MaxDam | Accuracy | AvDam | AvTime | AvSpeed | AvEffDam"; // 64 columns
static const char* _csv_title_line =
    "AvHitDam\tMaxDam\tAccuracy\tAvDam\tAvTime\tAvSpeed\tAvEffDam";

static string _fight_string(fight_data fdata, bool csv)
{
    return make_stringf(csv ? "%.1f\t%d\t%d%%\t%.1f\t%d\t%.2f\t%.1f"
                            : "   %5.1f |    %3d |     %3d%% |"
                              " %5.1f |   %3d  | %5.2f |    %5.1f",
                        fdata.av_hit_dam, fdata.max_dam, fdata.accuracy,
                        fdata.av_dam, fdata.av_time, fdata.av_speed,
                        fdata.av_eff_dam);
}

static skill_type _equipped_skill()
{
    const int weapon = you.equip[EQ_WEAPON];
    const item_def * iweap = weapon != -1 ? &you.inv[weapon] : nullptr;

    if (iweap && iweap->base_type == OBJ_WEAPONS)
        return item_attack_skill(*iweap);

    return SK_UNARMED_COMBAT;
}

static string _equipped_weapon_name()
{
    const int weapon = you.equip[EQ_WEAPON];
    const item_def * iweap = weapon != -1 ? &you.inv[weapon] : nullptr;
    const int missile = you.m_quiver.get_fire_item();

    if (iweap)
    {
        string item_buf = iweap->name(DESC_PLAIN);
        // If it's a ranged weapon, add the description of the missile
        if (is_range_weapon(*iweap) && missile < ENDOFPACK && missile >= 0)
            item_buf += " with " + you.inv[missile].name(DESC_PLAIN);
        return "Wielding: " + item_buf;
    }

    if (missile != -1)
        return "Quivering: " + you.inv[missile].name(DESC_PLAIN);

    return "Unarmed";
}

static string _time_string()
{
    time_t curr_time = time(nullptr);
    struct tm *ltime = TIME_FN(&curr_time);
    if (ltime)
    {
        return make_stringf("%4d/%02d/%02d/%2d:%02d:%02d",
                 ltime->tm_year + 1900,
                 ltime->tm_mon  + 1,
                 ltime->tm_mday,
                 ltime->tm_hour,
                 ltime->tm_min,
                 ltime->tm_sec);
    }
    return "";
}

static void _write_version(FILE * o)
{
    fprintf(o, CRAWL " version %s\n", Version::Long);
}

static void _write_matchup(FILE * o, monster &mon, bool defend, int iter_limit)
{
    fprintf(o, "%s: %s %s vs. %s (%d rounds) (%s)\n",
            defend ? "Defense" : "Attack",
            species_name(you.species).c_str(),
            get_job_name(you.char_class),
            mon.name(DESC_PLAIN, true).c_str(),
            iter_limit,
            _time_string().c_str());
}

static void _write_you(FILE * o)
{
    fprintf(o, "%s %s: XL %d   Str %d   Int %d   Dex %d\n",
            species_name(you.species).c_str(),
            get_job_name(you.char_class),
            you.experience_level,
            you.strength(),
            you.intel(),
            you.dex());
}

static void _write_weapon(FILE * o)
{
    fprintf(o, "%s, Skill: %s\n",
            _equipped_weapon_name().c_str(),
            skill_name(_equipped_skill()));
}

static void _write_mon(FILE * o, monster &mon)
{
    fprintf(o, "%s: HD %d   AC %d   EV %d\n",
            mon.name(DESC_PLAIN, true).c_str(),
            mon.get_experience_level(),
            mon.armour_class(),
            mon.evasion());
}

static bool _equip_weapon(const string &weapon, bool &abort)
{
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        if (!you.inv[i].defined())
            continue;

        if (you.inv[i].name(DESC_PLAIN).find(weapon) != string::npos)
        {
            if (i != you.equip[EQ_WEAPON])
            {
                wield_weapon(true, i, false);
                if (i != you.equip[EQ_WEAPON])
                {
                    abort = true;
                    return true;
                }
            }
            return true;
        }
    }
    return false;
}

static bool _fsim_kit_equip(const string &kit, string &error)
{
    bool abort = false;
    string::size_type ammo_div = kit.find("/");
    string weapon = kit;
    string missile;
    if (ammo_div != string::npos)
    {
        weapon = kit.substr(0, ammo_div);
        missile = kit.substr(ammo_div + 1);
        trim_string(weapon);
        trim_string(missile);
    }

    if (!weapon.empty())
    {
        if (!_equip_weapon(weapon, abort))
        {
            int item = create_item_named("mundane not_cursed ident:all " + weapon,
                                         you.pos(), &error);
            if (item == NON_ITEM)
                return false;
            if (!move_item_to_inv(item, 1, true))
                return false;
            _equip_weapon(weapon, abort);
        }

        if (abort)
        {
            error = "Cannot wield weapon";
            return false;
        }
    }
    else if (you.weapon())
        unwield_item(false);

    you.wield_change = true;

    if (!missile.empty())
    {
        for (int i = 0; i < ENDOFPACK; ++i)
        {
            if (!you.inv[i].defined())
                continue;

            if (you.inv[i].name(DESC_PLAIN).find(missile) != string::npos)
            {
                quiver_item(i);
                you.redraw_quiver = true;
                break;
            }
        }
    }

    redraw_screen();
    return true;
}

// fight simulator internals
static monster* _init_fsim(monster_type mtype)
{
	monster * mon = nullptr;

	mgen_data temp = mgen_data::hostile_at(mtype, "fightsim", false, 0, 0,
		you.pos(), MG_DONT_COME);

	temp.extra_flags |= MF_HARD_RESET | MF_NO_REWARD;
	mon = create_monster(temp);
	if (!mon)
	{
		mpr("Failed to create monster.");
		return nullptr;
	}

	// move the monster next to the player
	// this probably works best in the arena, or at least somewhere
	// where there's no water or anything weird to interfere
	if (!adjacent(mon->pos(), you.pos()))
	{
		for (adjacent_iterator ai(you.pos()); ai; ++ai)
			if (mon->move_to_pos(*ai))
				break;
	}

	if (!adjacent(mon->pos(), you.pos()))
	{
		monster_die(mon, KILL_DISMISSED, NON_MONSTER);
		mpr("Could not put monster adjacent to player.");
		return 0;
	}

	// prevent distracted stabbing
	mon->foe = MHITYOU;
	// this line is actually kind of important for distortion now
	mon->hit_points = mon->max_hit_points = MAX_MONSTER_HP;
	mon->behaviour = BEH_SEEK;

	redraw_screen();

	return mon;
}

// fight simulator internals
static monster* _init_fsim()
{
    monster * mon = nullptr;
    monster_type mtype = get_monster_by_name(Options.fsim_mons, true);

    if (mtype == MONS_PROGRAM_BUG && monster_nearby())
    {
        // get a monster via targeting.
        dist moves;
        direction_chooser_args args;
        args.needs_path = false;
        args.top_prompt = "Select a monster, or hit Escape to use default.";
        direction(moves, args);
        if (monster_at(moves.target))
        {
            mon = clone_mons(monster_at(moves.target), true);
            if (mon)
                mon->flags |= MF_HARD_RESET | MF_NO_REWARD;
        }
    }

    if (!mon)
    {
        if (mtype == MONS_PROGRAM_BUG)
        {
            char specs[100];
            mprf(MSGCH_PROMPT, "Enter monster name (or MONS spec): ");
            if (cancellable_get_line_autohist(specs, sizeof specs) || !*specs)
            {
                canned_msg(MSG_OK);
                return nullptr;
            }
            mtype = get_monster_by_name(specs, true);

            // Wizmode users should be able to conjure up uniques even if they
            // were already created.
            if (mons_is_unique(mtype) && you.unique_creatures[mtype])
                you.unique_creatures.set(mtype, false);
        }

        mgen_data temp = mgen_data::hostile_at(mtype, "fightsim", false, 0, 0,
                                               you.pos(), MG_DONT_COME);

        temp.extra_flags |= MF_HARD_RESET | MF_NO_REWARD;
        mon = create_monster(temp);
        if (!mon)
        {
            mpr("Failed to create monster.");
            return nullptr;
        }
    }

    // move the monster next to the player
    // this probably works best in the arena, or at least somewhere
    // where there's no water or anything weird to interfere
    if (!adjacent(mon->pos(), you.pos()))
    {
        for (adjacent_iterator ai(you.pos()); ai; ++ai)
            if (mon->move_to_pos(*ai))
                break;
    }

    if (!adjacent(mon->pos(), you.pos()))
    {
        monster_die(mon, KILL_DISMISSED, NON_MONSTER);
        mpr("Could not put monster adjacent to player.");
        return 0;
    }

    // prevent distracted stabbing
    mon->foe = MHITYOU;
    // this line is actually kind of important for distortion now
    mon->hit_points = mon->max_hit_points = MAX_MONSTER_HP;
    mon->behaviour = BEH_SEEK;

    redraw_screen();

    return mon;
}

static void _uninit_fsim(monster *mon)
{
    monster_die(mon, KILL_DISMISSED, NON_MONSTER);
    reset_training();
}

static fight_data _get_fight_data(monster &mon, int iter_limit, bool defend)
{
    const monster orig = mon;
    unsigned int cumulative_damage = 0;
    unsigned int time_taken = 0;
    int hits = 0;
    fight_data fdata;
    fdata.max_dam = 0;

    const int weapon = you.equip[EQ_WEAPON];
    const item_def *iweap = weapon != -1 ? &you.inv[weapon] : nullptr;
    const int missile = you.m_quiver.get_fire_item();

    // now make sure the player is ready
    you.exp_available = 0;
    const int yhp  = you.hp;
    const int ymhp = you.hp_max;

    // disable death and delay, but make sure that these values
    // get reset when the function call ends
    unwind_var<FixedBitVector<NUM_DISABLEMENTS> > disabilities(crawl_state.disables);
    crawl_state.disables.set(DIS_DEATH);
    crawl_state.disables.set(DIS_DELAY);
    crawl_state.disables.set(DIS_AFFLICTIONS);

    no_messages mx;
    const int hunger = you.hunger;

    const coord_def start_pos = mon.pos();
    const coord_def you_start_pos = you.pos();

    if (!defend) // you're the attacker
    {
        for (int i = 0; i < iter_limit; i++)
        {
            // Don't reset the monster while it is constricted.
            you.stop_constricting(mon.mid, false, true);
            // This sets mgrid(mons.pos()) to NON_MONSTER
            mon = orig;
            // Re-place the combatants if they e.g. blinked away or were
            // trampled.
            mon.move_to_pos(start_pos);
            you.move_to_pos(you_start_pos);
            mon.hit_points = mon.max_hit_points;
            mon.shield_blocks = 0;
            you.time_taken = player_speed();

            // first, ranged weapons. note: this includes
            // being empty-handed but having a missile quivered
            if ((iweap && iweap->base_type == OBJ_WEAPONS &&
                        is_range_weapon(*iweap))
                || (!iweap && missile != -1))
            {
                ranged_attack attk(&you, &mon, &you.inv[missile], false);
                attk.simu = true;
                attk.attack();
                if (attk.ev_margin >= 0)
                    hits++;
                you.time_taken = you.attack_delay(you.weapon(),
                                                  &you.inv[missile]);
            }
            else // otherwise, melee combat
            {
                bool did_hit = false;
                fight_melee(&you, &mon, &did_hit, true);
                if (did_hit)
                    hits++;
            }
            you.hunger = hunger;
            time_taken += you.time_taken * 10;

            int damage = (mon.max_hit_points - mon.hit_points);
            cumulative_damage += damage;
            if (damage > fdata.max_dam)
                fdata.max_dam = damage;
        }
    }
    else // you're defending
    {
        for (int i = 0; i < iter_limit; i++)
        {
            you.hp = you.hp_max = 999; // again, arbitrary
            bool did_hit = false;
            you.shield_blocks = 0; // no blocks this round
            fight_melee(&mon, &you, &did_hit, true);

            time_taken += 1000 / (mon.speed ? mon.speed : 10);

            int damage = you.hp_max - you.hp;
            if (did_hit)
                hits++;
            cumulative_damage += damage;
            if (damage > fdata.max_dam)
                fdata.max_dam = damage;

            // Re-place the combatants if they e.g. blinked away or were
            // trampled.
            mon.move_to_pos(start_pos);
            you.move_to_pos(you_start_pos);
        }
        you.hp = yhp;
        you.hp_max = ymhp;
    }

    fdata.av_hit_dam = hits ? double(cumulative_damage) / hits : 0.0;
    fdata.accuracy = 100 * hits / iter_limit;
    fdata.av_dam = double(cumulative_damage) / iter_limit;
    fdata.av_time = double(time_taken) / iter_limit + 0.5; // round to nearest
    fdata.av_speed = double(iter_limit) * 100 / time_taken;
    fdata.av_eff_dam = fdata.av_dam * 100 / fdata.av_time;

    return fdata;
}

// this is the skeletal simulator call, and the one that's easily accessed
void wizard_quick_fsim()
{
    // we could declare this in the fight calls, but i'm worried that
    // the actual monsters that are made will be slightly different,
    // so it's safer to do it here.
    monster *mon = _init_fsim();
    if (!mon)
        return;

    const int iter_limit = Options.fsim_rounds;
    fight_data fdata = _get_fight_data(*mon, iter_limit, false);
    mprf("           %s\nAttacking: %s", _title_line,
         _fight_string(fdata, false).c_str());

    fdata = _get_fight_data(*mon, iter_limit, true);
    mprf("Defending: %s", _fight_string(fdata, false).c_str());

    _uninit_fsim(mon);
    return;
}

static string _init_scale(skill_map &scale, bool &xl_mode)
{
    string ret;

    for (string sk_str : Options.fsim_scale)
    {
        if (sk_str == "xl")
        {
            xl_mode = true;
            ret = "XL";
            continue;
        }

        int divider = 1;
        skill_type sk;

        string::size_type sep = sk_str.find("/");
        if (sep == string::npos)
            sep = sk_str.find(":");
        if (sep != string::npos)
        {
            string divider_str = sk_str.substr(sep + 1);
            sk_str = sk_str.substr(0, sep);
            trim_string(sk_str);
            trim_string(divider_str);
            divider = atoi(divider_str.c_str());
        }

        if (sk_str == "weapon")
            sk = _equipped_skill();
        else
            sk = skill_from_name(sk_str.c_str());

        scale[sk] = divider;
        if (divider == 1 && ret.empty())
            ret = skill_name(sk);
    }

    if (xl_mode)
    {
        you.training.init(0);
        for (const auto &entry : scale)
            you.training[entry.first] = entry.second;
    }

    return ret;
}

static void _fsim_simple_scale(FILE * o, monster* mon, bool defense)
{
    skill_map scale;
    bool xl_mode = false;
    string col_name;

    if (Options.fsim_scale.empty())
    {
        skill_type sk = defense ? SK_ARMOUR : _equipped_skill();
        scale[sk] = 1;
        col_name = skill_name(sk);
    }
    else
        col_name = _init_scale(scale, xl_mode);

    const string title = make_stringf("%10.10s | %s", col_name.c_str(),
                                      _title_line);
    if (Options.fsim_csv)
        fprintf(o, "%s\t%s\n", col_name.c_str(), _csv_title_line);
    else
        fprintf(o, "%s\n", title.c_str());

    mpr(title);

    const int iter_limit = Options.fsim_rounds;
    for (int i = xl_mode ? 1 : 0; i <= 27; i++)
    {
        clear_messages();

        if (xl_mode)
            set_xl(i, true);
        else
        {
            for (const auto &entry : scale)
                set_skill_level(entry.first, i / entry.second);
        }

        fight_data fdata = _get_fight_data(*mon, iter_limit, defense);
        const string line = make_stringf("        %2d | %s", i,
                                         _fight_string(fdata, false).c_str());
        mpr(line);
        if (Options.fsim_csv)
            fprintf(o, "%d\t%s\n", i, _fight_string(fdata, true).c_str());
        else
            fprintf(o, "%s\n", line.c_str());
        fflush(o);

        // kill the loop if the user hits escape
        if (kbhit() && getchk() == 27)
        {
            mpr("Cancelling simulation.\n");
            fprintf(o, "Simulation cancelled!\n\n");
            break;
        }
    }
}

static void _fsim_double_scale(FILE * o, monster* mon, bool defense)
{
    skill_type skx, sky;
    if (defense)
    {
        skx = SK_ARMOUR;
        sky = SK_DODGING;
    }
    else
    {
        skx = SK_FIGHTING;
        sky = _equipped_skill();
    }

    fprintf(o, "%s(x) vs %s(y)\n", skill_name(skx), skill_name(sky));
    fprintf(o, Options.fsim_csv ? "\t" : "  ");
    for (int y = 1; y <= 27; y += 2)
        fprintf(o,Options.fsim_csv ? "%d\t" : "   %2d", y);

    fprintf(o,"\n");

    const int iter_limit = Options.fsim_rounds;
    for (int y = 1; y <= 27; y += 2)
    {
        fprintf(o, Options.fsim_csv ? "%d\t" : "%2d", y);
        for (int x = 1; x <= 27; x += 2)
        {
            clear_messages();
            set_skill_level(skx, x);
            set_skill_level(sky, y);
            fight_data fdata = _get_fight_data(*mon, iter_limit, defense);
            mprf("%s %d, %s %d: %d", skill_name(skx), x, skill_name(sky), y,
                 int(fdata.av_eff_dam));
            fprintf(o,Options.fsim_csv ? "%.1f\t" : "%5.1f", fdata.av_eff_dam);
            fflush(o);

            // kill the loop if the user hits escape
            if (kbhit() && getchk() == 27)
            {
                mpr("Cancelling simulation.\n");
                fprintf(o, "\nSimulation cancelled!\n\n");
                return;
            }
        }
        fprintf(o,"\n");
    }
}

void wizard_fight_sim(bool double_scale)
{
    monster * mon = _init_fsim();
    if (!mon)
        return;

    bool defense = false;
    const char * fightstat = Options.fsim_csv ? "fsim.csv" : "fsim.txt";

    FILE * o = fopen(fightstat, "a");
    if (!o)
    {
        mprf(MSGCH_ERROR, "Can't write %s: %s", fightstat, strerror(errno));
        _uninit_fsim(mon);
        return;
    }

    if (Options.fsim_mode.find("defen") != string::npos)
        defense = true;
    else if (Options.fsim_mode.find("attack") != string::npos
             || Options.fsim_mode.find("offen") != string::npos)
    {
        defense = false;
    }
    else
    {
        mprf(MSGCH_PROMPT, "(A)ttack or (D)efense?");

        switch (toalower(getchk()))
        {
        case 'a':
        case 'A':
            defense = false;
            break;
        case 'd':
        case 'D':
            defense = true;
            break;
        default:
            canned_msg(MSG_OK);
            _uninit_fsim(mon);
            return;
        }
    }

    _write_version(o);
    _write_matchup(o, *mon, defense, Options.fsim_rounds);
    _write_you(o);
    _write_weapon(o);
    _write_mon(o, *mon);
    fprintf(o,"\n");

    skill_state skill_backup;
    skill_backup.save();
    int xl = you.experience_level;

    // disable death and delay, but make sure that these values
    // get reset when the function call ends
    unwind_var<FixedBitVector<NUM_DISABLEMENTS> > disabilities(crawl_state.disables);
    crawl_state.disables.set(DIS_DEATH);
    crawl_state.disables.set(DIS_DELAY);

    void (*fsim_proc)(FILE * o, monster* mon, bool defense) = nullptr;
    fsim_proc = double_scale ? _fsim_double_scale : _fsim_simple_scale;

    if (Options.fsim_kit.empty())
        fsim_proc(o, mon, defense);
    else
        for (const string &kit : Options.fsim_kit)
        {
            string error;
            if (_fsim_kit_equip(kit, error))
            {
                _write_weapon(o);
                fsim_proc(o, mon, defense);
                fprintf(o, "\n");
            }
            else
            {
                mprf("Aborting sim on %s", kit.c_str());
                if (error != "")
                    mpr(error);
                break;
            }
        }

    if (!Options.fsim_csv)
        fprintf(o, "-----------------------------------\n\n");
    fclose(o);

    skill_backup.restore_levels();
    skill_backup.restore_training();
    if (you.experience_level != xl)
        set_xl(xl, false);

    _uninit_fsim(mon);
    mpr("Done.");
}

#endif

double _test_weapon_against_monster(const item_def *wep, monster_type mon, int iterations,
	                               int projectile_slot, double &time_taken, double &brand_damage)
{
	const monsterentry *data = get_monster_data(mon);
	const item_def *projectile = nullptr;
	unique_ptr<attack> attk_ptr;
	seed_rng(27);
	if (is_range_weapon(*wep) && projectile_slot != -1)
		projectile = &you.inv[projectile_slot];
	int ac = data->AC;
	int ev = data->ev;
	int damage = 0;
	int hits = 0;
	brand_damage = 0;
	time_taken = 0;
	if (is_range_weapon(*wep) || is_throwable(&you, *wep))
	{
		// Treat the projectile as the weapon if it is a throwable item
		if (is_throwable(&you, *wep))
		{
			projectile = wep;
			wep = nullptr;
		}
		attk_ptr.reset(new ranged_attack(&you, &you, (item_def*)projectile, false));
	}
	else
		attk_ptr.reset(new melee_attack(&you,nullptr));
	for (int i = 0; i < iterations; i++)
	{
		int to_hit = attk_ptr->calc_to_hit(true);
		int tmp_damage = attk_ptr->calc_raw_damage(ac);
		attk_ptr->damage_done = tmp_damage;
		int spec_damage = attk_ptr->calc_brand_damage();
		time_taken += you.attack_delay(wep, projectile);
		if (attk_ptr->test_hit(to_hit, ev, false) >= 0)
		{
			hits++;
			if (spec_damage > 0 && wep->brand != SPWPN_VORPAL ||
				(projectile != nullptr && projectile->brand != SPMSL_STEEL
				&& projectile->brand != SPMSL_NORMAL))
				brand_damage += spec_damage;
			else
				tmp_damage += spec_damage;
			damage += tmp_damage;
		}
	}
	time_taken /= (10 * iterations);
	double average_damage = ((double) damage/iterations)/time_taken;
	brand_damage = ((double)brand_damage / iterations) / time_taken;
	seed_rng();
	return average_damage;
}

string weapon_sim(const item_def &item, int slot)
{
	if (!you.can_wield(item))
		return "";
	const int iterations = 250;
	int projectile_slot = -1;
	double time_taken = 0.0f;
	double brand_damage = 0.0f;

	const string invalid_ammo_str = 
		"\n\nDamage estimates for ranged weapons require valid ammo.\n\n";

	const string short_header = "\n\nDamage Estimates   Basic";
	const string long_header = short_header + "      Resistable";

	// Abort if the item isn't actually in our inventory or identified
	if (!in_inventory(item) || !fully_identified(item))
		return "";

    // Also if its not a valid throwing weapons
	if (item.base_type == OBJ_MISSILES && !is_throwable(&you, item))
		return "\n\nTo view damage estimates for ammo,"
		"quiver it and select the corresponding weapon.";

	const unsigned int indent_length[2] = { 24, 16 };

	const vector<monster_type> test_mons = { MONS_NO_DEFENSE_TEST, 
	MONS_EV_TEST,  MONS_AC_TEST, MONS_DEFENSE_TEST };

	string output_str = "";
	const brand_type brand = get_weapon_brand(item);

	// Make sure we restore the original weapon on exiting; this overrides
	// curses etc in order to allow for viewing stats at any point
	unwind_var<int8_t> orig_weapon_slot(you.equip[EQ_WEAPON], (int8_t) slot);

	if (is_range_weapon(item))
	{
		item_def *item_out = nullptr;
		vector<int> order;
		you.m_quiver.get_fire_order(order, false);
		//Nothing to fire, abort
		if (order.size() == 0)
			return invalid_ammo_str;
		projectile_slot = you.m_quiver.get_fire_item();
		item_out = &you.inv[projectile_slot];
		if (is_launched(&you, &item, *item_out) != LRET_LAUNCHED)
		{
			for (int order_slot : order)
			{
				item_out = &you.inv[order_slot];
				if (is_launched(&you, &item, *item_out) == LRET_LAUNCHED)
				{
					projectile_slot = order_slot;
					break;
				}
			}
		}
		// No missile to be fired, nothing to be done here other than
		// let the user know
		if (projectile_slot == -1 || is_launched(&you, &item, *item_out) != LRET_LAUNCHED)
			return invalid_ammo_str;
		output_str = make_stringf("\n\nUsing %s as ammo.", item_out->name(DESC_PLAIN).c_str());
	}

	special_missile_type ammo = SPMSL_NORMAL;
	if (projectile_slot != -1)
		ammo = get_ammo_brand(you.inv[projectile_slot]);

	// All of these brands do resistable damage
	bool do_resistable = (brand == SPWPN_FLAMING || brand == SPWPN_FREEZING ||
		brand == SPWPN_HOLY_WRATH || brand == SPWPN_PAIN || brand == SPWPN_ELECTROCUTION || brand == SPWPN_CHAOS
		|| brand == SPWPN_FROST || brand == SPWPN_FLAME || ammo == SPMSL_FLAME || ammo == SPMSL_FROST ||
		ammo == SPMSL_CHAOS || item.base_type == OBJ_STAVES && (item.sub_type == STAFF_AIR || item.sub_type
		== STAFF_COLD || item.sub_type == STAFF_DEATH || item.sub_type == STAFF_FIRE));
	if (do_resistable)
		output_str += long_header + "\n";
	else
		output_str += short_header + "\n";
		
	for (monster_type mt : test_mons) {
		monsterentry *mon = get_monster_data(mt);
		string tmp_str, dmg_str;
		double damage = _test_weapon_against_monster(&item, mt,
		    iterations, projectile_slot, time_taken, brand_damage);
		tmp_str = mon->name;
		dmg_str = make_stringf("%.1f", damage);
		// All of the string handling is kind of a mess due to tabs
		// not working in the description window, manually append
		// the necessary number of spaces in order to align properly
		while (tmp_str.length() + dmg_str.length() < indent_length[0])
			tmp_str.append(" ");
		output_str.append(tmp_str);
		output_str.append(dmg_str);
		if (do_resistable)
		{
			tmp_str = "";
			dmg_str = make_stringf("%.1f", brand_damage);
			while (tmp_str.length() + dmg_str.length() < indent_length[1])
				tmp_str.append(" ");
			output_str.append(tmp_str + dmg_str);
		}
		output_str.append("\n");
	}
	output_str.append(make_stringf("\nAverage attack time: %.2f\n", time_taken));
	// Reseed the rng since it will always be in the same state
	seed_rng();
	return output_str;
}