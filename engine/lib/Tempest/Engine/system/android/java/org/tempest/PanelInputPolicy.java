package org.tempest;

/** Android panels receive Quest Touch interaction as pointer events, not a gamepad. */
final class PanelInputPolicy {
    private PanelInputPolicy() {}

    static boolean isGamepad(int vendorId, boolean hasGamepadSource) {
        // Horizon exposes its tracked and virtual Touch devices in InputDevice too.
        // Their mere presence must not disable ray/trigger (touch) input in a panel.
        return vendorId != 0x2833 && hasGamepadSource;
    }
}
