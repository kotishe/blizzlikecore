/*
 * This file is part of the BlizzLikeCore Project.
 * See CREDITS and LICENSE files for Copyright information.
 */

#include "Creature.h"
#include "MapManager.h"
#include "RandomMovementGenerator.h"
#include "Traveller.h"
#include "ObjectAccessor.h"
#include "DestinationHolderImp.h"
#include "Map.h"
#include "Util.h"
#include "CreatureFormations.h"

#define RUNNING_CHANCE_RANDOMMV 20                                  //will be "1 / RUNNING_CHANCE_RANDOMMV"

template<>
bool
RandomMovementGenerator<Creature>::GetDestination(float &x, float &y, float &z) const
{
    if (i_destinationHolder.HasArrived())
        return false;

    i_destinationHolder.GetDestination(x, y, z);
    return true;
}

template<>
void
RandomMovementGenerator<Creature>::_setRandomLocation(Creature &creature)
{
    float X,Y,Z,z,nx,ny,nz,ori,dist;

    creature.GetHomePosition(X, Y, Z, ori);

    z = creature.GetPositionZ();
    Map const* map = creature.GetBaseMap();

    // For 2D/3D system selection
    //bool is_land_ok  = creature.canWalk();
    //bool is_water_ok = creature.canSwim();
    bool is_air_ok   = creature.canFly();

    const float angle = rand_norm()*(M_PI*2);
    const float range = rand_norm()*wander_distance;
    const float distanceX = range * cos(angle);
    const float distanceY = range * sin(angle);

    nx = X + distanceX;
    ny = Y + distanceY;

    // prevent invalid coordinates generation
    BlizzLike::NormalizeMapCoord(nx);
    BlizzLike::NormalizeMapCoord(ny);

    dist = (nx - X)*(nx - X) + (ny - Y)*(ny - Y);

    if (is_air_ok)                                          // 3D system above ground and above water (flying mode)
    {
        // Limit height change
        const float distanceZ = rand_norm() * sqrtf(dist)/2.0f;
        nz = Z + distanceZ;
        float tz = map->GetHeight(nx, ny, nz-2.0f, false);  // Map check only, vmap needed here but need to alter vmaps checks for height.
        float wz = map->GetWaterLevel(nx, ny);

        // Problem here, we must fly above the ground and water, not under. Let's try on next tick
        if (tz >= nz || wz >= nz)
            return;
    }
    //else if (is_water_ok)                                 // 3D system under water and above ground (swimming mode)
    else                                                    // 2D only
    {
        dist = dist >= 100.0f ? 10.0f : sqrtf(dist);        // 10.0 is the max that vmap high can check (MAX_CAN_FALL_DISTANCE)

        // The fastest way to get an accurate result 90% of the time.
        // Better result can be obtained like 99% accuracy with a ray light, but the cost is too high and the code is too long.
        nz = map->GetHeight(nx, ny, Z+dist-2.0f, false);

        if (fabs(nz-Z) > dist)                              // Map check
        {
            nz = map->GetHeight(nx, ny, Z-2.0f, true);      // Vmap Horizontal or above

            if (fabs(nz-Z) > dist)
            {
                // Vmap Higher
                nz = map->GetHeight(nx, ny, Z+dist-2.0f, true);

                // let's forget this bad coords where a z cannot be find and retry at next tick
                if (fabs(nz-Z) > dist)
                    return;
            }
        }
    }

    Traveller<Creature> traveller(creature);

    creature.SetOrientation(creature.GetAngle(nx, ny));
    i_destinationHolder.SetDestination(traveller, nx, ny, nz);
    creature.addUnitState(UNIT_STAT_ROAMING);

    if (is_air_ok)
    {
        i_nextMoveTime.Reset(i_destinationHolder.GetTotalTravelTime());
        creature.AddUnitMovementFlag(MOVEFLAG_FLYING2);
    }
    //else if (is_water_ok)                                 // Swimming mode to be done with more than this check
    else
    {
        i_nextMoveTime.Reset(urand(500+i_destinationHolder.GetTotalTravelTime(), 10000+i_destinationHolder.GetTotalTravelTime()));
        creature.SetUnitMovementFlags(MOVEFLAG_WALK_MODE);
    }
}

template<>
void RandomMovementGenerator<Creature>::Initialize(Creature &creature)
{
    if (!creature.isAlive())
        return;

    if (!wander_distance)
        wander_distance = creature.GetRespawnRadius();

    if (irand(0,RUNNING_CHANCE_RANDOMMV) > 0)
        creature.AddUnitMovementFlag(MOVEFLAG_WALK_MODE);

    _setRandomLocation(creature);
}

template<>
void RandomMovementGenerator<Creature>::Reset(Creature &creature)
{
    Initialize(creature);
}

template<>
void RandomMovementGenerator<Creature>::Finalize(Creature &creature){}

template<>
bool RandomMovementGenerator<Creature>::Update(Creature &creature, const uint32 &diff)
{
    if (creature.hasUnitState(UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DISTRACTED))
    {
        i_nextMoveTime.Update(i_nextMoveTime.GetExpiry());  // Expire the timer
        creature.clearUnitState(UNIT_STAT_ROAMING);
        return true;
    }

    i_nextMoveTime.Update(diff);

    if (i_destinationHolder.HasArrived() && !creature.IsStopped() && !creature.canFly())
        creature.clearUnitState(UNIT_STAT_ROAMING | UNIT_STAT_MOVE);

    if (!i_destinationHolder.HasArrived() && creature.IsStopped())
        creature.addUnitState(UNIT_STAT_ROAMING);

    CreatureTraveller traveller(creature);

    if (i_destinationHolder.UpdateTraveller(traveller, diff, true))
    {
        if (i_nextMoveTime.Passed())
        {
            if (creature.canFly())
                creature.AddUnitMovementFlag(MOVEFLAG_FLYING2);
            else
                creature.SetUnitMovementFlags(irand(0,RUNNING_CHANCE_RANDOMMV) > 0 ? MOVEFLAG_WALK_MODE : MOVEFLAG_NONE);

            _setRandomLocation(creature);
        }
        else if (creature.isPet() && creature.GetOwner() && !creature.IsWithinDist(creature.GetOwner(), PET_FOLLOW_DIST + 2.5f))
        {
            creature.SetUnitMovementFlags(MOVEFLAG_NONE);
            _setRandomLocation(creature);
        }
    }
    return true;
}

