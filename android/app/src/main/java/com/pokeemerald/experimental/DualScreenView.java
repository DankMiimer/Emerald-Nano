package com.pokeemerald.experimental;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Bottom-screen UI. Canvas-drawn, no external dependencies. Tabs along the
 * bottom edge; content area above renders the active tab from the latest
 * DualScreenState.
 */
public final class DualScreenView extends View {
    public static final int TAB_PARTY = 0;
    public static final int TAB_BATTLE = 1;
    public static final int TAB_MAP = 2;
    public static final int TAB_BAG = 3;
    public static final int TAB_CARD = 4;
    private static final String[] TAB_NAMES = {"PARTY", "BATTLE", "MAP", "BAG", "CARD"};

    private static final String[] TYPE_NAMES = {
        "NORMAL", "FIGHT", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST",
        "STEEL", "???", "FIRE", "WATER", "GRASS", "ELECTR", "PSYCHC", "ICE",
        "DRAGON", "DARK"
    };
    private static final int[] TYPE_COLORS = {
        0xFFA8A878, 0xFFC03028, 0xFFA890F0, 0xFFA040A0, 0xFFE0C068, 0xFFB8A038,
        0xFFA8B820, 0xFF705898, 0xFFB8B8D0, 0xFF68A090, 0xFFF08030, 0xFF6890F0,
        0xFF78C850, 0xFFF8D030, 0xFFF85888, 0xFF98D8D8, 0xFF7038F8, 0xFF705848
    };

    // Emerald-ish palette.
    private static final int BG = 0xFF10281C;
    private static final int PANEL = 0xFF1C3A2A;
    private static final int PANEL_LIGHT = 0xFF2A5540;
    private static final int ACCENT = 0xFF58C88A;
    private static final int TEXT_MAIN = 0xFFF0F8F0;
    private static final int TEXT_DIM = 0xFF90B8A0;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SparseArray<Bitmap> iconCache = new SparseArray<>();
    private DualScreenState state = new DualScreenState();
    private int tab = TAB_PARTY;

    public DualScreenView(Context context) {
        super(context);
        setBackgroundColor(BG);
        textPaint.setColor(TEXT_MAIN);
        textPaint.setFakeBoldText(true);
    }

    public void setState(DualScreenState next) {
        boolean autoBattle = next.inBattle && !state.inBattle;
        boolean autoParty = !next.inBattle && state.inBattle && tab == TAB_BATTLE;
        state = next;
        if (autoBattle) tab = TAB_BATTLE;
        if (autoParty) tab = TAB_PARTY;
        invalidate();
    }

    private Bitmap monIcon(int species) {
        Bitmap cached = iconCache.get(species);
        if (cached != null) {
            return cached;
        }
        int[] pixels = DualScreenBridge.nativeGetMonIcon(species);
        if (pixels == null || pixels.length != 32 * 32) {
            return null;
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, 32, 32, Bitmap.Config.ARGB_8888);
        iconCache.put(species, bitmap);
        return bitmap;
    }

    private float tabBarHeight() {
        return getHeight() * 0.11f;
    }

    private RectF tabRect(int index) {
        float w = getWidth() / (float) TAB_NAMES.length;
        float top = getHeight() - tabBarHeight();
        return new RectF(index * w, top, (index + 1) * w, getHeight());
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            for (int i = 0; i < TAB_NAMES.length; i++) {
                if (tabRect(i).contains(event.getX(), event.getY())) {
                    tab = i;
                    invalidate();
                    return true;
                }
            }
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawTabBar(canvas);
        if (!state.inGame) {
            drawCenteredMessage(canvas, "Waiting for the adventure to start…");
            return;
        }
        switch (tab) {
        case TAB_PARTY:  drawParty(canvas); break;
        case TAB_BATTLE: drawBattle(canvas); break;
        case TAB_MAP:    drawPlaceholder(canvas, "Map — coming soon"); break;
        case TAB_BAG:    drawPlaceholder(canvas, "Bag — coming soon"); break;
        case TAB_CARD:   drawTrainerCard(canvas); break;
        }
    }

    private void drawTabBar(Canvas canvas) {
        for (int i = 0; i < TAB_NAMES.length; i++) {
            RectF r = tabRect(i);
            paint.setColor(i == tab ? ACCENT : PANEL);
            canvas.drawRect(r, paint);
            textPaint.setColor(i == tab ? BG : TEXT_DIM);
            textPaint.setTextSize(r.height() * 0.38f);
            textPaint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(TAB_NAMES[i], r.centerX(), r.centerY() + textPaint.getTextSize() * 0.35f, textPaint);
        }
        textPaint.setTextAlign(Paint.Align.LEFT);
        textPaint.setColor(TEXT_MAIN);
    }

    private void drawCenteredMessage(Canvas canvas, String message) {
        textPaint.setColor(TEXT_DIM);
        textPaint.setTextSize(getHeight() * 0.04f);
        textPaint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(message, getWidth() / 2f, (getHeight() - tabBarHeight()) / 2f, textPaint);
        textPaint.setTextAlign(Paint.Align.LEFT);
        textPaint.setColor(TEXT_MAIN);
    }

    private void drawPlaceholder(Canvas canvas, String message) {
        drawCenteredMessage(canvas, message);
    }

    private String statusLabel(long status) {
        if ((status & 0x7) != 0) return "SLP";
        if ((status & 0x8) != 0 || (status & 0x80) != 0) return "PSN";
        if ((status & 0x10) != 0) return "BRN";
        if ((status & 0x20) != 0) return "FRZ";
        if ((status & 0x40) != 0) return "PAR";
        return null;
    }

    private int hpColor(int hp, int maxHp) {
        if (maxHp <= 0) return TEXT_DIM;
        float ratio = hp / (float) maxHp;
        if (ratio > 0.5f) return 0xFF58D080;
        if (ratio > 0.2f) return 0xFFF8B050;
        return 0xFFF05868;
    }

    private void drawHpBar(Canvas canvas, float left, float top, float width, float height, int hp, int maxHp) {
        paint.setColor(0xFF0A1810);
        canvas.drawRoundRect(new RectF(left, top, left + width, top + height), height / 2, height / 2, paint);
        if (maxHp > 0 && hp > 0) {
            float fill = Math.max(height, width * hp / (float) maxHp);
            paint.setColor(hpColor(hp, maxHp));
            canvas.drawRoundRect(new RectF(left, top, left + fill, top + height), height / 2, height / 2, paint);
        }
    }

    private void drawTypeBadge(Canvas canvas, int type, float left, float top, float height) {
        if (type < 0 || type >= TYPE_NAMES.length) return;
        float width = height * 3.2f;
        paint.setColor(TYPE_COLORS[Math.min(type, TYPE_COLORS.length - 1)]);
        RectF r = new RectF(left, top, left + width, top + height);
        canvas.drawRoundRect(r, height * 0.2f, height * 0.2f, paint);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(height * 0.62f);
        textPaint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(TYPE_NAMES[type], r.centerX(), r.centerY() + textPaint.getTextSize() * 0.35f, textPaint);
        textPaint.setTextAlign(Paint.Align.LEFT);
        textPaint.setColor(TEXT_MAIN);
    }

    private void drawParty(Canvas canvas) {
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.015f;
        float rowH = (contentHeight - pad * 4) / 3;
        float colW = (getWidth() - pad * 3) / 2;
        for (int i = 0; i < 6; i++) {
            float left = pad + (i % 2) * (colW + pad);
            float top = pad + (i / 2) * (rowH + pad);
            RectF card = new RectF(left, top, left + colW, top + rowH);
            paint.setColor(i < state.party.size() ? PANEL : 0xFF14301F);
            canvas.drawRoundRect(card, pad, pad, paint);
            if (i >= state.party.size()) continue;
            DualScreenState.Mon mon = state.party.get(i);

            float inset = rowH * 0.14f;
            float iconSize = rowH * 0.55f;
            Bitmap icon = mon.isEgg ? null : monIcon(mon.species);
            if (icon != null) {
                paint.setFilterBitmap(false);
                canvas.drawBitmap(icon, null,
                        new RectF(card.left + inset, card.top + inset,
                                  card.left + inset + iconSize, card.top + inset + iconSize), paint);
            }
            float textLeft = card.left + inset + iconSize + inset;

            textPaint.setTextSize(rowH * 0.20f);
            String title = mon.isEgg ? "EGG" : mon.nick;
            canvas.drawText(title, textLeft, card.top + inset + rowH * 0.18f, textPaint);

            if (!mon.isEgg) {
                textPaint.setColor(TEXT_DIM);
                textPaint.setTextSize(rowH * 0.15f);
                String sub = mon.name + "  Lv" + mon.level
                        + (mon.gender == 0 ? " ♂" : mon.gender == 1 ? " ♀" : "");
                canvas.drawText(sub, textLeft, card.top + inset + rowH * 0.38f, textPaint);
                textPaint.setColor(TEXT_MAIN);

                float barTop = card.top + inset + rowH * 0.46f;
                drawHpBar(canvas, textLeft, barTop, card.right - inset - textLeft, rowH * 0.09f, mon.hp, mon.maxHp);
                textPaint.setTextSize(rowH * 0.15f);
                String hpText = mon.hp + " / " + mon.maxHp;
                String status = statusLabel(mon.status);
                if (mon.hp == 0) status = "FNT";
                canvas.drawText(hpText, textLeft, barTop + rowH * 0.25f, textPaint);
                if (status != null) {
                    textPaint.setColor(0xFFF05868);
                    textPaint.setTextAlign(Paint.Align.RIGHT);
                    canvas.drawText(status, card.right - inset, barTop + rowH * 0.25f, textPaint);
                    textPaint.setTextAlign(Paint.Align.LEFT);
                    textPaint.setColor(TEXT_MAIN);
                }
                float badgeTop = card.bottom - inset - rowH * 0.16f;
                drawTypeBadge(canvas, mon.types[0], card.left + inset, badgeTop, rowH * 0.16f);
                if (mon.types[1] != mon.types[0]) {
                    drawTypeBadge(canvas, mon.types[1], card.left + inset + rowH * 0.16f * 3.2f + inset / 2, badgeTop, rowH * 0.16f);
                }
                if (mon.itemName != null && !mon.itemName.isEmpty()) {
                    textPaint.setColor(TEXT_DIM);
                    textPaint.setTextSize(rowH * 0.13f);
                    textPaint.setTextAlign(Paint.Align.RIGHT);
                    canvas.drawText(mon.itemName, card.right - inset, badgeTop + rowH * 0.13f, textPaint);
                    textPaint.setTextAlign(Paint.Align.LEFT);
                    textPaint.setColor(TEXT_MAIN);
                }
            }
        }
    }

    private void drawBattle(Canvas canvas) {
        if (!state.inBattle || state.battlePlayerMon == null) {
            drawCenteredMessage(canvas, "Not in battle");
            return;
        }
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.02f;

        DualScreenState.Mon enemy = state.battleEnemyMon;
        if (enemy != null) {
            RectF top = new RectF(pad, pad, getWidth() - pad, contentHeight * 0.28f);
            paint.setColor(PANEL);
            canvas.drawRoundRect(top, pad, pad, paint);
            Bitmap icon = monIcon(enemy.species);
            float inset = top.height() * 0.15f;
            float iconSize = top.height() * 0.7f;
            if (icon != null) {
                paint.setFilterBitmap(false);
                canvas.drawBitmap(icon, null,
                        new RectF(top.left + inset, top.top + inset,
                                  top.left + inset + iconSize, top.top + inset + iconSize), paint);
            }
            float textLeft = top.left + inset + iconSize + inset;
            textPaint.setTextSize(top.height() * 0.24f);
            canvas.drawText((state.battleKind == 1 ? "FOE " : "WILD ") + enemy.name + "  Lv" + enemy.level,
                    textLeft, top.top + top.height() * 0.35f, textPaint);
            drawHpBar(canvas, textLeft, top.top + top.height() * 0.5f,
                    top.right - inset - textLeft, top.height() * 0.12f, enemy.hp, enemy.maxHp);
            textPaint.setTextSize(top.height() * 0.2f);
            textPaint.setColor(TEXT_DIM);
            canvas.drawText(enemy.hp + " / " + enemy.maxHp, textLeft, top.top + top.height() * 0.85f, textPaint);
            textPaint.setColor(TEXT_MAIN);
            drawTypeBadge(canvas, enemy.types[0], top.right - inset - top.height() * 0.2f * 3.2f, top.top + inset, top.height() * 0.2f);
        }

        DualScreenState.Mon self = state.battlePlayerMon;
        float movesTop = contentHeight * 0.32f;
        RectF header = new RectF(pad, movesTop, getWidth() - pad, movesTop + contentHeight * 0.12f);
        textPaint.setTextSize(header.height() * 0.5f);
        canvas.drawText(self.nick + "  Lv" + self.level + "   " + self.hp + "/" + self.maxHp + " HP",
                header.left + pad, header.centerY() + textPaint.getTextSize() * 0.35f, textPaint);

        float gridTop = header.bottom + pad;
        float cellH = (contentHeight - gridTop - pad * 2) / 2;
        float cellW = (getWidth() - pad * 3) / 2;
        for (int i = 0; i < 4; i++) {
            float left = pad + (i % 2) * (cellW + pad);
            float top = gridTop + (i / 2) * (cellH + pad);
            RectF cell = new RectF(left, top, left + cellW, top + cellH);
            paint.setColor(i < self.moves.size() ? PANEL_LIGHT : 0xFF14301F);
            canvas.drawRoundRect(cell, pad, pad, paint);
            if (i >= self.moves.size()) continue;
            DualScreenState.Move move = self.moves.get(i);
            float inset = cellH * 0.16f;
            textPaint.setTextSize(cellH * 0.24f);
            canvas.drawText(move.name, cell.left + inset, cell.top + inset + cellH * 0.2f, textPaint);
            drawTypeBadge(canvas, move.type, cell.left + inset, cell.top + cellH * 0.5f, cellH * 0.2f);
            textPaint.setColor(move.pp == 0 ? 0xFFF05868 : TEXT_DIM);
            textPaint.setTextSize(cellH * 0.2f);
            textPaint.setTextAlign(Paint.Align.RIGHT);
            canvas.drawText("PP " + move.pp + "/" + move.maxPp, cell.right - inset, cell.bottom - inset, textPaint);
            textPaint.setTextAlign(Paint.Align.LEFT);
            textPaint.setColor(TEXT_MAIN);
        }
    }

    private void drawTrainerCard(Canvas canvas) {
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.03f;
        RectF card = new RectF(pad * 2, pad * 2, getWidth() - pad * 2, contentHeight - pad * 2);
        paint.setColor(PANEL);
        canvas.drawRoundRect(card, pad, pad, paint);

        float inset = pad * 1.5f;
        textPaint.setTextSize(card.height() * 0.11f);
        canvas.drawText(state.playerName, card.left + inset, card.top + inset + card.height() * 0.1f, textPaint);

        textPaint.setTextSize(card.height() * 0.07f);
        textPaint.setColor(TEXT_DIM);
        float line = card.top + inset + card.height() * 0.28f;
        canvas.drawText("MONEY", card.left + inset, line, textPaint);
        canvas.drawText("PLAY TIME", card.left + inset, line + card.height() * 0.16f, textPaint);
        canvas.drawText("BADGES", card.left + inset, line + card.height() * 0.32f, textPaint);
        canvas.drawText("LOCATION", card.left + inset, line + card.height() * 0.48f, textPaint);
        textPaint.setColor(TEXT_MAIN);
        float valueX = card.left + card.width() * 0.38f;
        canvas.drawText("$" + state.money, valueX, line, textPaint);
        canvas.drawText(state.hours + "h " + String.format("%02dm", state.minutes), valueX, line + card.height() * 0.16f, textPaint);
        canvas.drawText(Integer.toString(state.badges), valueX, line + card.height() * 0.32f, textPaint);
        canvas.drawText(state.mapName, valueX, line + card.height() * 0.48f, textPaint);
    }
}
