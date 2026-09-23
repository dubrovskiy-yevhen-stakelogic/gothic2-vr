# Controls

Default controls for Quest Touch and compatible PCVR controllers. Gameplay buttons can be changed in **VR settings > Button mapping**. Menu confirmation and navigation use separate bindings.

## Basic actions

| Action | Control |
| --- | --- |
| Look | Head movement |
| Move | Left stick |
| Run | Press L3 to toggle running; press again to return to walking |
| Turn | Right stick; 30 degrees snap by default |
| Jump | A |
| Interact | B; look at the target, whose name appears on the HUD |
| Inventory | X |
| Journal | Y |
| Crouch action | Right stick button (R3) |
| Game menu | Hold both grips and press Y |
| VR settings | Press both stick buttons together (L3 + R3) |
| Menu selection | Left stick |
| Confirm in menus | A |
| Back in menus | B |

The shortcuts work even if Steam Link intercepts Menu. The original Menu button for the game menu and both grips + Menu for VR settings remain fallbacks where the runtime exposes them. The new shortcuts take priority over the normal Journal, Run and Crouch mappings; holding a shortcut does not repeat it. Release Y or both stick clicks and center both sticks after opening or closing a menu. You may keep holding grips to retain a weapon. In VR settings, left-stick up/down selects a row; horizontal stick movement never changes values. Left trigger decreases and right trigger increases numeric values; A confirms. On action rows, the right trigger also confirms. B returns to the parent menu or closes the root menu. Head tracking remains active while the world is paused.

**Locomotion > Run button** defaults to **Toggle**. Select **Hold** to restore running only while the mapped button is held. The preference is saved. Menus and loss of controller focus clear the running toggle.

## Swimming

Physical swimming uses the stroke and buoyancy model from the GTA San Andreas VR Quest port. Pull either hand backwards relative to your head to swim along your gaze; larger strokes give more thrust. Sweep your hands sideways or push down to tread water and rise. Look down and stroke to dive; look up and stroke to ascend. With still hands you slowly sink, as in that port. The movement stick does not propel you in this mode. Once the water is shallow enough to stand, use the left stick to walk onto the bank; normal walking and jumping resume. Tracked hands remain visible while swimming and diving.

Near the surface, face a ledge and press the mapped Jump button (A by default) to attempt Gothic's collision-checked climb. Underwater depth controls Gothic's normal breath and drowning rules. Losing focus or opening a menu clears pending strokes; returning controllers do not produce a spurious stroke.

## Hands, weapons and holsters

The **grip** is the side button under your middle finger; the **trigger** is the front button under your index finger. Reach to a holster and press the grip to draw an available inventory item. With the default settings, keep holding the grip. Release at the matching holster to stow, or away from it to drop or throw.

| Release holster | Assignment |
| --- | --- |
| Right belt | Two-handed axe (`ITMW_2H_AXE_L_01`), if owned |
| Chest | Empty |
| Back left | Empty |
| Back right | Arrows (`ITRW_ARROW`), if owned |

Holsters select a suitable inventory item. A picked-up melee weapon is assigned to the right belt and a bow or crossbow to the back left, unless that holster already holds an owned item or the weapon is assigned elsewhere. The **Holsters** menu shows all four slots and lets you assign, move, swap or drop an item, move the holster to the controller position and adjust its grab radius. Optional **Grip lock** keeps an item held without continuously pressing the grip; release the grip at its matching holster to stow it. **Weapon calibration** adjusts the model, aiming direction, support hand and holstered pose.

## Physical actions

- **Melee:** swing a held blade into an enemy, or punch with an empty hand. Place a blade in the incoming attack to parry. Hold the second grip near the handle for support. Two-handed weapons deal damage only while held with both hands; one-handed swings show a reminder. Physical attacks require **Physical combat** to be enabled.
- **Bow:** hold the bow and keep the other hand free. Compatible inventory ammunition is supplied automatically. Bring the free hand to the string, press its grip, draw back and release the grip to fire. Bow shooting requires **Physical combat**.
- **Pickup and throwing:** grip near a highlighted item with an empty hand. Release held weapons to throw, and grip near them to catch. Uncaught melee weapons and bows return to inventory after about 1.8 seconds; this does not apply to every item.
- **Potion:** draw it from the chest holster, bring it to your mouth and press that hand's trigger.

## Comfort and image settings

**Locomotion** controls running speed (0.75-2.0x), turning mode and speed, movement orientation, world size and room-scale movement. **Recenter tracking** recalibrates your position. Physical crouching lowers the camera but does not shrink the player's collision capsule.

**Character stats** in the main VR menu opens the original status screen with level, experience, learning points, attributes and talents.

Weapons whose strength or dexterity requirement is not met deal a quarter of their damage and show a warning. **Ignore weapon requirements** removes this penalty.

**HUD** controls interface placement, distance, **World item highlight**, highlight range and **Bow sight**. **Open game interface** opens the original inventory in a theater panel. **Performance** controls render resolution, draw distance, detail, lighting and the profiler. **Foveation** defaults to Off. On numeric calibration rows, the right trigger increases the value and the left trigger decreases it. Settings save automatically in `VR.ini`.

The bow calibration has separate string height, vertical center, horizontal X/Z offsets and arrow scale. Support-hand position controls move the palm independently of the string. Use **Use as bow default** or **Use as crossbow default** to apply alignment across that weapon family.
