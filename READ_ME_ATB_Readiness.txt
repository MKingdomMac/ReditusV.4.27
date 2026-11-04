Close Unreal Editor. Extract this archive into the Unreal project root (the folder
containing the .uproject). Replace the three files under Source/Chapter1Reditus/ATB.
Build Development Editor in Visual Studio, then reopen Unreal.

In BP_BattleController, select ReditusATB. Under Tuning > Clock, turn on
Allow Effect-Free Readiness During Actions. Keep Fill During Actions on.
Compile and save BP_BattleController. Keep the previous HUD files and wiring.

Test: while an enemy attacks, let an ally without a start-of-turn status reach
a full gauge. Its Phase should change to Ready and the command panel should open.
You can queue an ally action; it should execute only after the enemy action and
its cleanup complete. Units with regen, damage over time, sleep or stun still
use the normal Blueprint cycle-start path.

The opt-in setting skips On Cycle Start Requested only for the matching
effect-free path. Leave it off if your Blueprint adds mandatory work to that
path for units without those statuses.

The portable scheduler passed 23 scenarios / 183 assertions. Unreal Engine
compilation and playtesting must be done in your project.
