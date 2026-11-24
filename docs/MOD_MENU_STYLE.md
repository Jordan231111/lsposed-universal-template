# Mod menu style

The default Nyx panel uses the following styles.

- dark translucent panel: `#E00E0E14`
- lavender border/accent: `#B89AFF` and `#6B5BFF`
- rounded rectangle radius: `16dp`
- dark pill buttons: `#1A1A24` with `#3A3550` stroke
- small oval bubble with `Nyx` text
- flat buttons with no default Material elevation

Behavior:

- Drag the bubble to move it. Tap it to open or close the panel. Drag the panel by its header.
- **X** closes the panel and leaves the bubble visible. The bottom-left **hide** text conceals both.
  Tap the bubble's last location once to show the bubble and reopen the panel at the same scroll position.
- Swipe up or down to see more features. On a long menu, drag the scrollbar to jump through the list.
  Narrow windows show one feature per row. Wider windows show two when they fit at the current text size.
- Change a numeric value with its horizontal slider or step buttons. Tap its label to enter an exact number.
- The optional tab example in `buildPanelView()` groups controls into All, Toggles and Numbers.
  It is commented out by default.
- overlay is added as a child of the target Activity decor view rather than as a top-level `TYPE_APPLICATION_OVERLAY` window, so it does not rely on `SYSTEM_ALERT_WINDOW` in the target process

[Panel layout](../app/src/main/java/com/template/lsposed/ui/OverlayController.java) · [Gestures](../app/src/main/java/com/template/lsposed/ui/OverlayDrag.java) · [Feature rows](../app/src/main/java/com/template/lsposed/ui/OverlayFeatures.java)
