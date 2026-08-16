package com.pokeemerald.experimental;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.ViewGroup;

/** Hosts the DualScreenView on a secondary (bottom) display. */
public final class DualScreenPresentation extends Presentation {
    private DualScreenView view;
    private Runnable settingsListener;

    public DualScreenPresentation(Context context, Display display) {
        super(context, display);
    }

    public void setSettingsListener(Runnable listener) {
        settingsListener = listener;
        if (view != null) {
            view.setSettingsListener(listener);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Never take key focus: the game activity must keep receiving
        // controller input while the bottom screen is touched.
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        view = new DualScreenView(getContext());
        view.setSettingsListener(settingsListener);
        setContentView(view, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    public void updateState(DualScreenState state) {
        if (view != null) {
            view.setState(state);
        }
    }

    /** Whether the bottom screen currently wants the physical buttons. */
    public boolean isCapturingKeys() {
        return view != null && view.isCapturingKeys();
    }

    /** Routes one button press from the game activity to the bottom screen. */
    public void navigate(int action) {
        if (view != null) {
            view.navigate(action);
        }
    }
}
