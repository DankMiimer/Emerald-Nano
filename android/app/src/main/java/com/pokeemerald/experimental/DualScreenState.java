package com.pokeemerald.experimental;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** Parsed snapshot of game state, produced from the bridge's JSON. */
public final class DualScreenState {
    public static final class Move {
        public int id;
        public String name = "";
        public int pp;
        public int maxPp;
        public int type;
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
        public final List<Move> moves = new ArrayList<>();
    }

    public boolean inGame;
    public boolean inBattle;
    public int battleKind; // 0 wild, 1 trainer
    public String playerName = "";
    public int playerGender;
    public int money;
    public int badges;
    public int hours;
    public int minutes;
    public String mapName = "";
    public int mapsec = -1;
    public final List<Mon> party = new ArrayList<>();
    public Mon battlePlayerMon;
    public Mon battleEnemyMon;

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
                state.hours = player.optInt("hours");
                state.minutes = player.optInt("minutes");
                state.mapName = player.optString("mapName");
                state.mapsec = player.optInt("mapsec", -1);
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
                JSONObject p = battle.optJSONObject("playerMon");
                if (p != null) state.battlePlayerMon = parseMon(p);
                JSONObject e = battle.optJSONObject("enemyMon");
                if (e != null) state.battleEnemyMon = parseMon(e);
            }
        } catch (JSONException ignored) {
            // Torn/partial snapshot; keep whatever parsed.
        }
        return state;
    }
}
