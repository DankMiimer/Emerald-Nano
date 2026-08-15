package com.pokeemerald.experimental;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** Parsed snapshot of game state, produced from the bridge's JSON. */
public final class DualScreenState {
    public static final class BagItem {
        public int id;
        public String name = "";
        public int quantity;
    }

    public static final class Move {
        public int id;
        public String name = "";
        public int pp;
        public int maxPp;
        public int type;
        public int power;    // 0 = status move (shown as em dash)
        public int accuracy; // 0 = never misses (shown as em dash)
        // Effectiveness class vs the current foes ([0] left, [1] right):
        // -1 no hint, 0 no effect, 1 not very, 2 normal, 3 super.
        public final int[] eff = {-1, -1};
    }

    public static final class Mon {
        public int species;
        public int dex;
        public String name = "";
        public String nick = "";
        public int level;
        public int hp;
        public int maxHp;
        public long status;
        public int item;
        public String itemName = "";
        public boolean isEgg;
        public int gender; // 0 male, 1 female, 2 genderless
        public int[] types = {0, 0};
        public int expPct;
        public int[] stats = new int[5]; // atk, def, speed, spatk, spdef
        public String nature = "";
        public String ability = "";
        public final List<Move> moves = new ArrayList<>();
    }

    public boolean inGame;
    public boolean inBattle;
    public int battleKind; // 0 wild, 1 trainer
    public int battleMenu; // 0 none, 1 action select, 2 move select
    public int actionCursor;
    public int moveCursor;
    public boolean battleDouble;
    public boolean battleCanUseItems; // battle type allows the bag at all
    public boolean battleCanCatch;    // wild battle: balls pocket is legal
    public int battleSub;             // 0 none, 1 bag wait, 2 party wait
    public int battleSubCase;         // PARTY_ACTION_* for the party wait
    public int battleSubResult;       // DS_BMENU_RESULT_* of last submission
    public int battleSubSeq;
    public final int[] battleActive = {-1, -1}; // party indexes on the field
    public int battlePrevSel = 6;     // partner's already-chosen switch slot
    public String playerName = "";
    public int playerGender;
    public int money;
    public int badges;
    public int badgeFlags;
    public int dexSeen;
    public int dexCaught;
    public int trainerId;
    public int stars;
    public int hours;
    public int minutes;
    public String mapName = "";
    public int mapsec = -1;
    public int posX;
    public int posY;
    public final List<Mon> party = new ArrayList<>();
    public final List<List<BagItem>> bag = new ArrayList<>();
    public Mon battlePlayerMon;
    public Mon battleEnemyMon;
    public Mon battlePlayerMon2; // doubles: the menu battler's partner
    public Mon battleEnemyMon2;  // doubles: the right-side foe

    private static Mon parseMon(JSONObject o) throws JSONException {
        Mon m = new Mon();
        m.species = o.optInt("species");
        m.dex = o.optInt("dex");
        m.name = o.optString("name");
        m.nick = o.optString("nick", m.name);
        m.level = o.optInt("level");
        m.hp = o.optInt("hp");
        m.maxHp = o.optInt("maxHp");
        m.status = o.optLong("status");
        m.item = o.optInt("item");
        m.itemName = o.optString("itemName");
        m.isEgg = o.optInt("isEgg") != 0;
        m.gender = o.optInt("gender");
        JSONArray types = o.optJSONArray("types");
        if (types != null && types.length() >= 2) {
            m.types[0] = types.getInt(0);
            m.types[1] = types.getInt(1);
        }
        m.expPct = o.optInt("expPct");
        m.nature = o.optString("nature");
        m.ability = o.optString("ability");
        JSONArray stats = o.optJSONArray("stats");
        if (stats != null && stats.length() >= 5) {
            for (int i = 0; i < 5; i++) {
                m.stats[i] = stats.getInt(i);
            }
        }
        JSONArray moves = o.optJSONArray("moves");
        if (moves != null) {
            for (int i = 0; i < moves.length(); i++) {
                JSONObject mo = moves.getJSONObject(i);
                Move move = new Move();
                move.id = mo.optInt("id");
                move.name = mo.optString("name");
                move.pp = mo.optInt("pp");
                move.maxPp = mo.optInt("maxPp");
                move.type = mo.optInt("type");
                move.power = mo.optInt("pw");
                move.accuracy = mo.optInt("ac");
                JSONArray eff = mo.optJSONArray("eff");
                if (eff != null && eff.length() >= 2) {
                    move.eff[0] = eff.getInt(0);
                    move.eff[1] = eff.getInt(1);
                }
                m.moves.add(move);
            }
        }
        return m;
    }

    public static DualScreenState parse(String json) {
        DualScreenState state = new DualScreenState();
        if (json == null || json.isEmpty()) {
            return state;
        }
        try {
            JSONObject root = new JSONObject(json);
            state.inGame = root.optInt("inGame") != 0;
            state.inBattle = root.optInt("inBattle") != 0;
            JSONObject player = root.optJSONObject("player");
            if (player != null) {
                state.playerName = player.optString("name");
                state.playerGender = player.optInt("gender");
                state.money = player.optInt("money");
                state.badges = player.optInt("badges");
                state.badgeFlags = player.optInt("badgeFlags");
                state.dexSeen = player.optInt("dexSeen");
                state.dexCaught = player.optInt("dexCaught");
                state.trainerId = player.optInt("trainerId");
                state.stars = player.optInt("stars");
                state.hours = player.optInt("hours");
                state.minutes = player.optInt("minutes");
                state.mapName = player.optString("mapName");
                state.mapsec = player.optInt("mapsec", -1);
                state.posX = player.optInt("x");
                state.posY = player.optInt("y");
            }
            JSONArray bag = root.optJSONArray("bag");
            if (bag != null) {
                for (int p = 0; p < bag.length(); p++) {
                    List<BagItem> pocket = new ArrayList<>();
                    JSONArray items = bag.getJSONArray(p);
                    for (int i = 0; i < items.length(); i++) {
                        JSONObject io = items.getJSONObject(i);
                        BagItem item = new BagItem();
                        item.id = io.optInt("id");
                        item.name = io.optString("n");
                        item.quantity = io.optInt("q");
                        pocket.add(item);
                    }
                    state.bag.add(pocket);
                }
            }
            JSONArray party = root.optJSONArray("party");
            if (party != null) {
                for (int i = 0; i < party.length(); i++) {
                    state.party.add(parseMon(party.getJSONObject(i)));
                }
            }
            JSONObject battle = root.optJSONObject("battle");
            if (battle != null) {
                state.battleKind = battle.optInt("kind");
                state.battleMenu = battle.optInt("menu");
                state.actionCursor = battle.optInt("actionCursor");
                state.moveCursor = battle.optInt("moveCursor");
                state.battleDouble = battle.optInt("double") != 0;
                state.battleCanUseItems = battle.optInt("canUseItems") != 0;
                state.battleCanCatch = battle.optInt("canCatch") != 0;
                state.battleSub = battle.optInt("sub");
                state.battleSubCase = battle.optInt("subCase");
                state.battleSubResult = battle.optInt("subResult");
                state.battleSubSeq = battle.optInt("subSeq");
                JSONArray active = battle.optJSONArray("active");
                if (active != null && active.length() >= 2) {
                    state.battleActive[0] = active.getInt(0);
                    state.battleActive[1] = active.getInt(1);
                }
                state.battlePrevSel = battle.optInt("prevSel", 6);
                JSONObject p = battle.optJSONObject("playerMon");
                if (p != null) state.battlePlayerMon = parseMon(p);
                JSONObject e = battle.optJSONObject("enemyMon");
                if (e != null) state.battleEnemyMon = parseMon(e);
                JSONObject p2 = battle.optJSONObject("playerMon2");
                if (p2 != null) state.battlePlayerMon2 = parseMon(p2);
                JSONObject e2 = battle.optJSONObject("enemyMon2");
                if (e2 != null) state.battleEnemyMon2 = parseMon(e2);
            }
        } catch (JSONException ignored) {
            // Torn/partial snapshot; keep whatever parsed.
        }
        return state;
    }
}
