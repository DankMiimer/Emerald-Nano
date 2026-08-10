package com.pokeemerald.experimental;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Bottom-screen UI, styled after the game's own Pokenav: dotted mint
 * background, cream menu bars, and the game's real font and graphics
 * (all decoded at runtime from game data over the bridge).
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

    // Pokenav palette.
    private static final int BG_MINT = 0xFFD8F8E8;
    private static final int BG_DOT = 0xFFB8E4CE;
    private static final int HEADER_GREEN = 0xFF50C484;
    private static final int HEADER_GREEN_DARK = 0xFF2E9A62;
    private static final int BAR_CREAM = 0xFFF8F0B0;
    private static final int BAR_CREAM_DARK = 0xFFE8CE7A;
    private static final int BAR_BORDER = 0xFFA88848;
    private static final int PANEL_WHITE = 0xFFFFFFFF;
    private static final int PANEL_BORDER = 0xFF58585A;
    private static final int TEXT_DARK = 0xFF484850;
    private static final int TEXT_SHADOW = 0xFFD0D0C8;
    private static final int TEXT_WHITE = 0xFFFFFFFF;
    private static final int TEXT_GREEN_SHADOW = 0xFF2E9A62;
    private static final int SEA_BLUE = 0xFF9CC7E8;
    private static final int HP_GREEN = 0xFF58D080;
    private static final int HP_YELLOW = 0xFFF8B050;
    private static final int HP_RED = 0xFFF05868;

    private static final class MapEntry {
        int id, x, y, w, h;
        String name = "";
    }

    private final Paint paint = new Paint();
    private final Paint pixelPaint = new Paint();
    private final SparseArray<Bitmap> iconCache = new SparseArray<>();
    private DualScreenState state = new DualScreenState();
    private int tab = TAB_PARTY;
    private int bagPocket;
    private java.util.List<MapEntry> mapEntries;
    private Bitmap regionMap;

    public DualScreenView(Context context) {
        super(context);
        pixelPaint.setFilterBitmap(false);
    }

    public void setState(DualScreenState next) {
        boolean autoBattle = next.inBattle && !state.inBattle;
        boolean autoParty = !next.inBattle && state.inBattle && tab == TAB_BATTLE;
        state = next;
        if (autoBattle) tab = TAB_BATTLE;
        if (autoParty) tab = TAB_PARTY;
        invalidate();
    }

    private GbaFont font() {
        return GbaFont.get();
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
        return getHeight() * 0.105f;
    }

    private RectF tabRect(int index) {
        float w = getWidth() / (float) TAB_NAMES.length;
        float top = getHeight() - tabBarHeight();
        return new RectF(index * w, top, (index + 1) * w, getHeight());
    }

    private static final String[] POCKET_NAMES = {"ITEMS", "BALLS", "TM-HM", "BERRIES", "KEY"};

    private RectF pocketRect(int index) {
        float w = getWidth() / (float) POCKET_NAMES.length;
        float h = (getHeight() - tabBarHeight()) * 0.105f;
        return new RectF(index * w, 0, (index + 1) * w, h);
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
            if (tab == TAB_BAG) {
                for (int i = 0; i < POCKET_NAMES.length; i++) {
                    if (pocketRect(i).contains(event.getX(), event.getY())) {
                        bagPocket = i;
                        invalidate();
                        return true;
                    }
                }
            }
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Shared chrome
    // ------------------------------------------------------------------

    private void drawBackground(Canvas canvas) {
        canvas.drawColor(BG_MINT);
        paint.setColor(BG_DOT);
        float step = getWidth() / 40f;
        float radius = step * 0.10f;
        for (float y = step / 2; y < getHeight(); y += step) {
            for (float x = step / 2; x < getWidth(); x += step) {
                canvas.drawCircle(x, y, radius, paint);
            }
        }
    }

    private void drawBar(Canvas canvas, RectF r, boolean selected, String label, float textScale) {
        paint.setColor(BAR_BORDER);
        canvas.drawRoundRect(r, 6, 6, paint);
        RectF inner = new RectF(r);
        inner.inset(3, 3);
        paint.setColor(selected ? BAR_CREAM : BAR_CREAM_DARK);
        canvas.drawRoundRect(inner, 4, 4, paint);
        if (selected) {
            paint.setColor(0xFFF8B850);
            RectF edge = new RectF(inner.left, inner.bottom - 6, inner.right, inner.bottom);
            canvas.drawRoundRect(edge, 3, 3, paint);
        }
        GbaFont f = font();
        if (f != null && label != null) {
            float w = f.measure(label, textScale);
            f.draw(canvas, label, r.centerX() - w / 2,
                    r.centerY() - GbaFont.LINE_HEIGHT * textScale / 2, textScale, TEXT_DARK, TEXT_SHADOW);
        }
    }

    private void drawHeader(Canvas canvas, String title, float scale) {
        RectF bar = new RectF(0, 0, getWidth(), GbaFont.LINE_HEIGHT * scale * 1.6f);
        paint.setColor(HEADER_GREEN);
        canvas.drawRect(bar, paint);
        paint.setColor(HEADER_GREEN_DARK);
        canvas.drawRect(new RectF(bar.left, bar.bottom - 4, bar.right, bar.bottom), paint);
        GbaFont f = font();
        if (f != null) {
            f.draw(canvas, title, scale * 8,
                    bar.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale, TEXT_WHITE, TEXT_GREEN_SHADOW);
        }
    }

    private void drawTabBar(Canvas canvas) {
        float scale = tabBarHeight() / (GbaFont.LINE_HEIGHT * 2.4f);
        paint.setColor(HEADER_GREEN);
        canvas.drawRect(new RectF(0, getHeight() - tabBarHeight(), getWidth(), getHeight()), paint);
        for (int i = 0; i < TAB_NAMES.length; i++) {
            RectF r = tabRect(i);
            r.inset(6, 7);
            drawBar(canvas, r, i == tab, TAB_NAMES[i], scale);
        }
    }

    private void drawCenteredMessage(Canvas canvas, String message) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float scale = getWidth() / 420f;
        float w = f.measure(message, scale);
        f.draw(canvas, message, (getWidth() - w) / 2,
                (getHeight() - tabBarHeight()) / 2 - GbaFont.LINE_HEIGHT * scale / 2,
                scale, TEXT_DARK, TEXT_SHADOW);
    }

    private int hpColor(int hp, int maxHp) {
        if (maxHp <= 0) return HP_GREEN;
        float ratio = hp / (float) maxHp;
        if (ratio > 0.5f) return HP_GREEN;
        if (ratio > 0.2f) return HP_YELLOW;
        return HP_RED;
    }

    private void drawHpBar(Canvas canvas, float left, float top, float width, float height, int hp, int maxHp) {
        paint.setColor(PANEL_BORDER);
        canvas.drawRoundRect(new RectF(left - 2, top - 2, left + width + 2, top + height + 2), height / 2, height / 2, paint);
        paint.setColor(PANEL_WHITE);
        canvas.drawRoundRect(new RectF(left, top, left + width, top + height), height / 2, height / 2, paint);
        if (maxHp > 0 && hp > 0) {
            float fill = Math.max(height, width * hp / (float) maxHp);
            paint.setColor(hpColor(hp, maxHp));
            canvas.drawRoundRect(new RectF(left, top, left + fill, top + height), height / 2, height / 2, paint);
        }
    }

    private void drawTypeBadge(Canvas canvas, int type, float left, float top, float height) {
        if (type < 0 || type >= TYPE_NAMES.length) return;
        GbaFont f = font();
        float scale = height / (GbaFont.LINE_HEIGHT * 1.3f);
        float width = height * 3.1f;
        RectF r = new RectF(left, top, left + width, top + height);
        paint.setColor(0x40000000);
        canvas.drawRoundRect(new RectF(r.left, r.top + 2, r.right, r.bottom + 2), 5, 5, paint);
        paint.setColor(TYPE_COLORS[Math.min(type, TYPE_COLORS.length - 1)]);
        canvas.drawRoundRect(r, 5, 5, paint);
        if (f != null) {
            String name = TYPE_NAMES[type];
            float w = f.measure(name, scale);
            f.draw(canvas, name, r.centerX() - w / 2,
                    r.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale, TEXT_WHITE, 0xFF585858);
        }
    }

    private String statusLabel(long status, int hp) {
        if (hp == 0) return "FNT";
        if ((status & 0x7) != 0) return "SLP";
        if ((status & 0x8) != 0 || (status & 0x80) != 0) return "PSN";
        if ((status & 0x10) != 0) return "BRN";
        if ((status & 0x20) != 0) return "FRZ";
        if ((status & 0x40) != 0) return "PAR";
        return null;
    }

    // ------------------------------------------------------------------
    // Tabs
    // ------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawBackground(canvas);
        if (!state.inGame) {
            drawCenteredMessage(canvas, "Waiting for the adventure to start~");
            drawTabBar(canvas);
            return;
        }
        switch (tab) {
        case TAB_PARTY:  drawParty(canvas); break;
        case TAB_BATTLE: drawBattle(canvas); break;
        case TAB_MAP:    drawMap(canvas); break;
        case TAB_BAG:    drawBag(canvas); break;
        case TAB_CARD:   drawTrainerCard(canvas); break;
        }
        drawTabBar(canvas);
    }

    private void drawParty(Canvas canvas) {
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.012f;
        float rowH = (contentHeight - pad * 4) / 3;
        float colW = (getWidth() - pad * 3) / 2;
        float scale = rowH / (GbaFont.LINE_HEIGHT * 4.6f);

        for (int i = 0; i < 6; i++) {
            float left = pad + (i % 2) * (colW + pad);
            float top = pad + (i / 2) * (rowH + pad);
            RectF card = new RectF(left, top, left + colW, top + rowH);
            paint.setColor(i < state.party.size() ? PANEL_WHITE : 0x66FFFFFF);
            canvas.drawRoundRect(card, 10, 10, paint);
            paint.setColor(i < state.party.size() ? BAR_BORDER : 0x66A88848);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3);
            canvas.drawRoundRect(card, 10, 10, paint);
            paint.setStyle(Paint.Style.FILL);
            if (i >= state.party.size()) continue;
            DualScreenState.Mon mon = state.party.get(i);

            float inset = rowH * 0.12f;
            float iconSize = rowH * 0.52f;
            Bitmap icon = mon.isEgg ? null : monIcon(mon.species);
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(card.left + inset, card.top + inset,
                                  card.left + inset + iconSize, card.top + inset + iconSize), pixelPaint);
            }
            float textLeft = card.left + inset + iconSize + inset;
            if (f == null) continue;

            String title = mon.isEgg ? "EGG" : mon.nick;
            f.draw(canvas, title, textLeft, card.top + inset, scale, TEXT_DARK, TEXT_SHADOW);

            if (!mon.isEgg) {
                String sub = "Lv" + mon.level + (mon.gender == 0 ? " ♂" : mon.gender == 1 ? " ♀" : "");
                float subScale = scale * 0.85f;
                f.draw(canvas, sub, textLeft, card.top + inset + GbaFont.LINE_HEIGHT * scale + 4,
                        subScale, TEXT_DARK, TEXT_SHADOW);

                float barTop = card.top + inset + GbaFont.LINE_HEIGHT * scale * 1.95f;
                float barH = rowH * 0.075f;
                drawHpBar(canvas, textLeft, barTop, card.right - inset - textLeft, barH, mon.hp, mon.maxHp);

                String hpText = mon.hp + "/" + mon.maxHp;
                f.draw(canvas, hpText, textLeft, barTop + barH + 6, subScale, TEXT_DARK, TEXT_SHADOW);
                String status = statusLabel(mon.status, mon.hp);
                if (status != null) {
                    float w = f.measure(status, subScale);
                    f.draw(canvas, status, card.right - inset - w, barTop + barH + 6,
                            subScale, HP_RED, TEXT_SHADOW);
                }
            }
        }
    }

    private void drawBattle(Canvas canvas) {
        GbaFont f = font();
        if (!state.inBattle || state.battlePlayerMon == null || f == null) {
            drawCenteredMessage(canvas, "Not in battle");
            return;
        }
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 430f;

        DualScreenState.Mon enemy = state.battleEnemyMon;
        if (enemy != null) {
            RectF top = new RectF(pad, pad, getWidth() - pad, contentHeight * 0.27f);
            paint.setColor(PANEL_WHITE);
            canvas.drawRoundRect(top, 10, 10, paint);
            paint.setColor(BAR_BORDER);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3);
            canvas.drawRoundRect(top, 10, 10, paint);
            paint.setStyle(Paint.Style.FILL);

            float inset = top.height() * 0.14f;
            float iconSize = top.height() * 0.68f;
            Bitmap icon = monIcon(enemy.species);
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(top.left + inset, top.top + inset,
                                  top.left + inset + iconSize, top.top + inset + iconSize), pixelPaint);
            }
            float textLeft = top.left + inset + iconSize + inset;
            String header = (state.battleKind == 1 ? "FOE " : "WILD ") + enemy.name + "  Lv" + enemy.level;
            f.draw(canvas, header, textLeft, top.top + inset, scale, TEXT_DARK, TEXT_SHADOW);
            float barTop = top.top + inset + GbaFont.LINE_HEIGHT * scale + 10;
            drawHpBar(canvas, textLeft, barTop, top.right - inset - textLeft - top.height() * 0.9f,
                    top.height() * 0.1f, enemy.hp, enemy.maxHp);
            f.draw(canvas, enemy.hp + "/" + enemy.maxHp, textLeft,
                    barTop + top.height() * 0.1f + 8, scale * 0.85f, TEXT_DARK, TEXT_SHADOW);
            drawTypeBadge(canvas, enemy.types[0], top.right - inset - top.height() * 0.26f * 3.1f,
                    top.top + inset, top.height() * 0.26f);
        }

        DualScreenState.Mon self = state.battlePlayerMon;
        float headerTop = contentHeight * 0.30f;
        f.draw(canvas, self.nick + "  Lv" + self.level + "  " + self.hp + "/" + self.maxHp + " HP",
                pad * 1.5f, headerTop, scale, TEXT_DARK, TEXT_SHADOW);

        float gridTop = headerTop + GbaFont.LINE_HEIGHT * scale + pad;
        float cellH = (contentHeight - gridTop - pad * 2) / 2;
        float cellW = (getWidth() - pad * 3) / 2;
        for (int i = 0; i < 4; i++) {
            float left = pad + (i % 2) * (cellW + pad);
            float top = gridTop + (i / 2) * (cellH + pad);
            RectF cell = new RectF(left, top, left + cellW, top + cellH);
            if (i < self.moves.size()) {
                drawBar(canvas, cell, false, null, scale);
                DualScreenState.Move move = self.moves.get(i);
                float inset = cellH * 0.16f;
                f.draw(canvas, move.name, cell.left + inset, cell.top + inset, scale, TEXT_DARK, TEXT_SHADOW);
                drawTypeBadge(canvas, move.type, cell.left + inset,
                        cell.bottom - inset - cellH * 0.24f, cellH * 0.24f);
                String pp = "PP " + move.pp + "/" + move.maxPp;
                float w = f.measure(pp, scale * 0.9f);
                f.draw(canvas, pp, cell.right - inset - w,
                        cell.bottom - inset - GbaFont.LINE_HEIGHT * scale * 0.9f,
                        scale * 0.9f, move.pp == 0 ? HP_RED : TEXT_DARK, TEXT_SHADOW);
            } else {
                paint.setColor(0x44A88848);
                canvas.drawRoundRect(cell, 8, 8, paint);
            }
        }
    }

    private void ensureMapData() {
        if (mapEntries == null) {
            mapEntries = new java.util.ArrayList<>();
            try {
                org.json.JSONArray entries = new org.json.JSONArray(DualScreenBridge.nativeGetRegionMapJson());
                for (int i = 0; i < entries.length(); i++) {
                    org.json.JSONObject o = entries.getJSONObject(i);
                    MapEntry e = new MapEntry();
                    e.id = o.optInt("id");
                    e.x = o.optInt("x");
                    e.y = o.optInt("y");
                    e.w = Math.max(o.optInt("w"), 1);
                    e.h = Math.max(o.optInt("h"), 1);
                    e.name = o.optString("n");
                    mapEntries.add(e);
                }
            } catch (org.json.JSONException ignored) {
            }
        }
        if (regionMap == null) {
            int[] pixels = DualScreenBridge.nativeGetRegionMapImage();
            if (pixels != null && pixels.length == 240 * 160) {
                regionMap = Bitmap.createBitmap(pixels, 240, 160, Bitmap.Config.ARGB_8888);
            }
        }
    }

    private void drawMap(Canvas canvas) {
        ensureMapData();
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();

        if (regionMap == null) {
            drawCenteredMessage(canvas, "Map unavailable");
            return;
        }

        // Integer-scale the 240x160 map, centered.
        int scale = (int) Math.min(getWidth() / 240f, contentHeight / 160f);
        if (scale < 1) scale = 1;
        float mapW = 240 * scale;
        float mapH = 160 * scale;
        float originX = (getWidth() - mapW) / 2f;
        float originY = (contentHeight - mapH) / 2f;

        paint.setColor(SEA_BLUE);
        canvas.drawRect(new RectF(0, 0, getWidth(), contentHeight), paint);
        canvas.drawBitmap(regionMap, null, new RectF(originX, originY, originX + mapW, originY + mapH), pixelPaint);

        // Player marker: grid starts at tile (1, 2); blink like the game.
        if (state.mapsec >= 0) {
            for (MapEntry e : mapEntries) {
                if (e.id != state.mapsec) continue;
                boolean blink = (System.currentTimeMillis() / 400) % 2 == 0;
                float cx = originX + ((e.x + 1) * 8 + e.w * 4) * scale;
                float cy = originY + ((e.y + 2) * 8 + e.h * 4) * scale;
                paint.setColor(blink ? 0xFFF83030 : 0xFFF8A0A0);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(3f * scale / 2);
                RectF marker = new RectF(
                        originX + (e.x + 1) * 8 * scale, originY + (e.y + 2) * 8 * scale,
                        originX + ((e.x + 1) * 8 + e.w * 8) * scale, originY + ((e.y + 2) * 8 + e.h * 8) * scale);
                canvas.drawRect(marker, paint);
                paint.setStyle(Paint.Style.FILL);
                canvas.drawCircle(cx, cy, 3f * scale / 2, paint);
                break;
            }
        }

        // Location label box, like the in-game map.
        if (f != null && !state.mapName.isEmpty()) {
            float labelScale = scale * 0.9f;
            float textW = f.measure(state.mapName, labelScale);
            RectF box = new RectF(originX + 8 * scale,
                    originY + mapH - GbaFont.LINE_HEIGHT * labelScale - 18 * scale / 2f,
                    originX + 8 * scale + textW + 24,
                    originY + mapH - 4 * scale / 2f);
            paint.setColor(PANEL_WHITE);
            canvas.drawRoundRect(box, 6, 6, paint);
            paint.setColor(PANEL_BORDER);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3);
            canvas.drawRoundRect(box, 6, 6, paint);
            paint.setStyle(Paint.Style.FILL);
            f.draw(canvas, state.mapName, box.left + 12,
                    box.centerY() - GbaFont.LINE_HEIGHT * labelScale / 2, labelScale, TEXT_DARK, TEXT_SHADOW);
        }
    }

    private void drawBag(Canvas canvas) {
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 460f;

        for (int i = 0; i < POCKET_NAMES.length; i++) {
            RectF r = pocketRect(i);
            r.inset(5, 5);
            drawBar(canvas, r, i == bagPocket, POCKET_NAMES[i], scale * 0.85f);
        }

        if (f == null || bagPocket >= state.bag.size() || state.bag.get(bagPocket).isEmpty()) {
            drawCenteredMessage(canvas, "Empty pocket");
            return;
        }
        java.util.List<DualScreenState.BagItem> items = state.bag.get(bagPocket);
        float listTop = contentHeight * 0.13f;
        int rows = 11;
        float rowH = (contentHeight - listTop - pad) / rows;
        int columns = 2;
        float colW = (getWidth() - pad * (columns + 1)) / columns;
        int visible = Math.min(items.size(), rows * columns);
        for (int i = 0; i < visible; i++) {
            int col = i / rows;
            int row = i % rows;
            float left = pad + col * (colW + pad);
            float top = listTop + row * rowH;
            DualScreenState.BagItem item = items.get(i);
            f.draw(canvas, item.name, left, top + (rowH - GbaFont.LINE_HEIGHT * scale) / 2,
                    scale, TEXT_DARK, TEXT_SHADOW);
            String qty = "x" + item.quantity;
            float w = f.measure(qty, scale);
            f.draw(canvas, qty, left + colW - w, top + (rowH - GbaFont.LINE_HEIGHT * scale) / 2,
                    scale, TEXT_DARK, TEXT_SHADOW);
        }
        if (items.size() > visible && f != null) {
            f.draw(canvas, "+" + (items.size() - visible) + " more~", pad,
                    contentHeight - GbaFont.LINE_HEIGHT * scale - 4, scale * 0.85f, TEXT_DARK, TEXT_SHADOW);
        }
    }

    private void drawTrainerCard(Canvas canvas) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.05f;
        float scale = getWidth() / 400f;
        RectF card = new RectF(pad, pad, getWidth() - pad, contentHeight - pad);

        paint.setColor(0xFF48A868);
        canvas.drawRoundRect(card, 18, 18, paint);
        paint.setColor(0xFF389858);
        canvas.drawRect(new RectF(card.left, card.top + card.height() * 0.28f,
                card.right, card.top + card.height() * 0.30f), paint);
        paint.setColor(0xFFF8F8F8);
        RectF inner = new RectF(card.left + card.width() * 0.06f, card.top + card.height() * 0.36f,
                card.right - card.width() * 0.06f, card.bottom - card.height() * 0.08f);
        canvas.drawRoundRect(inner, 10, 10, paint);

        f.draw(canvas, "TRAINER CARD", card.left + card.width() * 0.06f,
                card.top + card.height() * 0.07f, scale * 0.9f, TEXT_WHITE, TEXT_GREEN_SHADOW);
        f.draw(canvas, state.playerName, card.left + card.width() * 0.06f,
                card.top + card.height() * 0.16f, scale * 1.15f, TEXT_WHITE, TEXT_GREEN_SHADOW);

        float lineX = inner.left + inner.width() * 0.06f;
        float valueX = inner.left + inner.width() * 0.52f;
        float lineY = inner.top + inner.height() * 0.10f;
        float lineStep = inner.height() * 0.21f;
        String[][] rowsData = {
            {"MONEY", "$" + state.money},
            {"TIME", state.hours + ":" + String.format("%02d", state.minutes)},
            {"BADGES", Integer.toString(state.badges)},
            {"PLACE", state.mapName},
        };
        for (int i = 0; i < rowsData.length; i++) {
            f.draw(canvas, rowsData[i][0], lineX, lineY + i * lineStep, scale, TEXT_DARK, TEXT_SHADOW);
            f.draw(canvas, rowsData[i][1], valueX, lineY + i * lineStep, scale, TEXT_DARK, TEXT_SHADOW);
        }
    }
}
