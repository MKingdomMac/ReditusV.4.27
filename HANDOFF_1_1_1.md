Historical 1.1.1 handoff. Current package: 1.1.2; see README_FIRST.md and VALIDATION.md for the sequence-lookup and layout corrections.

# Recovered working state

Continue from the combined quest/NPC/dialogue package, recovered from the frozen **Controller Wear Queue Logic** conversation. The prior delivered version was 1.1.0. The user confirmed that TestNPC interaction/dialogue worked with Fallback Dialogue set to AmbushDialogue1. This is user-reported working behavior, not a new engine test in this workspace.

The current package is 1.1.1. It integrates the requested transparent and more decorative dialogue presentation, preserves fixed source portrait bounds, centers translucent response choices, and limits the bottom control strip to Level Sequence dialogue. All quest, NPC and objective logic is carried forward unchanged. No additional pending quest-system change was specified in the recovered handoff.

Current responsibility split: NPC/component selects conversation and marker; the existing controller owns quest progress, rewards and saving; the existing DialoguePlugin widget owns dialogue, choices, recruitment and sequence-resume flow; the native presentation supplies the new view and its playback controls.

Next project check: cold Development Editor / Win64 build, then TestNPC, a response choice, sequence dialogue, recruitment and quest start/objective/turn-in/save-load. The offline bridge/binding/playback checks passed. Unreal compilation, rendering and gameplay have not been run here. Full-sequence skipping remains pending.
