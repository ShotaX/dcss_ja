#include "AppHdr.h"

#include "branch.h"
#include "externs.h"
#include "files.h"
#include "player.h"
#include "traps.h"
#include "travel.h"
#include "branch-data.h"

FixedVector<int, NUM_BRANCHES> startdepth, brdepth;
branch_type root_branch;

const Branch& your_branch()
{
    return branches[you.where_are_you];
}

bool at_branch_bottom()
{
    return brdepth[you.where_are_you] == you.depth;
}

level_id current_level_parent()
{
    // Never called from X[], we don't have to support levels you're not on.
    if (!you.level_stack.empty())
        return you.level_stack.back().id;

    return find_up_level(level_id::current());
}

bool is_hell_subbranch(branch_type branch)
{
    return (branch >= BRANCH_FIRST_HELL
            && branch <= BRANCH_LAST_HELL
            && branch != BRANCH_VESTIBULE_OF_HELL);
}

bool is_random_subbranch(branch_type branch)
{
    return (parent_branch(branch) == BRANCH_LAIR
            && branch != BRANCH_SLIME_PITS)
           || branch == BRANCH_CRYPT
           || branch == BRANCH_FOREST;
}

bool is_connected_branch(branch_type branch)
{
    ASSERT_RANGE(branch, 0, NUM_BRANCHES);
    return !(branches[branch].branch_flags & BFLAG_NO_XLEV_TRAVEL);
}

bool is_connected_branch(level_id place)
{
    return is_connected_branch(place.branch);
}

branch_type str_to_branch(const string &branch, branch_type err)
{
    for (int i = 0; i < NUM_BRANCHES; ++i)
        if (branches[i].abbrevname && branches[i].abbrevname == branch)
            return static_cast<branch_type>(i);

    return err;
}

int current_level_ambient_noise()
{
    return branches[you.where_are_you].ambient_noise;
}

branch_type get_branch_at(const coord_def& pos)
{
    return level_id::current().get_next_level_id(pos).branch;
}

bool branch_is_unfinished(branch_type branch)
{
#if TAG_MAJOR_VERSION == 34
    if (branch == BRANCH_UNUSED || branch == BRANCH_DWARVEN_HALL)
        return true;
#endif
    return branch == BRANCH_FOREST;
}

branch_type parent_branch(branch_type branch)
{
    if (branch == BRANCH_TOMB && startdepth[BRANCH_CRYPT] == -1)
        return BRANCH_FOREST;
    else if (branch == root_branch)
        return NUM_BRANCHES;

    return branches[branch].parent_branch;
}
