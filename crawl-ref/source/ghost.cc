/**
 * @file
 * @brief Player ghost and random Pandemonium demon handling.
**/

#include "AppHdr.h"

#include "ghost.h"

#include "act-iter.h"
#include "artefact.h"
#include "colour.h"
#include "database.h"
#include "env.h"
#include "externs.h"
#include "itemname.h"
#include "itemprop.h"
#include "libutil.h"
#include "ng-input.h"
#include "random.h"
#include "skills2.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "strings.h"
#include "mon-util.h"
#include "mon-transit.h"
#include "player.h"

#include <vector>

#define MAX_GHOST_DAMAGE     50
#define MAX_GHOST_HP        400
#define MAX_GHOST_EVASION    60
#define MIN_GHOST_SPEED       6
#define MAX_GHOST_SPEED      13

vector<ghost_demon> ghosts;

// Order for looking for conjurations for the 1st & 2nd spell slots,
// when finding spells to be remembered by a player's ghost.
static spell_type search_order_conj[] =
{
    SPELL_LEHUDIBS_CRYSTAL_SPEAR,
    SPELL_FIRE_STORM,
    SPELL_GLACIATE,
    SPELL_CHAIN_LIGHTNING,
    SPELL_BOLT_OF_DRAINING,
    SPELL_AGONY,
    SPELL_DISINTEGRATE,
    SPELL_LIGHTNING_BOLT,
    SPELL_AIRSTRIKE,
    SPELL_STICKY_FLAME,
    SPELL_ISKENDERUNS_MYSTIC_BLAST,
    SPELL_IOOD,
    SPELL_BOLT_OF_MAGMA,
    SPELL_THROW_ICICLE,
    SPELL_BOLT_OF_FIRE,
    SPELL_BOLT_OF_COLD,
    SPELL_FIREBALL,
    SPELL_DELAYED_FIREBALL,
    SPELL_VENOM_BOLT,
    SPELL_IRON_SHOT,
    SPELL_LRD,
    SPELL_STONE_ARROW,
    SPELL_FORCE_LANCE,
    SPELL_DISCHARGE,
    SPELL_DAZZLING_SPRAY,
    SPELL_THROW_FLAME,
    SPELL_THROW_FROST,
    SPELL_FREEZE,
    SPELL_PAIN,
    SPELL_STING,
    SPELL_SHOCK,
    SPELL_SANDBLAST,
    SPELL_MAGIC_DART,
    SPELL_HIBERNATION,
    SPELL_FLAME_TONGUE,
    SPELL_CORONA,
    SPELL_NO_SPELL,                        // end search
};

// Order for looking for summonings and self-enchants for the 3rd spell
// slot.
static spell_type search_order_third[] =
{
    SPELL_SYMBOL_OF_TORMENT,
    SPELL_SUMMON_GREATER_DEMON,
    SPELL_DRAGON_CALL,
    SPELL_SUMMON_HORRIBLE_THINGS,
    SPELL_HAUNT,
    SPELL_SUMMON_HYDRA,
    SPELL_SUMMON_DEMON,
    SPELL_HASTE,
    SPELL_SILENCE,
    SPELL_BATTLESPHERE,
    SPELL_SUMMON_BUTTERFLIES,
    SPELL_SUMMON_SWARM,
    SPELL_MONSTROUS_MENAGERIE,
    SPELL_SWIFTNESS,
    SPELL_SUMMON_ICE_BEAST,
    SPELL_ANIMATE_DEAD,
    SPELL_TWISTED_RESURRECTION,
    SPELL_INVISIBILITY,
    SPELL_CALL_IMP,
    SPELL_SUMMON_SMALL_MAMMAL,
    SPELL_MALIGN_GATEWAY,
    SPELL_CONTROLLED_BLINK,
    SPELL_BLINK,
    SPELL_NO_SPELL,                        // end search
    // No Simulacrum: iffy for pghosts (picking up material components),
    // largely useless on Pan lords.
};

// Order for looking for enchants for the 4th & 5th spell slots.  If
// this fails, go through conjurations.  Note: Dig must be in misc2
// (5th) position to work.
static spell_type search_order_misc[] =
{
    SPELL_TORNADO,
    SPELL_SHATTER,
    SPELL_AGONY,
    SPELL_BANISHMENT,
    SPELL_FREEZING_CLOUD,
    SPELL_OZOCUBUS_REFRIGERATION,
    SPELL_OLGREBS_TOXIC_RADIANCE,
    SPELL_MASS_CONFUSION,
    SPELL_ENGLACIATION,
    SPELL_DISPEL_UNDEAD,
    SPELL_CONJURE_BALL_LIGHTNING,
    SPELL_PARALYSE,
    SPELL_CONFUSE,
    SPELL_MEPHITIC_CLOUD,
    SPELL_SLOW,
    SPELL_PETRIFY,
    SPELL_POLYMORPH,
    SPELL_TELEPORT_OTHER,
    SPELL_DIG,
    SPELL_CORONA,
    SPELL_NO_SPELL,                        // end search
};

// Last slot (emergency) can only be Teleport Self or Blink.

ghost_demon::ghost_demon()
{
    reset();
}

void ghost_demon::reset()
{
    name.clear();
    species          = SP_UNKNOWN;
    job              = JOB_UNKNOWN;
    religion         = GOD_NO_GOD;
    best_skill       = SK_FIGHTING;
    best_skill_level = 0;
    xl               = 0;
    max_hp           = 0;
    ev               = 0;
    ac               = 0;
    damage           = 0;
    speed            = 10;
    see_invis        = false;
    brand            = SPWPN_NORMAL;
    att_type         = AT_HIT;
    att_flav         = AF_PLAIN;
    resists          = 0;
    spellcaster      = false;
    cycle_colours    = false;
    colour           = BLACK;
    fly              = FL_NONE;
    acting_part      = MONS_0;
}

static brand_type _random_special_pan_lord_brand()
{
    brand_type brand;

    do
    {
        brand = static_cast<brand_type>(random2(MAX_PAN_LORD_BRANDS));
        // some brands inappropriate (e.g. holy wrath)
    }
    while (brand == SPWPN_HOLY_WRATH
#if TAG_MAJOR_VERSION == 34
           || brand == SPWPN_ORC_SLAYING
           || brand == SPWPN_RETURNING
           || brand == SPWPN_REACHING
           || brand == SPWPN_FLAME
           || brand == SPWPN_FROST
           || brand == SPWPN_DRAGON_SLAYING
#endif
           || brand == SPWPN_PROTECTION
           || brand == SPWPN_EVASION
           );

    return brand;
}

void ghost_demon::init_random_demon()
{
    do name = make_name(random_int(), false);
        while (!getLongDescription(name).empty());

    // hp - could be defined below (as could ev, AC, etc.). Oh well, too late:
    max_hp = 100 + roll_dice(3, 50);

    ev = 5 + random2(20);
    ac = 5 + random2(20);

    see_invis = true;

    resists = 0;

    if (!one_chance_in(3))
        resists |= MR_RES_FIRE * random_range(1, 2);
    else if (one_chance_in(10))
        resists |= MR_VUL_FIRE;

    if (!one_chance_in(3))
        resists |= MR_RES_COLD * random_range(1, 2);
    else
        resists |= MR_VUL_COLD;

    // Demons, like ghosts, automatically get poison res. and life prot.

    // resist electricity:
    if (one_chance_in(3))
        resists |= MR_RES_ELEC; // no rElec++ for Pan lords, because of witches

    // HTH damage:
    damage = 20 + roll_dice(2, 20);

    // Does demon fly?
    fly = (one_chance_in(3) ? FL_NONE :
           one_chance_in(5) ? FL_LEVITATE
                            : FL_WINGED);

    // hit dice:
    xl = 10 + roll_dice(2, 10);

    // Is demon a spellcaster?
    // Non-spellcasters always have branded melee and are faster instead.
    spellcaster = x_chance_in_y(3,4);

    if (one_chance_in(3) || !spellcaster)
        brand = _random_special_pan_lord_brand();
    else
        brand = SPWPN_NORMAL;

    // Non-caster demons are fast, casters may get haste.
    if (!spellcaster)
        speed = 11 + roll_dice(2,4);
    else if (one_chance_in(3))
        speed = 10;
    else
        speed = 8 + roll_dice(2,5);

    spells.init(SPELL_NO_SPELL);

    if (spellcaster)
    {
        // This bit uses the list of player spells to find appropriate
        // spells for the demon, then converts those spells to the monster
        // spell indices.  Some special monster-only spells are at the end.

        if (coinflip())
            spells[0] = RANDOM_ELEMENT(search_order_conj);

        // Might duplicate the first spell, but that isn't a problem.
        if (coinflip())
            spells[1] = RANDOM_ELEMENT(search_order_conj);

        if (!one_chance_in(4))
            spells[2] = RANDOM_ELEMENT(search_order_third);

        if (coinflip())
        {
            spells[3] = RANDOM_ELEMENT(search_order_misc);
            if (spells[3] == SPELL_DIG)
                spells[3] = SPELL_NO_SPELL;
        }

        if (coinflip())
            spells[4] = RANDOM_ELEMENT(search_order_misc);

        spells[5] = random_choose_weighted(2, SPELL_TELEPORT_SELF,
                                           1, SPELL_BLINK,
                                           1, SPELL_NO_SPELL,
                                           0);

        // Convert the player spell indices to monster spell ones.
        // Pan lords also get their Agony upgraded to Torment.
        for (int i = 0; i < NUM_MONSTER_SPELL_SLOTS; ++i)
        {
            spells[i] = translate_spell(spells[i]);
            if (spells[i] == SPELL_AGONY)
                spells[i] = SPELL_SYMBOL_OF_TORMENT;
            if (spells[i] == SPELL_CONJURE_BALL_LIGHTNING
                || spells[i] == SPELL_OZOCUBUS_REFRIGERATION
                || spells[i] == SPELL_TORNADO)
            {
                spells[i] = SPELL_NO_SPELL;
            }
        }

        // Give demon a chance for some monster-only spells.
        // Demon-summoning should be fairly common.
        if (one_chance_in(4))
        {
            spells[0] = random_choose(SPELL_HELLFIRE_BURST,
                                      SPELL_FIRE_STORM,
                                      SPELL_GLACIATE,
                                      SPELL_METAL_SPLINTERS,
             /* eye of devastation */ SPELL_ENERGY_BOLT,
                                      SPELL_ORB_OF_ELECTRICITY,
                                      -1);
        }

        if (one_chance_in(25))
            spells[1] = SPELL_STEAM_BALL;
        if (one_chance_in(25))
            spells[1] = SPELL_QUICKSILVER_BOLT;
        if (one_chance_in(25))
            spells[1] = SPELL_HELLFIRE;
        if (one_chance_in(25))
            spells[1] = SPELL_IOOD;

        if (one_chance_in(25))
            spells[2] = SPELL_SMITING;
        if (one_chance_in(25))
            spells[2] = SPELL_HELLFIRE_BURST;
        if (one_chance_in(22))
            spells[2] = SPELL_SUMMON_HYDRA;
        if (one_chance_in(20))
            spells[2] = SPELL_SUMMON_DRAGON;
        if (one_chance_in(12))
            spells[2] = SPELL_SUMMON_GREATER_DEMON;
        if (one_chance_in(12))
            spells[2] = SPELL_SUMMON_DEMON;
        if (one_chance_in(10))
            spells[2] = SPELL_SUMMON_EYEBALLS;

        if (one_chance_in(20))
            spells[3] = SPELL_SUMMON_GREATER_DEMON;
        if (one_chance_in(20))
            spells[3] = SPELL_SUMMON_DEMON;
        if (one_chance_in(20))
            spells[3] = SPELL_MALIGN_GATEWAY;

        // At least they can summon demons.
        if (spells[3] == SPELL_NO_SPELL)
            spells[3] = SPELL_SUMMON_DEMON;

        if (one_chance_in(15))
            spells[4] = SPELL_DIG;
    }

    // Does demon cycle colours?
    cycle_colours = one_chance_in(10);

    colour = random_colour();

}

// Returns the movement speed for a player ghost.  Note that this is a
// real speed, not a movement cost, so higher is better.
static int _player_ghost_base_movement_speed()
{
    int speed = 10;

    if (int fast = player_mutation_level(MUT_FAST, false))
        speed += fast + 1;
    if (int slow = player_mutation_level(MUT_SLOW, false))
        speed -= slow + 1;

    if (you.wearing_ego(EQ_BOOTS, SPARM_RUNNING))
        speed += 1;

    // Cap speeds.
    if (speed < MIN_GHOST_SPEED)
        speed = MIN_GHOST_SPEED;
    else if (speed > MAX_GHOST_SPEED)
        speed = MAX_GHOST_SPEED;

    return speed;
}

void ghost_demon::init_player_ghost()
{
    name   = you.your_name;
    max_hp = ((get_real_hp(false) >= MAX_GHOST_HP)
             ? MAX_GHOST_HP
             : get_real_hp(false));
    ev     = player_evasion();
    ac     = you.armour_class();

    if (ev > MAX_GHOST_EVASION)
        ev = MAX_GHOST_EVASION;

    see_invis      = you.can_see_invisible();
    resists        = 0;
    set_resist(resists, MR_RES_FIRE, player_res_fire());
    set_resist(resists, MR_RES_COLD, player_res_cold());
    set_resist(resists, MR_RES_ELEC, player_res_electricity());
    // clones might lack innate rPois, copy it.  pghosts don't care.
    set_resist(resists, MR_RES_POISON, player_res_poison());
    set_resist(resists, MR_RES_NEG, you.res_negative_energy());
    set_resist(resists, MR_RES_ACID, player_res_acid());
    // multi-level for players, boolean as an innate monster resistance
    set_resist(resists, MR_RES_STEAM, player_res_steam() ? 1 : 0);
    set_resist(resists, MR_RES_STICKY_FLAME, player_res_sticky_flame());
    set_resist(resists, MR_RES_ASPHYX, you.res_asphyx());
    set_resist(resists, MR_RES_ROTTING, you.res_rotting());
    speed          = _player_ghost_base_movement_speed();

    damage = 4;
    brand = SPWPN_NORMAL;

    if (you.weapon())
    {
        // This includes ranged weapons, but they're treated as melee.

        const item_def& weapon = *you.weapon();
        if (is_weapon(weapon))
        {
            damage = property(weapon, PWPN_DAMAGE);

            damage *= 25 + you.skills[melee_skill(weapon)];
            damage /= 25;

            if (weapon.base_type == OBJ_WEAPONS)
            {
                brand = static_cast<brand_type>(get_weapon_brand(weapon));

                // Ghosts can't get holy wrath, but they get to keep
                // the weapon.
                if (brand == SPWPN_HOLY_WRATH)
                    brand = SPWPN_NORMAL;

                // Don't copy ranged-only brands from launchers (reaping etc.).
                if (brand > MAX_PAN_LORD_BRANDS)
                    brand = SPWPN_NORMAL;
            }
            else if (weapon.base_type == OBJ_STAVES)
            {
                switch (static_cast<stave_type>(weapon.sub_type))
                {
                // very bad approximations
                case STAFF_FIRE: brand = SPWPN_FLAMING; break;
                case STAFF_COLD: brand = SPWPN_FREEZING; break;
                case STAFF_POISON: brand = SPWPN_VENOM; break;
                case STAFF_DEATH: brand = SPWPN_PAIN; break;
                case STAFF_AIR: brand = SPWPN_ELECTROCUTION; break;
                case STAFF_EARTH: brand = SPWPN_VORPAL; break;
                default: ;
                }
            }
        }
    }
    else
    {
        // Unarmed combat.
        if (you.innate_mutation[MUT_CLAWS])
            damage += you.experience_level;

        damage += you.skills[SK_UNARMED_COMBAT];
    }

    damage *= 30 + you.skills[SK_FIGHTING];
    damage /= 30;

    damage += you.strength() / 4;

    if (damage > MAX_GHOST_DAMAGE)
        damage = MAX_GHOST_DAMAGE;

    species = you.species;
    job = you.char_class;

    religion = you.religion;

    best_skill = ::best_skill(SK_FIRST_SKILL, SK_LAST_SKILL);
    best_skill_level = you.skills[best_skill];
    xl = you.experience_level;

    // These are the same as in mon-data.h.
    colour = WHITE;
    fly = FL_LEVITATE;

    add_spells();
}

static colour_t _ugly_thing_assign_colour(colour_t force_colour,
                                          colour_t force_not_colour)
{
    colour_t colour;

    if (force_colour != BLACK)
        colour = force_colour;
    else
    {
        do
            colour = ugly_thing_random_colour();
        while (force_not_colour != BLACK && colour == force_not_colour);
    }

    return colour;
}

static attack_flavour _very_ugly_thing_flavour_upgrade(attack_flavour u_att_flav)
{
    switch (u_att_flav)
    {
    case AF_FIRE:
        u_att_flav = AF_STICKY_FLAME;
        break;

    case AF_POISON:
        u_att_flav = AF_POISON_STRONG;
        break;

    default:
        break;
    }

    return u_att_flav;
}

static attack_flavour _ugly_thing_colour_to_flavour(colour_t u_colour)
{
    attack_flavour u_att_flav = AF_PLAIN;

    switch (make_low_colour(u_colour))
    {
    case RED:
        u_att_flav = AF_FIRE;
        break;

    case BROWN:
        u_att_flav = AF_ACID;
        break;

    case GREEN:
        u_att_flav = AF_POISON;
        break;

    case CYAN:
        u_att_flav = AF_ELEC;
        break;

    case LIGHTGREY:
        u_att_flav = AF_COLD;
        break;

    default:
        break;
    }

    if (is_high_colour(u_colour))
        u_att_flav = _very_ugly_thing_flavour_upgrade(u_att_flav);

    return u_att_flav;
}

/**
 * Init a ghost demon object corresponding to an ugly thing monster.
 *
 * @param very_ugly     Whether the ugly thing is a very ugly thing.
 * @param only_mutate   Whether to mutate the ugly thing's colour away from its
 *                      old colour (the force_colour).
 * @param force_colour  The ugly thing's colour. (Default BLACK = random)
 */
void ghost_demon::init_ugly_thing(bool very_ugly, bool only_mutate,
                                  colour_t force_colour)
{
    const monster_type type = very_ugly ? MONS_VERY_UGLY_THING
                                        : MONS_UGLY_THING;
    const monsterentry* stats = get_monster_data(type);

    speed = stats->speed;
    // randomize slightly (+-1), keep the same midpoint
    ev = stats->ev + random2(3) - 1;
    ac = stats->AC + random2(3) - 1;
    damage = stats->attack[0].damage + random2(3) - 1;

    // If we're mutating an ugly thing, leave its experience level, hit
    // dice and maximum hit points as they are.
    if (!only_mutate)
    {
        xl = stats->hpdice[0];
        max_hp = hit_points(xl, stats->hpdice[1], stats->hpdice[2]);
    }

    const attack_type att_types[] =
    {
        AT_BITE, AT_STING, AT_ENGULF, AT_CLAW, AT_PECK, AT_HEADBUTT, AT_PUNCH,
        AT_KICK, AT_TENTACLE_SLAP, AT_TAIL_SLAP, AT_GORE, AT_TRUNK_SLAP
    };

    att_type = RANDOM_ELEMENT(att_types);

    // An ugly thing always gets a low-intensity colour.  If we're
    // mutating it, it always gets a different colour from what it had
    // before.
    colour = _ugly_thing_assign_colour(make_low_colour(force_colour),
                                       only_mutate ? make_low_colour(colour)
                                                   : BLACK);

    // Pick a compatible attack flavour for this colour.
    att_flav = _ugly_thing_colour_to_flavour(colour);
    if (colour == MAGENTA)
        damage += 5;

    // Pick a compatible resistance for this attack flavour.
    ugly_thing_add_resistance(false, att_flav);

    // If this is a very ugly thing, upgrade it properly.
    if (very_ugly)
        ugly_thing_to_very_ugly_thing();
}

void ghost_demon::ugly_thing_to_very_ugly_thing()
{
    // A very ugly thing always gets a high-intensity colour.
    colour = make_high_colour(colour);

    // A very ugly thing sometimes gets an upgraded attack flavour.
    att_flav = _very_ugly_thing_flavour_upgrade(att_flav);

    // Pick a compatible resistance for this attack flavour.
    ugly_thing_add_resistance(true, att_flav);
}

static resists_t _ugly_thing_resists(bool very_ugly, attack_flavour u_att_flav)
{
    switch (u_att_flav)
    {
    case AF_FIRE:
    case AF_STICKY_FLAME:
        return MR_RES_FIRE * (very_ugly ? 2 : 1) | MR_RES_STICKY_FLAME;

    case AF_ACID:
        return MR_RES_ACID;

    case AF_POISON:
    case AF_POISON_STRONG:
        return MR_RES_POISON * (very_ugly ? 2 : 1);

    case AF_ELEC:
        return MR_RES_ELEC * (very_ugly ? 2 : 1);

    case AF_COLD:
        return MR_RES_COLD * (very_ugly ? 2 : 1);

    default:
        return 0;
    }
}

void ghost_demon::ugly_thing_add_resistance(bool very_ugly,
                                            attack_flavour u_att_flav)
{
    resists = _ugly_thing_resists(very_ugly, u_att_flav);
}

void ghost_demon::init_dancing_weapon(const item_def& weapon, int power)
{
    int delay = property(weapon, PWPN_SPEED);
    int damg  = property(weapon, PWPN_DAMAGE);

    if (power > 100)
        power = 100;

    colour = weapon.colour;
    fly = FL_LEVITATE;

    // We want Tukima to reward characters who invest heavily in
    // Hexes skill. Therefore, weapons benefit from very high skill.

    // First set up what the monsters will look like with 100 power.
    // Daggers are weak here! In the table, "44+22" means d44+d22 with
    // d22 being base damage and d44 coming from power.
    // Giant spiked club: speed 12, 44+22 damage, 22 AC, 36 HP, 16 EV
    // Bardiche:          speed 10, 40+20 damage, 18 AC, 40 HP, 15 EV
    // Dagger:            speed 20,  8+ 4 damage,  4 AC, 20 HP, 20 EV
    // Quick blade:       speed 23, 10+ 5 damage,  5 AC, 14 HP, 22 EV
    // Cutlass:           speed 18, 14+ 7 damage,  7 AC, 24 HP, 19 EV

    xl = 15;

    speed   = 30 - delay;
    ev      = 25 - delay / 2;
    ac      = damg;
    damage  = 2 * damg;
    max_hp  = delay * 2;

    // Don't allow the speed to become too low.
    speed = max(3, (speed / 2) * (1 + power / 100));

    ev    = max(3, ev * power / 100);
    ac = ac * power / 100;
    max_hp = max(5, max_hp * power / 100);
    damage = max(1, damage * power / 100);
}

void ghost_demon::init_spectral_weapon(const item_def& weapon,
                                       int power, int wpn_skill)
{
    int damg  = property(weapon, PWPN_DAMAGE);

    if (power > 100)
        power = 100;

    // skill is on a 10 scale
    if (wpn_skill > 270)
        wpn_skill = 270;

    colour = weapon.colour;
    fly = FL_LEVITATE;

    // Hit dice (to hit) scales with weapon skill alone.
    // Damage scales with weapon skill, but how well depends on spell power.
    // Defenses scale with spell power alone.
    // Appropriate investment is rewarded with a stronger spectral weapon.

    xl = max(wpn_skill / 10, 1);

    // At 0 power, weapon skill is 1/3 as effective as on the player
    // At max power, weapon skill is as effective as on the player.
    // Power has a linear effect between those endpoints.
    // It's possible this ends up too strong,
    // but 100 power on Hexes/Charms will take significant investment
    // most players wouldn't otherwise get.
    //
    // Damage multiplier table:
    //     |            weapon skill
    // pow |   3       9       15      21      27
    // --- |   -----   ----    ----    ----    ----
    // 0   |   1.04    1.12    1.20    1.28    1.36
    // 10  |   1.05    1.14    1.24    1.34    1.43
    // 20  |   1.06    1.17    1.28    1.39    1.50
    // 30  |   1.06    1.19    1.32    1.45    1.58
    // 40  |   1.07    1.22    1.36    1.50    1.65
    // 50  |   1.08    1.24    1.40    1.56    1.72
    // 60  |   1.09    1.26    1.44    1.62    1.79
    // 70  |   1.10    1.29    1.48    1.67    1.87
    // 80  |   1.10    1.31    1.52    1.73    1.94
    // 90  |   1.11    1.34    1.56    1.79    2.01
    // 100 |   1.12    1.36    1.60    1.84    2.08
    damage  = damg;
    int scale = 250 * 150 / (50 + power);
    damage *= scale + wpn_skill;
    damage /= scale;

    speed   = 30;
    ev      = 10 + div_rand_round(power,10);
    ac      = 2 + div_rand_round(power,10);
    max_hp  = 10 + div_rand_round(power,3);
}

static bool _know_spell(spell_type spell)
{
    return you.has_spell(spell) && spell_fail(spell) < 50;
}

/**
 * Searches a list of ghost spells for the first one that
 * the player can cast.
 * @param spells The list of spells; it must be terminated by SPELL_NO_SPELL.
 * @param ignore_up_to_spell Ignore entries in the list up to and
 *                           including this one.
 * @return  The first spell the player knows.
 */
static spell_type search_spell_list(spell_type* spells, spell_type ignore_up_to_spell)
{
    unsigned i = 0;
    while (ignore_up_to_spell != SPELL_NO_SPELL
            && spells[i] != SPELL_NO_SPELL)
    {
        if (spells[i++] == ignore_up_to_spell)
            break;
    }

    while (spells[i] != SPELL_NO_SPELL)
    {
        if (_know_spell(spells[i]))
            return spells[i];
        ++i;
    }

    return SPELL_NO_SPELL;
}

// Used when creating ghosts: goes through and finds spells for the
// ghost to cast.  Death is a traumatic experience, so ghosts only
// remember a few spells.
void ghost_demon::add_spells()
{
    spells.init(SPELL_NO_SPELL);

    spells[0] = search_spell_list(search_order_conj, SPELL_NO_SPELL);
    spells[1] = search_spell_list(search_order_conj, spells[0]);
    spells[2] = search_spell_list(search_order_third, SPELL_NO_SPELL);
    spells[3] = search_spell_list(search_order_misc, SPELL_NO_SPELL);
    spells[4] = search_spell_list(search_order_misc, spells[3]);

    if (spells[3] == SPELL_NO_SPELL)
    {
        spells[3] = search_spell_list(search_order_conj, spells[1]);
        if (spells[4] == SPELL_NO_SPELL)
            spells[4] = search_spell_list(search_order_conj, spells[3]);
    }
    else if (spells[4] == SPELL_NO_SPELL)
         spells[4] = search_spell_list(search_order_conj, spells[1]);

    // Look for Blink or Teleport Self for the emergency slot.
    if (_know_spell(SPELL_CONTROLLED_BLINK)
        || _know_spell(SPELL_BLINK))
    {
        spells[5] = SPELL_CONTROLLED_BLINK;
    }

    for (int i = 0; i < NUM_MONSTER_SPELL_SLOTS; ++i)
        spells[i] = translate_spell(spells[i]);

    spellcaster = has_spells();
}

bool ghost_demon::has_spells() const
{
    for (int i = 0; i < NUM_MONSTER_SPELL_SLOTS; ++i)
        if (spells[i] != SPELL_NO_SPELL)
            return true;
    return false;
}

// When passed the number for a player spell, returns the equivalent
// monster spell.  Returns SPELL_NO_SPELL on failure (no equivalent).
spell_type ghost_demon::translate_spell(spell_type spell) const
{
    switch (spell)
    {
    case SPELL_CONTROLLED_BLINK:
        return SPELL_BLINK;        // approximate
    case SPELL_DELAYED_FIREBALL:
        return SPELL_FIREBALL;
    case SPELL_DRAGON_CALL:
        return SPELL_SUMMON_DRAGON;
    default:
        break;
    }

    return spell;
}

vector<ghost_demon> ghost_demon::find_ghosts()
{
    vector<ghost_demon> gs;

    if (!you.is_undead)
    {
        ghost_demon player;
        player.init_player_ghost();
        announce_ghost(player);
        gs.push_back(player);
    }

    // Pick up any other ghosts that happen to be on the level if we
    // have space.  If the player is undead, add one to the ghost quota
    // for the level.
    find_extra_ghosts(gs, n_extra_ghosts() + 1 - gs.size());

    return gs;
}

void ghost_demon::find_transiting_ghosts(
    vector<ghost_demon> &gs, int n)
{
    if (n <= 0)
        return;

    const m_transit_list *mt = get_transit_list(level_id::current());
    if (mt)
    {
        for (m_transit_list::const_iterator i = mt->begin();
             i != mt->end() && n > 0; ++i)
        {
            if (i->mons.type == MONS_PLAYER_GHOST)
            {
                const monster& m = i->mons;
                if (m.ghost.get())
                {
                    announce_ghost(*m.ghost);
                    gs.push_back(*m.ghost);
                    --n;
                }
            }
        }
    }
}

void ghost_demon::announce_ghost(const ghost_demon &g)
{
#if defined(DEBUG_BONES) || defined(DEBUG_DIAGNOSTICS)
    mprf(MSGCH_DIAGNOSTICS, "Saving ghost: %s", g.name.c_str());
#endif
}

void ghost_demon::find_extra_ghosts(vector<ghost_demon> &gs, int n)
{
    for (monster_iterator mi; mi && n > 0; ++mi)
    {
        if (mi->type == MONS_PLAYER_GHOST && mi->ghost.get())
        {
            // Bingo!
            announce_ghost(*(mi->ghost));
            gs.push_back(*(mi->ghost));
            --n;
        }
    }

    // Check the transit list for the current level.
    find_transiting_ghosts(gs, n);
}

// Returns the number of extra ghosts allowed on the level.
int ghost_demon::n_extra_ghosts()
{
    if (env.absdepth0 < 10)
        return 0;

    return MAX_GHOSTS - 1;
}

// Sanity checks for some ghost values.
bool debug_check_ghosts()
{
    for (unsigned int k = 0; k < ghosts.size(); ++k)
    {
        ghost_demon ghost = ghosts[k];
        // Values greater than the allowed maximum or less then the
        // allowed minimum signalise bugginess.
        if (ghost.damage < 0 || ghost.damage > MAX_GHOST_DAMAGE)
            return false;
        if (ghost.max_hp < 1 || ghost.max_hp > MAX_GHOST_HP)
            return false;
        if (ghost.xl < 1 || ghost.xl > 27)
            return false;
        if (ghost.ev > MAX_GHOST_EVASION)
            return false;
        if (ghost.speed < MIN_GHOST_SPEED || ghost.speed > MAX_GHOST_SPEED)
            return false;
        if (get_resist(ghost.resists, MR_RES_ELEC) < 0)
            return false;
        if (ghost.brand < SPWPN_NORMAL || ghost.brand > MAX_PAN_LORD_BRANDS)
            return false;
        if (ghost.species < 0 || ghost.species >= NUM_SPECIES)
            return false;
        if (ghost.job < JOB_FIGHTER || ghost.job >= NUM_JOBS)
            return false;
        if (ghost.best_skill < SK_FIGHTING || ghost.best_skill >= NUM_SKILLS)
            return false;
        if (ghost.best_skill_level < 0 || ghost.best_skill_level > 27)
            return false;
        if (ghost.religion < GOD_NO_GOD || ghost.religion >= NUM_GODS)
            return false;

        if (ghost.brand == SPWPN_HOLY_WRATH)
            return false;

        // Only (very) ugly things get non-plain attack types and
        // flavours.
        if (ghost.att_type != AT_HIT || ghost.att_flav != AF_PLAIN)
            return false;

        // Only Pandemonium lords cycle colours.
        if (ghost.cycle_colours)
            return false;

        // Name validation.
        if (!validate_player_name(ghost.name, false))
            return false;
        // Many combining characters can come per every letter, but if there's
        // that much, it's probably a maliciously forged ghost of some kind.
        if (ghost.name.length() > kNameLen * 10 || ghost.name.empty())
            return false;
        if (ghost.name != trimmed_string(ghost.name))
            return false;

        // Check for non-existing spells.
        for (int sp = 0; sp < NUM_MONSTER_SPELL_SLOTS; ++sp)
            if (ghost.spells[sp] < 0 || ghost.spells[sp] >= NUM_SPELLS)
                return false;
    }
    return true;
}

int ghost_level_to_rank(const int xl)
{
    if (xl <  4) return 0;
    if (xl <  7) return 1;
    if (xl < 11) return 2;
    if (xl < 16) return 3;
    if (xl < 22) return 4;
    if (xl < 26) return 5;
    if (xl < 27) return 6;
    return 7;
}

static spell_type servitor_spells_primary[] =
{
    SPELL_LEHUDIBS_CRYSTAL_SPEAR,
    SPELL_IOOD,
    SPELL_IRON_SHOT,
    SPELL_BOLT_OF_FIRE,
    SPELL_BOLT_OF_COLD,
    SPELL_POISON_ARROW,
    SPELL_LIGHTNING_BOLT,
    SPELL_BOLT_OF_MAGMA,
    SPELL_BOLT_OF_DRAINING,
    SPELL_VENOM_BOLT,
    SPELL_THROW_ICICLE,
    SPELL_STONE_ARROW,
    SPELL_ISKENDERUNS_MYSTIC_BLAST,
    SPELL_NO_SPELL,                        // end search
};

static spell_type servitor_spells_secondary[] =
{
    SPELL_CONJURE_BALL_LIGHTNING,
    SPELL_FIREBALL,
    SPELL_AIRSTRIKE,
    SPELL_LRD,
    SPELL_FREEZING_CLOUD,
    SPELL_POISONOUS_CLOUD,
    SPELL_FORCE_LANCE,
    SPELL_DAZZLING_SPRAY,
    SPELL_MEPHITIC_CLOUD,
    SPELL_NO_SPELL,                        // end search
};

static spell_type servitor_spells_fallback[] =
{
    SPELL_STICKY_FLAME,
    SPELL_THROW_FLAME,
    SPELL_THROW_FROST,
    SPELL_FREEZE,
    SPELL_FLAME_TONGUE,
    SPELL_STING,
    SPELL_SANDBLAST,
    SPELL_MAGIC_DART,
    SPELL_NO_SPELL,                        // end search
};

static spell_type _best_aligned_spell(vector<spell_type> spells, skill_type skill)
{
    for (unsigned int i = 0; i < spells.size(); ++i)
    {
        if (spell_typematch(spells[i], skill))
            return spells[i];
    }

    // If we couldn't find any that match, just pick the first one
    return spells[0];
}

// Select servitor spells based on those known to the player
// (Primary determines whether we are populating the first 3 or next 2 slots)
bool ghost_demon::populate_servitor_spells(spell_type* spell_list, bool primary,
                                           skill_type primary_skill)
{
    vector<spell_type> candidates;
    const unsigned int num = (primary ? 3 : 2);
    const unsigned int offset = (primary ? 0 : 3);

    int i = 0;
    spell_type spell = SPELL_NO_SPELL;
    while ((spell = spell_list[i++]) != SPELL_NO_SPELL)
    {
        if (_know_spell(spell))
            candidates.push_back(spell);
    }

    if (candidates.size() >= num)
    {
        for (unsigned int j = offset; j < offset + num; ++j)
            spells[j] = candidates[j - offset];
    }
    else if (candidates.size() > 0)
    {
        // Choose the highest-level spell best aligned with our spell
        // skills to duplicate
        const spell_type copy_spell = _best_aligned_spell(candidates,
                                                            primary_skill);

        for (unsigned int j = offset; j < offset + num; ++j)
        {
            if (candidates.size() > j - offset)
                spells[j] = candidates[j - offset];
            else
                spells[j] = copy_spell;
        }
    }

    return candidates.size() > 0;
}

void ghost_demon::init_spellforged_servitor()
{
    // Determine highest magic skill (used for solving some tie-breakers)
    skill_type best_magic_skill = NUM_SKILLS;
    int skill_level = -1;
    for (int i = SK_FIRE_MAGIC; i <= SK_POISON_MAGIC; ++i)
    {
        if (you.skill((skill_type)i) >= skill_level)
        {
            skill_level = you.skill((skill_type)i);
            best_magic_skill = (skill_type)i;
        }
    }

    int pow = calc_spell_power(SPELL_SPELLFORGED_SERVITOR, true);

    colour = LIGHTMAGENTA; // cf. mon-data.h
    speed = 10;
    ev = 10;
    ac = 10;
    xl = 9 + div_rand_round(pow, 14);
    max_hp = 80;
    spellcaster = true;
    damage = 0;
    att_type = AT_NONE;

    // Give the servitor its spells
    bool primary   = populate_servitor_spells(servitor_spells_primary, true,
                                              best_magic_skill);
    bool secondary = populate_servitor_spells(servitor_spells_secondary, false,
                                              best_magic_skill);

    if (!primary && !secondary)
        populate_servitor_spells(servitor_spells_fallback, true, best_magic_skill);

}
