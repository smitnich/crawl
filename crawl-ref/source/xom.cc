/*
 *  File:       xom.cc
 *  Summary:    All things Xomly
 *  Written by: Zooko
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"

#include <algorithm>

#include "beam.h"
#include "branch.h"
#include "database.h"
#include "effects.h"
#include "it_use2.h"
#include "items.h"
#include "makeitem.h"
#include "message.h"
#include "misc.h"
#include "mon-util.h"
#include "monplace.h"
#include "monstuff.h"
#include "mutation.h"
#include "ouch.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "spells2.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "view.h"
#include "xom.h"

#if DEBUG_RELIGION
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_GIFTS       1
#endif

#if DEBUG_XOM
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_RELIGION    1
#    define DEBUG_GIFTS       1
#endif

// Which spells?  First I copied all spells from your_spells(), and then
// I filtered some out, especially conjurations.  Then I sorted them in
// roughly ascending order of power.
static const spell_type _xom_spells[] =
{
    SPELL_BLINK, SPELL_CONFUSING_TOUCH, SPELL_MAGIC_MAPPING,
    SPELL_DETECT_ITEMS, SPELL_DETECT_CREATURES, SPELL_MASS_CONFUSION,
    SPELL_MASS_SLEEP, SPELL_DISPERSAL, SPELL_STONESKIN, SPELL_RING_OF_FLAMES,
    SPELL_OLGREBS_TOXIC_RADIANCE, SPELL_TUKIMAS_VORPAL_BLADE,
    SPELL_MAXWELLS_SILVER_HAMMER, SPELL_FIRE_BRAND, SPELL_FREEZING_AURA,
    SPELL_POISON_WEAPON, SPELL_STONEMAIL, SPELL_LETHAL_INFUSION,
    SPELL_EXCRUCIATING_WOUNDS, SPELL_WARP_BRAND, SPELL_TUKIMAS_DANCE,
    SPELL_RECALL, SPELL_SUMMON_BUTTERFLIES, SPELL_SUMMON_SMALL_MAMMALS,
    SPELL_SUMMON_SCORPIONS, SPELL_SUMMON_SWARM, SPELL_FLY, SPELL_SPIDER_FORM,
    SPELL_STATUE_FORM, SPELL_ICE_FORM, SPELL_DRAGON_FORM, SPELL_ANIMATE_DEAD,
    SPELL_SUMMON_WRAITHS, SPELL_SHADOW_CREATURES, SPELL_SUMMON_HORRIBLE_THINGS,
    SPELL_CALL_CANINE_FAMILIAR, SPELL_SUMMON_ICE_BEAST, SPELL_SUMMON_UGLY_THING,
    SPELL_CONJURE_BALL_LIGHTNING, SPELL_SUMMON_DRAGON, SPELL_DEATH_CHANNEL,
    SPELL_NECROMUTATION
};

static const char *_xom_message_arrays[NUM_XOM_MESSAGE_TYPES][6] =
{
    // XM_NORMAL
    {
        "Xom roars with laughter!",
        "Xom thinks this is hilarious!",
        "Xom is highly amused!",
        "Xom is amused.",
        "Xom is mildly amused.",
        "Xom is interested."
    },

    // XM_INTRIGUED
    {
        "Xom is fascinated!",
        "Xom is very intrigued!",
        "Xom is intrigued!",
        "Xom is extremely interested.",
        "Xom is very interested.",
        "Xom is interested."
    }
};

const char *describe_xom_favour()
{
    return (you.piety > 160) ? "A beloved toy of Xom." :
           (you.piety > 145) ? "A favourite toy of Xom." :
           (you.piety > 130) ? "A very special toy of Xom." :
           (you.piety > 115) ? "A special toy of Xom." :
           (you.piety > 100) ? "A toy of Xom." :
           (you.piety >  85) ? "A plaything of Xom." :
           (you.piety >  70) ? "A special plaything of Xom." :
           (you.piety >  55) ? "A very special plaything of Xom." :
           (you.piety >  40) ? "A favourite plaything of Xom."
                             : "A beloved plaything of Xom.";
}

static std::string _get_xom_speech(const std::string key)
{
    std::string result = getSpeakString("Xom " + key);

    if (result.empty())
        result = getSpeakString("Xom general effect");

    if (result.empty())
        return ("Xom makes something happen.");

    return (result);
}

bool xom_is_nice()
{
    // If you.gift_timeout was 0, then Xom was BORED.  He HATES that.
    return (you.gift_timeout > 0 && you.piety > (MAX_PIETY / 2)) || coinflip();
}

static void _xom_is_stimulated(int maxinterestingness,
                               const char *message_array[],
                               bool force_message)
{
    if (you.religion != GOD_XOM || maxinterestingness <= 0)
        return;

    // Xom is not directly stimulated by his own acts.
    if (crawl_state.which_god_acting() == GOD_XOM)
        return;

    int interestingness = random2(maxinterestingness);

#if DEBUG_RELIGION || DEBUG_GIFTS || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "Xom: maxinterestingness = %d, interestingness = %d",
         maxinterestingness, interestingness);
#endif

    interestingness = std::min(255, interestingness);

    bool was_stimulated = false;
    if (interestingness > you.gift_timeout && interestingness >= 12)
    {
        you.gift_timeout = interestingness;
        was_stimulated = true;
    }

    if (was_stimulated || force_message)
        god_speaks(GOD_XOM,
                   ((interestingness > 200) ? message_array[5] :
                    (interestingness > 100) ? message_array[4] :
                    (interestingness >  75) ? message_array[3] :
                    (interestingness >  50) ? message_array[2] :
                    (interestingness >  25) ? message_array[1]
                                            : message_array[0]));
}

void xom_is_stimulated(int maxinterestingness, xom_message_type message_type,
                       bool force_message)
{
    _xom_is_stimulated(maxinterestingness, _xom_message_arrays[message_type],
                       force_message);
}

void xom_is_stimulated(int maxinterestingness, const std::string& message,
                       bool force_message)
{
    const char *message_array[6];

    for (int i = 0; i < 6; ++i)
        message_array[i] = message.c_str();

    _xom_is_stimulated(maxinterestingness, message_array, force_message);
}

static void _xom_makes_you_cast_random_spell(int sever)
{
    int spellenum = sever;

    god_acting gdact(GOD_XOM);

    const int nxomspells = ARRAYSZ(_xom_spells);
    spellenum = std::min(nxomspells, spellenum);

    const spell_type spell = _xom_spells[random2(spellenum)];

    god_speaks(GOD_XOM, _get_xom_speech("spell effect").c_str());

#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "_xom_makes_you_cast_random_spell(); spell: %d, spellenum: %d",
         spell, spellenum);
#endif

    your_spells(spell, sever, false);
}

static void _xom_make_item(object_class_type base, int subtype, int power)
{
    int thing_created =
        items(true, base, subtype, true, power, MAKE_ITEM_RANDOM_RACE);

    if (thing_created == NON_ITEM)
    {
        god_speaks(GOD_XOM, "\"No, never mind.\"");
        return;
    }

    god_acting gdact(GOD_XOM);

    move_item_to_grid(&thing_created, you.x_pos, you.y_pos);
    mitm[thing_created].inscription = "god gift";
    canned_msg(MSG_SOMETHING_APPEARS);
    stop_running();

    origin_acquired(mitm[thing_created], GOD_XOM);
}

static object_class_type _get_unrelated_wield_class(object_class_type ref)
{
    object_class_type objtype = OBJ_WEAPONS;
    if (ref == OBJ_WEAPONS)
    {
        if (random2(10))
            objtype = OBJ_STAVES;
        else
            objtype = OBJ_MISCELLANY;
    }
    else if (ref == OBJ_STAVES)
    {
        if (random2(10))
            objtype = OBJ_WEAPONS;
        else
            objtype = OBJ_MISCELLANY;
    }
    else
    {
        const int temp_rand = random2(3);
        objtype = (temp_rand == 0) ? OBJ_WEAPONS :
                  (temp_rand == 1) ? OBJ_STAVES
                                   : OBJ_MISCELLANY;
    }

    return (objtype);
}

static bool _xom_annoyance_gift(int power)
{
    god_acting gdact(GOD_XOM);

    if (coinflip() && player_in_a_dangerous_place())
    {
        const item_def *weapon = you.weapon();

        // Xom has a sense of humour.
        if (coinflip() && weapon && weapon->cursed())
        {
            // If you are wielding a cursed item then Xom will give you
            // an item of that same type.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            if (coinflip())
                // For added humour, give the same sub-type.
                _xom_make_item(weapon->base_type, weapon->sub_type, power * 3);
            else
                acquirement(weapon->base_type, GOD_XOM);
            return (true);
        }

        const item_def *gloves = you.slot_item(EQ_GLOVES);
        if (coinflip() && gloves && gloves->cursed())
        {
            // If you are wearing cursed gloves, then Xom will give you
            // a ring.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        };

        const item_def *amulet = you.slot_item(EQ_AMULET);
        if (coinflip() && amulet && amulet->cursed())
        {
            // If you are wearing a cursed amulet, then Xom will give
            // you an amulet.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_amulet_type(), power * 3);
            return (true);
        };

        const item_def *left_ring = you.slot_item(EQ_LEFT_RING);
        const item_def *right_ring = you.slot_item(EQ_RIGHT_RING);
        if (coinflip() && ((left_ring && left_ring->cursed())
                           || (right_ring && right_ring->cursed())))
        {
            // If you are wearing a cursed ring, then Xom will give you
            // a ring.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("ring gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        }

        if (one_chance_in(5) && weapon)
        {
            // Xom will give you a wielded item of a type different from
            // what you are currently wielding.
            god_speaks(GOD_XOM, _get_xom_speech("weapon gift").c_str());

            const object_class_type objtype =
                _get_unrelated_wield_class(weapon->base_type);

            if (power > random2(256))
                acquirement(objtype, GOD_XOM);
            else
                _xom_make_item(objtype, OBJ_RANDOM, power * 3);
            return (true);
        }
    }

    return (false);
}

static bool _xom_gives_item(int power)
{
    if (_xom_annoyance_gift(power))
        return (true);

    const item_def *cloak = you.slot_item(EQ_CLOAK);
    if (coinflip() && cloak && cloak->cursed())
    {
        // If you are wearing a cursed cloak, then Xom will give you a
        // cloak or body armour.  Ha ha!
        god_speaks(GOD_XOM, _get_xom_speech("armour gift").c_str());
        _xom_make_item(OBJ_ARMOUR,
                       random2(10) ?
                           get_random_body_armour_type(you.your_level * 2)
                       : ARM_CLOAK,
                       power * 3);
        return (true);
    }

    god_speaks(GOD_XOM, _get_xom_speech("general gift").c_str());

    // There are two kinds of Xom gifts: acquirement and random object.
    // The result from acquirement is very good (usually as good or
    // better than random object), and it is sometimes tuned to the
    // player's skills and nature.  Being tuned to the player's skills
    // and nature is not very Xomlike...
    if (power > random2(256))
    {
        // Random-type acquirement.
        const int r = random2(7);
        const object_class_type objtype = (r == 0) ? OBJ_WEAPONS :
                                          (r == 1) ? OBJ_ARMOUR :
                                          (r == 2) ? OBJ_JEWELLERY :
                                          (r == 3) ? OBJ_BOOKS :
                                          (r == 4) ? OBJ_STAVES :
                                          (r == 5) ? OBJ_FOOD :
                                          (r == 6) ? OBJ_MISCELLANY
                                                   : OBJ_GOLD;

        god_acting gdact(GOD_XOM);

        acquirement(objtype, GOD_XOM);
    }
    else
    {
        // Random-type random object.
        _xom_make_item(OBJ_RANDOM, OBJ_RANDOM, power * 3);
    }

    more();

    return (true);
}

static bool _choose_mutatable_monster(const monsters* mon)
{
    return (mon->alive() && mon->can_safely_mutate()
            && !mons_is_submerged(mon));
}

static monster_type _xom_random_demon(int sever, bool use_greater_demons = true)
{
    const int roll = random2(1000 - (27 - you.experience_level) * 10);
#ifdef DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "_xom_random_demon(); sever = %d, roll: %d",
         sever, roll);
#endif
    const demon_class_type dct =
        (roll >= 850) ? DEMON_GREATER :
        (roll >= 340) ? DEMON_COMMON
                      : DEMON_LESSER;

    monster_type demon = MONS_PROGRAM_BUG;

    // Sometimes, send a holy being instead.
    if (dct == DEMON_GREATER && coinflip())
        demon = summon_any_holy_being(HOLY_BEING_RANDOM);
    else
        demon = summon_any_demon(
            (use_greater_demons || dct != DEMON_GREATER) ? dct : DEMON_COMMON);

    return (demon);
}

// Returns a demon suitable for use in Xom's punishments, filtering out
// the really nasty ones early on.
static monster_type _xom_random_punishment_demon(int sever)
{
    monster_type demon = MONS_PROGRAM_BUG;

    do
        demon = _xom_random_demon(sever);
    while ((demon == MONS_HELLION
            && you.experience_level < 12
            && !one_chance_in(3 + (12 - you.experience_level) / 2)));

    return (demon);
}

// The nicer stuff (note: these things are not necessarily nice).
static bool _xom_is_good(int sever)
{
    bool done = false;

    god_acting gdact(GOD_XOM);

    // This series of random calls produces a poisson-looking
    // distribution: initial hump, plus a long-ish tail.

    if (random2(sever) <= 1)
    {
        potion_type pot =
            static_cast<potion_type>(
                random_choose(POT_HEALING, POT_HEAL_WOUNDS, POT_SPEED,
                              POT_MIGHT, POT_INVISIBILITY, POT_BERSERK_RAGE,
                              POT_EXPERIENCE, -1));

        // Downplay this one a bit.
        if (pot == POT_EXPERIENCE && !one_chance_in(6))
            pot = POT_BERSERK_RAGE;

        if (pot != POT_BERSERK_RAGE || you.can_go_berserk(false))
        {
            god_speaks(GOD_XOM, _get_xom_speech("potion effect").c_str());

            if (pot == POT_BERSERK_RAGE)
                you.berserk_penalty = NO_BERSERK_PENALTY;

            potion_effect(pot, 150);

            done = true;
        }
    }
    else if (random2(sever) <= 2)
    {
        _xom_makes_you_cast_random_spell(sever);

        done = true;
    }
    else if (random2(sever) <= 3)
    {
        const int numdemons =
            std::min(random2(random2(random2(sever+1)+1)+1)+2, 16);
        int numdifferent = 0;

        // If we have a mix of demons and non-demons, there's a chance
        // that one or both of the factions may be hostile.
        int hostile = random2(12);
        int hostiletype =
            (hostile <  3) ? 0 :                  //  1/4: both are friendly
            (hostile < 11) ? (coinflip() ? 1 : 2) //  2/3: one is hostile
                           : 3;                   // 1/12: both are hostile

        bool *is_demonic = new bool[numdemons];
        int *summons = new int[numdemons];

        bool success = false;

        for (int i = 0; i < numdemons; ++i)
        {
            monster_type monster = _xom_random_demon(sever);

            is_demonic[i] = (mons_class_holiness(monster) == MH_DEMONIC);

            // If it's not a demon, Xom got it someplace else, so use
            // different messages below.
            if (!is_demonic[i])
                numdifferent++;

            summons[i] =
                create_monster(
                    mgen_data(monster, BEH_FRIENDLY, 3,
                        you.pos(), you.pet_target, MG_FORCE_BEH, GOD_XOM));

            if (summons[i] != -1)
                success = true;
        }

        if (success)
        {
            if (numdifferent == numdemons)
                god_speaks(GOD_XOM, _get_xom_speech("multiple holy summons").c_str());
            else if (numdifferent > 0)
                god_speaks(GOD_XOM, _get_xom_speech("multiple mixed summons").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("multiple summons").c_str());

            // If we have only non-demons, there's a chance that they
            // may be hostile.
            if (numdifferent == numdemons && one_chance_in(4))
                hostiletype = 2;
            // If we have only demons, they'll always be friendly.
            else if (numdifferent == 0)
                hostiletype = 0;

            for (int i = 0; i < numdemons; ++i)
            {
                monsters *mon = &menv[i];

                if (hostiletype != 0)
                {
                    // Mark factions hostile as appropriate.
                    if (hostiletype == 3
                        || (is_demonic[i] && hostiletype == 1)
                        || (!is_demonic[i] && hostiletype == 2))
                    {
                        mon->attitude = ATT_HOSTILE;
                        behaviour_event(mon, ME_ALERT, MHITYOU);
                    }
                }

                player_angers_monster(mon);
            }

            done = true;
        }

        delete[] is_demonic;
        delete[] summons;
    }
    else if (random2(sever) <= 4)
    {
        // This can fail with radius 1, or in open areas.
        if (vitrify_area(random2avg(sever / 2, 3) + 1))
        {
            god_speaks(GOD_XOM, _get_xom_speech("vitrification").c_str());

            done = true;
        }
    }
    else if (random2(sever) <= 5)
    {
        _xom_gives_item(sever);

        done = true;
    }
    else if (random2(sever) <= 6)
    {
        monster_type mon = _xom_random_demon(sever);
        const bool is_demonic = (mons_class_holiness(mon) == MH_DEMONIC);

        // If we have a non-demon, Xom got it someplace else, so use
        // different messages below.
        bool different = !is_demonic;

        beh_type beha = BEH_FRIENDLY;
        unsigned short hitting = you.pet_target;

        // There's a chance that a non-demon may be hostile.
        if (different && one_chance_in(4))
        {
            beha = BEH_HOSTILE;
            hitting = MHITYOU;
        }

        int summons = create_monster(
                          mgen_data(mon, beha, 6,
                              you.pos(), hitting, MG_FORCE_BEH, GOD_XOM));

        if (summons != -1)
        {
            if (different)
                god_speaks(GOD_XOM, _get_xom_speech("single holy summon").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("single summon").c_str());

            player_angers_monster(&menv[summons]);

            done = true;
        }
    }
    else if (random2(sever) <= 7)
    {
        if (there_are_monsters_nearby())
        {
            monsters *mon =
                choose_random_nearby_monster(0, _choose_mutatable_monster);

            if (mon)
            {
                god_speaks(GOD_XOM, _get_xom_speech("good monster polymorph").c_str());

                monster_polymorph(mon, RANDOM_MONSTER,
                    mons_wont_attack(mon) ? PPT_MORE : PPT_LESS);

                if (one_chance_in(8)
                    && !mon->has_ench(
                            ENCH_GLOWING_SHAPESHIFTER, ENCH_SHAPESHIFTER))
                {
                    mon->add_ench(one_chance_in(3) ?
                        ENCH_GLOWING_SHAPESHIFTER : ENCH_SHAPESHIFTER);
                }

                done = true;
            }
        }
    }
    else if (random2(sever) <= 8)
    {
        _xom_gives_item(sever);

        done = true;
    }
    else if (random2(sever) <= 9)
    {
        if (you.can_safely_mutate()
            && player_mutation_level(MUT_MUTATION_RESISTANCE) < 3)
        {
            god_speaks(GOD_XOM, _get_xom_speech("good mutations").c_str());

            mpr("Your body is suffused with distortional energy.");

            set_hp(1 + random2(you.hp), false);
            deflate_hp(you.hp_max / 2, true);

            bool failMsg = true;

            for (int i = random2(4); i >= 0; --i)
            {
                if (mutate(RANDOM_GOOD_MUTATION, failMsg, false, true))
                    done = true;
                else
                    failMsg = false;
            }
        }
    }
    else if (random2(sever) <= 10)
    {
        monster_type mon = _xom_random_demon(sever);
        const bool is_demonic = (mons_class_holiness(mon) == MH_DEMONIC);

        // If we have a non-demon, Xom got it someplace else, so use
        // different messages below.
        bool different = !is_demonic;

        beh_type beha = BEH_FRIENDLY;
        unsigned short hitting = you.pet_target;

        // There's a chance that a non-demon may be hostile.
        if (different && one_chance_in(4))
        {
            beha = BEH_HOSTILE;
            hitting = MHITYOU;
        }

        int summons = create_monster(
                          mgen_data(_xom_random_demon(sever, one_chance_in(8)),
                              beha, 0,
                              you.pos(), hitting, MG_FORCE_BEH, GOD_XOM));

        if (summons != -1)
        {
            if (different)
                god_speaks(GOD_XOM, _get_xom_speech("single major holy summon").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("single major demon summon").c_str());

            player_angers_monster(&menv[summons]);

            done = true;
        }
    }
    else if (random2(sever) <= 11)
    {
        if (you.hp <= random2(201) && player_in_a_dangerous_place())
            you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 1;

        god_speaks(GOD_XOM, "The area is suffused with divine lightning!");

        bolt beam;
        beam.beam_source  = NON_MONSTER;
        beam.type         = dchar_glyph(DCHAR_FIRED_BURST);
        beam.damage       = dice_def(3, 30);
        beam.flavour      = BEAM_ELECTRICITY;
        beam.target_x     = you.x_pos;
        beam.target_y     = you.y_pos;
        beam.name         = "blast of lightning";
        beam.colour       = LIGHTCYAN;
        beam.thrower      = KILL_MISC;
        beam.aux_source   = "Xom's lightning strike";
        beam.ex_size      = 2;
        beam.is_tracer    = false;
        beam.is_explosion = true;
        explosion(beam);

        if (you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION])
        {
            mpr("Your divine protection wanes.");
            you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 0;
        }

        done = true;
    }

    return (done);
}

static bool _xom_is_bad(int sever)
{
    bool done = false;

    god_acting gdact(GOD_XOM);

    while (!done)
    {
        if (random2(sever) <= 2)
        {
            god_speaks(GOD_XOM, _get_xom_speech("zero miscast effect").c_str());

            miscast_effect(SPTYP_RANDOM, 0, 0, 0, "the mischief of Xom");

            done = true;
        }
        else if (random2(sever) <= 3)
        {
            god_speaks(GOD_XOM, _get_xom_speech("minor miscast effect").c_str());

            miscast_effect(SPTYP_RANDOM, 0, 0, random2(2),
                           "the capriciousness of Xom");

            done = true;
        }
        else if (random2(sever) <= 4)
        {
            god_speaks(GOD_XOM, _get_xom_speech("lose stats").c_str());

            lose_stat(STAT_RANDOM, 1 + random2(3), true,
                      "the capriciousness of Xom" );

            done = true;
        }
        else if (random2(sever) <= 5)
        {
            god_speaks(GOD_XOM, _get_xom_speech("medium miscast effect").c_str());

            miscast_effect(SPTYP_RANDOM, 0, 0, random2(3),
                           "the capriciousness of Xom");

            done = true;
        }
        else if (random2(sever) <= 6)
        {
            if (you.can_safely_mutate()
                && player_mutation_level(MUT_MUTATION_RESISTANCE) < 3)
            {
                god_speaks(GOD_XOM, _get_xom_speech("random mutations").c_str());

                mpr("Your body is suffused with distortional energy.");

                set_hp(1 + random2(you.hp), false);
                deflate_hp(you.hp_max / 2, true);

                bool failMsg = true;

                for (int i = random2(4); i >= 0; --i)
                {
                    if (mutate(RANDOM_XOM_MUTATION, failMsg, false, true))
                        done = true;
                    else
                        failMsg = false;
                }
            }
        }
        else if (random2(sever) <= 7)
        {
            if (there_are_monsters_nearby())
            {
                monsters *mon =
                    choose_random_nearby_monster(0, _choose_mutatable_monster);

                if (mon)
                {
                    god_speaks(GOD_XOM, _get_xom_speech("bad monster polymorph").c_str());

                    monster_polymorph(mon, RANDOM_MONSTER,
                        mons_wont_attack(mon) ? PPT_LESS : PPT_MORE);

                    if (one_chance_in(8)
                        && !mon->has_ench(
                                ENCH_GLOWING_SHAPESHIFTER, ENCH_SHAPESHIFTER))
                    {
                        mon->add_ench(one_chance_in(3) ?
                            ENCH_GLOWING_SHAPESHIFTER : ENCH_SHAPESHIFTER);
                    }

                    done = true;
                }
            }
        }
        else if (random2(sever) <= 8)
        {
            const char *speech = _get_xom_speech("draining or torment").c_str();

            if (one_chance_in(4))
            {
                if (player_prot_life() < 3)
                {
                    god_speaks(GOD_XOM, speech);

                    drain_exp();
                    if (random2(sever) > 3)
                        drain_exp();
                    if (random2(sever) > 3)
                        drain_exp();

                    done = true;
                }
            }
            else
            {
                if (!player_res_torment())
                {
                    god_speaks(GOD_XOM, speech);

                    torment_player(0, TORMENT_XOM);

                    done = true;
                }
            }
        }
        else if (random2(sever) <= 9)
        {
            const char *speech = _get_xom_speech("hostile monster").c_str();

            // Nasty, but fun.
            if (one_chance_in(4))
            {
                god_speaks(GOD_XOM, speech);

                cast_tukimas_dance(100, GOD_XOM, true);

                done = true;
            }
            else
            {
                const int numdemons =
                    std::min(random2(random2(random2(sever+1)+1)+1)+1, 14);

                bool success = false;

                for (int i = 0; i < numdemons; ++i)
                {
                    if (create_monster(
                            mgen_data::hostile_at(
                                _xom_random_punishment_demon(sever),
                                you.pos(), 4, 0, true, GOD_XOM)) != -1)
                    {
                        success = true;
                    }
                }

                if (success)
                {
                    god_speaks(GOD_XOM, speech);

                    done = true;
                }
            }
        }
        else if (random2(sever) <= 10)
        {
            god_speaks(GOD_XOM, _get_xom_speech("major miscast effect").c_str());

            miscast_effect(SPTYP_RANDOM, 0, 0, random2(4),
                           "the severe capriciousness of Xom");

            done = true;
        }
        else if (one_chance_in(sever) && (you.level_type != LEVEL_ABYSS))
        {
            god_speaks(GOD_XOM, _get_xom_speech("banishment").c_str());

            banished(DNGN_ENTER_ABYSS, "Xom");

            done = true;
        }
    }

    return (done);
}

void xom_acts(bool niceness, int sever)
{
#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS, "xom_acts(%u, %d); piety: %u, interest: %u\n",
         niceness, sever, you.piety, you.gift_timeout);
#endif

    entry_cause_type old_entry_cause = you.entry_cause;

    sever = std::max(1, sever);

    // Drawing the Xom card from Nemelex's decks of oddities or punishment.
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        god_type which_god = crawl_state.which_god_acting();

        if (crawl_state.is_god_retribution())
        {
            niceness = false;
            simple_god_message(" asks Xom for help in punishing you, and "
                       "Xom happily agrees.", which_god);
        }
        else
        {
            niceness = true;
            simple_god_message(" calls in a favour from Xom.", which_god);
        }
    }

    if (niceness && !one_chance_in(5))
    {
        // Good stuff.
        while (!_xom_is_good(sever))
            ;
    }
    else
    {
        // Bad mojo.
        while (!_xom_is_bad(sever))
            ;
    }

    // Drawing the Xom card from Nemelex's decks of oddities or punishment.
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        if (old_entry_cause != you.entry_cause
            && you.entry_cause_god == GOD_XOM)
        {
            you.entry_cause_god = crawl_state.which_god_acting();
        }
    }

    if (you.religion == GOD_XOM && coinflip())
        you.piety = MAX_PIETY - you.piety;
}

static void _xom_check_less_runes(int runes_gone)
{
    if (player_in_branch(BRANCH_HALL_OF_ZOT)
        || !(branches[BRANCH_HALL_OF_ZOT].branch_flags & BFLAG_HAS_ORB))
    {
        return;
    }

    int runes_avail = you.attribute[ATTR_UNIQUE_RUNES]
        + you.attribute[ATTR_DEMONIC_RUNES]
        + you.attribute[ATTR_ABYSSAL_RUNES]
        - you.attribute[ATTR_RUNES_IN_ZOT];
    int was_avail = runes_avail + runes_gone;

    // No longer enough available runes to get into Zot.
    if (was_avail >= NUMBER_OF_RUNES_NEEDED
        && runes_avail < NUMBER_OF_RUNES_NEEDED)
    {
        xom_is_stimulated(128, "Xom snickers.", true);
    }
}

void xom_check_lost_item(const item_def& item)
{
    if (item.base_type == OBJ_ORBS)
        xom_is_stimulated(255, "Xom laughs nastily.", true);
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        // If you'd dropped it, check if that means you'd dropped your
        // third rune, and now you don't have enough to get into Zot.
        if (item.flags & ISFLAG_BEEN_IN_INV)
            _xom_check_less_runes(item.quantity);

        if (is_unique_rune(item))
            xom_is_stimulated(255, "Xom snickers loudly.", true);
        else if (you.entry_cause == EC_SELF_EXPLICIT
            && !(item.flags & ISFLAG_BEEN_IN_INV))
        {
            // Player voluntarily entered Pan or the Abyss looking for
            // runes, yet never found them.
            if (item.plus == RUNE_ABYSSAL
                && you.attribute[ATTR_ABYSSAL_RUNES] == 0)
            {
                // Ignore Abyss area shifts.
                if (you.level_type != LEVEL_ABYSS)
                {
                    // Abyssal runes are a lot more trouble to find than
                    // demonic runes, so they get twice the stimulation.
                    xom_is_stimulated(128, "Xom snickers.", true);
                }
            }
            else if (item.plus == RUNE_DEMONIC
                     && you.attribute[ATTR_DEMONIC_RUNES] == 0)
            {
                xom_is_stimulated(64, "Xom snickers softly.", true);
            }
        }
    }
}

void xom_check_destroyed_item(const item_def& item, int cause)
{
    int amusement = 0;

    if (item.base_type == OBJ_ORBS)
    {
        xom_is_stimulated(255, "Xom laughs nastily.", true);
        return;
    }
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        _xom_check_less_runes(item.quantity);

        if (is_unique_rune(item) || item.plus == RUNE_ABYSSAL)
            amusement = 255;
        else
            amusement = 64 * item.quantity;
    }

    xom_is_stimulated(amusement,
                      (amusement > 128) ? "Xom snickers loudly." :
                      (amusement > 64)  ? "Xom snickers."
                                        : "Xom snickers softly.",
                      true);
}
