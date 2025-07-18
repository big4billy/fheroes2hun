/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2023 - 2025                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "maps_tiles_helper.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <list>
#include <optional>
#include <ostream>
#include <type_traits>
#include <vector>

#include "army.h"
#include "army_troop.h"
#include "artifact.h"
#include "castle.h"
#include "direction.h"
#include "game_static.h"
#include "logging.h"
#include "map_object_info.h"
#include "maps.h"
#include "maps_tiles.h"
#include "math_base.h"
#include "monster.h"
#include "mp2.h"
#include "profit.h"
#include "race.h"
#include "rand.h"
#include "resource.h"
#include "skill.h"
#include "spell.h"
#include "tools.h"
#include "week.h"
#include "world.h"
#include "world_object_uid.h"

namespace
{
    void updateMonsterPopulationOnTile( Maps::Tile & tile )
    {
        const Troop & troop = getTroopFromTile( tile );
        const uint32_t troopCount = troop.GetCount();

        if ( troopCount == 0 ) {
            Maps::setMonsterCountOnTile( tile, troop.GetRNDSize() );
        }
        else {
            const uint32_t bonusUnit = ( Rand::Get( 1, 7 ) <= ( troopCount % 7 ) ) ? 1 : 0;
            Maps::setMonsterCountOnTile( tile, troopCount * 8 / 7 + bonusUnit );
        }
    }

    void updateRandomResource( Maps::Tile & tile )
    {
        assert( tile.getMainObjectType() == MP2::OBJ_RANDOM_RESOURCE );

        tile.setMainObjectType( MP2::OBJ_RESOURCE );

        const uint8_t resourceSprite = Resource::GetIndexSprite( Resource::Rand( true ) );

        uint32_t uidResource = tile.getObjectIdByObjectIcnType( MP2::OBJ_ICN_TYPE_OBJNRSRC );
        if ( uidResource == 0 ) {
            uidResource = tile.getMainObjectPart()._uid;
        }

        Maps::Tile::updateTileObjectIcnIndex( tile, uidResource, resourceSprite );

        // Replace shadow of the resource.
        if ( Maps::isValidDirection( tile.GetIndex(), Direction::LEFT ) ) {
            assert( resourceSprite > 0 );
            Maps::Tile::updateTileObjectIcnIndex( world.getTile( Maps::GetDirectionIndex( tile.GetIndex(), Direction::LEFT ) ), uidResource, resourceSprite - 1 );
        }
    }

    void updateRandomArtifact( Maps::Tile & tile )
    {
        Artifact art;

        switch ( tile.getMainObjectType() ) {
        case MP2::OBJ_RANDOM_ARTIFACT:
            art = Artifact::Rand( Artifact::ART_LEVEL_ALL_NORMAL );
            break;
        case MP2::OBJ_RANDOM_ARTIFACT_TREASURE:
            art = Artifact::Rand( Artifact::ART_LEVEL_TREASURE );
            break;
        case MP2::OBJ_RANDOM_ARTIFACT_MINOR:
            art = Artifact::Rand( Artifact::ART_LEVEL_MINOR );
            break;
        case MP2::OBJ_RANDOM_ARTIFACT_MAJOR:
            art = Artifact::Rand( Artifact::ART_LEVEL_MAJOR );
            break;
        default:
            // Did you add another random artifact type? Add the logic above!
            assert( 0 );
            return;
        }

        if ( !art.isValid() ) {
            DEBUG_LOG( DBG_GAME, DBG_WARN, "Failed to set an artifact over a random artifact on tile " << tile.GetIndex() )
            resetObjectMetadata( tile );
            tile.updateObjectType();
            return;
        }

        tile.setMainObjectType( MP2::OBJ_ARTIFACT );

        uint32_t uidArtifact = tile.getObjectIdByObjectIcnType( MP2::OBJ_ICN_TYPE_OBJNARTI );
        if ( uidArtifact == 0 ) {
            uidArtifact = tile.getMainObjectPart()._uid;
        }

        static_assert( std::is_same_v<decltype( Maps::Tile::updateTileObjectIcnIndex ), void( Maps::Tile &, uint32_t, uint8_t )> );

        // Please refer to ICN::OBJNARTI for artifact images. Since in the original game artifact UID start from 0 we have to deduct 1 from the current artifact ID.
        const uint32_t artSpriteIndex = ( art.GetID() - 1 ) * 2 + 1;

        assert( artSpriteIndex > std::numeric_limits<uint8_t>::min() && artSpriteIndex <= std::numeric_limits<uint8_t>::max() );

        Maps::Tile::updateTileObjectIcnIndex( tile, uidArtifact, static_cast<uint8_t>( artSpriteIndex ) );

        // replace artifact shadow
        if ( Maps::isValidDirection( tile.GetIndex(), Direction::LEFT ) ) {
            Maps::Tile::updateTileObjectIcnIndex( world.getTile( Maps::GetDirectionIndex( tile.GetIndex(), Direction::LEFT ) ), uidArtifact,
                                                  static_cast<uint8_t>( artSpriteIndex - 1 ) );
        }
    }

    void updateRandomMonster( Maps::Tile & tile )
    {
        Monster mons;

        switch ( tile.getMainObjectType() ) {
        case MP2::OBJ_RANDOM_MONSTER:
            mons = Monster::Rand( Monster::LevelType::LEVEL_ANY );
            break;
        case MP2::OBJ_RANDOM_MONSTER_WEAK:
            mons = Monster::Rand( Monster::LevelType::LEVEL_1 );
            break;
        case MP2::OBJ_RANDOM_MONSTER_MEDIUM:
            mons = Monster::Rand( Monster::LevelType::LEVEL_2 );
            break;
        case MP2::OBJ_RANDOM_MONSTER_STRONG:
            mons = Monster::Rand( Monster::LevelType::LEVEL_3 );
            break;
        case MP2::OBJ_RANDOM_MONSTER_VERY_STRONG:
            mons = Monster::Rand( Monster::LevelType::LEVEL_4 );
            break;
        default:
            // Did you add another random monster type? Add the logic above!
            assert( 0 );
            break;
        }

        if ( !mons.isValid() ) {
            DEBUG_LOG( DBG_GAME, DBG_WARN, "Failed to set a monster over a random monster on tile " << tile.GetIndex() )
            resetObjectMetadata( tile );
            tile.updateObjectType();
            return;
        }

        tile.setMainObjectType( MP2::OBJ_MONSTER );

        using IcnIndexType = decltype( tile.getMainObjectPart().icnIndex );
        static_assert( std::is_same_v<IcnIndexType, uint8_t> );

        assert( mons.GetID() > std::numeric_limits<IcnIndexType>::min() && mons.GetID() <= std::numeric_limits<IcnIndexType>::max() );

        tile.getMainObjectPart().icnIndex = static_cast<IcnIndexType>( mons.GetID() - 1 ); // ICN::MONS32 starts from PEASANT
    }

    bool placeObjectOnTile( const Maps::Tile & tile, const Maps::ObjectInfo & info )
    {
        // If this assertion blows up then what kind of object you are trying to place if it's empty?
        assert( !info.empty() );

        // Verify that the object is allowed to be placed.
        const fheroes2::Point mainTilePos = tile.GetCenter();

        for ( const auto & partInfo : info.groundLevelParts ) {
            if ( partInfo.layerType == Maps::SHADOW_LAYER || partInfo.layerType == Maps::TERRAIN_LAYER ) {
                // Shadows and terrain objects do not affect on passability so it is fine to ignore them not being rendered.
                continue;
            }

            const fheroes2::Point pos = mainTilePos + partInfo.tileOffset;
            if ( !Maps::isValidAbsPoint( pos.x, pos.y ) ) {
                // This shouldn't happen as the object must be verified before placement.
                assert( 0 );
                return false;
            }
        }

        for ( const auto & partInfo : info.topLevelParts ) {
            const fheroes2::Point pos = mainTilePos + partInfo.tileOffset;
            if ( !Maps::isValidAbsPoint( pos.x, pos.y ) ) {
                // This shouldn't happen as the object must be verified before placement.
                assert( 0 );
                return false;
            }
        }

        const uint32_t uid = Maps::getNewObjectUID();

        for ( const auto & partInfo : info.groundLevelParts ) {
            const fheroes2::Point pos = mainTilePos + partInfo.tileOffset;
            if ( !Maps::isValidAbsPoint( pos.x, pos.y ) ) {
                // Make sure that the above condition about object placement is correct.
                assert( partInfo.layerType == Maps::SHADOW_LAYER || partInfo.layerType == Maps::TERRAIN_LAYER );

                // Ignore this tile since it is out of the map.
                continue;
            }

            // We need to be very careful to update tile object type to make sure that this is a correct type.
            // Additionally, all object parts must be sorted based on their layer type.
            Maps::Tile & currentTile = world.getTile( pos.x, pos.y );

            bool setObjectType = false;
            // The first case if the existing tile has no object type being set.
            const MP2::MapObjectType tileObjectType = currentTile.getMainObjectType();

            // Always move the current object part to the back of the ground layer list to make proper sorting later.
            currentTile.moveMainObjectPartToGroundLevel();

            if ( tileObjectType == MP2::OBJ_NONE ) {
                setObjectType = ( partInfo.objectType != MP2::OBJ_NONE );
            }
            else if ( partInfo.objectType != MP2::OBJ_NONE ) {
                if ( MP2::isOffGameActionObject( partInfo.objectType ) ) {
                    // Since this is an action object we must enforce setting its type.
                    setObjectType = true;
                }
                else if ( !MP2::isOffGameActionObject( tileObjectType ) ) {
                    // The current tile does not have an action object.
                    // We need to run through each object part present at the tile and see if it has "higher" layer object.
                    bool higherObjectFound = false;

                    for ( const auto & topPart : currentTile.getTopObjectParts() ) {
                        const MP2::MapObjectType type = Maps::getObjectTypeByIcn( topPart.icnType, topPart.icnIndex );
                        if ( type != MP2::OBJ_NONE ) {
                            // A top object part is present.
                            higherObjectFound = true;
                            break;
                        }
                    }

                    if ( !higherObjectFound ) {
                        for ( const auto & groundPart : currentTile.getGroundObjectParts() ) {
                            if ( groundPart.layerType >= partInfo.layerType ) {
                                // A ground object part is has "lower" or equal layer type. Skip it.
                                continue;
                            }

                            const MP2::MapObjectType type = Maps::getObjectTypeByIcn( groundPart.icnType, groundPart.icnIndex );
                            if ( type != MP2::OBJ_NONE ) {
                                // A ground object part is present and it has "higher" layer type.
                                higherObjectFound = true;
                                break;
                            }
                        }
                    }

                    setObjectType = !higherObjectFound;
                }
            }

#if defined( WITH_DEBUG )
            // Check that we don't put the same object part.
            for ( const auto & groundPart : currentTile.getGroundObjectParts() ) {
                assert( groundPart._uid != uid || groundPart.icnIndex != static_cast<uint8_t>( partInfo.icnIndex ) || groundPart.icnType != partInfo.icnType
                        || groundPart.layerType != partInfo.layerType );
            }
#endif

            // Push the object to the ground (bottom) object parts.
            currentTile.pushGroundObjectPart( Maps::ObjectPart( partInfo.layerType, uid, partInfo.icnType, static_cast<uint8_t>( partInfo.icnIndex ) ) );

            // Sort all objects.
            currentTile.sortObjectParts();

            // Set object type if needed.
            if ( setObjectType ) {
                currentTile.setMainObjectType( partInfo.objectType );
            }
        }

        for ( const auto & partInfo : info.topLevelParts ) {
            const fheroes2::Point pos = mainTilePos + partInfo.tileOffset;
            if ( !Maps::isValidAbsPoint( pos.x, pos.y ) ) {
                // This shouldn't happen as the object must be verified before placement.
                assert( 0 );
                continue;
            }

            Maps::Tile & currentTile = world.getTile( pos.x, pos.y );
            // Top object parts do not need sorting.
            currentTile.pushTopObjectPart( Maps::ObjectPart( Maps::OBJECT_LAYER, uid, partInfo.icnType, static_cast<uint8_t>( partInfo.icnIndex ) ) );

            // Set object type only if the current object part has a type and the object is not an action object.
            if ( partInfo.objectType != MP2::OBJ_NONE && !MP2::isOffGameActionObject( currentTile.getMainObjectType() ) ) {
                currentTile.setMainObjectType( partInfo.objectType );
            }
        }

        return true;
    }
}

namespace Maps
{
    int32_t getMineSpellIdFromTile( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_MINE ) {
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            return Spell::NONE;
        }

        return static_cast<int32_t>( tile.metadata()[2] );
    }

    void setMineSpellOnTile( Tile & tile, const int32_t spellId )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_MINE ) {
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            return;
        }

        tile.metadata()[2] = spellId;
    }

    void removeMineSpellFromTile( Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_MINE ) {
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            return;
        }

        tile.metadata()[2] = 0;
    }

    Funds getDailyIncomeObjectResources( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_MINE:
        case MP2::OBJ_SAWMILL:
            return { static_cast<int>( tile.metadata()[0] ), tile.metadata()[1] };
        default:
            break;
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return {};
    }

    Spell getSpellFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_PYRAMID:
            return { static_cast<int>( tile.metadata()[0] ) };
        default:
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            break;
        }

        return { Spell::NONE };
    }

    void setSpellOnTile( Tile & tile, const int spellId )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
        case MP2::OBJ_PYRAMID:
            tile.metadata()[0] = spellId;
            break;
        case MP2::OBJ_ARTIFACT:
            // Only the Spell Scroll artifact can have a spell set.
            assert( tile.metadata()[0] == Artifact::SPELL_SCROLL );
            tile.metadata()[1] = spellId;
            break;
        default:
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            break;
        }
    }

    void setMonsterOnTileJoinCondition( Tile & tile, const int32_t condition )
    {
        if ( tile.getMainObjectType() == MP2::OBJ_MONSTER ) {
            tile.metadata()[2] = condition;
        }
        else {
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
        }
    }

    bool isMonsterOnTileJoinConditionSkip( const Tile & tile )
    {
        if ( tile.getMainObjectType() == MP2::OBJ_MONSTER ) {
            return ( tile.metadata()[2] == Monster::JOIN_CONDITION_SKIP );
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return false;
    }

    bool isMonsterOnTileJoinConditionFree( const Tile & tile )
    {
        if ( tile.getMainObjectType() == MP2::OBJ_MONSTER ) {
            return ( tile.metadata()[2] == Monster::JOIN_CONDITION_FREE );
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return false;
    }

    int getColorFromBarrierSprite( const MP2::ObjectIcnType objectIcnType, const uint8_t icnIndex )
    {
        // The color of the barrier is actually being stored in tile metadata but as of now we use sprite information.

        if ( MP2::OBJ_ICN_TYPE_X_LOC3 == objectIcnType && 60 <= icnIndex && 102 >= icnIndex ) {
            // 60, 66, 72, 78, 84, 90, 96, 102
            return ( ( icnIndex - 60 ) / 6 ) + 1;
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return 0;
    }

    int getColorFromTravellerTentSprite( const MP2::ObjectIcnType objectIcnType, const uint8_t icnIndex )
    {
        // The color of the barrier is actually being stored in tile metadata but as of now we use sprite information.

        if ( MP2::OBJ_ICN_TYPE_X_LOC3 == objectIcnType && 110 <= icnIndex && 138 >= icnIndex ) {
            // 110, 114, 118, 122, 126, 130, 134, 138
            return ( ( icnIndex - 110 ) / 4 ) + 1;
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return 0;
    }

    const ObjectPart * getObjectPartByActionType( const Tile & tile, const MP2::MapObjectType type )
    {
        if ( !MP2::isOffGameActionObject( type ) ) {
            return nullptr;
        }

        MP2::MapObjectType objectType = getObjectTypeByIcn( tile.getMainObjectPart().icnType, tile.getMainObjectPart().icnIndex );
        if ( objectType == type ) {
            return &tile.getMainObjectPart();
        }

        for ( const auto & objectPart : tile.getGroundObjectParts() ) {
            objectType = getObjectTypeByIcn( objectPart.icnType, objectPart.icnIndex );
            if ( objectType == type ) {
                return &objectPart;
            }
        }

        return nullptr;
    }

    Monster getMonsterFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_WATCH_TOWER:
            return { Monster::ORC };
        case MP2::OBJ_EXCAVATION:
            return { Monster::SKELETON };
        case MP2::OBJ_CAVE:
            return { Monster::CENTAUR };
        case MP2::OBJ_TREE_HOUSE:
            return { Monster::SPRITE };
        case MP2::OBJ_ARCHER_HOUSE:
            return { Monster::ARCHER };
        case MP2::OBJ_GOBLIN_HUT:
            return { Monster::GOBLIN };
        case MP2::OBJ_DWARF_COTTAGE:
            return { Monster::DWARF };
        case MP2::OBJ_HALFLING_HOLE:
            return { Monster::HALFLING };
        case MP2::OBJ_PEASANT_HUT:
            return { Monster::PEASANT };
        case MP2::OBJ_RUINS:
            return { Monster::MEDUSA };
        case MP2::OBJ_TREE_CITY:
            return { Monster::SPRITE };
        case MP2::OBJ_WAGON_CAMP:
            return { Monster::ROGUE };
        case MP2::OBJ_DESERT_TENT:
            return { Monster::NOMAD };
        case MP2::OBJ_TROLL_BRIDGE:
            return { Monster::TROLL };
        case MP2::OBJ_DRAGON_CITY:
            return { Monster::RED_DRAGON };
        case MP2::OBJ_CITY_OF_DEAD:
            return { Monster::POWER_LICH };
        case MP2::OBJ_GENIE_LAMP:
            return { Monster::GENIE };
        case MP2::OBJ_ABANDONED_MINE:
            return { Monster::GHOST };
        case MP2::OBJ_WATER_ALTAR:
            return { Monster::WATER_ELEMENT };
        case MP2::OBJ_AIR_ALTAR:
            return { Monster::AIR_ELEMENT };
        case MP2::OBJ_FIRE_ALTAR:
            return { Monster::FIRE_ELEMENT };
        case MP2::OBJ_EARTH_ALTAR:
            return { Monster::EARTH_ELEMENT };
        case MP2::OBJ_BARROW_MOUNDS:
            return { Monster::GHOST };
        case MP2::OBJ_MONSTER:
            return { tile.getMainObjectPart().icnIndex + 1 };
        default:
            break;
        }

        if ( MP2::isCaptureObject( tile.getMainObjectType( false ) ) ) {
            return { world.GetCapturedObject( tile.GetIndex() ).GetTroop().GetID() };
        }

        return { Monster::UNKNOWN };
    }

    Artifact getArtifactFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_DAEMON_CAVE:
        case MP2::OBJ_GRAVEYARD:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_SHIPWRECK:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
        case MP2::OBJ_SKELETON:
        case MP2::OBJ_TREASURE_CHEST:
        case MP2::OBJ_WAGON:
            return { static_cast<int>( tile.metadata()[0] ) };

        case MP2::OBJ_ARTIFACT:
            if ( tile.metadata()[2] == static_cast<uint32_t>( ArtifactCaptureCondition::CONTAINS_SPELL ) ) {
                Artifact art( Artifact::SPELL_SCROLL );
                art.SetSpell( static_cast<int32_t>( tile.metadata()[1] ) );
                return art;
            }

            return { static_cast<int>( tile.metadata()[0] ) };

        default:
            break;
        }

        // Why are you calling this function for an unsupported object type?
        assert( 0 );
        return { Artifact::UNKNOWN };
    }

    Skill::Secondary getArtifactSecondarySkillRequirement( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_ARTIFACT ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return {};
        }

        switch ( static_cast<ArtifactCaptureCondition>( tile.metadata()[2] ) ) {
        case ArtifactCaptureCondition::HAVE_WISDOM_SKILL:
            return { Skill::Secondary::WISDOM, Skill::Level::BASIC };
        case ArtifactCaptureCondition::HAVE_LEADERSHIP_SKILL:
            return { Skill::Secondary::LEADERSHIP, Skill::Level::BASIC };
        default:
            break;
        }

        // Why are you calling this for invalid conditions?
        assert( 0 );
        return {};
    }

    ArtifactCaptureCondition getArtifactCaptureCondition( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_ARTIFACT ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return ArtifactCaptureCondition::NO_CONDITIONS;
        }

        return static_cast<ArtifactCaptureCondition>( tile.metadata()[2] );
    }

    Funds getArtifactResourceRequirement( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_ARTIFACT ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return {};
        }

        switch ( static_cast<ArtifactCaptureCondition>( tile.metadata()[2] ) ) {
        case ArtifactCaptureCondition::PAY_2000_GOLD:
            return { Resource::GOLD, 2000 };
        case ArtifactCaptureCondition::PAY_2500_GOLD_AND_3_RESOURCES:
            return Funds{ Resource::GOLD, 2500 } + Funds{ Resource::getResourceTypeFromIconIndex( tile.metadata()[1] - 1 ), 3 };
        case ArtifactCaptureCondition::PAY_3000_GOLD_AND_5_RESOURCES:
            return Funds{ Resource::GOLD, 3000 } + Funds{ Resource::getResourceTypeFromIconIndex( tile.metadata()[1] - 1 ), 5 };
        default:
            break;
        }

        // Why are you calling this for invalid conditions?
        assert( 0 );
        return {};
    }

    DaemonCaveCaptureBonus getDaemonCaveBonusType( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_DAEMON_CAVE ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return DaemonCaveCaptureBonus::EMPTY;
        }

        return static_cast<DaemonCaveCaptureBonus>( tile.metadata()[2] );
    }

    Funds getDaemonPaymentCondition( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_DAEMON_CAVE ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return {};
        }

        if ( static_cast<DaemonCaveCaptureBonus>( tile.metadata()[2] ) != DaemonCaveCaptureBonus::PAY_2500_GOLD ) {
            // Why are you calling this for invalid conditions?
            assert( 0 );
            return {};
        }

        return { Resource::GOLD, tile.metadata()[1] };
    }

    ShipwreckCaptureCondition getShipwreckCaptureCondition( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_SHIPWRECK ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return ShipwreckCaptureCondition::EMPTY;
        }

        return static_cast<ShipwreckCaptureCondition>( tile.metadata()[2] );
    }

    Funds getTreeOfKnowledgeRequirement( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_TREE_OF_KNOWLEDGE ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return {};
        }

        return { static_cast<int>( tile.metadata()[0] ), tile.metadata()[1] };
    }

    Skill::Secondary getSecondarySkillFromWitchsHut( const Tile & tile )
    {
        if ( tile.getMainObjectType( false ) != MP2::OBJ_WITCHS_HUT ) {
            // Why are you calling this for an unsupported object type?
            assert( 0 );
            return {};
        }

        return { static_cast<int>( tile.metadata()[0] ), Skill::Level::BASIC };
    }

    void setResourceOnTile( Tile & tile, const int resourceType, uint32_t value )
    {
        tile.metadata()[0] = resourceType;
        tile.metadata()[1] = value;
    }

    Funds getFundsFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
            // Campfire or barrel contains N of non-Gold resources and (N * 100) Gold.
            return Funds{ static_cast<int>( tile.metadata()[0] ), tile.metadata()[1] } + Funds{ Resource::GOLD, tile.metadata()[1] * 100 };

        case MP2::OBJ_FLOTSAM:
            return Funds{ Resource::WOOD, tile.metadata()[0] } + Funds{ Resource::GOLD, tile.metadata()[1] };

        case MP2::OBJ_DAEMON_CAVE:
        case MP2::OBJ_GRAVEYARD:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_SHIPWRECK:
        case MP2::OBJ_TREASURE_CHEST:
            return { Resource::GOLD, tile.metadata()[1] };

        case MP2::OBJ_DERELICT_SHIP:
        case MP2::OBJ_LEAN_TO:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_WINDMILL:
        case MP2::OBJ_WATER_WHEEL:
            return { static_cast<int>( tile.metadata()[0] ), tile.metadata()[1] };

        case MP2::OBJ_WAGON:
            return { static_cast<int>( tile.metadata()[1] ), tile.metadata()[2] };

        default:
            break;
        }

        // Why are you calling this for an unsupported object type?
        assert( 0 );
        return {};
    }

    Troop getTroopFromTile( const Tile & tile )
    {
        return MP2::isCaptureObject( tile.getMainObjectType( false ) ) ? world.GetCapturedObject( tile.GetIndex() ).GetTroop()
                                                                       : Troop( getMonsterFromTile( tile ), getMonsterCountFromTile( tile ) );
    }

    PlayerColor getColorFromTile( const Tile & tile )
    {
        return world.ColorCapturedObject( tile.GetIndex() );
    }

    int getBarrierColorFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_BARRIER:
        case MP2::OBJ_TRAVELLER_TENT:
            return static_cast<int>( tile.metadata()[0] );
        default:
            // Have you added a new Barrier or Traveller Tent object? Update the logic above!
            assert( 0 );

            return 0;
        }
    }

    void setColorOnTile( const Tile & tile, const PlayerColor color )
    {
        world.CaptureObject( tile.GetIndex(), color );
    }

    void setBarrierColorOnTile( Tile & tile, const int barrierColor )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_BARRIER:
        case MP2::OBJ_TRAVELLER_TENT:
            tile.metadata()[0] = static_cast<uint32_t>( barrierColor );
            break;
        default:
            // Have you added a new Barrier or Traveller Tent object? Update the logic above!
            assert( 0 );

            break;
        }
    }

    bool doesTileContainValuableItems( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_ARTIFACT:
        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
        case MP2::OBJ_FLOTSAM:
        case MP2::OBJ_RESOURCE:
        case MP2::OBJ_SEA_CHEST:
        case MP2::OBJ_SHIPWRECK_SURVIVOR:
        case MP2::OBJ_TREASURE_CHEST:
            return true;

        case MP2::OBJ_PYRAMID:
            return getSpellFromTile( tile ).isValid();

        case MP2::OBJ_DERELICT_SHIP:
        case MP2::OBJ_GRAVEYARD:
        case MP2::OBJ_LEAN_TO:
        case MP2::OBJ_MAGIC_GARDEN:
        case MP2::OBJ_SHIPWRECK:
        case MP2::OBJ_WATER_WHEEL:
        case MP2::OBJ_WINDMILL:
            return tile.metadata()[1] > 0;

        case MP2::OBJ_SKELETON:
            return getArtifactFromTile( tile ) != Artifact::UNKNOWN;

        case MP2::OBJ_WAGON:
            return getArtifactFromTile( tile ) != Artifact::UNKNOWN || tile.metadata()[2] != 0;

        case MP2::OBJ_DAEMON_CAVE:
            return tile.metadata()[2] != 0;

        default:
            break;
        }

        return false;
    }

    void resetObjectMetadata( Tile & tile )
    {
        for ( uint32_t & value : tile.metadata() ) {
            value = 0;
        }
    }

    uint32_t getMonsterCountFromTile( const Tile & tile )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_ABANDONED_MINE:
        case MP2::OBJ_AIR_ALTAR:
        case MP2::OBJ_ARCHER_HOUSE:
        case MP2::OBJ_BARROW_MOUNDS:
        case MP2::OBJ_CAVE:
        case MP2::OBJ_CITY_OF_DEAD:
        case MP2::OBJ_DESERT_TENT:
        case MP2::OBJ_DRAGON_CITY:
        case MP2::OBJ_DWARF_COTTAGE:
        case MP2::OBJ_EARTH_ALTAR:
        case MP2::OBJ_EXCAVATION:
        case MP2::OBJ_FIRE_ALTAR:
        case MP2::OBJ_GENIE_LAMP:
        case MP2::OBJ_GOBLIN_HUT:
        case MP2::OBJ_HALFLING_HOLE:
        case MP2::OBJ_MONSTER:
        case MP2::OBJ_PEASANT_HUT:
        case MP2::OBJ_RUINS:
        case MP2::OBJ_TREE_CITY:
        case MP2::OBJ_TREE_HOUSE:
        case MP2::OBJ_TROLL_BRIDGE:
        case MP2::OBJ_WAGON_CAMP:
        case MP2::OBJ_WATCH_TOWER:
        case MP2::OBJ_WATER_ALTAR:
            return tile.metadata()[0];
        default:
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            break;
        }

        return 0;
    }

    void setMonsterCountOnTile( Tile & tile, uint32_t count )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_ABANDONED_MINE:
        case MP2::OBJ_AIR_ALTAR:
        case MP2::OBJ_ARCHER_HOUSE:
        case MP2::OBJ_BARROW_MOUNDS:
        case MP2::OBJ_CAVE:
        case MP2::OBJ_CITY_OF_DEAD:
        case MP2::OBJ_DESERT_TENT:
        case MP2::OBJ_DRAGON_CITY:
        case MP2::OBJ_DWARF_COTTAGE:
        case MP2::OBJ_EARTH_ALTAR:
        case MP2::OBJ_EXCAVATION:
        case MP2::OBJ_FIRE_ALTAR:
        case MP2::OBJ_GENIE_LAMP:
        case MP2::OBJ_GOBLIN_HUT:
        case MP2::OBJ_HALFLING_HOLE:
        case MP2::OBJ_MONSTER:
        case MP2::OBJ_PEASANT_HUT:
        case MP2::OBJ_RUINS:
        case MP2::OBJ_TREE_CITY:
        case MP2::OBJ_TREE_HOUSE:
        case MP2::OBJ_TROLL_BRIDGE:
        case MP2::OBJ_WAGON_CAMP:
        case MP2::OBJ_WATCH_TOWER:
        case MP2::OBJ_WATER_ALTAR:
            tile.metadata()[0] = count;
            return;
        default:
            // Why are you calling this function for an unsupported object type?
            assert( 0 );
            break;
        }
    }

    void updateDwellingPopulationOnTile( Tile & tile, const bool isFirstLoad )
    {
        uint32_t count = isFirstLoad ? 0 : getMonsterCountFromTile( tile );
        const MP2::MapObjectType objectType = tile.getMainObjectType( false );

        switch ( objectType ) {
        // join monsters
        case MP2::OBJ_HALFLING_HOLE:
            count += isFirstLoad ? Rand::Get( 20, 40 ) : Rand::Get( 5, 10 );
            break;
        case MP2::OBJ_PEASANT_HUT:
            count += isFirstLoad ? Rand::Get( 20, 50 ) : Rand::Get( 5, 10 );
            break;
        case MP2::OBJ_EXCAVATION:
        case MP2::OBJ_TREE_HOUSE:
            count += isFirstLoad ? Rand::Get( 10, 25 ) : Rand::Get( 4, 8 );
            break;
        case MP2::OBJ_CAVE:
            count += isFirstLoad ? Rand::Get( 10, 20 ) : Rand::Get( 3, 6 );
            break;
        case MP2::OBJ_GOBLIN_HUT:
            count += isFirstLoad ? Rand::Get( 15, 40 ) : Rand::Get( 3, 6 );
            break;

        case MP2::OBJ_TREE_CITY:
            count += isFirstLoad ? Rand::Get( 20, 40 ) : Rand::Get( 10, 20 );
            break;

        case MP2::OBJ_WATCH_TOWER:
            count += isFirstLoad ? Rand::Get( 7, 10 ) : Rand::Get( 2, 4 );
            break;
        case MP2::OBJ_ARCHER_HOUSE:
            count += isFirstLoad ? Rand::Get( 10, 25 ) : Rand::Get( 2, 4 );
            break;
        case MP2::OBJ_DWARF_COTTAGE:
            count += isFirstLoad ? Rand::Get( 10, 20 ) : Rand::Get( 3, 6 );
            break;
        case MP2::OBJ_WAGON_CAMP:
            count += isFirstLoad ? Rand::Get( 30, 50 ) : Rand::Get( 3, 6 );
            break;
        case MP2::OBJ_DESERT_TENT:
            count += isFirstLoad ? Rand::Get( 10, 20 ) : Rand::Get( 1, 3 );
            break;
        case MP2::OBJ_RUINS:
            count += isFirstLoad ? Rand::Get( 3, 5 ) : Rand::Get( 1, 3 );
            break;
        case MP2::OBJ_WATER_ALTAR:
        case MP2::OBJ_AIR_ALTAR:
        case MP2::OBJ_FIRE_ALTAR:
        case MP2::OBJ_EARTH_ALTAR:
        case MP2::OBJ_BARROW_MOUNDS:
            count += Rand::Get( 2, 5 );
            break;

        case MP2::OBJ_TROLL_BRIDGE:
        case MP2::OBJ_CITY_OF_DEAD:
            if ( isFirstLoad ) {
                count = Rand::Get( 4, 6 );
            }
            else if ( getColorFromTile( tile ) != PlayerColor::NONE ) {
                // If the Troll Bridge or City of Dead has been captured, its population is increased by 1-3 creature per week.
                count += Rand::Get( 1, 3 );
            }

            break;

        case MP2::OBJ_DRAGON_CITY:
            if ( isFirstLoad ) {
                count = 2;
            }
            else if ( getColorFromTile( tile ) != PlayerColor::NONE ) {
                // If the Dragon City has been captured or has 0 creatures, its population is increased by 1 dragon per week.
                ++count;
            }

            break;

        default:
            // Did you add a new dwelling on Adventure Map? Add the logic above!
            assert( 0 );
            break;
        }

        assert( count > 0 );
        setMonsterCountOnTile( tile, count );
    }

    void updateObjectInfoTile( Tile & tile, const bool isFirstLoad )
    {
        switch ( tile.getMainObjectType( false ) ) {
        case MP2::OBJ_WITCHS_HUT:
            assert( isFirstLoad );

            static_assert( Skill::Secondary::UNKNOWN == 0, "You are breaking the logic by changing the Skill::Secondary::UNKNOWN value!" );
            if ( tile.metadata()[0] != Skill::Secondary::UNKNOWN ) {
                // The skill has been set externally.
                break;
            }

            tile.metadata()[0] = Rand::Get( GameStatic::getSecondarySkillsForWitchsHut() );
            break;

        case MP2::OBJ_SHRINE_FIRST_CIRCLE:
            assert( isFirstLoad );

            static_assert( Spell::NONE == 0, "You are breaking the logic by changing the Spell::NONE value!" );
            if ( tile.metadata()[0] != Spell::NONE ) {
                // The spell has been set externally.
                break;
            }

            setSpellOnTile( tile, Spell::getRandomSpell( 1 ).GetID() );
            break;

        case MP2::OBJ_SHRINE_SECOND_CIRCLE:
            assert( isFirstLoad );

            static_assert( Spell::NONE == 0, "You are breaking the logic by changing the Spell::NONE value!" );
            if ( tile.metadata()[0] != Spell::NONE ) {
                // The spell has been set externally.
                break;
            }

            setSpellOnTile( tile, Spell::getRandomSpell( 2 ).GetID() );
            break;

        case MP2::OBJ_SHRINE_THIRD_CIRCLE:
            assert( isFirstLoad );

            static_assert( Spell::NONE == 0, "You are breaking the logic by changing the Spell::NONE value!" );
            if ( tile.metadata()[0] != Spell::NONE ) {
                // The spell has been set externally.
                break;
            }

            setSpellOnTile( tile, Spell::getRandomSpell( 3 ).GetID() );
            break;

        case MP2::OBJ_SKELETON: {
            assert( isFirstLoad );

            Rand::Queue percents( 2 );
            // 80%: empty
            percents.Push( 0, 80 );
            // 20%: artifact 1 or 2 or 3
            percents.Push( 1, 20 );

            if ( percents.Get() ) {
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_ALL_NORMAL );
            }
            else {
                tile.metadata()[0] = Artifact::UNKNOWN;
            }
            break;
        }

        case MP2::OBJ_WAGON: {
            assert( isFirstLoad );

            resetObjectMetadata( tile );

            Rand::Queue percents( 3 );
            // 20%: empty
            percents.Push( 0, 20 );
            // 10%: artifact 1 or 2
            percents.Push( 1, 10 );
            // 50%: resource
            percents.Push( 2, 50 );

            switch ( percents.Get() ) {
            case 1:
                tile.metadata()[0] = Artifact::Rand( Rand::Get( 1 ) ? Artifact::ART_LEVEL_TREASURE : Artifact::ART_LEVEL_MINOR );
                break;
            case 2:
                tile.metadata()[1] = Resource::Rand( false );
                tile.metadata()[2] = Rand::Get( 2, 5 );
                break;
            default:
                break;
            }
            break;
        }

        case MP2::OBJ_ARTIFACT: {
            assert( isFirstLoad );

            uint8_t artifactSpriteIndex = Artifact::UNKNOWN;
            if ( tile.getMainObjectPart().icnType == MP2::OBJ_ICN_TYPE_OBJNARTI ) {
                artifactSpriteIndex = tile.getMainObjectPart().icnIndex;
            }
            else {
                // On some hacked original maps artifact can be placed to the ground layer object parts.
                for ( const auto & part : tile.getGroundObjectParts() ) {
                    if ( part.icnType == MP2::OBJ_ICN_TYPE_OBJNARTI ) {
                        artifactSpriteIndex = part.icnIndex;
                        break;
                    }
                }
            }

            const int art = Artifact::getArtifactFromMapSpriteIndex( artifactSpriteIndex ).GetID();
            if ( Artifact::UNKNOWN == art ) {
                // This is an unknown artifact. Did you add a new one?
                assert( 0 );
                return;
            }

            if ( art == Artifact::SPELL_SCROLL ) {
                static_assert( Spell::FIREBALL < Spell::SETWGUARDIAN, "The order of spell IDs has been changed, check the logic below" );

                // Spell ID has a value of 1 bigger than in the original game.
                Artifact tempArt( art );
                tempArt.SetSpell( static_cast<int>( tile.metadata()[0] ) + 1 );

                const uint32_t spell
                    = std::clamp( static_cast<uint32_t>( tempArt.getSpellId() ), static_cast<uint32_t>( Spell::FIREBALL ), static_cast<uint32_t>( Spell::SETWGUARDIAN ) );

                tile.metadata()[1] = spell;
                tile.metadata()[2] = static_cast<uint32_t>( ArtifactCaptureCondition::CONTAINS_SPELL );
            }
            else {
                // 70% chance of no conditions.
                // Refer to ArtifactCaptureCondition enumeration.
                const uint32_t cond = Rand::Get( 1, 10 ) < 4 ? Rand::Get( 1, 13 ) : 0;

                tile.metadata()[2] = cond;

                if ( cond == static_cast<uint32_t>( ArtifactCaptureCondition::PAY_2500_GOLD_AND_3_RESOURCES )
                     || cond == static_cast<uint32_t>( ArtifactCaptureCondition::PAY_3000_GOLD_AND_5_RESOURCES ) ) {
                    // TODO: why do we use icon ICN index instead of map ICN index?
                    tile.metadata()[1] = Resource::getIconIcnIndex( Resource::Rand( false ) ) + 1;
                }
            }

            tile.metadata()[0] = art;

            break;
        }

        case MP2::OBJ_RESOURCE: {
            assert( isFirstLoad );

            int resourceType = Resource::UNKNOWN;

            if ( tile.getMainObjectPart().icnType == MP2::OBJ_ICN_TYPE_OBJNRSRC ) {
                // The resource is located at the top.
                resourceType = Resource::FromIndexSprite( tile.getMainObjectPart().icnIndex );
            }
            else {
                for ( const auto & part : tile.getGroundObjectParts() ) {
                    if ( part.icnType == MP2::OBJ_ICN_TYPE_OBJNRSRC ) {
                        resourceType = Resource::FromIndexSprite( part.icnIndex );
                        // If this happens we are in trouble. It looks like that map maker put the resource under an object which is impossible to do.
                        // Let's update the tile's object type to properly show the action object.
                        tile.updateObjectType();

                        break;
                    }
                }
            }

            if ( ( tile.metadata()[0] & Resource::ALL ) != 0 && tile.metadata()[1] > 0 ) {
                // The resource was set externally.
                break;
            }

            uint32_t count = 0;
            switch ( resourceType ) {
            case Resource::GOLD:
                count = 100 * Rand::Get( 5, 10 );
                break;
            case Resource::WOOD:
            case Resource::ORE:
                count = Rand::Get( 5, 10 );
                break;
            case Resource::MERCURY:
            case Resource::SULFUR:
            case Resource::CRYSTAL:
            case Resource::GEMS:
                count = Rand::Get( 3, 6 );
                break;
            default:
                // Some maps have broken resources being put which ideally we need to correct. Let's make them 0 Wood.
                DEBUG_LOG( DBG_GAME, DBG_WARN,
                           "Tile " << tile.GetIndex() << " contains unknown resource type. Object ICN type " << tile.getMainObjectPart().icnType << ", image index "
                                   << tile.getMainObjectPart().icnIndex )
                resourceType = Resource::WOOD;
                count = 0;
                break;
            }

            setResourceOnTile( tile, resourceType, count );
            break;
        }

        case MP2::OBJ_BARREL:
        case MP2::OBJ_CAMPFIRE:
            assert( isFirstLoad );

            // 4-6 random resource and + 400-600 gold
            setResourceOnTile( tile, Resource::Rand( false ), Rand::Get( 4, 6 ) );
            break;

        case MP2::OBJ_MAGIC_GARDEN:
            // 5 gems or 500 gold
            if ( Rand::Get( 1 ) )
                setResourceOnTile( tile, Resource::GEMS, 5 );
            else
                setResourceOnTile( tile, Resource::GOLD, 500 );
            break;

        case MP2::OBJ_WATER_WHEEL:
            // first week 500 gold, next week 1000 gold
            setResourceOnTile( tile, Resource::GOLD, ( 0 == world.CountDay() ? 500 : 1000 ) );
            break;

        case MP2::OBJ_WINDMILL: {
            int res = Resource::WOOD;
            while ( res == Resource::WOOD ) {
                res = Resource::Rand( false );
            }

            // 2 pieces of random resources.
            setResourceOnTile( tile, res, 2 );
            break;
        }

        case MP2::OBJ_LEAN_TO:
            assert( isFirstLoad );

            // 1-4 pieces of random resources.
            setResourceOnTile( tile, Resource::Rand( false ), Rand::Get( 1, 4 ) );
            break;

        case MP2::OBJ_FLOTSAM: {
            assert( isFirstLoad );

            switch ( Rand::Get( 1, 4 ) ) {
            // 25%: 500 gold + 10 wood
            case 1:
                tile.metadata()[0] = 10;
                tile.metadata()[1] = 500;
                break;
            // 25%: 200 gold + 5 wood
            case 2:
                tile.metadata()[0] = 5;
                tile.metadata()[1] = 200;
                break;
            // 25%: 5 wood
            case 3:
                tile.metadata()[0] = 5;
                break;
            // 25%: empty
            default:
                break;
            }
            break;
        }

        case MP2::OBJ_SHIPWRECK_SURVIVOR: {
            assert( isFirstLoad );

            Rand::Queue percents( 3 );
            // 55%: artifact 1
            percents.Push( 1, 55 );
            // 30%: artifact 2
            percents.Push( 2, 30 );
            // 15%: artifact 3
            percents.Push( 3, 15 );

            switch ( percents.Get() ) {
            case 1:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_TREASURE );
                break;
            case 2:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_MINOR );
                break;
            case 3:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_MAJOR );
                break;
            default:
                // Check your logic above!
                assert( 0 );
                tile.metadata()[0] = Artifact::UNKNOWN;
                break;
            }
            break;
        }

        case MP2::OBJ_SEA_CHEST: {
            assert( isFirstLoad );

            Rand::Queue percents( 3 );
            // 20% - empty
            percents.Push( 0, 20 );
            // 70% - 1500 gold
            percents.Push( 1, 70 );
            // 10% - 1000 gold + art
            percents.Push( 2, 10 );

            switch ( percents.Get() ) {
            case 0:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 0;
                break;
            case 1:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 1500;
                break;
            case 2:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_TREASURE );
                tile.metadata()[1] = 1000;
                break;
            default:
                // Check your logic above!
                assert( 0 );

                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 0;
                break;
            }
            break;
        }

        case MP2::OBJ_TREASURE_CHEST:
            assert( isFirstLoad );

            if ( tile.isWater() ) {
                // On original map "Alteris 2" there is a treasure chest placed on the water and there might be other maps with such bug.
                // If there is a bug then remove of the MP2::OBJ_TREASURE_CHEST will return 'true' and we can replace it with a Sea Chest object.
                if ( removeObjectFromTileByType( tile, MP2::OBJ_TREASURE_CHEST ) ) {
                    const auto & objects = Maps::getObjectsByGroup( Maps::ObjectGroup::ADVENTURE_WATER );

                    for ( size_t i = 0; i < objects.size(); ++i ) {
                        if ( objects[i].objectType == MP2::OBJ_SEA_CHEST ) {
                            const auto & objectInfo = Maps::getObjectInfo( Maps::ObjectGroup::ADVENTURE_WATER, static_cast<int32_t>( i ) );
                            setObjectOnTile( tile, objectInfo, true );

                            break;
                        }
                    }
                }
                else {
                    tile.setMainObjectType( MP2::OBJ_SEA_CHEST );
                }

                updateObjectInfoTile( tile, isFirstLoad );
                return;
            }

            {
                Rand::Queue percents( 4 );
                // 31% - 2000 gold or 1500 exp
                percents.Push( 1, 31 );
                // 32% - 1500 gold or 1000 exp
                percents.Push( 2, 32 );
                // 32% - 1000 gold or 500 exp
                percents.Push( 3, 32 );
                // 5% - art
                percents.Push( 4, 5 );

                switch ( percents.Get() ) {
                case 1:
                    tile.metadata()[0] = Artifact::UNKNOWN;
                    tile.metadata()[1] = 2000;
                    break;
                case 2:
                    tile.metadata()[0] = Artifact::UNKNOWN;
                    tile.metadata()[1] = 1500;
                    break;
                case 3:
                    tile.metadata()[0] = Artifact::UNKNOWN;
                    tile.metadata()[1] = 1000;
                    break;
                case 4:
                    tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_TREASURE );
                    tile.metadata()[1] = 0;
                    break;
                default:
                    // Check your logic above!
                    tile.metadata()[0] = Artifact::UNKNOWN;
                    tile.metadata()[1] = 0;
                    assert( 0 );
                    break;
                }
            }
            break;

        case MP2::OBJ_DERELICT_SHIP:
            assert( isFirstLoad );

            setResourceOnTile( tile, Resource::GOLD, 5000 );
            break;

        case MP2::OBJ_SHIPWRECK: {
            assert( isFirstLoad );

            Rand::Queue percents( 4 );
            // 40% - 10ghost(1000g)
            percents.Push( 1, 40 );
            // 30% - 15 ghost(2000g)
            percents.Push( 2, 30 );
            // 20% - 25ghost(5000g)
            percents.Push( 3, 20 );
            // 10% - 50ghost(2000g+art)
            percents.Push( 4, 10 );

            tile.metadata()[2] = percents.Get();

            switch ( static_cast<ShipwreckCaptureCondition>( tile.metadata()[2] ) ) {
            case ShipwreckCaptureCondition::FIGHT_10_GHOSTS_AND_GET_1000_GOLD:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 1000;
                break;
            case ShipwreckCaptureCondition::FIGHT_15_GHOSTS_AND_GET_2000_GOLD:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 2000;
                break;
            case ShipwreckCaptureCondition::FIGHT_25_GHOSTS_AND_GET_5000_GOLD:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 5000;
                break;
            case ShipwreckCaptureCondition::FIGHT_50_GHOSTS_AND_GET_2000_GOLD_WITH_ARTIFACT:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_ALL_NORMAL );
                tile.metadata()[1] = 2000;
                break;
            default:
                // Check your logic above!
                assert( 0 );
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 0;
                break;
            }
            break;
        }

        case MP2::OBJ_GRAVEYARD:
            assert( isFirstLoad );

            tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_ALL_NORMAL );
            tile.metadata()[1] = 1000;
            break;

        case MP2::OBJ_PYRAMID: {
            assert( isFirstLoad );

            static_assert( Spell::NONE == 0, "You are breaking the logic by changing the Spell::NONE value!" );
            if ( tile.metadata()[0] != Spell::NONE ) {
                // The spell has been set externally.
                break;
            }

            // Random spell of level 5.
            setSpellOnTile( tile, Spell::getRandomSpell( 5 ).GetID() );
            break;
        }

        case MP2::OBJ_DAEMON_CAVE: {
            assert( isFirstLoad );

            // 1000 exp or 1000 exp + 2500 gold or 1000 exp + art or (-2500 or remove hero)
            tile.metadata()[2] = Rand::Get( 1, 4 );
            switch ( static_cast<DaemonCaveCaptureBonus>( tile.metadata()[2] ) ) {
            case DaemonCaveCaptureBonus::GET_1000_EXPERIENCE:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 0;
                break;
            case DaemonCaveCaptureBonus::GET_1000_EXPERIENCE_AND_2500_GOLD:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 2500;
                break;
            case DaemonCaveCaptureBonus::GET_1000_EXPERIENCE_AND_ARTIFACT:
                tile.metadata()[0] = Artifact::Rand( Artifact::ART_LEVEL_ALL_NORMAL );
                tile.metadata()[1] = 0;
                break;
            case DaemonCaveCaptureBonus::PAY_2500_GOLD:
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 2500;
                break;
            default:
                // Check your logic above!
                assert( 0 );
                tile.metadata()[0] = Artifact::UNKNOWN;
                tile.metadata()[1] = 0;
                break;
            }
            break;
        }

        case MP2::OBJ_TREE_OF_KNOWLEDGE:
            assert( isFirstLoad );

            // variant: 10 gems, 2000 gold or free
            switch ( Rand::Get( 1, 3 ) ) {
            case 1:
                setResourceOnTile( tile, Resource::GEMS, 10 );
                break;
            case 2:
                setResourceOnTile( tile, Resource::GOLD, 2000 );
                break;
            default:
                break;
            }
            break;

        case MP2::OBJ_BARRIER:
            assert( isFirstLoad );

            setBarrierColorOnTile( tile, getColorFromBarrierSprite( tile.getMainObjectPart().icnType, tile.getMainObjectPart().icnIndex ) );
            break;

        case MP2::OBJ_TRAVELLER_TENT:
            assert( isFirstLoad );

            setBarrierColorOnTile( tile, getColorFromTravellerTentSprite( tile.getMainObjectPart().icnType, tile.getMainObjectPart().icnIndex ) );
            break;

        case MP2::OBJ_ALCHEMIST_LAB: {
            assert( isFirstLoad );

            const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::MERCURY ).mercury );
            assert( resourceCount.has_value() && resourceCount > 0U );

            setResourceOnTile( tile, Resource::MERCURY, resourceCount.value() );
            break;
        }

        case MP2::OBJ_SAWMILL: {
            assert( isFirstLoad );

            const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::WOOD ).wood );
            assert( resourceCount.has_value() && resourceCount > 0U );

            setResourceOnTile( tile, Resource::WOOD, resourceCount.value() );
            break;
        }

        case MP2::OBJ_MINE: {
            assert( isFirstLoad );

            // Mines must have an object part of OBJ_ICN_TYPE_EXTRAOVR type on top.
            // However, some mines do not follow this rule so we need to fix it!
            if ( tile.getMainObjectPart().icnType != MP2::OBJ_ICN_TYPE_EXTRAOVR ) {
                for ( auto & part : tile.getGroundObjectParts() ) {
                    if ( part.icnType == MP2::OBJ_ICN_TYPE_EXTRAOVR && part._uid == tile.getMainObjectPart()._uid ) {
                        // We found the missing object part. Swap it.
                        std::swap( tile.getMainObjectPart(), part );
                        break;
                    }
                }
            }

            if ( tile.getMainObjectPart().icnType != MP2::OBJ_ICN_TYPE_EXTRAOVR ) {
                // This is an unknown mine type. Most likely it was added by some hex editing.
                tile.setMainObjectType( MP2::OBJ_NONE );
                break;
            }

            switch ( tile.getMainObjectPart().icnIndex ) {
            case 0: {
                const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::ORE ).ore );
                assert( resourceCount.has_value() && resourceCount > 0U );

                setResourceOnTile( tile, Resource::ORE, resourceCount.value() );
                break;
            }
            case 1: {
                const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::SULFUR ).sulfur );
                assert( resourceCount.has_value() && resourceCount > 0U );

                setResourceOnTile( tile, Resource::SULFUR, resourceCount.value() );
                break;
            }
            case 2: {
                const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::CRYSTAL ).crystal );
                assert( resourceCount.has_value() && resourceCount > 0U );

                setResourceOnTile( tile, Resource::CRYSTAL, resourceCount.value() );
                break;
            }
            case 3: {
                const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::GEMS ).gems );
                assert( resourceCount.has_value() && resourceCount > 0U );

                setResourceOnTile( tile, Resource::GEMS, resourceCount.value() );
                break;
            }
            case 4: {
                const auto resourceCount = fheroes2::checkedCast<uint32_t>( ProfitConditions::FromMine( Resource::GOLD ).gold );
                assert( resourceCount.has_value() && resourceCount > 0U );

                setResourceOnTile( tile, Resource::GOLD, resourceCount.value() );
                break;
            }
            default:
                // This is an unknown mine type. Most likely it was added by some hex editing.
                tile.setMainObjectType( MP2::OBJ_NONE );
                break;
            }
            break;
        }

        case MP2::OBJ_ABANDONED_MINE:
            // The number of Ghosts is set only when loading the map and does not change anymore.
            if ( isFirstLoad ) {
                setMonsterCountOnTile( tile, Rand::Get( 30, 60 ) );
            }
            break;

        case MP2::OBJ_BOAT:
            assert( isFirstLoad );

            // This is a special case. Boats are different in the original editor.
            tile.getMainObjectPart().icnType = MP2::OBJ_ICN_TYPE_BOAT32;
            tile.getMainObjectPart().icnIndex = 18;
            break;

        case MP2::OBJ_RANDOM_ARTIFACT:
        case MP2::OBJ_RANDOM_ARTIFACT_TREASURE:
        case MP2::OBJ_RANDOM_ARTIFACT_MINOR:
        case MP2::OBJ_RANDOM_ARTIFACT_MAJOR:
            assert( isFirstLoad );

            updateRandomArtifact( tile );
            updateObjectInfoTile( tile, isFirstLoad );
            return;

        case MP2::OBJ_RANDOM_RESOURCE:
            assert( isFirstLoad );

            updateRandomResource( tile );
            updateObjectInfoTile( tile, isFirstLoad );
            return;

        case MP2::OBJ_MONSTER:
            if ( world.CountWeek() > 1 )
                updateMonsterPopulationOnTile( tile );
            else
                updateMonsterInfoOnTile( tile );
            break;

        case MP2::OBJ_RANDOM_MONSTER:
        case MP2::OBJ_RANDOM_MONSTER_WEAK:
        case MP2::OBJ_RANDOM_MONSTER_MEDIUM:
        case MP2::OBJ_RANDOM_MONSTER_STRONG:
        case MP2::OBJ_RANDOM_MONSTER_VERY_STRONG:
            assert( isFirstLoad );

            updateRandomMonster( tile );
            updateObjectInfoTile( tile, isFirstLoad );
            return;

        case MP2::OBJ_GENIE_LAMP:
            // The number of Genies is set when loading the map and does not change anymore
            if ( isFirstLoad ) {
                setMonsterCountOnTile( tile, Rand::Get( 2, 4 ) );
            }
            break;

        case MP2::OBJ_AIR_ALTAR:
        case MP2::OBJ_ARCHER_HOUSE:
        case MP2::OBJ_BARROW_MOUNDS:
        case MP2::OBJ_CAVE:
        case MP2::OBJ_CITY_OF_DEAD:
        case MP2::OBJ_DESERT_TENT:
        case MP2::OBJ_DRAGON_CITY:
        case MP2::OBJ_DWARF_COTTAGE:
        case MP2::OBJ_EARTH_ALTAR:
        case MP2::OBJ_EXCAVATION:
        case MP2::OBJ_FIRE_ALTAR:
        case MP2::OBJ_GOBLIN_HUT:
        case MP2::OBJ_HALFLING_HOLE:
        case MP2::OBJ_PEASANT_HUT:
        case MP2::OBJ_RUINS:
        case MP2::OBJ_TREE_CITY:
        case MP2::OBJ_TREE_HOUSE:
        case MP2::OBJ_TROLL_BRIDGE:
        case MP2::OBJ_WAGON_CAMP:
        case MP2::OBJ_WATCH_TOWER:
        case MP2::OBJ_WATER_ALTAR:
            updateDwellingPopulationOnTile( tile, isFirstLoad );
            break;

        case MP2::OBJ_EVENT:
            assert( isFirstLoad );
            // Event should be invisible on Adventure Map.
            tile.resetMainObjectPart();
            resetObjectMetadata( tile );
            break;

        default:
            if ( isFirstLoad ) {
                resetObjectMetadata( tile );
            }
            break;
        }
    }

    void updateMonsterInfoOnTile( Tile & tile )
    {
        const Monster mons = Monster( tile.getMainObjectPart().icnIndex + 1 ); // ICN::MONS32 start from PEASANT
        setMonsterOnTile( tile, mons, tile.metadata()[0] );
    }

    void setMonsterOnTile( Tile & tile, const Monster & mons, const uint32_t count )
    {
        tile.setMainObjectType( MP2::OBJ_MONSTER );

        Maps::ObjectPart & mainObjectPart = tile.getMainObjectPart();

        if ( mainObjectPart.icnType != MP2::OBJ_ICN_TYPE_MONS32 ) {
            if ( mainObjectPart.icnType != MP2::OBJ_ICN_TYPE_UNKNOWN ) {
                // If there is another object sprite here (shadow for example) push it down to add-ons.
                tile.pushGroundObjectPart( mainObjectPart );
            }

            // Set unique UID for placed monster.
            mainObjectPart._uid = getNewObjectUID();
            mainObjectPart.icnType = MP2::OBJ_ICN_TYPE_MONS32;
            mainObjectPart.layerType = OBJECT_LAYER;
        }

        using IcnIndexType = decltype( mainObjectPart.icnIndex );
        static_assert( std::is_same_v<IcnIndexType, uint8_t> );

        const uint32_t monsSpriteIndex = mons.GetSpriteIndex();
        assert( monsSpriteIndex >= std::numeric_limits<IcnIndexType>::min() && monsSpriteIndex <= std::numeric_limits<IcnIndexType>::max() );

        mainObjectPart.icnIndex = static_cast<IcnIndexType>( monsSpriteIndex );

        const bool setDefinedCount = ( count > 0 );

        if ( setDefinedCount ) {
            setMonsterCountOnTile( tile, count );
        }
        else {
            setMonsterCountOnTile( tile, mons.GetRNDSize() );
        }

        if ( mons.GetID() == Monster::GHOST || mons.isElemental() ) {
            // Ghosts and elementals never join hero's army.
            setMonsterOnTileJoinCondition( tile, Monster::JOIN_CONDITION_SKIP );
        }
        else if ( setDefinedCount || ( world.GetWeekType().GetType() == WeekName::MONSTERS && world.GetWeekType().GetMonster() == mons.GetID() ) ) {
            // Wandering monsters with the number of units specified by the map designer are always considered as "hostile" and always join only for money.

            // Monsters will be willing to join for some amount of money.
            setMonsterOnTileJoinCondition( tile, Monster::JOIN_CONDITION_MONEY );
        }
        else {
            // 20% chance for join
            if ( 3 > Rand::Get( 1, 10 ) ) {
                setMonsterOnTileJoinCondition( tile, Monster::JOIN_CONDITION_FREE );
            }
            else {
                setMonsterOnTileJoinCondition( tile, Monster::JOIN_CONDITION_MONEY );
            }
        }
    }

    std::pair<PlayerColor, int> getColorRaceFromHeroSprite( const uint32_t heroSpriteIndex )
    {
        std::pair<PlayerColor, int> res;

        if ( 7 > heroSpriteIndex ) {
            res.first = PlayerColor::BLUE;
        }
        else if ( 14 > heroSpriteIndex ) {
            res.first = PlayerColor::GREEN;
        }
        else if ( 21 > heroSpriteIndex ) {
            res.first = PlayerColor::RED;
        }
        else if ( 28 > heroSpriteIndex ) {
            res.first = PlayerColor::YELLOW;
        }
        else if ( 35 > heroSpriteIndex ) {
            res.first = PlayerColor::ORANGE;
        }
        else {
            res.first = PlayerColor::PURPLE;
        }

        switch ( heroSpriteIndex % 7 ) {
        case 0:
            res.second = Race::KNGT;
            break;
        case 1:
            res.second = Race::BARB;
            break;
        case 2:
            res.second = Race::SORC;
            break;
        case 3:
            res.second = Race::WRLK;
            break;
        case 4:
            res.second = Race::WZRD;
            break;
        case 5:
            res.second = Race::NECR;
            break;
        case 6:
            res.second = Race::RAND;
            break;
        default:
            assert( 0 );
            break;
        }

        return res;
    }

    bool isCaptureObjectProtected( const Tile & tile )
    {
        const MP2::MapObjectType objectType = tile.getMainObjectType( false );

        if ( !MP2::isCaptureObject( objectType ) ) {
            return false;
        }

        if ( MP2::OBJ_CASTLE == objectType ) {
            Castle * castle = world.getCastleEntrance( tile.GetCenter() );
            assert( castle != nullptr );

            if ( castle ) {
                return castle->GetArmy().isValid();
            }

            return false;
        }

        return getTroopFromTile( tile ).isValid();
    }

    void restoreAbandonedMine( Tile & tile, const int resource )
    {
        assert( tile.getMainObjectType( false ) == MP2::OBJ_ABANDONED_MINE );
        assert( tile.getMainObjectPart()._uid != 0 );

        const Funds info = ProfitConditions::FromMine( resource );
        std::optional<uint32_t> resourceCount;

        switch ( resource ) {
        case Resource::ORE:
            resourceCount = fheroes2::checkedCast<uint32_t>( info.ore );
            break;
        case Resource::SULFUR:
            resourceCount = fheroes2::checkedCast<uint32_t>( info.sulfur );
            break;
        case Resource::CRYSTAL:
            resourceCount = fheroes2::checkedCast<uint32_t>( info.crystal );
            break;
        case Resource::GEMS:
            resourceCount = fheroes2::checkedCast<uint32_t>( info.gems );
            break;
        case Resource::GOLD:
            resourceCount = fheroes2::checkedCast<uint32_t>( info.gold );
            break;
        default:
            assert( 0 );
            break;
        }

        assert( resourceCount.has_value() && resourceCount > 0U );

        setResourceOnTile( tile, resource, resourceCount.value() );

        const auto restoreLeftSprite = [resource]( MP2::ObjectIcnType & objectIcnType, uint8_t & imageIndex ) {
            if ( MP2::OBJ_ICN_TYPE_OBJNGRAS == objectIcnType && imageIndex == 6 ) {
                objectIcnType = MP2::OBJ_ICN_TYPE_MTNGRAS;
                imageIndex = 82;
            }
            else if ( MP2::OBJ_ICN_TYPE_OBJNDIRT == objectIcnType && imageIndex == 8 ) {
                objectIcnType = MP2::OBJ_ICN_TYPE_MTNDIRT;
                imageIndex = 112;
            }
            else if ( MP2::OBJ_ICN_TYPE_EXTRAOVR == objectIcnType && imageIndex == 5 ) {
                switch ( resource ) {
                case Resource::ORE:
                    imageIndex = 0;
                    break;
                case Resource::SULFUR:
                    imageIndex = 1;
                    break;
                case Resource::CRYSTAL:
                    imageIndex = 2;
                    break;
                case Resource::GEMS:
                    imageIndex = 3;
                    break;
                case Resource::GOLD:
                    imageIndex = 4;
                    break;
                default:
                    break;
                }
            }
        };

        const auto restoreRightSprite = []( MP2::ObjectIcnType & objectIcnType, uint8_t & imageIndex ) {
            if ( MP2::OBJ_ICN_TYPE_OBJNDIRT == objectIcnType && imageIndex == 9 ) {
                objectIcnType = MP2::OBJ_ICN_TYPE_MTNDIRT;
                imageIndex = 113;
            }
            else if ( MP2::OBJ_ICN_TYPE_OBJNGRAS == objectIcnType && imageIndex == 7 ) {
                objectIcnType = MP2::OBJ_ICN_TYPE_MTNGRAS;
                imageIndex = 83;
            }
        };

        const auto restoreMineObjectType = [&tile]( int directionVector ) {
            if ( Maps::isValidDirection( tile.GetIndex(), directionVector ) ) {
                Tile & mineTile = world.getTile( Maps::GetDirectionIndex( tile.GetIndex(), directionVector ) );
                if ( ( mineTile.getMainObjectType() == MP2::OBJ_NON_ACTION_ABANDONED_MINE )
                     && ( mineTile.getMainObjectPart()._uid == tile.getMainObjectPart()._uid || mineTile.getGroundObjectPart( tile.getMainObjectPart()._uid )
                          || mineTile.getTopObjectPart( tile.getMainObjectPart()._uid ) ) ) {
                    mineTile.setMainObjectType( MP2::OBJ_NON_ACTION_MINE );
                }
            }
        };

        MP2::ObjectIcnType objectIcnTypeTemp{ tile.getMainObjectPart().icnType };
        uint8_t imageIndexTemp{ tile.getMainObjectPart().icnIndex };

        restoreLeftSprite( objectIcnTypeTemp, imageIndexTemp );
        tile.getMainObjectPart().icnType = objectIcnTypeTemp;
        tile.getMainObjectPart().icnIndex = imageIndexTemp;

        for ( auto & part : tile.getGroundObjectParts() ) {
            if ( part._uid == tile.getMainObjectPart()._uid ) {
                restoreLeftSprite( part.icnType, part.icnIndex );
            }
        }

        if ( Maps::isValidDirection( tile.GetIndex(), Direction::RIGHT ) ) {
            Tile & rightTile = world.getTile( Maps::GetDirectionIndex( tile.GetIndex(), Direction::RIGHT ) );

            if ( rightTile.getMainObjectPart()._uid == tile.getMainObjectPart()._uid ) {
                objectIcnTypeTemp = rightTile.getMainObjectPart().icnType;
                imageIndexTemp = rightTile.getMainObjectPart().icnIndex;
                restoreRightSprite( objectIcnTypeTemp, imageIndexTemp );

                rightTile.getMainObjectPart().icnType = objectIcnTypeTemp;
                rightTile.getMainObjectPart().icnIndex = imageIndexTemp;
            }

            ObjectPart * part = rightTile.getGroundObjectPart( tile.getMainObjectPart()._uid );

            if ( part ) {
                restoreRightSprite( part->icnType, part->icnIndex );
            }
        }

        restoreMineObjectType( Direction::LEFT );
        restoreMineObjectType( Direction::RIGHT );
        restoreMineObjectType( Direction::TOP );
        restoreMineObjectType( Direction::TOP_LEFT );
        restoreMineObjectType( Direction::TOP_RIGHT );
    }

    void removeMainObjectFromTile( const Tile & tile )
    {
        removeObjectFromTileByType( tile, tile.getMainObjectType() );
    }

    bool removeObjectFromTileByType( const Tile & tile, const MP2::MapObjectType objectType )
    {
        assert( objectType != MP2::OBJ_NONE );

        // Verify that this tile indeed contains an object with given object type.
        uint32_t objectUID = 0;

        if ( Maps::getObjectTypeByIcn( tile.getMainObjectPart().icnType, tile.getMainObjectPart().icnIndex ) == objectType ) {
            objectUID = tile.getMainObjectPart()._uid;
        }

        if ( objectUID == 0 ) {
            for ( auto iter = tile.getTopObjectParts().rbegin(); iter != tile.getTopObjectParts().rend(); ++iter ) {
                if ( Maps::getObjectTypeByIcn( iter->icnType, iter->icnIndex ) == objectType ) {
                    objectUID = iter->_uid;
                    break;
                }
            }
        }

        if ( objectUID == 0 ) {
            for ( auto iter = tile.getGroundObjectParts().rbegin(); iter != tile.getGroundObjectParts().rend(); ++iter ) {
                if ( Maps::getObjectTypeByIcn( iter->icnType, iter->icnIndex ) == objectType ) {
                    objectUID = iter->_uid;
                    break;
                }
            }
        }

        if ( objectUID == 0 ) {
            return false;
        }

        return removeObjectFromMapByUID( tile.GetIndex(), objectUID );
    }

    bool removeObjectFromMapByUID( const int32_t startTileIndex, const uint32_t objectUID )
    {
        assert( startTileIndex >= 0 && startTileIndex < world.w() * world.h() );

        assert( objectUID > 0 );

        std::vector<int32_t> tiles;
        tiles.push_back( startTileIndex );

        std::set<int32_t> processedTileIndicies;

        for ( size_t currentId = 0; currentId < tiles.size(); ++currentId ) {
            if ( processedTileIndicies.count( tiles[currentId] ) == 1 ) {
                // This tile is already processed, skip it.
                continue;
            }

            if ( world.getTile( tiles[currentId] ).removeObjectPartsByUID( objectUID ) ) {
                // This tile has the object. Get neighboring tiles to see if they have the same.
                const Maps::Indexes tileIndices = Maps::getAroundIndexes( tiles[currentId], 1 );
                for ( const int32_t tileIndex : tileIndices ) {
                    if ( tileIndex < 0 ) {
                        // Invalid tile index.
                        continue;
                    }

                    if ( processedTileIndicies.count( tileIndex ) == 0 ) {
                        tiles.push_back( tileIndex );
                    }
                }
            }

            processedTileIndicies.emplace( tiles[currentId] );
        }

        return !processedTileIndicies.empty();
    }

    bool isClearGround( const Tile & tile )
    {
        const MP2::MapObjectType objectType = tile.getMainObjectType( true );

        switch ( objectType ) {
        case MP2::OBJ_NONE:
        case MP2::OBJ_COAST:
            return true;
        case MP2::OBJ_BOAT:
            return false;

        default:
            break;
        }

        if ( ( tile.getMainObjectPart().icnType == MP2::OBJ_ICN_TYPE_UNKNOWN ) || ( tile.getMainObjectPart().layerType == Maps::SHADOW_LAYER )
             || ( tile.getMainObjectPart().layerType == Maps::TERRAIN_LAYER ) ) {
            return !MP2::isInGameActionObject( objectType, tile.isWater() );
        }

        return false;
    }

    void updateFogDirectionsInArea( const fheroes2::Point & minPos, const fheroes2::Point & maxPos, const PlayerColorsSet colors )
    {
        assert( ( minPos.x <= maxPos.x ) && ( minPos.y <= maxPos.y ) );

        const int32_t worldWidth = world.w();
        const int32_t worldHeight = world.w();

        // Do not get over the world borders.
        const int32_t minX = std::max( minPos.x, 0 );
        const int32_t minY = std::max( minPos.y, 0 );
        // Add extra 1 to reach the given maxPos point.
        const int32_t maxX = std::min( maxPos.x + 1, worldWidth );
        const int32_t maxY = std::min( maxPos.y + 1, worldHeight );

        // Fog data range is 1 tile bigger from each side as for the fog directions we have to check all tiles around each tile in the area.
        const int32_t fogMinX = std::max( minX - 1, 0 );
        const int32_t fogMinY = std::max( minY - 1, 0 );
        const int32_t fogMaxX = std::min( maxX + 1, worldWidth );
        const int32_t fogMaxY = std::min( maxY + 1, worldHeight );

        const int32_t fogDataWidth = maxX - minX + 2;
        const int32_t fogDataSize = fogDataWidth * ( maxY - minY + 2 );

        // A vector to cache 'isFog()' data. This vector type is not <bool> as using std::vector<uint8_t> gives the higher performance.
        // 1 is for the 'true' state, 0 is for the 'false' state.
        std::vector<uint8_t> fogData( fogDataSize, 1 );

        // Set the 'fogData' index offset from the tile index.
        const int32_t fogDataOffset = 1 - minX + ( 1 - minY ) * fogDataWidth;

        // Cache the 'fogData' data for the given area to use it in fog direction calculation.
        // The loops run only within the world area, if 'fogData' area includes tiles outside the world borders we do not update them as the are already set to 1.
        for ( int32_t y = fogMinY; y < fogMaxY; ++y ) {
            const int32_t fogTileOffsetY = y * worldWidth;
            const int32_t fogDataOffsetY = y * fogDataWidth + fogDataOffset;

            for ( int32_t x = fogMinX; x < fogMaxX; ++x ) {
                fogData[x + fogDataOffsetY] = world.getTile( x + fogTileOffsetY ).isFog( colors ) ? 1 : 0;
            }
        }

        // Set the 'fogData' index offset from the tile index for the TOP LEFT direction from the tile.
        const int32_t topLeftDirectionOffset = -1 - fogDataWidth;

#ifndef NDEBUG
        // Cache the maximum border for fogDataIndex corresponding the "CENTER" tile to use in assertion. Should be removed if assertion is removed.
        const int32_t centerfogDataIndexLimit = fogDataSize + topLeftDirectionOffset;
#endif

        // Calculate fog directions using the cached 'isFog' data.
        for ( int32_t y = minY; y < maxY; ++y ) {
            const int32_t fogCenterDataOffsetY = y * fogDataWidth + fogDataOffset;

            for ( int32_t x = minX; x < maxX; ++x ) {
                Maps::Tile & tile = world.getTile( x, y );

                int32_t fogDataIndex = x + fogCenterDataOffsetY;

                if ( fogData[fogDataIndex] == 0 ) {
                    // For the tile is without fog we set the UNKNOWN direction.
                    tile.setFogDirection( Direction::UNKNOWN );
                }
                else {
                    // The tile is under the fog so its CENTER direction for fog is true.
                    uint16_t fogDirection = Direction::CENTER;

                    // 'fogDataIndex' should not get out of maximum 'fogData' vector index after all increments.
                    assert( fogDataIndex < centerfogDataIndexLimit );

                    // Check all tiles around for 'fogData' starting from the top left direction and if it is true then logically add the direction to 'fogDirection'.
                    fogDataIndex += topLeftDirectionOffset;

                    assert( fogDataIndex >= 0 );

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::TOP_LEFT;
                    }

                    ++fogDataIndex;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::TOP;
                    }

                    ++fogDataIndex;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::TOP_RIGHT;
                    }

                    // Set index to the left direction tile of the next fog data raw.
                    fogDataIndex += fogDataWidth - 2;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::LEFT;
                    }

                    // Skip the center tile as it was already checked.
                    fogDataIndex += 2;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::RIGHT;
                    }

                    fogDataIndex += fogDataWidth - 2;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::BOTTOM_LEFT;
                    }

                    ++fogDataIndex;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::BOTTOM;
                    }

                    ++fogDataIndex;

                    if ( fogData[fogDataIndex] == 1 ) {
                        fogDirection |= Direction::BOTTOM_RIGHT;
                    }

                    tile.setFogDirection( fogDirection );
                }
            }
        }
    }

    bool setObjectOnTile( Tile & tile, const ObjectInfo & info, const bool updateMapPassabilities )
    {
        assert( !info.empty() );

        switch ( info.objectType ) {
        case MP2::OBJ_MONSTER:
            setMonsterOnTile( tile, static_cast<int32_t>( info.metadata[0] ), 0 );
            // Since setMonsterOnTile() function interprets 0 as a random number of monsters it is important to set the correct value.
            setMonsterCountOnTile( tile, 0 );
            return true;
        case MP2::OBJ_ARTIFACT:
            if ( !placeObjectOnTile( tile, info ) ) {
                return false;
            }
            // The artifact ID is stored in metadata[0]. It is used by the other engine functions.
            tile.metadata()[0] = info.metadata[0];
            return true;
        case MP2::OBJ_ALCHEMIST_LAB:
        case MP2::OBJ_MINE:
        case MP2::OBJ_SAWMILL:
            if ( !placeObjectOnTile( tile, info ) ) {
                return false;
            }
            // Set resource type and income per day.
            tile.metadata()[0] = info.metadata[0];
            tile.metadata()[1] = info.metadata[1];

            if ( updateMapPassabilities ) {
                world.updatePassabilities();
            }
            return true;
        case MP2::OBJ_CASTLE:
        case MP2::OBJ_RANDOM_CASTLE:
        case MP2::OBJ_RANDOM_TOWN:
            if ( !placeObjectOnTile( tile, info ) ) {
                return false;
            }

            if ( updateMapPassabilities ) {
                world.updatePassabilities();
            }
            return true;
        case MP2::OBJ_MAGIC_GARDEN:
            // Magic Garden uses the metadata to indicate whether it is empty or not.
            // The Editor does not allow to edit this object but it is important to show it non-empty.
            if ( !placeObjectOnTile( tile, info ) ) {
                return false;
            }

            // These values will be replaced during map loading.
            tile.metadata()[0] = 1;
            tile.metadata()[1] = 1;

            if ( updateMapPassabilities ) {
                world.updatePassabilities();
            }
            return true;
        default:
            break;
        }

        if ( !placeObjectOnTile( tile, info ) ) {
            return false;
        }

        if ( updateMapPassabilities ) {
            world.updatePassabilities();
        }

        return true;
    }

    std::set<uint32_t> getObjectUidsInArea( const int32_t startTileId, const int32_t endTileId )
    {
        const int32_t mapWidth = world.w();
        const int32_t maxTileId = mapWidth * world.h() - 1;

        if ( startTileId < 0 || startTileId > maxTileId || endTileId < 0 || endTileId > maxTileId ) {
            // Why are you trying to get object UID outside of the map? Check the logic of the caller function.
            assert( 0 );
            return {};
        }

        const fheroes2::Point startTileOffset = GetPoint( startTileId );
        const fheroes2::Point endTileOffset = GetPoint( endTileId );

        const int32_t startX = std::min( startTileOffset.x, endTileOffset.x );
        const int32_t startY = std::min( startTileOffset.y, endTileOffset.y );
        const int32_t endX = std::max( startTileOffset.x, endTileOffset.x );
        const int32_t endY = std::max( startTileOffset.y, endTileOffset.y );

        std::set<uint32_t> objectsUids;

        for ( int32_t y = startY; y <= endY; ++y ) {
            const int32_t tileOffset = y * mapWidth;
            for ( int32_t x = startX; x <= endX; ++x ) {
                const Maps::Tile & currentTile = world.getTile( x + tileOffset );

                if ( currentTile.getMainObjectPart()._uid != 0 && ( currentTile.getMainObjectPart().layerType != SHADOW_LAYER ) ) {
                    objectsUids.insert( currentTile.getMainObjectPart()._uid );
                }

                for ( const auto & part : currentTile.getGroundObjectParts() ) {
                    if ( part._uid != 0 && ( part.layerType != SHADOW_LAYER ) && part.icnType != MP2::OBJ_ICN_TYPE_FLAG32 ) {
                        objectsUids.insert( part._uid );
                    }
                }

                // The top layer objects are not taken into account to correspond the original editor
                // and because they can be small or even zero-sized parts of the Main or Bottom layer
                // objects which can be selected by their non-top parts.
            }
        }

        return objectsUids;
    }
}
