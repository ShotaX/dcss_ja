/**
 * @file
 * @brief Implementing the actor interface for player.
**/

#include "AppHdr.h"

#include "player.h"

#include <math.h>

#include "areas.h"
#include "art-enum.h"
#include "artefact.h"
#include "coordit.h"
#include "database.h"
#include "dgnevent.h"
#include "env.h"
#include "food.h"
#include "goditem.h"
#include "hints.h"
#include "itemname.h"
#include "itemprop.h"
#include "items.h"
#include "libutil.h"
#include "misc.h"
#include "monster.h"
#include "spl-damage.h"
#include "state.h"
#include "terrain.h"
#include "transform.h"
#include "traps.h"
#include "viewgeom.h"

int player::mindex() const
{
    return MHITYOU;
}

kill_category player::kill_alignment() const
{
    return KC_YOU;
}

god_type player::deity() const
{
    return religion;
}

bool player::alive() const
{
    // Simplistic, but if the player dies the game is over anyway, so
    // nobody can ask further questions.
    return !crawl_state.game_is_arena();
}

bool player::is_summoned(int* _duration, int* summon_type) const
{
    if (_duration != NULL)
        *_duration = -1;
    if (summon_type != NULL)
        *summon_type = 0;

    return false;
}

void player::moveto(const coord_def &c, bool clear_net)
{
    if (clear_net && c != pos())
        clear_trapping_net();

    crawl_view.set_player_at(c);
    set_position(c);

    clear_far_constrictions();
    end_searing_ray();
}

bool player::move_to_pos(const coord_def &c, bool clear_net)
{
    actor *target = actor_at(c);
    if (!target || target->submerged())
    {
        moveto(c, clear_net);
        return true;
    }
    return false;
}

void player::apply_location_effects(const coord_def &oldpos,
                                    killer_type killer,
                                    int killernum)
{
    moveto_location_effects(env.grid(oldpos));
}

void player::set_position(const coord_def &c)
{
    ASSERT(!crawl_state.game_is_arena());

    const bool real_move = (c != pos());
    actor::set_position(c);

    if (real_move)
    {
        reset_prev_move();

        if (duration[DUR_QUAD_DAMAGE])
            invalidate_agrid(true);

        if (player_has_orb())
        {
            env.orb_pos = c;
            invalidate_agrid(true);
        }

        dungeon_events.fire_position_event(DET_PLAYER_MOVED, c);
    }
}

bool player::swimming() const
{
    return in_water() && can_swim();
}

bool player::submerged() const
{
    return false;
}

bool player::floundering() const
{
    return in_water() && !can_swim() && !extra_balanced();
}

bool player::extra_balanced() const
{
    const dungeon_feature_type grid = grd(pos());
    return (species == SP_GREY_DRACONIAN
               || grid == DNGN_SHALLOW_WATER
                   && (species == SP_NAGA // tails, not feet
                       || body_size(PSIZE_BODY) >= SIZE_LARGE)
                   && (form == TRAN_LICH || form == TRAN_STATUE
                       || !form_changed_physiology()));
}

int player::get_experience_level() const
{
    return experience_level;
}

bool player::can_pass_through_feat(dungeon_feature_type grid) const
{
    return !feat_is_solid(grid) && grid != DNGN_MALIGN_GATEWAY;
}

bool player::is_habitable_feat(dungeon_feature_type actual_grid) const
{
    if (!can_pass_through_feat(actual_grid))
        return false;

    if (airborne() || species == SP_DJINNI)
        return true;

    if (actual_grid == DNGN_LAVA
        || actual_grid == DNGN_DEEP_WATER && !can_swim())
    {
        return false;
    }

    return true;
}

size_type player::body_size(size_part_type psize, bool base) const
{
    if (base)
        return species_size(species, psize);
    else
    {
        size_type tf_size = transform_size(form, psize);
        return (tf_size == SIZE_CHARACTER ? species_size(species, psize)
                                          : tf_size);
    }
}

int player::body_weight(bool base) const
{
    int weight = actor::body_weight(base);

    if (base)
        return weight;

    switch (form)
    {
    case TRAN_STATUE:
        weight *= 2;
        break;
    case TRAN_LICH:
        weight /= 2;
        break;
    default:
        break;
    }

    return weight;
}

int player::total_weight() const
{
    return (body_weight() + burden);
}

int player::damage_type(int)
{
    if (const item_def* wp = weapon())
        return get_vorpal_type(*wp);
    else if (form == TRAN_BLADE_HANDS)
        return DVORP_SLICING;
    else if (has_usable_claws())
        return DVORP_CLAWING;
    else if (has_usable_tentacles())
        return DVORP_TENTACLE;

    return DVORP_CRUSHING;
}

brand_type player::damage_brand(int)
{
    brand_type ret = SPWPN_NORMAL;
    const int wpn = equip[EQ_WEAPON];

    if (wpn != -1 && !melded[EQ_WEAPON])
    {
        if (!is_range_weapon(inv[wpn]) && !suppressed())
            ret = get_weapon_brand(inv[wpn]);
    }
    else if (duration[DUR_CONFUSING_TOUCH])
        ret = SPWPN_CONFUSE;
    else
    {
        switch (form)
        {
        case TRAN_SPIDER:
            ret = SPWPN_VENOM;
            break;

        case TRAN_ICE_BEAST:
            ret = SPWPN_FREEZING;
            break;

        case TRAN_LICH:
            ret = SPWPN_DRAINING;
            break;

        case TRAN_BAT:
            if (species == SP_VAMPIRE && one_chance_in(8))
                ret = SPWPN_VAMPIRICISM;
            break;

        case TRAN_JELLY:
            ret = SPWPN_ACID;
            break;

        default:
            break;
        }
    }

    return ret;
}

// Returns the item in the given equipment slot, NULL if the slot is empty.
// eq must be in [EQ_WEAPON, EQ_RING_EIGHT], or bad things will happen.
item_def *player::slot_item(equipment_type eq, bool include_melded) const
{
    ASSERT_RANGE(eq, EQ_WEAPON, NUM_EQUIP);

    const int item = equip[eq];
    if (item == -1 || !include_melded && melded[eq])
        return NULL;
    return (const_cast<item_def *>(&inv[item]));
}

// Returns the item in the player's weapon slot.
item_def *player::weapon(int /* which_attack */) const
{
    if (melded[EQ_WEAPON])
        return NULL;

    return slot_item(EQ_WEAPON, false);
}

bool player::can_wield(const item_def& item, bool ignore_curse,
                       bool ignore_brand, bool ignore_shield,
                       bool ignore_transform) const
{
    if (equip[EQ_WEAPON] != -1 && !ignore_curse)
    {
        if (inv[equip[EQ_WEAPON]].cursed())
            return false;
    }

    // Unassigned means unarmed combat.
    const bool two_handed = item.base_type == OBJ_UNASSIGNED
                            || hands_reqd(item, body_size()) == HANDS_TWO;

    if (two_handed && !ignore_shield && player_wearing_slot(EQ_SHIELD))
        return false;

    return could_wield(item, ignore_brand, ignore_transform);
}

bool player::could_wield(const item_def &item, bool ignore_brand,
                         bool ignore_transform) const
{
    if (species == SP_FELID)
        return false;
    if (body_size(PSIZE_TORSO, ignore_transform) < SIZE_LARGE
            && (item_mass(item) >= 500
                || item.base_type == OBJ_WEAPONS
                    && item_mass(item) >= 300))
        return false;

    // Anybody can wield missiles to enchant, item_mass permitting
    if (item.base_type == OBJ_MISSILES)
        return true;

    // Or any other object, although there's no point here.
    if (!is_weapon(item))
        return true;

    // Small species wielding large weapons...
    if (body_size(PSIZE_BODY, ignore_transform) < SIZE_MEDIUM
        && !check_weapon_wieldable_size(item,
               body_size(PSIZE_BODY, ignore_transform)))
    {
        return false;
    }

    if (!ignore_brand)
    {
        if (undead_or_demonic() && is_holy_item(item))
            return false;
    }

    return true;
}

// Returns the shield the player is wearing, or NULL if none.
item_def *player::shield() const
{
    return slot_item(EQ_SHIELD, false);
}

void player::make_hungry(int hunger_increase, bool silent)
{
    if (hunger_increase > 0)
        ::make_hungry(hunger_increase, silent);
    else if (hunger_increase < 0)
        ::lessen_hunger(-hunger_increase, silent);
}

string player::name(description_level_type dt, bool) const
{
    switch (dt)
    {
    case DESC_NONE:
        return "";
    case DESC_A: case DESC_THE:
    default:
        return jtrans("you");
    case DESC_YOUR:
    case DESC_ITS:
        return jtrans("your");
    }
}

string player::pronoun(pronoun_type pro, bool) const
{
    switch (pro)
    {
    default:
    case PRONOUN_SUBJECTIVE:        return jtrans("you");
    case PRONOUN_POSSESSIVE:        return jtrans("your");
    case PRONOUN_REFLEXIVE:         return jtrans("yourself");
    case PRONOUN_OBJECTIVE:         return jtrans("you");
    }
}

string player::conj_verb(const string &verb) const
{
    return verb;
}

string player::hand_name(bool plural, bool *can_plural) const
{
    bool _can_plural;
    if (can_plural == NULL)
        can_plural = &_can_plural;
    *can_plural = true;

    string str;

    if (form == TRAN_BAT || form == TRAN_DRAGON)
        str = jtrans("foreclaw");
    else if (form == TRAN_PIG || form == TRAN_SPIDER || form == TRAN_PORCUPINE)
        str = jtrans("front leg");
    else if (form == TRAN_ICE_BEAST)
        str = jtrans("paw");
    else if (form == TRAN_BLADE_HANDS)
        str = jtrans("scythe-like blade");
    else if (form == TRAN_TREE)
        str = jtrans("branch");
    else if (form == TRAN_WISP)
        str = jtrans("misty tendril");
    else if (form == TRAN_JELLY)
        str = jtrans("bump"); // not even pseudopods...
    else if (form == TRAN_LICH || form == TRAN_STATUE
             || !form_changed_physiology())
    {
        if (species == SP_FELID)
            str = jtrans("paw");
        else if (has_usable_claws())
            str = jtrans("claw2");
        else if (has_usable_tentacles())
            str = jtrans("tentacle");
    }

    if (str.empty())
        return (plural ? jtrans("hands") : jtrans("hand"));

    if (plural && *can_plural)
        str = pluralise(str);

    return str;
}

string player::foot_name(bool plural, bool *can_plural) const
{
    bool _can_plural;
    if (can_plural == NULL)
        can_plural = &_can_plural;
    *can_plural = true;

    string str;

    if (form == TRAN_SPIDER)
        str = jtrans("hind leg");
    else if (form == TRAN_TREE)
        str = jtrans("root");
    else if (form == TRAN_WISP)
        str = jtrans("strand");
    else if (form == TRAN_JELLY)
        str = jtrans("underside"), *can_plural = false;
    else if (form == TRAN_LICH || form == TRAN_STATUE
             || !form_changed_physiology())
    {
        if (player_mutation_level(MUT_HOOVES) >= 3)
            str = jtrans("hoof");
        else if (has_usable_talons())
            str = jtrans("talon");
        else if (has_usable_tentacles())
        {
            str         = jtrans("tentacles");
            *can_plural = false;
        }
        else if (species == SP_NAGA)
        {
            str         = jtrans("underbelly");
            *can_plural = false;
        }
        else if (species == SP_FELID)
            str = "paw";
        else if (fishtail)
        {
            str         = jtrans("tail");
            *can_plural = false;
        }
    }

    if (str.empty())
        return (plural ? jtrans("feet") : jtrans("foot"));

    if (plural && *can_plural)
        str = pluralise(str);

    return str;
}

string player::arm_name(bool plural, bool *can_plural) const
{
    if (form_changed_physiology())
        return hand_name(plural, can_plural);

    if (can_plural != NULL)
        *can_plural = true;

    string adj;
    string str = "arm";

    if (player_genus(GENPC_DRACONIAN) || species == SP_NAGA)
        adj = jtrans("scaled");
    else if (species == SP_TENGU)
        adj = jtrans("feathered");
    else if (species == SP_MUMMY)
        adj = jtrans("bandage-wrapped");
    else if (species == SP_OCTOPODE)
        str = jtrans("tentacle2");

    if (form == TRAN_LICH)
        adj = jtrans("bony");

    if (!adj.empty())
        str = adj + " " + str;

    if (plural)
        str = pluralise(str);

    return str;
}

string player::unarmed_attack_name() const
{
    string text = "Nothing wielded"; // Default

    if (species == SP_FELID)
        text = jtrans("Teeth and claws");
    else if (has_usable_claws(true))
        text = jtrans("Claws");
    else if (has_usable_tentacles(true))
        text = jtrans("Tentacles");

    switch (form)
    {
    case TRAN_SPIDER:
        text = jtrans("Fangs (venom)");
        break;
    case TRAN_BLADE_HANDS:
        text = jtrans("Blade") + blade_parts(true);
        break;
    case TRAN_STATUE:
        if (has_usable_claws(true))
            text = jtrans("Stone claws");
        else if (has_usable_tentacles(true))
            text = jtrans("Stone tentacles");
        else
            text = jtrans("Stone fists");
        break;
    case TRAN_ICE_BEAST:
        text = jtrans("Ice fists (freeze)");
        break;
    case TRAN_DRAGON:
        text = jtrans("Teeth and claws");
        break;
    case TRAN_LICH:
        text += jtrans("(drain)");
        break;
    case TRAN_BAT:
    case TRAN_PIG:
    case TRAN_PORCUPINE:
        text = jtrans("Teeth");
        break;
    case TRAN_TREE:
        text = jtrans("Branches");
        break;
    case TRAN_NONE:
    case TRAN_APPENDAGE:
    default:
        break;
    }
    return text;
}

bool player::fumbles_attack(bool verbose)
{
    bool did_fumble = false;

    // Fumbling in shallow water.
    if (floundering() || liquefied_ground())
    {
        if (x_chance_in_y(4, dex()) || one_chance_in(5))
        {
            if (verbose)
                mpr(jtrans("Your unstable footing causes you to fumble your attack."));
            did_fumble = true;
        }
        if (floundering())
            learned_something_new(HINT_FUMBLING_SHALLOW_WATER);
    }
    return did_fumble;
}

bool player::cannot_fight() const
{
    return false;
}

void player::attacking(actor *other)
{
    ASSERT(!crawl_state.game_is_arena());

    if (!other)
        return;

    if (other->is_monster())
    {
        const monster* mon = other->as_monster();
        if (!mon->friendly() && !mon->neutral())
            pet_target = mon->mindex();
    }

    if (mons_is_firewood((monster*) other))
        return;

    const int chance = pow(3, player_mutation_level(MUT_BERSERK) - 1);
    if (player_mutation_level(MUT_BERSERK) && x_chance_in_y(chance, 100))
        go_berserk(false);
}

void player::go_berserk(bool intentional, bool potion)
{
    ::go_berserk(intentional, potion);
}

bool player::can_go_berserk() const
{
    return can_go_berserk(false);
}

bool player::can_go_berserk(bool intentional, bool potion, bool quiet) const
{
    const bool verbose = (intentional || potion) && !quiet;

    if (berserk())
    {
        if (verbose)
            mpr(jtrans("You're already berserk!"));
        // or else you won't notice -- no message here.
        return false;
    }

    if (duration[DUR_EXHAUSTED])
    {
        if (verbose)
            mpr(jtrans("You're too exhausted to go berserk."));
        // or else they won't notice -- no message here
        return false;
    }

    if (duration[DUR_DEATHS_DOOR])
    {
        if (verbose)
            mpr(jtrans("Your body is effectively dead; that's not a shape for a blood rage."));
        return false;
    }

    if (beheld() && !player_equip_unrand_effect(UNRAND_DEMON_AXE))
    {
        if (verbose)
            mpr(jtrans("You are too mesmerised to rage."));
        // or else they won't notice -- no message here
        return false;
    }

    if (afraid())
    {
        if (verbose)
            mpr(jtrans("You are too terrified to rage."));

        return false;
    }

    if (is_lifeless_undead())
    {
        if (verbose)
            mpr(jtrans("You cannot raise a blood rage in your lifeless body."));

        return false;
    }

    // Stasis, but only for identified amulets; unided amulets will
    // trigger when the player attempts to activate berserk,
    // auto-iding at that point, but also killing the berserk and
    // wasting a turn.
    if (stasis(false))
    {
        if (verbose)
        {
            const item_def *amulet = slot_item(EQ_AMULET, false);
            mprf(jtrans("You cannot go berserk with %s on.").c_str(),
                 amulet? amulet->name(DESC_YOUR).c_str() : jtrans("your amulet").c_str());
        }
        return false;
    }

    if (!intentional && !potion && clarity())
    {
        if (verbose)
        {
            mpr(jtrans("You're too calm and focused to rage."));
            if (!clarity(false))
                maybe_id_clarity();
        }

        return false;
    }

    ASSERT(HUNGER_STARVING - 100 + BERSERK_NUTRITION < 2066);
    if (hunger <= 2066)
    {
        if (verbose)
            mpr(jtrans("You're too hungry to go berserk."));
        return false;
    }

    return true;
}

bool player::berserk() const
{
    return duration[DUR_BERSERK];
}

bool player::can_cling_to_walls() const
{
    return form == TRAN_SPIDER;
}

bool player::is_web_immune() const
{
    // Spider form
    return form == TRAN_SPIDER;
}

bool player::shove(const char* feat_name)
{
    for (distance_iterator di(pos()); di; ++di)
        if (in_bounds(*di) && !actor_at(*di) && !is_feat_dangerous(grd(*di))
            && can_pass_through_feat(grd(*di)))
        {
            moveto(*di);
            if (*feat_name)
                mprf(jtrans("You are pushed out of the %s.").c_str(), feat_name);
            dprf("Moved to (%d, %d).", pos().x, pos().y);
            return true;
        }
    return false;
}

int player::constriction_damage() const
{
    return roll_dice(2, div_rand_round(strength(), 5));
}
