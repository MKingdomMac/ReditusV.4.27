REDITUS JRPG CONTROLLER FOUNDATION - PHASE 1
=============================================

WHAT THIS IS
------------
This is the safe replacement foundation for BP_JRPGPlayerController.

It uses the SAME migration philosophy as the new BattleController backend:

    existing BP_JRPGPlayerController
               |
               | keeps all existing public Blueprint events/variables
               v
    ReditusJRPGControllerBridgeComponent
               |
               v
    UReditusPlayerRosterSubsystem   (GameInstanceSubsystem)
               |
               v
          DT_PlayerUnits

DO NOT REPARENT BP_JRPGPlayerController YET.

WHY
---
The current Blueprint has many direct references to playerUnits and multiple
party-management events. Reparenting it right now creates risk with no useful
benefit.

The new backend is fully Blueprint-callable, so you can continue editing
BP_JRPGPlayerController normally while we migrate one function at a time.


FILES
-----
Copy the files from:

    Source/Chapter1Reditus/

into the real project:

    ReditusNewCoding/Source/Chapter1Reditus/

Files:

    Data/ReditusPlayerUnitDefinition.h
    ReditusPlayerRosterTypes.h
    ReditusPlayerRosterSubsystem.h
    ReditusPlayerRosterSubsystem.cpp
    ReditusJRPGControllerBridgeComponent.h
    ReditusJRPGControllerBridgeComponent.cpp


DEPENDENCY
----------
This expects the current file:

    Source/Chapter1Reditus/Data/ReditusUnitStats.h

with the CURRENT single:

    Attack
    MagicalAttack

stat fields.


IMPORTANT PLAYER ARCHITECTURE
-----------------------------
We are preserving THREE DIFFERENT concepts.

1. RECRUITED ROSTER

    RecruitedUnitIDs

    Every character the player has recruited.

2. ACTIVE PARTY MEMBERS

    PartyMemberIDs

    Stable active-party order.
    This DOES NOT change when the tactical formation is reordered.

3. PARTY FORMATION

    FormationUnitIDs

    Same active units, independently reorderable.

Example:

    Recruited:
        Augustus
        Caesar
        Macy
        Airloft
        Odin

    PartyMemberIDs:
        Augustus
        Caesar
        Macy
        Airloft

    FormationUnitIDs:
        Caesar
        Airloft
        Augustus
        Macy

Swapping formation members changes ONLY FormationUnitIDs.


DT_PLAYERUNITS
--------------
Create a DataTable from:

    FReditusPlayerUnitDefinition

The DataTable ROW NAME is the permanent PlayerUnitID.

Examples:

    Augustus
    Caesar
    Macy
    Airloft
    Odin

A row existing in DT_PlayerUnits DOES NOT mean the player owns the character.

The row defines what the character IS.

RecruitPlayerUnit("Macy") defines whether the player currently OWNS Macy.


LEGACY BLUEPRINT CLASSES
------------------------
Each row contains:

    LegacyBattleUnitClass

so the system can resolve:

    BP_MacyPlayerUnit
         ->
    PlayerUnitID "Macy"

and also:

    PlayerUnitID "Macy"
         ->
    BP_MacyPlayerUnit

This is how we keep current Blueprint references working during migration.


FIRST EDITOR STEP - AFTER C++ BUILDS
------------------------------------
1. Create DT_PlayerUnits using FReditusPlayerUnitDefinition.
2. Fill ONLY enough rows to test the existing starting characters first.
3. Open BP_JRPGPlayerController.
4. Add Component:
       Reditus JRPGController Bridge
5. Assign DT_PlayerUnits to the component.
6. Leave ALL existing controller logic untouched.

No behavior changes yet.


FIRST MIGRATION SEAM
--------------------
Before rewriting AddPlayerUnit, mirror the CURRENT controller state into the
backend.

Use:

    Import Legacy Controller Membership

Inputs:

    Legacy Recruited Classes
        = Keys from existing playerUnits map

    Legacy Party Member Classes
        = existing partyMembers array

    Legacy Formation Classes
        = existing Party Members Formation array

This does NOT modify the old arrays/maps.

It only gives the C++ backend an identical ID-based copy.


THEN MIGRATE THESE IN THIS ORDER
--------------------------------
1. AddPlayerUnit
2. AddToParty
3. RemoveFromParty
4. SwapFormationMembers
5. RemovePlayerUnit
6. SetPartyMembers

Do NOT start by replacing all 51 playerUnits reads.

The public Blueprint event signatures can stay exactly the same while their
implementation slowly forwards to the C++ backend.


ADDPLAYERUNIT MIGRATION
-----------------------
Existing callers currently pass:

    playerUnit class
    exp

Keep that signature.

Inside BP_JRPGPlayerController::AddPlayerUnit:

    playerUnit class
        |
        v
    Bridge -> Recruit Legacy Player Unit Class
        |
        +--> returns PlayerUnitID
        |
        v
    existing S_PlayerUnitData initialization remains FOR NOW

This means:
- new C++ backend knows the character is recruited
- old Blueprint state still carries EXP / HP / MP / equipment / statuses
- no callers break

Later we will migrate S_PlayerUnitData itself into native C++ runtime/save state.


WHY GAMEINSTANCE SUBSYSTEM
--------------------------
Roster and party state are not battle-map state.

They need to conceptually survive:
    exploration -> battle -> another map

So the backend is a UGameInstanceSubsystem rather than a UWorldSubsystem.


WHAT WE ARE NOT DOING YET
-------------------------
- no enemy spawning
- no BattleController changes
- no CalculateDamage migration
- no player battle-spawn migration
- no deletion of playerUnits
- no deletion of partyMembers
- no deletion of Party Members Formation
- no reparenting BP_JRPGPlayerController

First we stabilize player identity / recruitment / active party / formation.


NEXT PHASE
----------
After this compiles, the next task is to build DT_PlayerUnits from the actual
existing player-unit Blueprint data, then migrate AddPlayerUnit first.
