# Controls

Default controls for Quest Touch. Gameplay buttons can be changed in **VR settings в†’ Button mapping**. Menu confirmation and navigation use separate bindings.

## Basic actions

| Action | Control |
| --- | --- |
| Look | Head movement |
| Move | Left stick |
| Run | Hold the left stick button (L3) while moving |
| Turn | Right stick; 30В° snap by default |
| Jump | A |
| Interact | B; look at the target, whose name appears on the HUD |
| Inventory | X |
| Journal | Y |
| Crouch action | Right stick button (R3) |
| Game menu | Left controller Menu |
| VR settings | Hold both grips and press Menu |
| Menu selection | Left stick |
| Confirm in menus | A |
| Back in menus | B |

Release the buttons and center both sticks after opening or closing a menu. In VR settings, left-stick up/down selects a row; left/right changes its value. Left trigger decreases and right trigger increases numeric values; A confirms. On action rows, the right trigger also confirms. B returns to the parent menu or closes the root menu. Head tracking remains active while the world is paused.

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

**Locomotion** controls running speed (0.75–2.0×), turning mode and speed, movement orientation, world size and room-scale movement. **Recenter tracking** recalibrates your position. Physical crouching lowers the camera but does not shrink the player's collision capsule.

**Character stats** in the main VR menu opens the original status screen with level, experience, learning points, attributes and talents.

Weapons whose strength or dexterity requirement is not met deal a quarter of their damage and show a warning. **Ignore weapon requirements** removes this penalty.

**HUD** controls interface placement, distance, **World item highlight**, highlight range and **Bow sight**. **Open game interface** opens the original inventory in a theater panel. **Performance** controls render resolution, draw distance, detail, lighting and the profiler. **Foveation** defaults to Off. On numeric calibration rows, the right trigger increases the value and the left trigger decreases it. Settings save automatically in `VR.ini`.

The bow calibration has separate string height, vertical center, horizontal X/Z offsets and arrow scale. Support-hand position controls move the palm independently of the string. Use **Use as bow default** or **Use as crossbow default** to apply alignment across that weapon family.
