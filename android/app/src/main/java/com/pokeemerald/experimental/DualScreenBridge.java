package com.pokeemerald.experimental;

/**
 * JNI boundary to the in-process game (libmain.so). The game code fills a
 * JSON snapshot of its state once per frame window; these calls read it.
 */
public final class DualScreenBridge {
    private DualScreenBridge() {}

    /** Latest game-state snapshot as JSON. Never null once the game has booted. */
    public static native String nativeGetSnapshotJson();

    /**
     * Renders the 32x32 party icon for a species into ARGB_8888 pixels
     * (row-major, length 32*32), decoded from the game's own icon data.
     * Returns null for invalid species.
     */
    public static native int[] nativeGetMonIcon(int species);
}
