package com.pokeemerald.experimental;

import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.util.Arrays;

import org.libsdl.app.SDLActivity;

public class PokeEmeraldActivity extends SDLActivity {
    private static final long SNAPSHOT_INTERVAL_MS = 120;

    private DualScreenPresentation presentation;
    private final Handler snapshotHandler = new Handler(Looper.getMainLooper());
    private final Runnable snapshotPump = new Runnable() {
        @Override
        public void run() {
            // Self-heal: the Thor's system UI can steal the bottom display and
            // dismiss the presentation; re-show it whenever it is gone.
            if (presentation == null || !presentation.isShowing()) {
                presentation = null;
                showBottomScreen();
            }
            if (presentation != null && presentation.isShowing()) {
                String json = DualScreenBridge.nativeGetSnapshotJson();
                presentation.updateState(DualScreenState.parse(json));
            }
            // The overlay paints letterbox bars from the live setting. On a
            // release cold start DualScreen_FillAssets runs before the config
            // is read, so the first draw sees widescreen=0 and those bars
            // stick until something invalidates this view.
            if (controls != null) {
                controls.postInvalidate();
            }
            snapshotHandler.postDelayed(this, SNAPSHOT_INTERVAL_MS);
        }
    };

    private GbaControlsView controls;

    // Button navigation of the bottom screen. Gamepad buttons do not
    // auto-repeat on Android, so a held direction is repeated here or a long
    // bag list would need one press per row.
    private static final int NAV_NONE = -1;
    private static final long NAV_REPEAT_DELAY_MS = 400;
    private static final long NAV_REPEAT_INTERVAL_MS = 120;
    private int navHeldKey = KeyEvent.KEYCODE_UNKNOWN;
    private final Handler navHandler = new Handler(Looper.getMainLooper());
    private final Runnable navRepeat = new Runnable() {
        @Override
        public void run() {
            // Stops on its own if the key was released, the panel closed, or
            // the bottom screen went away, so it can never run loose.
            if (navHeldKey == KeyEvent.KEYCODE_UNKNOWN
                    || presentation == null || !presentation.isCapturingKeys()) {
                navHeldKey = KeyEvent.KEYCODE_UNKNOWN;
                return;
            }
            presentation.navigate(navAction(navHeldKey));
            navHandler.postDelayed(this, NAV_REPEAT_INTERVAL_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Hide the bars before the first layout so SDL's SurfaceView is
        // sized to the full display, not inset and then resized.
        applyImmersiveFlags();
        controls = new GbaControlsView(this);
        mLayout.addView(controls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    @Override
    protected void onResume() {
        super.onResume();
        showBottomScreen();
        snapshotHandler.removeCallbacks(snapshotPump);
        snapshotHandler.postDelayed(snapshotPump, SNAPSHOT_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        snapshotHandler.removeCallbacks(snapshotPump);
        navHandler.removeCallbacks(navRepeat);
        navHeldKey = KeyEvent.KEYCODE_UNKNOWN;
        dismissBottomScreen();
        super.onPause();
    }

    private void applyImmersiveFlags() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // setSystemUiVisibility is deprecated from API 30 and does not stop
            // the decor insetting the content here, which is what shrank the
            // SurfaceView. setDecorFitsSystemWindows(false) is the call that
            // actually gives the content the whole window.
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private void showBottomScreen() {
        if (presentation != null && presentation.isShowing()) {
            return;
        }
        DisplayManager displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        Display[] displays = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (displays.length == 0) {
            return; // Single-display device; game stays fullscreen.
        }
        presentation = new DualScreenPresentation(this, displays[0]);
        presentation.setSettingsListener(() -> {
            if (controls != null) {
                controls.postInvalidate();
            }
        });
        presentation.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        try {
            presentation.show();
        } catch (WindowManager.InvalidDisplayException e) {
            presentation = null;
        }
    }

    private void dismissBottomScreen() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }

    /**
     * Physical buttons drive the bottom screen while a battle takeover panel
     * is open. SDLActivity.dispatchKeyEvent only defers to super, which is
     * what eventually reaches SDL's surface, so intercepting here pre-empts
     * the game without the Presentation ever taking real key focus - it stays
     * FLAG_NOT_FOCUSABLE.
     *
     * Only key-downs are consumed. The matching up is always passed through,
     * because SDL keeps a held-button mask: swallowing the release of a
     * button the game had already seen pressed would latch it down forever.
     * An up SDL never saw a down for just clears a bit that is already clear.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int action = presentation != null && presentation.isCapturingKeys()
                ? navAction(event.getKeyCode()) : NAV_NONE;
        if (action == NAV_NONE) {
            return super.dispatchKeyEvent(event);
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (event.getRepeatCount() == 0) {
                presentation.navigate(action);
                if (action <= DualScreenView.NAV_RIGHT) {
                    navHeldKey = event.getKeyCode();
                    navHandler.removeCallbacks(navRepeat);
                    navHandler.postDelayed(navRepeat, NAV_REPEAT_DELAY_MS);
                }
            }
            return true;
        }
        if (event.getKeyCode() == navHeldKey) {
            navHeldKey = KeyEvent.KEYCODE_UNKNOWN;
            navHandler.removeCallbacks(navRepeat);
        }
        return super.dispatchKeyEvent(event);
    }

    /**
     * Android synthesizes DPAD key events for gamepad hat axes, so the d-pad
     * arrives here whichever way the pad reports it. The analog sticks do not
     * - the game maps those itself from SDL axis events - so the sticks stay
     * with the game.
     */
    private static int navAction(int keyCode) {
        switch (keyCode) {
        case KeyEvent.KEYCODE_DPAD_UP:    return DualScreenView.NAV_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN:  return DualScreenView.NAV_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT:  return DualScreenView.NAV_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return DualScreenView.NAV_RIGHT;
        case KeyEvent.KEYCODE_BUTTON_A:
        case KeyEvent.KEYCODE_Z:          return DualScreenView.NAV_CONFIRM;
        case KeyEvent.KEYCODE_BUTTON_B:
        case KeyEvent.KEYCODE_X:          return DualScreenView.NAV_CANCEL;
        default:                          return NAV_NONE;
        }
    }

    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // The manifest already keeps this activity in sensor landscape mode.
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) {
            return;
        }

        // Re-applied on every focus gain because IMMERSIVE_STICKY only hides
        // the bars again after the user swipes them back in.
        applyImmersiveFlags();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && mSurface != null) {
            mSurface.post(() -> {
                int width = mSurface.getWidth();
                int height = mSurface.getHeight();
                mSurface.setSystemGestureExclusionRects(Arrays.asList(
                        new Rect(0, height / 2, width / 5, height),
                        new Rect(width * 4 / 5, height / 2, width, height)));
            });
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
