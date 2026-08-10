package com.pokeemerald.experimental;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.view.Display;
import android.view.ViewGroup;

/** Hosts the DualScreenView on a secondary (bottom) display. */
public final class DualScreenPresentation extends Presentation {
    private DualScreenView view;

    public DualScreenPresentation(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Never take key focus: the game activity must keep receiving
        // controller input while the bottom screen is touched.
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        view = new DualScreenView(getContext());
        setContentView(view, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    public void updateState(DualScreenState state) {
        if (view != null) {
            view.setState(state);
        }
    }
}
