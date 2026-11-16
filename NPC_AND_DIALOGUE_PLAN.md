# NPC and dialogue responsibility review

## Recommendation

Keep the working actor plus component approach. Use the actor as a reusable NPC, with the existing BP_InteractionDetector parent supplying the project's interaction prompt and input route. A quest is optional: a row with just a flipbook and Fallback Dialogue already creates an ordinary talking NPC.

The current component coordinates NPC appearance, conversation selection, dialogue opening/closing and its NPC marker. It reads existing quest state through the bridge. It is not a second quest manager or save system. There is no need to rename existing classes or assets just to make the NPC more general.

## Boundaries for the next implementation

| Part | Responsibility |
| --- | --- |
| Placed NPC Blueprint | Position, sprite adjustments and existing interaction mechanism. |
| NPC component and definition row | NPC identity, flipbooks, dialogue selection rules and NPC quest marker settings. |
| Existing quest controller | Availability, progress, completion, rewards and saved quest state. Quest DataTables remain deferred. |
| Dialogue assets and events | Conversation branches and explicit story actions. |
| Dialogue presentation | Portraits, speaker names, text, choices, Auto, Skip and log controls. |
| Objective component | A destination marker at an NPC, location, chest or other actor; it reads quest progress. |

Existing gameplay that grants items, recruits robots or starts battles must continue through its existing events/controllers. A cosmetic UI change should not introduce another implementation of those rules.

## What the supplied widget export shows

The exported DemoDialogueWidget is more than a text box. Its connected graph handles negotiation points and recruitment outcomes, and resumes CurrentLevelSequence when dialogue closes in relevant states. It also contains variable-string substitution, sound handling, game-state transitions, controller input listeners, portrait selection and reply navigation.

Consequently, the next UI pass must preserve those paths while changing presentation. Replacing the widget with a generic text-and-choice player would lose project behavior. Any later extraction of negotiation or sequence coordination should be done separately, with its old and new behavior compared in the game.

The revised visual target in 1.1.2 is floating dialogue on a transparent background, fine gold ornaments, the existing portrait scale and centered translucent response choices with a teal highlight. The bottom Advance/Auto/Log/Skip strip appears only during Level Sequence dialogue. Auto and Skip stop at required choices and never execute an unselected branch. The existing widget remains the gameplay/dialogue controller. Full Level Sequence skipping and moving negotiation/sequence logic out of the old widget remain separate work.

## Correction made after reading the export

Removed has one RecruitmentEvents enum argument in the actual widget. The first NPC adapter expected no arguments. Version 1.0.2 accepts both known signatures, preserves other listeners and leaves the recruitment outcome with the existing widget/controller. It does not change the original widget graph.
