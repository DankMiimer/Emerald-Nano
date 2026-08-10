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

    /**
     * The game's region map location table as a JSON array of
     * {id, x, y, w, h, n} on the 28x15 region grid. Static data; fetch once.
     */
    public static native String nativeGetRegionMapJson();

    /**
     * The real Pokenav Hoenn map as 240x160 ARGB_8888 pixels, composed from
     * the game's own tileset. The location grid starts at tile (1, 2).
     */
    public static native int[] nativeGetRegionMapImage();

    /**
     * The game's normal Latin font: 256 ints of glyph advance widths,
     * followed by 256 glyphs * 256 (16x16) color indices
     * (0 transparent, 1 foreground, 2 shadow). Static data; fetch once.
     */
    public static native int[] nativeGetFontAtlas();
}
