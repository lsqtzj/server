/*
 * This code is part of MaNGOS. Contributor & Copyright details are in AUTHORS/THANKS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ByteBuffer.h"
#include "TargetedMovementGenerator.h"
#include "Errors.h"
#include "Creature.h"
#include "Player.h"
#include "World.h"
#include "movement/MoveSplineInit.h"
#include "movement/MoveSpline.h"

//-----------------------------------------------//
template<class T, typename D>
void TargetedMovementGeneratorMedium<T, D>::_setTargetLocation(T& owner, bool updateDestination)
{
    if (!i_target.isValid() || !i_target->IsInWorld())
        return;

    if (owner.hasUnitState(UNIT_STAT_NOT_MOVE))
        return;

    float x, y, z;

    // i_path can be NULL in case this is the first call for this MMGen (via Update)
    // Can happen for example if no path was created on MMGen-Initialize because of the owner being stunned
    if (updateDestination || !i_path)
    {
        // prevent redundant micro-movement for pets, other followers.
        if (i_offset && i_target->IsWithinDistInMap(&owner, 2 * i_offset))
        {
            if (!owner.movespline->Finalized())
                return;

            owner.GetPosition(x, y, z);
        }
        else if (!i_offset)
        {
            // to nearest contact position
            i_target->GetContactPoint(&owner, x, y, z);
        }
        else
        {
            // to at i_offset distance from target and i_angle from target facing
            i_target->GetClosePoint(x, y, z, owner.GetObjectBoundingRadius(), i_offset, i_angle, &owner);
        }
    }
    else
    {
        // the destination has not changed, we just need to refresh the path (usually speed change)
        G3D::Vector3 end = i_path->getEndPosition();
        x = end.x;
        y = end.y;
        z = end.z;
    }

    if (!i_path)
        i_path = new PathFinder(&owner);

    // allow pets following their master to cheat while generating paths
    bool forceDest = (owner.GetTypeId() == TYPEID_UNIT && ((Creature*)&owner)->IsPet()
                      && owner.hasUnitState(UNIT_STAT_FOLLOW));
    i_path->calculate(x, y, z, forceDest);
    if (i_path->getPathType() & PATHFIND_NOPATH)
        return;

    D::_addUnitStateMove(owner);
    i_targetReached = false;
    m_speedChanged = false;

    Movement::MoveSplineInit init(owner);
    init.MovebyPath(i_path->getPath());
    init.SetWalk(((D*)this)->EnableWalking());
    init.Launch();
}

template<class T, typename D>
bool TargetedMovementGeneratorMedium<T, D>::Update(T& owner, const uint32& time_diff)
{
    if (!i_target.isValid() || !i_target->IsInWorld())
        return false;

    if (!owner.isAlive())
        return true;

    if (owner.hasUnitState(UNIT_STAT_NOT_MOVE))
    {
        D::_clearUnitStateMove(owner);
        return true;
    }

    // prevent movement while casting spells with cast time or channel time
    if (owner.IsNonMeleeSpellCasted(false, false,  true))
    {
        if (!owner.IsStopped())
            owner.StopMoving();
        return true;
    }

    // prevent crash after creature killed pet
    if (static_cast<D*>(this)->_lostTarget(owner))
    {
        D::_clearUnitStateMove(owner);
        return true;
    }

    bool targetMoved = false;
    i_recheckDistance.Update(time_diff);
    if (i_recheckDistance.Passed())
    {
        i_recheckDistance.Reset(100);

        // More distance let have better performance, less distance let have more sensitive reaction at target move.
        float allowed_dist = owner.GetObjectBoundingRadius() + sWorld.getConfig(CONFIG_FLOAT_RATE_TARGET_POS_RECALCULATION_RANGE);
        G3D::Vector3 dest = owner.movespline->FinalDestination();

        if (owner.GetTypeId() == TYPEID_UNIT && ((Creature*)&owner)->CanFly())
            targetMoved = !i_target->IsWithinDist3d(dest.x, dest.y, dest.z, allowed_dist);
        else
            targetMoved = !i_target->IsWithinDist2d(dest.x, dest.y, allowed_dist);
    }

    if (m_speedChanged || targetMoved)
        _setTargetLocation(owner, targetMoved);

    if (owner.movespline->Finalized())
    {
        if (i_angle == 0.f && !owner.HasInArc(0.01f, i_target.getTarget()))
            owner.SetInFront(i_target.getTarget());

        if (!i_targetReached)
        {
            i_targetReached = true;
            static_cast<D*>(this)->_reachTarget(owner);
        }
    }
    return true;
}

//-----------------------------------------------//
template<class T>
void ChaseMovementGenerator<T>::_reachTarget(T& owner)
{
    if (owner.CanReachWithMeleeAttack(this->i_target.getTarget()))
        owner.Attack(this->i_target.getTarget(), true);
}

template<>
void ChaseMovementGenerator<Player>::Initialize(Player& owner)
{
    owner.addUnitState(UNIT_STAT_CHASE);                    // _MOVE set in _SetTargetLocation after required checks
    _setTargetLocation(owner, true);
}

template<>
void ChaseMovementGenerator<Creature>::Initialize(Creature& owner)
{
    owner.SetWalk(false, false);                            // Chase movement is running
    owner.addUnitState(UNIT_STAT_CHASE);                    // _MOVE set in _SetTargetLocation after required checks
    _setTargetLocation(owner, true);
}

template<class T>
void ChaseMovementGenerator<T>::Finalize(T& owner)
{
    owner.clearUnitState(UNIT_STAT_CHASE | UNIT_STAT_CHASE_MOVE);
}

template<class T>
void ChaseMovementGenerator<T>::Interrupt(T& owner)
{
    owner.clearUnitState(UNIT_STAT_CHASE | UNIT_STAT_CHASE_MOVE);
}

template<class T>
void ChaseMovementGenerator<T>::Reset(T& owner)
{
    Initialize(owner);
}

//-----------------------------------------------//
template<>
bool FollowMovementGenerator<Creature>::EnableWalking() const
{
    return i_target.isValid() && i_target->IsWalking();
}

template<>
bool FollowMovementGenerator<Player>::EnableWalking() const
{
    return false;
}

template<>
void FollowMovementGenerator<Player>::_updateSpeed(Player& /*u*/)
{
    // nothing to do for Player
}

template<>
void FollowMovementGenerator<Creature>::_updateSpeed(Creature& u)
{
    // pet only sync speed with owner
    if (!((Creature&)u).IsPet() || !i_target.isValid() || i_target->GetObjectGuid() != u.GetOwnerGuid())
        return;

    u.UpdateSpeed(MOVE_RUN, true);
    u.UpdateSpeed(MOVE_WALK, true);
    u.UpdateSpeed(MOVE_SWIM, true);
}

template<>
void FollowMovementGenerator<Player>::Initialize(Player& owner)
{
    owner.addUnitState(UNIT_STAT_FOLLOW);                   // _MOVE set in _SetTargetLocation after required checks
    _updateSpeed(owner);
    _setTargetLocation(owner, true);
}

template<>
void FollowMovementGenerator<Creature>::Initialize(Creature& owner)
{
    owner.addUnitState(UNIT_STAT_FOLLOW);                   // _MOVE set in _SetTargetLocation after required checks
    _updateSpeed(owner);
    _setTargetLocation(owner, true);
}

template<class T>
void FollowMovementGenerator<T>::Finalize(T& owner)
{
    owner.clearUnitState(UNIT_STAT_FOLLOW | UNIT_STAT_FOLLOW_MOVE);
    _updateSpeed(owner);
}

template<class T>
void FollowMovementGenerator<T>::Interrupt(T& owner)
{
    owner.clearUnitState(UNIT_STAT_FOLLOW | UNIT_STAT_FOLLOW_MOVE);
    _updateSpeed(owner);
}

template<class T>
void FollowMovementGenerator<T>::Reset(T& owner)
{
    Initialize(owner);
}

//-----------------------------------------------//
template void TargetedMovementGeneratorMedium<Player, ChaseMovementGenerator<Player> >::_setTargetLocation(Player&, bool);
template void TargetedMovementGeneratorMedium<Player, FollowMovementGenerator<Player> >::_setTargetLocation(Player&, bool);
template void TargetedMovementGeneratorMedium<Creature, ChaseMovementGenerator<Creature> >::_setTargetLocation(Creature&, bool);
template void TargetedMovementGeneratorMedium<Creature, FollowMovementGenerator<Creature> >::_setTargetLocation(Creature&, bool);
template bool TargetedMovementGeneratorMedium<Player, ChaseMovementGenerator<Player> >::Update(Player&, const uint32&);
template bool TargetedMovementGeneratorMedium<Player, FollowMovementGenerator<Player> >::Update(Player&, const uint32&);
template bool TargetedMovementGeneratorMedium<Creature, ChaseMovementGenerator<Creature> >::Update(Creature&, const uint32&);
template bool TargetedMovementGeneratorMedium<Creature, FollowMovementGenerator<Creature> >::Update(Creature&, const uint32&);

template void ChaseMovementGenerator<Player>::_reachTarget(Player&);
template void ChaseMovementGenerator<Creature>::_reachTarget(Creature&);
template void ChaseMovementGenerator<Player>::Finalize(Player&);
template void ChaseMovementGenerator<Creature>::Finalize(Creature&);
template void ChaseMovementGenerator<Player>::Interrupt(Player&);
template void ChaseMovementGenerator<Creature>::Interrupt(Creature&);
template void ChaseMovementGenerator<Player>::Reset(Player&);
template void ChaseMovementGenerator<Creature>::Reset(Creature&);

template void FollowMovementGenerator<Player>::Finalize(Player&);
template void FollowMovementGenerator<Creature>::Finalize(Creature&);
template void FollowMovementGenerator<Player>::Interrupt(Player&);
template void FollowMovementGenerator<Creature>::Interrupt(Creature&);
template void FollowMovementGenerator<Player>::Reset(Player&);
template void FollowMovementGenerator<Creature>::Reset(Creature&);
