/*
* ArcEmu MMORPG Server
* Copyright (C) 2005-2007 Ascent Team <http://www.ascentemu.com/>
* Copyright (C) 2008-2012 <http://www.ArcEmu.org/>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "StdAfx.h"
#include "Unit.h"
#include "UpdateData.h"
using namespace std;

uint32 GuidHigh2TypeId(uint32 guid_hi)
{
	switch (guid_hi)
	{
	case HIGHGUID_TYPE_ITEM:         return TYPEID_ITEM;
	//case HIGHGUID_CONTAINER:	     return TYPEID_CONTAINER; HIGHGUID_CONTAINER == HIGHGUID_ITEM currently
	case HIGHGUID_TYPE_UNIT:         return TYPEID_UNIT;
	case HIGHGUID_TYPE_PET:          return TYPEID_UNIT;
	case HIGHGUID_TYPE_PLAYER:       return TYPEID_PLAYER;
	case HIGHGUID_TYPE_GAMEOBJECT:   return TYPEID_GAMEOBJECT;
	case HIGHGUID_TYPE_DYNAMICOBJECT:return TYPEID_DYNAMICOBJECT;
	case HIGHGUID_TYPE_CORPSE:       return TYPEID_CORPSE;
	case HIGHGUID_TYPE_AREATRIGGER:  return TYPEID_AREATRIGGER;
	case HIGHGUID_TYPE_MO_TRANSPORT: return TYPEID_GAMEOBJECT;
	case HIGHGUID_TYPE_VEHICLE:      return TYPEID_UNIT;
	}
	return NUM_CLIENT_OBJECT_TYPES;                         // unknown
}

Object::Object() : m_position(0, 0, 0, 0), m_spawnLocation(0, 0, 0, 0)
{
	m_mapId = MAPID_NOT_IN_WORLD;
	m_zoneId = 0;

	m_uint32Values = 0;
	m_objectUpdated = false;

	m_currentSpell = NULL;
	m_valuesCount = 0;

	//official Values
	m_walkSpeed = 2.5f;
	m_runSpeed = 7.0f;
	m_base_runSpeed = m_runSpeed;
	m_base_walkSpeed = m_walkSpeed;

	m_flySpeed = 7.0f;
	m_backFlySpeed = 4.5f;

	m_backWalkSpeed = 4.5f;	// this should really be named m_backRunSpeed
	m_swimSpeed = 4.722222f;
	m_backSwimSpeed = 2.5f;
	m_turnRate = M_PI_FLOAT;
	m_pitchRate = 3.14f;

	m_phase = 1; //Set the default phase: 00000000 00000000 00000000 00000001

	m_mapMgr = 0;
	m_mapCell_x = m_mapCell_y = uint32(-1);

	m_faction = NULL;
	m_factionDBC = NULL;

	m_instanceId = INSTANCEID_NOT_IN_WORLD;
	Active = false;
	m_inQueue = false;
	m_extensions = NULL;
	m_loadedFromDB = false;

	m_faction = dbcFactionTemplate.LookupRow(0);
	m_factionDBC = dbcFaction.LookupRow(0);

	m_objectsInRange.clear();
	m_inRangePlayers.clear();
	m_oppFactsInRange.clear();
	m_sameFactsInRange.clear();

	Active = false;
}

Object::~Object()
{
	if (!IsItem())
		ARCEMU_ASSERT(!m_inQueue);

	ARCEMU_ASSERT(!IsInWorld());

	// for linux
	m_instanceId = INSTANCEID_NOT_IN_WORLD;

	if (m_extensions != NULL)
		delete m_extensions;

	if (m_currentSpell)
	{
		m_currentSpell->cancel();
		m_currentSpell = NULL;
	}

	//avoid leaving traces in eventmanager. Have to work on the speed. Not all objects ever had events so list iteration can be skipped
	sEventMgr.RemoveEvents(this);
}

void Object::Init()
{

}

void Object::_Create(uint32 mapid, float x, float y, float z, float ang)
{
	m_mapId = mapid;
	m_position.ChangeCoords(x, y, z, ang);
	m_spawnLocation.ChangeCoords(x, y, z, ang);
	m_lastMapUpdatePosition.ChangeCoords(x, y, z, ang);
}

//!!! This method needs improvements + cleanup

uint32 Object::BuildCreateUpdateBlockForPlayer(ByteBuffer* data, Player* target)
{
    uint8  updateType = UPDATETYPE_CREATE_OBJECT;
    uint16 flags = m_updateFlag;

    /** lower flag1 **/
    if (target == this)                                      // building packet for yourself
        flags |= UPDATEFLAG_SELF;

    switch (m_objectTypeId)
    {
    case TYPEID_PLAYER:
    case TYPEID_UNIT:
    case TYPEID_DYNAMICOBJECT:
    case TYPEID_AREATRIGGER:
        updateType = UPDATETYPE_CREATE_OBJECT;
        break;
        //case HIGHGUID_TYPE_UNIT:
        //case TYPEID_UNIT:
        //{
        //	if (TempSummon const* summon = ToUnit()->ToTempSummon())
        //		if (IS_PLAYER_GUID(summon->GetSummonerGUID()))
        //			updateType = UPDATETYPE_CREATE_OBJECT;

        //	break;
        //}
        //case HIGHGUID_TYPE_CORPSE:
        //{
        //	if (IS_PLAYER_GUID(ToCorpse()->GetOwnerGUID()))
        //		updateType = UPDATETYPE_CREATE_OBJECT2;
        //	break;
        //}
    case TYPEID_GAMEOBJECT:
    {
        updateType = UPDATETYPE_CREATE_OBJECT2;
        break;
    }
    default:
        break;
    }

    if (flags & UPDATEFLAG_HAS_POSITION)
    {
        // UPDATETYPE_CREATE_OBJECT2 for some gameobject types...
        if (IsGameObject())
        {
            switch (GetByte(GAMEOBJECT_FIELD_PERCENT_HEALTH, 0))
            {
            case GAMEOBJECT_TYPE_TRAP:
            case GAMEOBJECT_TYPE_DUEL_ARBITER:
            case GAMEOBJECT_TYPE_FLAGSTAND:
            case GAMEOBJECT_TYPE_FLAGDROP:
                updateType = UPDATETYPE_CREATE_OBJECT2;
                break;
            default:
                break;
            }
        }
    }

    if (IsUnit())
        if (TO_UNIT(this)->GetTargetGUID())
            flags |= UPDATEFLAG_HAS_TARGET;

    *data << updateType;

    // we shouldn't be here, under any circumstances, unless we have a wowguid..
    ASSERT(m_wowGuid.GetNewGuidLen());
    *data << m_wowGuid;

    *data << m_objectTypeId;

    _BuildMovementUpdate(data, flags, 0, target);

    // we have dirty data, or are creating for ourself.
    UpdateMask updateMask;
    updateMask.SetCount(m_valuesCount);
    _SetCreateBits(&updateMask, target);

    // this will cache automatically if needed
    _BuildValuesUpdate(data, &updateMask, target);

    /// @TODO Implement dynamic update fields
    *data << uint8(0);
    // update count: 1 ;)
    return 1;
}

WorldPacket* Object::BuildFieldUpdatePacket(uint32 index, uint32 value)
{
	WorldPacket* packet = new WorldPacket(1500);
	packet->SetOpcode(SMSG_UPDATE_OBJECT);

	*packet << (uint32)1;

	*packet << (uint8)UPDATETYPE_VALUES;		// update type == update
	packet->append(GetNewGUID());

	uint32 mBlocks = index / 32 + 1;
	*packet << (uint8)mBlocks;

	for (uint32 dword_n = mBlocks - 1; dword_n; dword_n--)
		*packet << (uint32)0;

	*packet << (((uint32)(1)) << (index % 32));
	*packet << value;

	return packet;
}

void Object::BuildFieldUpdatePacket(Player* Target, uint32 Index, uint32 Value)
{
	ByteBuffer buf(500);
	buf << uint8(UPDATETYPE_VALUES);
	buf.append(GetNewGUID());

	uint32 mBlocks = Index / 32 + 1;
	buf << (uint8)mBlocks;

	for (uint32 dword_n = mBlocks - 1; dword_n; dword_n--)
		buf << (uint32)0;

	buf << (((uint32)(1)) << (Index % 32));
	buf << Value;

	Target->PushUpdateData(&buf, 1);
}

void Object::BuildFieldUpdatePacket(ByteBuffer* buf, uint32 Index, uint32 Value)
{
	*buf << uint8(UPDATETYPE_VALUES);
	*buf << GetNewGUID();

	uint32 mBlocks = Index / 32 + 1;
	*buf << (uint8)mBlocks;

	for (uint32 dword_n = mBlocks - 1; dword_n; dword_n--)
		*buf << (uint32)0;

	*buf << (((uint32)(1)) << (Index % 32));
	*buf << Value;
}

uint32 Object::BuildValuesUpdateBlockForPlayer(ByteBuffer* data, Player* target)
{
	UpdateMask updateMask;
	updateMask.SetCount(m_valuesCount);
	_SetUpdateBits(&updateMask, target);
	for (uint32 x = 0; x < m_valuesCount; ++x)
	{
		if (updateMask.GetBit(x))
		{
			*data << (uint8)UPDATETYPE_VALUES;		// update type == update
			ARCEMU_ASSERT(m_wowGuid.GetNewGuidLen() > 0);
			*data << m_wowGuid;

			_BuildValuesUpdate(data, &updateMask, target);

			/// @TODO Implement dynamic update fields
			*data << uint8(0);

			return 1;
		}
	}

	return 0;
}

uint32 Object::BuildValuesUpdateBlockForPlayer(ByteBuffer* buf, UpdateMask* mask)
{
	*buf << (uint8)UPDATETYPE_VALUES;

	ARCEMU_ASSERT(m_wowGuid.GetNewGuidLen() > 0);
	*buf << m_wowGuid;

	_BuildValuesUpdate(buf, mask, 0);

	/// @TODO Implement dynamic update fields
	*buf << uint8(0);

	// 1 update.
	return 1;
}

uint32 TimeStamp();

void Object::_BuildMovementUpdate(ByteBuffer* data, uint16 flags, uint32 flags2, Player* target)
{
	uint16 moveflags2 = 0;
	bool hasTransport = false; // flags & UPDATEFLAG_TRANSPORT;
	bool isSplineEnabled = false; // spline not supported
	bool hasPitch = false; // (flags2 & (MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING)) || (moveflags2 & MOVEFLAG2_ALLOW_PITCHING); // (hasPitch == swimming) flags2 & (MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING)) || (moveflags2 & MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING)
	bool haveFallData = false; //; // = flags2 & MOVEFLAG2_INTERPOLATED_TURNING;
	bool hasFallDirection = false; // flags & MOVEMENTFLAG_FALLING;
	bool hasSplineElevation = false; //flags & 0x02000000; // MOVEMENTFLAG_SPLINE_ELEVATION, I think flags(1); 2: spline not supported/enabled
	bool hasOrientation = GetTypeId() != TYPEID_ITEM && GetTypeId() != TYPEID_CONTAINER;
	//bool hasVehicle = false; //flags & UPDATEFLAG_VEHICLE;
	//bool hasAnimKits = false; //flags & UPDATEFLAG_ANIMKITS;
	bool hasLiving = flags & UPDATEFLAG_LIVING;
	bool hasStationaryPosition = flags & UPDATEFLAG_HAS_POSITION;
	bool hasGobjectRotation = false; // flags & UPDATEFLAG_ROTATION;
	bool hasTransportPosition = false; // flags & UPDATEFLAG_POSITION;
	bool hasTarget = flags & UPDATEFLAG_HAS_TARGET;
	bool hasVehicle = false; //flags & UPDATEFLAG_VEHICLE;
	bool hasAnimKits = false; //flags & UPDATEFLAG_ANIMKITS;

	data->WriteBit(0);
	data->WriteBit(hasAnimKits);
	data->WriteBit(hasLiving);
	data->WriteBit(0);
	data->WriteBit(0);
	data->WriteBits(0, 22);
	data->WriteBit(hasVehicle);
	data->WriteBit(0);
	data->WriteBit(0);
	data->WriteBit(hasTransport);
	data->WriteBit(flags & UPDATEFLAG_ROTATION);
	data->WriteBit(0);
	data->WriteBit(flags & UPDATEFLAG_SELF);
	data->WriteBit(hasTarget);
	data->WriteBit(0);
	data->WriteBit(0);
	data->WriteBit(0);
	data->WriteBit(0);
	data->WriteBit(hasTransportPosition);
	data->WriteBit(0);
	data->WriteBit(hasStationaryPosition);

	Player* pThis = TO< Player* >(this);

	MovementInfo* moveinfo = NULL;

	if (IsPlayer())
	{
		if (pThis->GetSession())
			moveinfo = pThis->GetSession()->GetMovementInfo();
		if (target == this)
		{
			// Updating our last speeds.
			pThis->UpdateLastSpeeds();
		}
	}
	Creature* uThis = NULL;
	if (IsCreature())
		uThis = TO< Creature* >(this);


	if (hasLiving)
	{
		uint32 movementFlags = 0;
		uint16 movementFlagsExtra = 0;
		Unit const* self = TO_UNIT(this);

		movementFlags = self->m_movementInfo.GetMovementFlags();
		movementFlagsExtra = self->m_movementInfo.GetExtraMovementFlags();
		hasFallDirection = self->HasUnitMovementFlag(MOVEMENTFLAG_FALLING);
		haveFallData = hasFallDirection || self->m_movementInfo.jump.fallTime != 0;
		hasPitch = self->HasUnitMovementFlag(MovementFlags(MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING)) || self->HasExtraUnitMovementFlag(MOVEFLAG2_ALLOW_PITCHING);
		hasSplineElevation = self->HasUnitMovementFlag(MOVEMENTFLAG_SPLINE_ELEVATION);
		bool hasUnitTransport = false; // self->m_movementInfo.transport.guid;
		bool hasSpline = false;

		if (pThis && pThis->transporter_info.guid != 0)
			flags2 |= MOVEMENTFLAG_DISABLE_GRAVITY;
		else if (uThis != NULL && transporter_info.guid != 0 && uThis->transporter_info.guid != 0)
			flags2 |= MOVEMENTFLAG_DISABLE_GRAVITY;

		if ((pThis != NULL) && pThis->isRooted())
			flags2 |= MOVEMENTFLAG_ROOT;
		else if ((uThis != NULL) && uThis->isRooted())
			flags2 |= MOVEMENTFLAG_ROOT;

		if (GetTypeId() == TYPEID_UNIT)
		{
			flags2 &= MOVEMENTFLAG_MASK_CREATURE_ALLOWED;
		}

		ObjectGuid guid = GetGUID();

		data->WriteBit(guid[2]);
		data->WriteBit(0);
		data->WriteBit(!hasPitch);
		data->WriteBit(hasUnitTransport);
		data->WriteBit(0);

		if (hasUnitTransport)
        {
            ObjectGuid transGuid = self->m_movementInfo.transport.guid;
            data->WriteBit(transGuid[4]);
            data->WriteBit(transGuid[2]);
            data->WriteBit(self->m_movementInfo.transport.time3 && self->m_movementInfo.transport.guid);
            data->WriteBit(transGuid[0]);
            data->WriteBit(transGuid[1]);
            data->WriteBit(transGuid[3]);
            data->WriteBit(transGuid[6]);
            data->WriteBit(transGuid[7]);
            data->WriteBit(self->m_movementInfo.transport.time2 && self->m_movementInfo.transport.guid);
            data->WriteBit(transGuid[5]);
        }

		data->WriteBit(!self->m_movementInfo.time);
		data->WriteBit(guid[6]);
		data->WriteBit(guid[4]);
		data->WriteBit(guid[3]);

		data->WriteBit(G3D::fuzzyEq(self->GetOrientation(), 0.0f));
		
		data->WriteBit(!self->GetMovementCounter());
		data->WriteBit(guid[5]);
		data->WriteBits(0, 22); // Forces
		data->WriteBit(!movementFlags);
		data->WriteBits(0, 19);
		data->WriteBit(haveFallData);

		if (movementFlags)
			data->WriteBits(movementFlags, 30);

		data->WriteBit(!hasSplineElevation);
		data->WriteBit(hasSpline);
		data->WriteBit(0);
		data->WriteBit(guid[0]);
		data->WriteBit(guid[7]);
		data->WriteBit(guid[1]);

		// if (hasSpline)
		// ...

		data->WriteBit(!movementFlagsExtra);

		if (haveFallData)
			data->WriteBit(hasFallDirection);

		if (movementFlagsExtra)
			data->WriteBits(movementFlagsExtra, 13);
	}

    if (hasTransportPosition)
    {
		Unit const* self = TO_UNIT(this);
        ObjectGuid transGuid = self->m_movementInfo.transport.guid;
        data->WriteBit(transGuid[4]);
        data->WriteBit(transGuid[1]);
        data->WriteBit(transGuid[0]);
        data->WriteBit(self->m_movementInfo.transport.time2 && self->m_movementInfo.transport.guid);
        data->WriteBit(transGuid[6]);
        data->WriteBit(transGuid[5]);
        data->WriteBit(transGuid[3]);
        data->WriteBit(transGuid[2]);
        data->WriteBit(transGuid[7]);
        data->WriteBit(self->m_movementInfo.transport.time3 && self->m_movementInfo.transport.guid);
    }

	/*
	if (hasAnimKits)
	{
		data->WriteBit(1);  // Missing AnimKit1
		data->WriteBit(1);  // Missing AnimKit2
		data->WriteBit(1);  // Missing AnimKit3
	}
	*/

	if (hasTarget)
	{
		ObjectGuid victimGuid = TO_UNIT(this)->GetTargetGUID();

		data->WriteBit(victimGuid[4]);
		data->WriteBit(victimGuid[6]);
		data->WriteBit(victimGuid[5]);
		data->WriteBit(victimGuid[2]);
		data->WriteBit(victimGuid[0]);
		data->WriteBit(victimGuid[1]);
		data->WriteBit(victimGuid[3]);
		data->WriteBit(victimGuid[7]);
	}

	data->FlushBits();

	if (hasLiving)
	{
		Unit const* self = TO_UNIT(this);
		ObjectGuid guid = GetGUID();
		/*
		 if (hasTransport)
        {
            ObjectGuid transGuid = self->m_movementInfo.transport.guid;

            data->WriteByteSeq(transGuid[7]);
            *data << float(self->GetTransOffsetX());

            if (self->m_movementInfo.transport.time3 && self->m_movementInfo.transport.guid)
                *data << uint32(self->m_movementInfo.transport.time3);

            *data << float(self->GetTransOffsetO());
            *data << float(self->GetTransOffsetY());
            data->WriteByteSeq(transGuid[4]);
            data->WriteByteSeq(transGuid[1]);
            data->WriteByteSeq(transGuid[3]);
            *data << float(self->GetTransOffsetZ());
            data->WriteByteSeq(transGuid[5]);

            if (self->m_movementInfo.transport.time2 && self->m_movementInfo.transport.guid)
                *data << uint32(self->m_movementInfo.transport.time2);

            data->WriteByteSeq(transGuid[0]);
            *data << int8(self->GetTransSeat());
            data->WriteByteSeq(transGuid[6]);
            data->WriteByteSeq(transGuid[2]);
            *data << uint32(self->GetTransTime());
        }*/

		data->WriteByteSeq(guid[4]);

		// if (hasSpline)
		// ...

		*data << m_flySpeed;

		if (self->GetMovementCounter())
			*data << uint32(self->GetMovementCounter());

		data->WriteByteSeq(guid[2]);

		if (haveFallData)
		{
			if (hasFallDirection)
			{
				*data << float(self->m_movementInfo.jump.xyspeed);
				*data << float(self->m_movementInfo.jump.cosAngle);
				*data << float(self->m_movementInfo.jump.sinAngle);
			}

			*data << uint32(self->m_movementInfo.jump.fallTime);
			*data << float(self->m_movementInfo.jump.zspeed);
		}

        data->WriteByteSeq(guid[1]);
		*data << m_turnRate;

		if (self->m_movementInfo.time)
			*data << uint32(self->m_movementInfo.time);

		*data << m_backWalkSpeed;

		if (hasSplineElevation)
			*data << float(self->m_movementInfo.splineElevation);

		data->WriteByteSeq(guid[7]);
		*data << m_pitchRate;
		*data << self->GetPositionX();

		if (hasPitch)
			*data << float(self->m_movementInfo.pitch);

		if (!G3D::fuzzyEq(self->GetOrientation(), 0.0f))
			*data << float(Position::NormalizeOrientation(self->GetOrientation()));

		if (m_walkSpeed == 0)
			*data << 8.0f;
		else
			*data << m_walkSpeed;

		*data << self->GetPositionY();
		*data << m_backFlySpeed;
		data->WriteByteSeq(guid[3]);
		data->WriteByteSeq(guid[5]);
		data->WriteByteSeq(guid[6]);
		data->WriteByteSeq(guid[0]);
		*data << m_backSwimSpeed;

		if (m_runSpeed == 0)
			*data << 8.0f;
		else
			*data << m_runSpeed;

		*data << m_swimSpeed;
		*data << self->GetPositionZ();
	}

	  /*  if (hasTransportPosition)
    {
        //WorldObject const* self = static_cast<WorldObject const*>(this);
		Unit const* self = TO_UNIT(this);
        ObjectGuid transGuid = self->m_movementInfo.transport.guid;

        if (self->m_movementInfo.transport.time2 && self->m_movementInfo.transport.guid)
            *data << uint32(self->m_movementInfo.transport.time2);

        *data << float(self->GetTransOffsetY());
        *data << int8(self->GetTransSeat());
        *data << float(self->GetTransOffsetX());
        data->WriteBit(transGuid[2]);
        data->WriteBit(transGuid[4]);
        data->WriteBit(transGuid[1]);

        if (self->m_movementInfo.transport.time3 && self->m_movementInfo.transport.guid)
            *data << uint32(self->m_movementInfo.transport.time3);

        *data << uint32(self->GetTransTime());

        *data << float(self->GetTransOffsetO());
        *data << float(self->GetTransOffsetZ());

        data->WriteBit(transGuid[6]);
        data->WriteBit(transGuid[0]);
        data->WriteBit(transGuid[5]);
        data->WriteBit(transGuid[3]);
        data->WriteBit(transGuid[7]);
    }*/

	if (hasTarget)
	{
		ObjectGuid victimGuid = TO_UNIT(this)->GetTargetGUID();
		data->WriteByteSeq(victimGuid[7]);
		data->WriteByteSeq(victimGuid[1]);
		data->WriteByteSeq(victimGuid[5]);
		data->WriteByteSeq(victimGuid[2]);
		data->WriteByteSeq(victimGuid[6]);
		data->WriteByteSeq(victimGuid[3]);
		data->WriteByteSeq(victimGuid[0]);
		data->WriteByteSeq(victimGuid[4]);
	}

	if (hasVehicle)
	{
		Unit const* self = TO_UNIT(this);
		// vehicle id (uint32)
		// orientation (float)
	}

	if (hasStationaryPosition)
	{
		*data << (float)m_position.y;
		*data << GetPositionZ();
		*data << (float)m_position.o;
		*data << (float)m_position.x;
	}

	/*
	if (hasAnimKits)
	{
		if (hasAnimKit3)
		*data << uint16(animKit3);
		if (hasAnimKit2)
		*data << uint16(animKit2);
		if (hasAnimKit1)
		*data << uint16(animKit1);
	}
	*/
	/*
	if (hasTransport)
    {
        GameObject const* go = ToGameObject();

        if (go && go->ToTransport())
            *data << uint32(go->GetGOValue()->Transport.PathProgress);
        else
            *data << uint32(getMSTime());
    }
	*/

	if (flags & UPDATEFLAG_ROTATION)
	{
		if (IsGameObject())
			*data << TO< GameObject* >(this)->m_rotation;
	}

	// if (hasLiving && hasSpline)
	// ...
}


//=======================================================================================
//  Creates an update block with the values of this object as
//  determined by the updateMask.
//=======================================================================================
void Object::_BuildValuesUpdate(ByteBuffer* data, UpdateMask* updateMask, Player* target)
{
	bool activate_quest_object = false;
	bool reset = false;
	uint32 oldflags = 0;

	if (updateMask->GetBit(OBJECT_FIELD_GUID) && target)	   // We're creating.
	{
		if (IsCreature())
		{
			Creature* pThis = TO< Creature* >(this);
			if (pThis->IsTagged() && (pThis->loot.gold || pThis->loot.items.size()))
			{
				// Let's see if we're the tagger or not.
				oldflags = m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS];
				uint32 Flags = m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS];
				uint32 oldFlags = 0;

				if (pThis->GetTaggerGUID() == target->GetGUID())
				{
					// Our target is our tagger.
					oldFlags = U_DYN_FLAG_TAGGED_BY_OTHER;

					if (Flags & U_DYN_FLAG_TAGGED_BY_OTHER)
						Flags &= ~oldFlags;

					if (!(Flags & U_DYN_FLAG_LOOTABLE) && pThis->HasLootForPlayer(target))
						Flags |= U_DYN_FLAG_LOOTABLE;
				}
				else
				{
					// Target is not the tagger.
					oldFlags = U_DYN_FLAG_LOOTABLE;

					if (!(Flags & U_DYN_FLAG_TAGGED_BY_OTHER))
						Flags |= U_DYN_FLAG_TAGGED_BY_OTHER;

					if (Flags & U_DYN_FLAG_LOOTABLE)
						Flags &= ~oldFlags;
				}

				m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS] = Flags;

				updateMask->SetBit(OBJECT_FIELD_DYNAMIC_FLAGS);

				reset = true;
			}
		}

		if (target && IsGameObject())
		{
			GameObject* go = TO_GAMEOBJECT(this);
			QuestLogEntry* qle;
			GameObjectInfo* info;
			if (go->HasQuests())
			{
				std::list<QuestRelation*>::iterator itr;
				for (itr = go->QuestsBegin(); itr != go->QuestsEnd(); ++itr)
				{
					QuestRelation* qr = (*itr);
					if (qr != NULL)
					{
						Quest* qst = qr->qst;
						if (qst != NULL)
						{
							if ((qr->type & QUESTGIVER_QUEST_START && !target->HasQuest(qst->id))
								|| (qr->type & QUESTGIVER_QUEST_END && target->HasQuest(qst->id))
								)
							{
								activate_quest_object = true;
								break;
							}
						}
					}
				}
			}
			else
			{
				info = go->GetInfo();
				if (info && (info->goMap.size() || info->itemMap.size()))
				{
					for (GameObjectGOMap::iterator itr = go->GetInfo()->goMap.begin(); itr != go->GetInfo()->goMap.end(); ++itr)
					{
						qle = target->GetQuestLogForEntry(itr->first->id);
						if (qle != NULL)
						{
							if (qle->GetQuest()->count_required_mob == 0)
								continue;
							for (uint32 i = 0; i < 4; ++i)
							{
								if (qle->GetQuest()->required_mob[i] == static_cast<int32>(go->GetEntry()) && qle->GetMobCount(i) < qle->GetQuest()->required_mobcount[i])
								{
									activate_quest_object = true;
									break;
								}
							}
							if (activate_quest_object)
								break;
						}
					}

					if (!activate_quest_object)
					{
						for (GameObjectItemMap::iterator itr = go->GetInfo()->itemMap.begin();
							itr != go->GetInfo()->itemMap.end();
							++itr)
						{
							for (std::map<uint32, uint32>::iterator it2 = itr->second.begin();
								it2 != itr->second.end();
								++it2)
							{
								if ((qle = target->GetQuestLogForEntry(itr->first->id)) != 0)
								{
									if (target->GetItemInterface()->GetItemCount(it2->first) < it2->second)
									{
										activate_quest_object = true;
										break;
									}
								}
							}
							if (activate_quest_object)
								break;
						}
					}
				}
			}
		}
	}


	if (activate_quest_object)
	{
        oldflags = m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS];
        if (!updateMask->GetBit(OBJECT_FIELD_DYNAMIC_FLAGS))
            updateMask->SetBit(OBJECT_FIELD_DYNAMIC_FLAGS);
        m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS] = 1 | 8; // 8 to show sparkles
		reset = true;
	}

	ARCEMU_ASSERT(updateMask && updateMask->GetCount() == m_valuesCount);
	uint32 bc;
	uint32 values_count;
	if (m_valuesCount > (2 * 0x20))    //if number of blocks > 2->  unit and player+item container
	{
		bc = updateMask->GetUpdateBlockCount();
		values_count = min<uint32>(bc * 32, m_valuesCount);

	}
	else
	{
		bc = updateMask->GetBlockCount();
		values_count = m_valuesCount;
	}

	*data << (uint8)bc;
	data->append(updateMask->GetMask(), bc * 4);

	for (uint32 index = 0; index < values_count; index++)
	{
		if (updateMask->GetBit(index))
		{
			*data << m_uint32Values[index];
		}
	}

	if (reset)
	{
		switch (GetTypeId())
		{
		case TYPEID_UNIT:
			m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS] = oldflags;
			break;
		case TYPEID_GAMEOBJECT:
            m_uint32Values[OBJECT_FIELD_DYNAMIC_FLAGS] = oldflags;
			break;
		}
	}
}

bool Object::SetPosition(const LocationVector & v, bool allowPorting /* = false */)
{
	bool updateMap = false, result = true;

	//if (m_position.x != v.x || m_position.y != v.y)
    if (m_movementInfo.pos.GetPositionX() != v.x || m_movementInfo.pos.GetPositionY() != v.y)
		updateMap = true;

	//m_position = const_cast<LocationVector &>(v);
    m_movementInfo.pos.m_positionX = v.x;
    m_movementInfo.pos.m_positionY = v.y;
    m_movementInfo.pos.m_positionZ = v.z;
    m_movementInfo.pos.SetOrientation(v.o);

	if (!allowPorting && v.z < -500)
	{
		//m_position.z = 500;
        m_movementInfo.pos.m_positionZ = 500;
		LOG_ERROR("setPosition: fell through map; height ported");

		result = false;
	}

	if (IsInWorld() && updateMap)
	{
		m_mapMgr->ChangeObjectLocation(this);
	}

	return result;
}

bool Object::SetPosition2(const Position &pos, bool allowPorting /* = false */)
{
	return SetPosition(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), allowPorting);
}

bool Object::SetPosition(float newX, float newY, float newZ, float newOrientation, bool allowPorting)
{
	/*if (!allowPorting && newZ < -500)
	{
		m_position.z = 500;
		LOG_ERROR("setPosition: fell through map; height ported");

		return false;
	}

	bool updateMap = false;
	bool turn = (GetOrientation() != newOrientation);
	bool relocated = (allowPorting || GetPositionX() != newX || GetPositionY() != newY || GetPositionZ() != newZ);

	if (IsGameObject() && TO< GameObject* >(this)->GetInfo()->Type == GAMEOBJECT_TYPE_TRANSPORT)
		updateMap = true;

	if (m_lastMapUpdatePosition.Distance2DSq(newX, newY) > 4.0f)		/* 2.0f
		updateMap = true;

	if (turn)
		//RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TURNING);

		if (relocated)
		{
			//RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_MOVE);
			m_position.ChangeCoords(newX, newY, newZ, newOrientation);
		}
		else if (turn)
		{
			Unit *u = static_cast< Unit* >(this);
			if (u->GetVehicleComponent() != NULL)
			{
				u->GetVehicleComponent()->MovePassengers(newX, newY, newZ, newOrientation);
			}
			//UpdateOrientation(orientation);
		}

	// code block for underwater state update
	//UpdateUnderwaterState(GetMap(), x, y, z);

	if (IsInWorld() && updateMap)
	{
		m_lastMapUpdatePosition.ChangeCoords(newX, newY, newZ, newOrientation);
		m_mapMgr->ChangeObjectLocation(this);

		if (IsPlayer() && TO< Player* >(this)->GetGroup() && TO< Player* >(this)->m_last_group_position.Distance2DSq(m_position) > 25.0f)       // distance of 5.0
		{
			TO< Player* >(this)->GetGroup()->HandlePartialChange(PARTY_UPDATE_FLAG_POSITION, TO< Player* >(this));
		}
	}
	m_lastMapUpdatePosition.ChangeCoords(newX, newY, newZ, newOrientation);
	return (relocated || turn);*/

	bool updateMap = false, result = true;

	ARCEMU_ASSERT(!isnan(newX) && !isnan(newY) && !isnan(newOrientation));

	//It's a good idea to push through EVERY transport position change, no matter how small they are. By: VLack aka. VLsoft
	if (IsGameObject() && TO< GameObject* >(this)->GetInfo()->Type == GAMEOBJECT_TYPE_TRANSPORT)
	updateMap = true;

	//if (m_position.x != newX || m_position.y != newY)
	//updateMap = true;
	if (m_lastMapUpdatePosition.Distance2DSq(newX, newY) > 4.0f)		/* 2.0f*/
	updateMap = true;

	//m_position.ChangeCoords(newX, newY, newZ, newOrientation);
    m_movementInfo.pos.m_positionX = newX;
    m_movementInfo.pos.m_positionY = newY;
    m_movementInfo.pos.m_positionZ = newZ;
    m_movementInfo.pos.SetOrientation(newOrientation);

	if (!allowPorting && newZ < -500)
	{
	    //m_position.z = 500;
        m_movementInfo.pos.m_positionZ = 500;
	    LOG_ERROR("setPosition: fell through map; height ported");

	    result = false;
	}

	if (IsInWorld() && updateMap)
	{
	m_lastMapUpdatePosition.ChangeCoords(newX, newY, newZ, newOrientation);
	m_mapMgr->ChangeObjectLocation(this);

	if (IsPlayer() && TO< Player* >(this)->GetGroup() && TO< Player* >(this)->m_last_group_position.Distance2DSq(m_position) > 25.0f)       // distance of 5.0
	{
	TO< Player* >(this)->GetGroup()->HandlePartialChange(PARTY_UPDATE_FLAG_POSITION, TO< Player* >(this));
	}
	}

	if (IsUnit()){
	Unit *u = static_cast< Unit* >(this);
	if (u->GetVehicleComponent() != NULL)
	u->GetVehicleComponent()->MovePassengers(newX, newY, newZ, newOrientation);
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////
/// Fill the object's Update Values from a space delimitated list of values.
void Object::LoadValues(const char* data)
{
	// thread-safe ;) strtok is not.
	std::string ndata = data;
	std::string::size_type last_pos = 0, pos = 0;
	uint32 index = 0;
	uint32 val;
	do
	{
		// prevent overflow
		if (index >= m_valuesCount)
		{
			break;
		}
		pos = ndata.find(" ", last_pos);
		val = atol(ndata.substr(last_pos, (pos - last_pos)).c_str());
		if (m_uint32Values[index] == 0)
			m_uint32Values[index] = val;
		last_pos = pos + 1;
		++index;
	} while (pos != std::string::npos);
}

void Object::_SetUpdateBits(UpdateMask* updateMask, Player* target) const
{
	*updateMask = m_updateMask;
}


void Object::_SetCreateBits(UpdateMask* updateMask, Player* target) const
{

	for (uint32 i = 0; i < m_valuesCount; ++i)
		if (m_uint32Values[i] != 0)
			updateMask->SetBit(i);
}

void Object::AddToWorld()
{
	MapMgr* mapMgr = sInstanceMgr.GetInstance(this);
	if (mapMgr == NULL)
	{
		LOG_ERROR("AddToWorld() failed for Object with GUID " I64FMT " MapId %u InstanceId %u", GetGUID(), GetMapId(), GetInstanceID());
		return;
	}

	if (IsPlayer())
	{
		Player* plr = TO< Player* >(this);
		if (mapMgr->pInstance != NULL && !plr->HasFlag(PLAYER_FLAGS, PLAYER_FLAG_GM))
		{
			// Player limit?
			if (mapMgr->GetMapInfo()->playerlimit && mapMgr->GetPlayerCount() >= mapMgr->GetMapInfo()->playerlimit)
				return;
			Group* group = plr->GetGroup();
			// Player in group?
			if (group == NULL && mapMgr->pInstance->m_creatorGuid == 0)
				return;
			// If set: Owns player the instance?
			if (mapMgr->pInstance->m_creatorGuid != 0 && mapMgr->pInstance->m_creatorGuid != plr->GetLowGUID())
				return;

			if (group != NULL)
			{
				// Is instance empty or owns our group the instance?
				if (mapMgr->pInstance->m_creatorGroup != 0 && mapMgr->pInstance->m_creatorGroup != group->GetID())
				{
					// Player not in group or another group is already playing this instance.
					sChatHandler.SystemMessageToPlr(plr, "Another group is already inside this instance of the dungeon.");
					if (plr->GetSession()->GetPermissionCount() > 0)
						sChatHandler.BlueSystemMessageToPlr(plr, "Enable your GameMaster flag to ignore this rule.");
					return;
				}
				else if (mapMgr->pInstance->m_creatorGroup == 0)
					// Players group now "owns" the instance.
					mapMgr->pInstance->m_creatorGroup = group->GetID();
			}
		}
	}

	m_mapMgr = mapMgr;
	m_inQueue = true;

	// correct incorrect instance id's
	m_instanceId = m_mapMgr->GetInstanceID();
	m_mapId = m_mapMgr->GetMapId();
	mapMgr->AddObject(this);
}

void Object::AddToWorld(MapMgr* pMapMgr)
{
	if (!pMapMgr || (pMapMgr->GetMapInfo()->playerlimit && this->IsPlayer() && pMapMgr->GetPlayerCount() >= pMapMgr->GetMapInfo()->playerlimit))
		return; //instance add failed

	m_mapMgr = pMapMgr;
	m_inQueue = true;

	pMapMgr->AddObject(this);

	// correct incorrect instance id's
	m_instanceId = pMapMgr->GetInstanceID();
	m_mapId = m_mapMgr->GetMapId();
}

//Unlike addtoworld it pushes it directly ignoring add pool
//this can only be called from the thread of mapmgr!!!
void Object::PushToWorld(MapMgr* mgr)
{
	ARCEMU_ASSERT(t_currentMapContext.get() == mgr);

	if (mgr == NULL)
	{
		LOG_ERROR("Invalid push to world of Object " I64FMT, GetGUID());
		return; //instance add failed
	}

	m_mapId = mgr->GetMapId();
	//note: there's no need to set the InstanceId before calling PushToWorld() because it's already set here.
	m_instanceId = mgr->GetInstanceID();

	if (IsPlayer())
	{
		TO_PLAYER(this)->m_cache->SetInt32Value(CACHE_MAPID, m_mapId);
		TO_PLAYER(this)->m_cache->SetInt32Value(CACHE_INSTANCEID, m_instanceId);
	}

	m_mapMgr = mgr;
	OnPrePushToWorld();

	mgr->PushObject(this);

	// correct incorrect instance id's
	m_inQueue = false;

	event_Relocate();

	// call virtual function to handle stuff.. :P
	OnPushToWorld();
}

//! Remove object from world
void Object::RemoveFromWorld(bool free_guid)
{
	ARCEMU_ASSERT(m_mapMgr != NULL);

	OnPreRemoveFromWorld();

	MapMgr* m = m_mapMgr;
	m_mapMgr = NULL;

	m->RemoveObject(this, free_guid);

	OnRemoveFromWorld();

	std::set<Spell*>::iterator itr, itr2;
	Spell* sp;
	for (itr = m_pendingSpells.begin(); itr != m_pendingSpells.end();)
	{
		itr2 = itr++;
		sp = *itr2;
		//if the spell being deleted is the same being casted, Spell::cancel will take care of deleting the spell
		//if it's not the same removing us from world. Otherwise finish() will delete the spell once all SpellEffects are handled.
		if (sp == m_currentSpell)
			sp->cancel();
		else
			delete sp;
	}
	//shouldnt need to clear, spell destructor will erase
	//m_pendingSpells.clear();

	m_instanceId = INSTANCEID_NOT_IN_WORLD;
	m_mapId = MAPID_NOT_IN_WORLD;
	//m_inQueue is set to true when AddToWorld() is called. AddToWorld() queues the Object to be pushed, but if it's not pushed and RemoveFromWorld()
	//is called, m_inQueue will still be true even if the Object is no more inworld, nor queued.
	m_inQueue = false;

	// update our event holder
	event_Relocate();
}

//! Set uint32 property
void Object::SetUInt32Value(const uint32 index, const uint32 value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	//! Save updating when val isn't changing.
	if (m_uint32Values[index] == value)
		return;

	m_uint32Values[index] = value;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}

	//! Group update handling
	if (IsPlayer())
	{
		TO_PLAYER(this)->HandleUpdateFieldChanged(index);
		if (IsInWorld())
		{
			Group* pGroup = TO< Player* >(this)->GetGroup();
			if (pGroup != NULL)
				pGroup->HandleUpdateFieldChange(index, TO< Player* >(this));
		}

		switch (index)
		{
		case UNIT_FIELD_POWER + 0:
		case UNIT_FIELD_POWER + 1:
		case UNIT_FIELD_POWER + 3:
		//case UNIT_FIELD_POWER + 5:
			TO< Unit* >(this)->SendPowerUpdate(true);
			break;
		default:
			break;
		}
	}
	else if (IsCreature())
	{
		switch (index)
		{
		case UNIT_FIELD_POWER + 0:
		case UNIT_FIELD_POWER + 1:
		case UNIT_FIELD_POWER + 2:
		case UNIT_FIELD_POWER + 3:
		case UNIT_FIELD_POWER + 4:
			TO_CREATURE(this)->SendPowerUpdate(false);
			break;
		default:
			break;
		}
	}
}

uint32 Object::GetModPUInt32Value(const uint32 index, const int32 value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	int32 basevalue = (int32)m_uint32Values[index];
	return ((basevalue * value) / 100);
}

void Object::ModUnsigned32Value(uint32 index, int32 mod)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	if (mod == 0)
		return;

	m_uint32Values[index] += mod;
	if ((int32)m_uint32Values[index] < 0)
		m_uint32Values[index] = 0;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}

	if (IsPlayer())
	{
		switch (index)
		{
		case UNIT_FIELD_POWER + 1:
		case UNIT_FIELD_POWER + 2:
		case UNIT_FIELD_POWER + 4:
		case UNIT_FIELD_POWER + 5:
			TO< Unit* >(this)->SendPowerUpdate(true);
			break;
		default:
			break;
		}
	}
	else if (IsCreature())
	{
		switch (index)
		{
		case UNIT_FIELD_POWER + 1:
		case UNIT_FIELD_POWER + 2:
		case UNIT_FIELD_POWER + 3:
		case UNIT_FIELD_POWER + 4:
		case UNIT_FIELD_POWER + 5:
			TO_CREATURE(this)->SendPowerUpdate(false);
			break;
		default:
			break;
		}
	}
}

void Object::ModSignedInt32Value(uint32 index, int32 value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	if (value == 0)
		return;

	m_uint32Values[index] += value;
	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}

void Object::ModFloatValue(const uint32 index, const float value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	m_floatValues[index] += value;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}
void Object::ModFloatValueByPCT(const uint32 index, int32 byPct)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	if (byPct > 0)
		m_floatValues[index] *= 1.0f + byPct / 100.0f;
	else
		m_floatValues[index] /= 1.0f - byPct / 100.0f;


	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}

//! Set uint64 property
void Object::SetUInt64Value(const uint32 index, const uint64 value)
{
	ARCEMU_ASSERT(index + 1 < m_valuesCount);

	uint64* p = reinterpret_cast< uint64* >(&m_uint32Values[index]);

	if (*p == value)
		return;
	else
		*p = value;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);
		m_updateMask.SetBit(index + 1);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}

//! Set float property
void Object::SetFloatValue(const uint32 index, const float value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	if (m_floatValues[index] == value)
		return;

	m_floatValues[index] = value;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}


void Object::SetFlag(const uint32 index, uint32 newFlag)
{
	SetUInt32Value(index, GetUInt32Value(index) | newFlag);
}


void Object::RemoveFlag(const uint32 index, uint32 oldFlag)
{
	SetUInt32Value(index, GetUInt32Value(index) & ~oldFlag);
}

////////////////////////////////////////////////////////////

float Object::CalcDistance(Object* Ob)
{
	ARCEMU_ASSERT(Ob != NULL);
	return CalcDistance(this->GetPositionX(), this->GetPositionY(), this->GetPositionZ(), Ob->GetPositionX(), Ob->GetPositionY(), Ob->GetPositionZ());
}
float Object::CalcDistance(float ObX, float ObY, float ObZ)
{
	return CalcDistance(this->GetPositionX(), this->GetPositionY(), this->GetPositionZ(), ObX, ObY, ObZ);
}
float Object::CalcDistance(Object* Oa, Object* Ob)
{
	ARCEMU_ASSERT(Oa != NULL);
	ARCEMU_ASSERT(Ob != NULL);
	return CalcDistance(Oa->GetPositionX(), Oa->GetPositionY(), Oa->GetPositionZ(), Ob->GetPositionX(), Ob->GetPositionY(), Ob->GetPositionZ());
}
float Object::CalcDistance(Object* Oa, float ObX, float ObY, float ObZ)
{
	ARCEMU_ASSERT(Oa != NULL);
	return CalcDistance(Oa->GetPositionX(), Oa->GetPositionY(), Oa->GetPositionZ(), ObX, ObY, ObZ);
}

float Object::CalcDistance(float OaX, float OaY, float OaZ, float ObX, float ObY, float ObZ)
{
	float xdest = OaX - ObX;
	float ydest = OaY - ObY;
	float zdest = OaZ - ObZ;
	return sqrtf(zdest * zdest + ydest * ydest + xdest * xdest);
}

bool Object::IsWithinDistInMap(Object* obj, const float dist2compare) const
{
	ARCEMU_ASSERT(obj != NULL);
	float xdest = this->GetPositionX() - obj->GetPositionX();
	float ydest = this->GetPositionY() - obj->GetPositionY();
	float zdest = this->GetPositionZ() - obj->GetPositionZ();
	return sqrtf(zdest * zdest + ydest * ydest + xdest * xdest) <= dist2compare;
}

bool Object::IsWithinLOSInMap(Object* obj)
{
	ARCEMU_ASSERT(obj != NULL);
	if (!IsInMap(obj)) return false;
	LocationVector location;
	location = obj->GetPosition();
	return IsWithinLOS(location);
}

bool Object::IsWithinLOS(LocationVector location)
{
	LocationVector location2;
	location2 = GetPosition();

	if (sWorld.Collision)
	{
		return CollideInterface.CheckLOS(GetMapId(), location2.x, location2.y, location2.z + 2.0f, location.x, location.y, location.z + 2.0f);
	}
	else
	{
		return true;
	}
}


float Object::calcAngle(float Position1X, float Position1Y, float Position2X, float Position2Y)
{
	float dx = Position2X - Position1X;
	float dy = Position2Y - Position1Y;
	double angle = 0.0f;

	// Calculate angle
	if (dx == 0.0)
	{
		if (dy == 0.0)
			angle = 0.0;
		else if (dy > 0.0)
			angle = M_PI * 0.5 /* / 2 */;
		else
			angle = M_PI * 3.0 * 0.5/* / 2 */;
	}
	else if (dy == 0.0)
	{
		if (dx > 0.0)
			angle = 0.0;
		else
			angle = M_PI;
	}
	else
	{
		if (dx < 0.0)
			angle = atanf(dy / dx) + M_PI;
		else if (dy < 0.0)
			angle = atanf(dy / dx) + (2 * M_PI);
		else
			angle = atanf(dy / dx);
	}

	// Convert to degrees
	angle = angle * float(180 / M_PI);

	// Return
	return float(angle);
}

float Object::calcRadAngle(float Position1X, float Position1Y, float Position2X, float Position2Y)
{
	double dx = double(Position2X - Position1X);
	double dy = double(Position2Y - Position1Y);
	double angle = 0.0;

	// Calculate angle
	if (dx == 0.0)
	{
		if (dy == 0.0)
			angle = 0.0;
		else if (dy > 0.0)
			angle = M_PI * 0.5/*/ 2.0*/;
		else
			angle = M_PI * 3.0 * 0.5/*/ 2.0*/;
	}
	else if (dy == 0.0)
	{
		if (dx > 0.0)
			angle = 0.0;
		else
			angle = M_PI;
	}
	else
	{
		if (dx < 0.0)
			angle = atan(dy / dx) + M_PI;
		else if (dy < 0.0)
			angle = atan(dy / dx) + (2 * M_PI);
		else
			angle = atan(dy / dx);
	}

	// Return
	return float(angle);
}

float Object::getEasyAngle(float angle)
{
	while (angle < 0)
	{
		angle = angle + 360;
	}
	while (angle >= 360)
	{
		angle = angle - 360;
	}
	return angle;
}

bool Object::inArc(float Position1X, float Position1Y, float FOV, float Orientation, float Position2X, float Position2Y)
{
	float angle = calcAngle(Position1X, Position1Y, Position2X, Position2Y);
	float lborder = getEasyAngle((Orientation - (FOV * 0.5f/*/2*/)));
	float rborder = getEasyAngle((Orientation + (FOV * 0.5f/*/2*/)));
	//LOG_DEBUG("Orientation: %f Angle: %f LeftBorder: %f RightBorder %f",Orientation,angle,lborder,rborder);
	if (((angle >= lborder) && (angle <= rborder)) || ((lborder > rborder) && ((angle < rborder) || (angle > lborder))))
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool Object::isInFront(Object* target)
{
	// check if we facing something ( is the object within a 180 degree slice of our positive y axis )

	double x = target->GetPositionX() - m_position.x;
	double y = target->GetPositionY() - m_position.y;

	double angle = atan2(y, x);
	angle = (angle >= 0.0) ? angle : 2.0 * M_PI + angle;
	angle -= m_position.o;

	while (angle > M_PI)
		angle -= 2.0 * M_PI;

	while (angle < -M_PI)
		angle += 2.0 * M_PI;

	// replace M_PI in the two lines below to reduce or increase angle

	double left = -1.0 * (M_PI / 2.0);
	double right = (M_PI / 2.0);

	return((angle >= left) && (angle <= right));
}

bool Object::isInBack(Object* target)
{
	// check if we are behind something ( is the object within a 180 degree slice of our negative y axis )

	double x = m_position.x - target->GetPositionX();
	double y = m_position.y - target->GetPositionY();

	double angle = atan2(y, x);
	angle = (angle >= 0.0) ? angle : 2.0 * M_PI + angle;

	// if we are a creature and have a UNIT_FIELD_TARGET then we are always facing them
	if (IsCreature() && TO_CREATURE(this)->GetTargetGUID() != 0)
	{
		Unit* pTarget = TO_CREATURE(this)->GetAIInterface()->getNextTarget();
		if (pTarget != NULL)
			angle -= double(Object::calcRadAngle(target->m_position.x, target->m_position.y, pTarget->m_position.x, pTarget->m_position.y));
		else
			angle -= target->GetOrientation();
	}
	else
		angle -= target->GetOrientation();

	while (angle > M_PI)
		angle -= 2.0 * M_PI;

	while (angle < -M_PI)
		angle += 2.0 * M_PI;

	// replace M_H_PI in the two lines below to reduce or increase angle

	double left = -1.0 * (M_H_PI / 2.0);
	double right = (M_H_PI / 2.0);

	return((angle <= left) && (angle >= right));
}
bool Object::isInArc(Object* target, float angle) // angle in degrees
{
	return inArc(GetPositionX(), GetPositionY(), angle, GetOrientation(), target->GetPositionX(), target->GetPositionY());
}

bool Object::HasInArc(float degrees, Object* target)
{
	return isInArc(target, degrees);
}

bool Object::isInRange(Object* target, float range)
{
	if (!this->IsInWorld() || !target) return false;
	float dist = CalcDistance(target);
	return(dist <= range);
}

void Object::_setFaction()
{
	FactionTemplateDBC* factT = NULL;

	if (IsUnit())
	{
		factT = dbcFactionTemplate.LookupEntryForced(TO_UNIT(this)->GetFaction());
		if (!factT)
			LOG_ERROR("Unit does not have a valid faction. It will make him act stupid in world. Don't blame us, blame yourself for not checking :P, faction %u set to entry %u", TO_UNIT(this)->GetFaction(), GetEntry());
	}
	else if (IsGameObject())
	{
		factT = dbcFactionTemplate.LookupEntryForced(TO< GameObject* >(this)->GetFaction());
		if (!factT)
			LOG_ERROR("Game Object does not have a valid faction. It will make him act stupid in world. Don't blame us, blame yourself for not checking :P, faction %u set to entry %u", TO< GameObject* >(this)->GetFaction(), GetEntry());
	}

	if (!factT)
	{
		factT = dbcFactionTemplate.LookupRow(0);
		//this is causing a lot of crashes cause people have shitty dbs
		//		return;
	}
	m_faction = factT;
	m_factionDBC = dbcFaction.LookupEntryForced(factT->Faction);
	if (!m_factionDBC)
		m_factionDBC = dbcFaction.LookupRow(0);
}

void Object::UpdateOppFactionSet()
{
	m_oppFactsInRange.clear();

	for (std::set< Object* >::iterator itr = m_objectsInRange.begin(); itr != m_objectsInRange.end(); ++itr)
	{
		Object* i = *itr;

		if (i->IsUnit() || i->IsGameObject())
		{
			if (isHostile(this, i))
			{
				if (!i->IsInRangeOppFactSet(this))
					i->m_oppFactsInRange.insert(this);
				if (!IsInRangeOppFactSet(i))
					m_oppFactsInRange.insert(i);

			}
			else
			{
				if (i->IsInRangeOppFactSet(this))
					i->m_oppFactsInRange.erase(this);
				if (IsInRangeOppFactSet(i))
					m_oppFactsInRange.erase(i);
			}
		}
	}
}

void Object::UpdateSameFactionSet()
{
	m_sameFactsInRange.clear();


	for (std::set< Object* >::iterator itr = m_objectsInRange.begin(); itr != m_objectsInRange.end(); ++itr)
	{
		Object* i = *itr;

		if (i->IsUnit() || i->IsGameObject())
		{
			if (isFriendly(this, i))
			{
				if (!i->IsInRangeSameFactSet(this))
					i->m_sameFactsInRange.insert(this);

				if (!IsInRangeOppFactSet(i))
					m_sameFactsInRange.insert(i);

			}
			else
			{
				if (i->IsInRangeSameFactSet(this))
					i->m_sameFactsInRange.erase(this);

				if (IsInRangeSameFactSet(i))
					m_sameFactsInRange.erase(i);
			}
		}
	}
}

void Object::EventSetUInt32Value(uint32 index, uint32 value)
{
	SetUInt32Value(index, value);
}

void Object::DealDamage(Unit* pVictim, uint32 damage, uint32 targetEvent, uint32 unitEvent, uint32 spellId, bool no_remove_auras)
{
}

void Object::SpellNonMeleeDamageLog(Unit* pVictim, uint32 spellID, uint32 damage, bool allowProc, bool static_damage, bool no_remove_auras)
{
	//==========================================================================================
	//==============================Unacceptable Cases Processing===============================
	//==========================================================================================
	if (pVictim == NULL || !pVictim->isAlive())
		return;

	SpellEntry* spellInfo = dbcSpellEntry.LookupEntryForced(spellID);
	if (spellInfo == NULL)
		return;

	if (this->IsPlayer() && !TO< Player* >(this)->canCast(spellInfo))
		return;
	//==========================================================================================
	//==============================Variables Initialization====================================
	//==========================================================================================
	float res = static_cast< float >(damage);
	bool critical = false;

	uint32 aproc = PROC_ON_ANY_HOSTILE_ACTION; /*| PROC_ON_SPELL_HIT;*/
	uint32 vproc = PROC_ON_ANY_HOSTILE_ACTION | PROC_ON_ANY_DAMAGE_VICTIM; /*| PROC_ON_SPELL_HIT_VICTIM;*/

	//A school damage is not necessarily magic
	switch (spellInfo->Spell_Dmg_Type)
	{
	case SPELL_DMG_TYPE_RANGED:
	{
		aproc |= PROC_ON_RANGED_ATTACK;
		vproc |= PROC_ON_RANGED_ATTACK_VICTIM;
	}
	break;

	case SPELL_DMG_TYPE_MELEE:
	{
		aproc |= PROC_ON_MELEE_ATTACK;
		vproc |= PROC_ON_MELEE_ATTACK_VICTIM;
	}
	break;

	case SPELL_DMG_TYPE_MAGIC:
	{
		aproc |= PROC_ON_SPELL_HIT;
		vproc |= PROC_ON_SPELL_HIT_VICTIM;
	}
	break;
	}

	//==========================================================================================
	//==============================+Spell Damage Bonus Calculations============================
	//==========================================================================================
	//------------------------------by stats----------------------------------------------------
	if (IsUnit() && !static_damage)
	{
		Unit* caster = TO< Unit* >(this);

		caster->RemoveAurasByInterruptFlag(AURA_INTERRUPT_ON_START_ATTACK);

		res += static_cast< float >(caster->GetSpellDmgBonus(pVictim, spellInfo, damage, false));

		if (res < 0.0f)
			res = 0.0f;
	}
	//==========================================================================================
	//==============================Post +SpellDamage Bonus Modifications=======================
	//==========================================================================================
    if (res > 0.0f && !(spellInfo->misc.AttributesExB & ATTRIBUTESEXB_CANT_CRIT))
	{
		critical = this->IsCriticalDamageForSpell(pVictim, spellInfo);

		//==========================================================================================
		//==============================Spell Critical Hit==========================================
		//==========================================================================================
		if (critical)
		{
			res = this->GetCriticalDamageBonusForSpell(pVictim, spellInfo, res);

			switch (spellInfo->Spell_Dmg_Type)
			{
			case SPELL_DMG_TYPE_RANGED:
			{
				aproc |= PROC_ON_RANGED_CRIT_ATTACK;
				vproc |= PROC_ON_RANGED_CRIT_ATTACK_VICTIM;
			}
			break;

			case SPELL_DMG_TYPE_MELEE:
			{
				aproc |= PROC_ON_CRIT_ATTACK;
				vproc |= PROC_ON_CRIT_HIT_VICTIM;
			}
			break;

			case SPELL_DMG_TYPE_MAGIC:
			{
				aproc |= PROC_ON_SPELL_CRIT_HIT;
				vproc |= PROC_ON_SPELL_CRIT_HIT_VICTIM;
			}
			break;
			}
		}
	}

	//==========================================================================================
	//==============================Post Roll Calculations======================================
	//==========================================================================================

	//------------------------------damage reduction--------------------------------------------
	if (this->IsUnit())
		res += TO< Unit* >(this)->CalcSpellDamageReduction(pVictim, spellInfo, res);
	//------------------------------absorption--------------------------------------------------
	uint32 ress = static_cast< uint32 >(res);
	uint32 abs_dmg = pVictim->AbsorbDamage(spellInfo->School, &ress);
	uint32 ms_abs_dmg = pVictim->ManaShieldAbsorb(ress);
	if (ms_abs_dmg)
	{
		if (ms_abs_dmg > ress)
			ress = 0;
		else
			ress -= ms_abs_dmg;

		abs_dmg += ms_abs_dmg;
	}

	if (abs_dmg)
		vproc |= PROC_ON_ABSORB;

	// Incanter's Absorption
	if (pVictim->IsPlayer() && pVictim->HasAurasWithNameHash(SPELL_HASH_INCANTER_S_ABSORPTION))
	{
		float pctmod = 0.0f;
		Player* pl = TO< Player* >(pVictim);
		if (pl->HasAura(44394))
			pctmod = 0.05f;
		else if (pl->HasAura(44395))
			pctmod = 0.10f;
		else if (pl->HasAura(44396))
			pctmod = 0.15f;

		uint32 hp = static_cast< uint32 >(0.05f * pl->GetUInt32Value(UNIT_FIELD_MAXHEALTH));
		uint32 spellpower = static_cast< uint32 >(pctmod * pl->GetPosDamageDoneMod(SCHOOL_NORMAL));

		if (spellpower > hp)
			spellpower = hp;

		SpellEntry* entry = dbcSpellEntry.LookupEntryForced(44413);
		if (!entry)
			return;

		Spell* sp = sSpellFactoryMgr.NewSpell(pl, entry, true, NULL);
		sp->GetProto()->eff[0].EffectBasePoints = spellpower;
		SpellCastTargets targets;
		targets.m_unitTarget = pl->GetGUID();
		sp->prepare(&targets);
	}

	if (ress < 0) ress = 0;

	res = static_cast< float >(ress);
	dealdamage dmg;
	dmg.school_type = spellInfo->School;
	dmg.full_damage = ress;
	dmg.resisted_damage = 0;

	if (res <= 0)
		dmg.resisted_damage = dmg.full_damage;

	//------------------------------resistance reducing-----------------------------------------
	if (res > 0 && this->IsUnit())
	{
		TO< Unit* >(this)->CalculateResistanceReduction(pVictim, &dmg, spellInfo, 0);
		if ((int32)dmg.resisted_damage > dmg.full_damage)
			res = 0;
		else
			res = static_cast< float >(dmg.full_damage - dmg.resisted_damage);
	}
	//------------------------------special states----------------------------------------------
	if (pVictim->IsPlayer() && TO< Player* >(pVictim)->GodModeCheat == true)
	{
		res = static_cast< float >(dmg.full_damage);
		dmg.resisted_damage = dmg.full_damage;
	}

	// Paladin: Blessing of Sacrifice, and Warlock: Soul Link
	if (pVictim->m_damageSplitTarget)
	{
		res = (float)pVictim->DoDamageSplitTarget((uint32)res, spellInfo->School, false);
	}

	//==========================================================================================
	//==============================Data Sending ProcHandling===================================
	//==========================================================================================
	SendSpellNonMeleeDamageLog(this, pVictim, spellID, static_cast< int32 >(res), static_cast< uint8 >(spellInfo->School), abs_dmg, dmg.resisted_damage, false, 0, critical, IsPlayer());
	DealDamage(pVictim, static_cast< int32 >(res), 2, 0, spellID);

	if (IsUnit())
	{
		int32 dmg2 = static_cast< int32 >(res);

		pVictim->HandleProc(vproc, TO< Unit* >(this), spellInfo, !allowProc, dmg2, abs_dmg);
		pVictim->m_procCounter = 0;
		TO< Unit* >(this)->HandleProc(aproc, pVictim, spellInfo, !allowProc, dmg2, abs_dmg);
		TO< Unit* >(this)->m_procCounter = 0;
	}
	if (this->IsPlayer())
	{
		TO< Player* >(this)->m_casted_amount[spellInfo->School] = (uint32)res;
	}

	if (!(dmg.full_damage == 0 && abs_dmg))
	{
		//Only pushback the victim current spell if it's not fully absorbed
		if (pVictim->GetCurrentSpell())
			pVictim->GetCurrentSpell()->AddTime(spellInfo->School);
	}

	//==========================================================================================
	//==============================Post Damage Processing======================================
	//==========================================================================================
	if ((int32)dmg.resisted_damage == dmg.full_damage && !abs_dmg)
	{
		//Magic Absorption
		if (pVictim->IsPlayer())
		{
			if (TO< Player* >(pVictim)->m_RegenManaOnSpellResist)
			{
				Player* pl = TO< Player* >(pVictim);

				uint32 maxmana = pl->GetMaxPower(POWER_TYPE_MANA);
				uint32 amount = static_cast< uint32 >(maxmana * pl->m_RegenManaOnSpellResist);

				pVictim->Energize(pVictim, 29442, amount, POWER_TYPE_MANA);
			}
			// we still stay in combat dude
			TO< Player* >(pVictim)->CombatStatusHandler_ResetPvPTimeout();
		}
		if (IsPlayer())
			TO< Player* >(this)->CombatStatusHandler_ResetPvPTimeout();
	}
	if (spellInfo->School == SCHOOL_SHADOW)
	{
		if (pVictim->isAlive() && this->IsUnit())
		{
			//Shadow Word:Death
			if (spellID == 32379 || spellID == 32996 || spellID == 48157 || spellID == 48158)
			{
				uint32 damage2 = static_cast< uint32 >(res + abs_dmg);
				uint32 absorbed = TO< Unit* >(this)->AbsorbDamage(spellInfo->School, &damage2);
				DealDamage(TO< Unit* >(this), damage2, 2, 0, spellID);
				SendSpellNonMeleeDamageLog(this, this, spellID, damage2, static_cast< uint8 >(spellInfo->School), absorbed, 0, false, 0, false, IsPlayer());
			}
		}
	}
}

//*****************************************************************************************
//* SpellLog packets just to keep the code cleaner and better to read
//*****************************************************************************************

void Object::SendSpellLog(Object* Caster, Object* Target, uint32 Ability, uint8 SpellLogType)
{
	if (Caster == NULL || Target == NULL || Ability == 0)
		return;


	WorldPacket data(SMSG_SPELLLOGMISS, 26);

	data << uint32(Ability);					// spellid
	data << Caster->GetGUID();					// caster / player
	data << uint8(1);							// unknown but I think they are const
	data << uint32(1);						// unknown but I think they are const
	data << Target->GetGUID();					// target
	data << uint8(SpellLogType);				// spelllogtype

	Caster->SendMessageToSet(&data, true);
}

void Object::SendSpellNonMeleeDamageLog(Object* Caster, Object* Target, uint32 SpellID, uint32 Damage, uint8 School, uint32 AbsorbedDamage, uint32 ResistedDamage, bool PhysicalDamage, uint32 BlockedDamage, bool CriticalHit, bool bToset)
{
	if (!Caster || !Target || !SpellID)
		return;

    ObjectGuid attackerGuid = Caster->GetGUID();
    ObjectGuid targetGuid = Target->GetGUID();
    int32 Overkill = 0;

	if (Damage > Target->GetUInt32Value(UNIT_FIELD_HEALTH))
        Overkill = Damage - Target->GetUInt32Value(UNIT_FIELD_HEALTH);

	WorldPacket data(SMSG_SPELLNONMELEEDAMAGELOG, 48);
    data.WriteBit(targetGuid[2]);
    data.WriteBit(attackerGuid[7]);
    data.WriteBit(attackerGuid[6]);
    data.WriteBit(attackerGuid[1]);
    data.WriteBit(attackerGuid[5]);
    data.WriteBit(0); // Unk
    data.WriteBit(attackerGuid[0]);
    data.WriteBit(targetGuid[0]);
    data.WriteBit(targetGuid[7]);
    data.WriteBit(attackerGuid[3]);
    data.WriteBit(targetGuid[6]);
    data.WriteBit(0); // Unk
    data.WriteBit(0); // HasPowerData
    data.WriteBit(targetGuid[1]);
    data.WriteBit(0); // No floats
    data.WriteBit(targetGuid[5]);
    data.WriteBit(attackerGuid[2]);
    data.WriteBit(attackerGuid[4]);
    data.WriteBit(targetGuid[3]);
    data.WriteBit(targetGuid[4]);

    data.FlushBits();

    data << uint32(BlockedDamage);
    data.WriteByteSeq(attackerGuid[1]);
    data << uint32(Overkill > 0 ? Overkill : 0);
    data.WriteByteSeq(targetGuid[3]);
    data.WriteByteSeq(attackerGuid[0]);
    data.WriteByteSeq(targetGuid[6]);
    data.WriteByteSeq(targetGuid[4]);
    data.WriteByteSeq(attackerGuid[7]);
    data << uint32(ResistedDamage);
    data << uint32(AbsorbedDamage);
    data.WriteByteSeq(attackerGuid[5]);
    data.WriteByteSeq(targetGuid[5]);
    data.WriteByteSeq(attackerGuid[3]);
    data.WriteByteSeq(attackerGuid[2]);
    data.WriteByteSeq(targetGuid[2]);
    data.WriteByteSeq(attackerGuid[6]);
    data.WriteByteSeq(targetGuid[0]);
    data.WriteByteSeq(attackerGuid[4]);
    data << uint32(Damage);
    data << uint8(g_spellSchoolConversionTable[School]);
    data.WriteByteSeq(targetGuid[7]);
    data << uint32(PhysicalDamage); // Hit info
    data.WriteByteSeq(targetGuid[1]);
    data << uint32(SpellID);

	Caster->SendMessageToSet(&data, bToset);
}

void Object::SendAttackerStateUpdate(Object* Caster, Object* Target, dealdamage* Dmg, uint32 Damage, uint32 Abs, uint32 BlockedDamage, uint32 HitStatus, uint32 VState)
{
	if (!Caster || !Target || !Dmg)
		return;

    printf("GOT DMG %u FULL_DMG %u BLOCKED %u ABSORBED %u \n", Damage, Dmg->full_damage, BlockedDamage, Abs);

	size_t maxsize = 4 + 5 + 5 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 * 12;
    WorldPacket data(SMSG_ATTACKERSTATEUPDATE, maxsize); // we guess size

	uint32 Overkill = 0;
	if (Damage > Target->GetUInt32Value(UNIT_FIELD_MAXHEALTH))
		Overkill = Damage - Target->GetUInt32Value(UNIT_FIELD_HEALTH);

    bool hasUnkFlags = HitStatus & HITINFO_UNK26;
	uint32 unkCounter = 0;

	data.WriteBit(hasUnkFlags);

	if (hasUnkFlags)
	{
		data.WriteBits(unkCounter, 21);
		data.FlushBits();

		data << uint32(0);

		for (uint32 i = 0; i < unkCounter; ++i)
		{
			data << uint32(0);
			data << uint32(0);
		}

		data << uint32(0);
		data << uint32(0);
	}

	// Needs to be flushed because data.wpos() wouldn't return the correct placeholder
	data.FlushBits();

	size_t size = data.wpos();
	data << uint32(0); // size

	data << uint32(HitStatus);
	data << Caster->GetNewGUID();
    data << Target->GetNewGUID();
    data << uint32(Damage); // Real damage
    data << uint32(Overkill < 0 ? 0 : Overkill); // Overkill
    data << uint8(1); // Damage type counter / swing type

    data << uint32(g_spellSchoolConversionTable[Dmg->school_type]); // Damage school
    data << float(Dmg->full_damage); // Damage float
    data << uint32(Dmg->full_damage); // Damage amount

	if (HitStatus & (HITINFO_FULL_ABSORB | HITINFO_PARTIAL_ABSORB))
    //if (HitStatus & HITSTATUS_ABSORBED)
            data << uint32(Abs); // Absorb

	if (HitStatus & (HITINFO_FULL_RESIST | HITINFO_PARTIAL_RESIST))
    //if (HitStatus & HITSTATUS_RESIST)
            data << uint32(Dmg->resisted_damage); // Resist

	data << uint8(VState); // Victim state
    data << uint32(0); // Unknown attackerstate
    data << uint32(0); // Melee spellid

	if (HitStatus & HITINFO_BLOCK)
    //if (HitStatus & HITSTATUS_BLOCK)
		data << uint32(BlockedDamage);

	if (HitStatus & HITINFO_RAGE_GAIN)
    //if (HitStatus & HITSTATUS_UNK2) // Rage gain
		data << uint32(0); // Rage gained

	//! Probably used for debugging purposes, as it is not known to appear on retail servers
    if (HitStatus & HITINFO_UNK0)
    //if (HitStatus & HITSTATUS_UNK)
	{
        data << uint32(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0);
        data << float(0); // Found in loop
        data << float(0); // Found in loop
        data << uint32(0);
	}

	if (HitStatus & (HITINFO_BLOCK | HITINFO_UNK12))
    //if (HitStatus & HITSTATUS_BLOCK);
		data << float(0);

	data.put(size, data.wpos() - size - 4); // Why Blizzard, why!?

	SendMessageToSet(&data, Caster->IsPlayer());
}

int32 Object::event_GetInstanceID()
{
	// return -1 for non-inworld.. so we get our shit moved to the right thread
	//default value is -1, if it's something else then we are/will be soon InWorld.
	return m_instanceId;
}

void Object::EventSpellDamage(uint64 Victim, uint32 SpellID, uint32 Damage)
{
	if (!IsInWorld())
		return;

	Unit* pUnit = GetMapMgr()->GetUnit(Victim);
	if (pUnit == NULL)
		return;

	SpellNonMeleeDamageLog(pUnit, SpellID, Damage, true);
}

//! Object has an active state
bool Object::CanActivate()
{
	switch (m_objectTypeId)
	{
	case TYPEID_UNIT:
	{
		if (!IsPet())
			return true;
	}
	break;

	case TYPEID_GAMEOBJECT:
	{
		if (TO_GAMEOBJECT(this)->HasAI() && TO_GAMEOBJECT(this)->GetType() != GAMEOBJECT_TYPE_TRAP)
			return true;
	}
	break;
	}

	return false;
}

void Object::Activate(MapMgr* mgr)
{
	switch (m_objectTypeId)
	{
	case TYPEID_UNIT:
		mgr->activeCreatures.insert(TO_CREATURE(this));
		break;

	case TYPEID_GAMEOBJECT:
		mgr->activeGameObjects.insert(TO_GAMEOBJECT(this));
		break;
	}
	// Objects are active so set to true.
	Active = true;
}

void Object::Deactivate(MapMgr* mgr)
{
	if (mgr == NULL)
		return;

	switch (m_objectTypeId)
	{
	case TYPEID_UNIT:
		// check iterator
		if (mgr->creature_iterator != mgr->activeCreatures.end() && (*mgr->creature_iterator)->GetGUID() == GetGUID())
			++mgr->creature_iterator;
		mgr->activeCreatures.erase(TO_CREATURE(this));
		break;

	case TYPEID_GAMEOBJECT:
		mgr->activeGameObjects.erase(TO_GAMEOBJECT(this));
		break;
	}
	Active = false;
}

void Object::SetByte(uint32 index, uint32 index1, uint8 value)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	// save updating when val isn't changing.

	uint8* v = &((uint8*)m_uint32Values)[index * 4 + index1];

	if (*v == value)
		return;

	*v = value;

	if (IsInWorld())
	{
		m_updateMask.SetBit(index);

		if (!m_objectUpdated)
		{
			m_mapMgr->ObjectUpdated(this);
			m_objectUpdated = true;
		}
	}
}

void Object::SetByteFlag(uint16 index, uint8 offset, uint8 newFlag)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	ARCEMU_ASSERT(offset < 4);

	offset <<= 3;

	if (!(uint8(m_uint32Values[index] >> offset) & newFlag))
	{
		m_uint32Values[index] |= uint32(uint32(newFlag) << offset);

		if (IsInWorld())
		{
			m_updateMask.SetBit(index);

			if (!m_objectUpdated)
			{
				m_mapMgr->ObjectUpdated(this);
				m_objectUpdated = true;
			}
		}
	}
}

void Object::RemoveByteFlag(uint16 index, uint8 offset, uint8 oldFlag)
{
	ARCEMU_ASSERT(index < m_valuesCount);
	ARCEMU_ASSERT(offset < 4);

	offset <<= 3;

	if (uint8(m_uint32Values[index] >> offset) & oldFlag)
	{
		m_uint32Values[index] &= ~uint32(uint32(oldFlag) << offset);

		if (IsInWorld())
		{
			m_updateMask.SetBit(index);

			if (!m_objectUpdated)
			{
				m_mapMgr->ObjectUpdated(this);
				m_objectUpdated = true;
			}
		}
	}
}

void Object::SetZoneId(uint32 newZone)
{
	m_zoneId = newZone;

	if (IsPlayer())
	{
		TO_PLAYER(this)->m_cache->SetUInt32Value(CACHE_PLAYER_ZONEID, newZone);
		if (TO_PLAYER(this)->GetGroup() != NULL)
			TO_PLAYER(this)->GetGroup()->HandlePartialChange(PARTY_UPDATE_FLAG_ZONEID, TO_PLAYER(this));
	}
}

void Object::PlaySoundToSet(uint32 sound_entry)
{
	WorldPacket data(SMSG_PLAY_SOUND, 4);
	data << sound_entry;
	data << uint64(GetGUID());

	SendMessageToSet(&data, true);
}

void Object::_SetExtension(const string & name, void* ptr)
{
	if (m_extensions == NULL)
		m_extensions = new ExtensionSet;

	m_extensions->insert(make_pair(name, ptr));
}

bool Object::IsInBg()
{
	MapInfo* pMapinfo = WorldMapInfoStorage.LookupEntry(GetMapId());

	if (pMapinfo != NULL)
	{
		return (pMapinfo->type == INSTANCE_BATTLEGROUND);
	}

	return false;
}

uint32 Object::GetTeam()
{
	switch (m_factionDBC->ID)
	{
		// Human
	case 1:
		// Dwarf
	case 3:
		// Nightelf
	case 4:
		// Gnome
	case 8:
		// Draenei
	case 927:
		return TEAM_ALLIANCE;

		// Orc
	case 2:
		// Undead
	case 5:
		// Tauren
	case 6:
		// Troll
	case 9:
		// Bloodelf
	case 914:
		return TEAM_HORDE;
	}

	return static_cast< uint32 >(-1);
}

// Manipulates the phase value, see "enum PHASECOMMANDS" in Object.h for a longer explanation!
void Object::Phase(uint8 command, uint32 newphase)
{
	switch (command)
	{
	case PHASE_SET:
		m_phase = newphase;
		break;
	case PHASE_ADD:
		m_phase |= newphase;
		break;
	case PHASE_DEL:
		m_phase &= ~newphase;
		break;
	case PHASE_RESET:
		m_phase = 1;
		break;
	default:
		ARCEMU_ASSERT(false);
	}

	return;
}

void Object::AddInRangeObject(Object* pObj)
{

	ARCEMU_ASSERT(pObj != NULL);

	if (pObj == this)
		LOG_ERROR("We are in range of ourselves!");

	if (pObj->IsPlayer())
		m_inRangePlayers.insert(pObj);

	m_objectsInRange.insert(pObj);
}

void Object::OutPacketToSet(uint32 Opcode, uint16 Len, const void* Data, bool self)
{
	if (!IsInWorld())
		return;

	// We are on Object level, which means we can't send it to ourselves so we only send to Players inrange
	for (std::set< Object* >::iterator itr = m_inRangePlayers.begin(); itr != m_inRangePlayers.end(); ++itr)
	{
		Object* o = *itr;

		o->OutPacket(Opcode, Len, Data);
	}
}

void Object::SendMessageToSet(WorldPacket* data, bool bToSelf, bool myteam_only)
{
	if (!IsInWorld())
		return;

	uint32 myphase = GetPhase();
	for (std::set< Object* >::iterator itr = m_inRangePlayers.begin(); itr != m_inRangePlayers.end(); ++itr)
	{
		Object* o = *itr;
		if ((o->GetPhase() & myphase) != 0)
			o->SendPacket(data);
	}
}

void Object::RemoveInRangeObject(Object* pObj)
{
	ARCEMU_ASSERT(pObj != NULL);

	if (pObj->IsPlayer())
	{
		ARCEMU_ASSERT(m_inRangePlayers.erase(pObj) == 1);
	}

	ARCEMU_ASSERT(m_objectsInRange.erase(pObj) == 1);

	OnRemoveInRangeObject(pObj);
}

void Object::RemoveSelfFromInrangeSets()
{
	std::set< Object* >::iterator itr;

	for (itr = m_objectsInRange.begin(); itr != m_objectsInRange.end(); ++itr)
	{
		Object* o = *itr;

		ARCEMU_ASSERT(o != NULL);

		o->RemoveInRangeObject(this);
	}
}


void Object::OnRemoveInRangeObject(Object* pObj)
{
	/* This method will remain empty for now, don't remove it!
	-dfighter
	*/
}

Object* Object::GetMapMgrObject(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

	return GetMapMgr()->_GetObject(guid);
}

Pet* Object::GetMapMgrPet(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

	return GetMapMgr()->GetPet(GUID_LOPART_TEST(guid));
}

Unit* Object::GetMapMgrUnit(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

	return GetMapMgr()->GetUnit(guid);
}

Player* Object::GetMapMgrPlayer(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

    return GetMapMgr()->GetPlayer(GUID_LOPART_TEST(guid));
}

Creature* Object::GetMapMgrCreature(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

    return GetMapMgr()->GetCreature(GUID_LOPART_TEST(guid));
}

GameObject* Object::GetMapMgrGameObject(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

    return GetMapMgr()->GetGameObject(GUID_LOPART_TEST(guid));
}

DynamicObject* Object::GetMapMgrDynamicObject(const uint64 & guid)
{
	if (!IsInWorld())
		return NULL;

    return GetMapMgr()->GetDynamicObject(GUID_LOPART_TEST(guid));
}

Object* Object::GetPlayerOwner()
{
	return NULL;
}

MapCell* Object::GetMapCell() const
{
	ARCEMU_ASSERT(m_mapMgr != NULL);
	return m_mapMgr->GetCell(m_mapCell_x, m_mapCell_y);
}

void Object::SetMapCell(MapCell* cell)
{
	if (cell == NULL)
	{
		//mapcell coordinates are uint16, so using uint32(-1) will always make GetMapCell() return NULL.
		m_mapCell_x = m_mapCell_y = uint32(-1);
	}
	else
	{
		m_mapCell_x = cell->GetPositionX();
		m_mapCell_y = cell->GetPositionY();
	}
}

void Object::SendAIReaction(uint32 reaction)
{
	//WorldPacket data(SMSG_AI_REACTION, 12);
	//data << uint64(GetGUID());
	//data << uint32(reaction);
	//SendMessageToSet(&data, false);
}

void Object::SendDestroyObject()
{
	Player* target;
    ObjectGuid guid = GetGUID();
	bool onDeath = false;

	ASSERT(target);

    WorldPacket data(SMSG_DESTROY_OBJECT, 2 + 8);
    data.WriteBit(guid[3]);
    data.WriteBit(guid[2]);
    data.WriteBit(guid[4]);
    data.WriteBit(guid[1]);

    //! OnDeath() does for eg trigger death animation and interrupts certain spells/missiles/auras/sounds...
    data.WriteBit(onDeath);

    data.WriteBit(guid[7]);
    data.WriteBit(guid[0]);
    data.WriteBit(guid[6]);
    data.WriteBit(guid[5]);

    data.FlushBits();

    data.WriteByteSeq(guid[0]);
    data.WriteByteSeq(guid[4]);
    data.WriteByteSeq(guid[7]);
    data.WriteByteSeq(guid[2]);
    data.WriteByteSeq(guid[6]);
    data.WriteByteSeq(guid[3]);
    data.WriteByteSeq(guid[1]);
    data.WriteByteSeq(guid[5]);

	LOG_ERROR("DESTROYED OBJECT : %u", GetGUID());

	target->GetSession()->SendPacket(&data);
	SendMessageToSet(&data, false);
}

bool Object::GetPoint(float angle, float rad, float & outx, float & outy, float & outz, bool sloppypath)
{
	if (!IsInWorld())
		return false;
	outx = GetPositionX() + rad * cos(angle);
	outy = GetPositionY() + rad * sin(angle);
	outz = GetMapMgr()->GetLandHeight(outx, outy, GetPositionZ() + 2);
	float waterz;
	uint32 watertype;
	GetMapMgr()->GetLiquidInfo(outx, outy, GetPositionZ() + 2, waterz, watertype);
	outz = max(waterz, outz);

	NavMeshData* nav = CollideInterface.GetNavMesh(GetMapId());

	if (nav != NULL)
	{
		//if we can path there, go for it
		if (!IsUnit() || !sloppypath || !TO_UNIT(this)->GetAIInterface()->CanCreatePath(outx, outy, outz))
		{
			//raycast nav mesh to see if this place is valid
			float start[3] = { GetPositionY(), GetPositionZ() + 0.5f, GetPositionX() };
			float end[3] = { outy, outz + 0.5f, outx };
			float extents[3] = { 3, 5, 3 };
			dtQueryFilter filter;
			filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_SLIME | NAV_MAGMA);

			dtPolyRef startref;
			nav->query->findNearestPoly(start, extents, &filter, &startref, NULL);

			float point, pointNormal;
			float result[3];
			int numvisited;
			dtPolyRef visited[MAX_PATH_LENGTH];

			dtStatus rayresult = nav->query->raycast(startref, start, end, &filter, &point, &pointNormal, visited, &numvisited, MAX_PATH_LENGTH);

			if (point <= 1.0f)
			{
				if (numvisited == 0 || rayresult == DT_FAILURE)
				{
					result[0] = start[0];
					result[1] = start[1];
					result[2] = start[2];
				}
				else
				{
					result[0] = start[0] + ((end[0] - start[0]) * point);
					result[1] = start[1] + ((end[1] - start[1]) * point);
					result[2] = start[2] + ((end[2] - start[2]) * point);
					nav->query->getPolyHeight(visited[numvisited - 1], result, &result[1]);
				}

				//copy end back to function floats
				outy = result[0];
				outz = result[1];
				outx = result[2];
			}
		}
	}
	else //test against vmap if mmap isn't available
	{
		float testx, testy, testz;

		if (CollideInterface.GetFirstPoint(GetMapId(), GetPositionX(), GetPositionY(), GetPositionZ() + 2, outx, outy, outz + 2, testx, testy, testz, -0.5f))
		{
			//hit something
			outx = testx;
			outy = testy;
			outz = testz;
		}

		outz = GetMapMgr()->GetLandHeight(outx, outy, outz + 2);
	}

	return true;
}