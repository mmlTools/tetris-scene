/*
Stack Party - chat-powered falling-block game source for OBS Studio
Copyright (C) 2026 MMLTECH

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace sp {

constexpr int BoardW = 10;
constexpr int BoardH = 20;
constexpr int QueueSize = 3;

enum PieceType : int {
	PIECE_I,
	PIECE_T,
	PIECE_O,
	PIECE_S,
	PIECE_Z,
	PIECE_J,
	PIECE_L,
	/* "cursed" pentominoes sent by chat */
	PIECE_PLUS,
	PIECE_U,
	PIECE_W,
	PIECE_BIGT,
	/* single-cell explosive sent by chat */
	PIECE_BOMB,
	PIECE_COUNT
};
constexpr int ClassicPieceCount = 7;

enum CellKind : uint8_t { CELL_EMPTY = 0, CELL_BLOCK, CELL_GARBAGE, CELL_GOLD };

struct Cell {
	uint8_t kind = CELL_EMPTY;
	uint8_t type = 0; /* PieceType that created the cell, used for colour */
};

struct Offset {
	int x;
	int y;
};

struct Piece {
	int type = PIECE_T;
	int rot = 0;
	int x = 3;
	int y = -1;
	bool golden = false;
};

/* Fills `out` with the occupied cells of a piece type/rotation, returns the count (max 5). */
int pieceCells(int type, int rot, Offset out[5]);
bool isCursed(int type);

enum class Action {
	Left,
	Right,
	SoftDrop,
	RotateCW,
	RotateCCW,
	HardDrop,
	Hold,
	Pause,
	Reset,
	Autopilot,
	Panic,
	ToggleChat,
	Count
};
constexpr int ActionCount = static_cast<int>(Action::Count);

enum Effect : int {
	FX_FLIP,
	FX_MIRROR,
	FX_TURBO,
	FX_SLOW,
	FX_DARK,
	FX_SHAKE,
	FX_WOBBLE,
	FX_DISCO,
	FX_HYPE,
	FX_CHILL,
	FX_COUNT
};

enum class Team { Helper, Troll, Fun, Control, System };

enum CommandId : int {
	/* troll team */
	CMD_FLIP,
	CMD_MIRROR,
	CMD_TURBO,
	CMD_GARBAGE,
	CMD_DARK,
	CMD_SHAKE,
	CMD_WOBBLE,
	CMD_CURSE,
	/* helper team */
	CMD_SLOW,
	CMD_BOMB,
	CMD_LONG,
	CMD_SHIELD,
	CMD_SWEEP,
	/* party */
	CMD_DISCO,
	CMD_HYPE,
	CMD_GG,
	CMD_POG,
	CMD_LOL,
	CMD_RIP,
	CMD_LOVE,
	CMD_WOW,
	CMD_HELP,
	/* chat plays */
	CMD_LEFT,
	CMD_RIGHT,
	CMD_ROTATE,
	CMD_CCW,
	CMD_DOWN,
	CMD_DROP,
	CMD_HOLD,
	/* wheel-only outcomes */
	CMD_JACKPOT,
	CMD_NOTHING,
	CMD_COUNT
};

enum class ChatPlaysMode { Off = 0, Anarchy = 1, Democracy = 2 };

/* A message coming from Twitch chat, the HTTP bridge or a test button. */
struct InboxMsg {
	enum Kind { Chat, Sub, GiftSub, MysteryGift, Raid, Follow, Bits };
	Kind kind = Chat;
	std::string user;
	std::string text;
	int amount = 0;
	bool privileged = false;
	bool firstMessage = false;
	bool bypassCooldown = false;
	std::string command; /* bridge: command name without prefix, e.g. "flip" */
};

struct Settings {
	int theme = 0;
	bool showGrid = true;
	bool showGhost = true;
	bool transparent = false;
	bool showPanels = true;
	int startLevel = 1;
	float autoRestart = 8.0f;
	float autopilotIdle = 0.0f;
	float effectScale = 1.0f;

	bool chatEnabled = true;
	std::string prefix = "!";
	float userCooldown = 10.0f;
	float globalCooldown = 20.0f;
	bool privilegedBypass = true;
	bool allowTroll = true;
	bool allowHelper = true;
	bool allowFun = true;
	bool plainReactions = true;
	bool welcomeFirst = true;
	int bitsThreshold = 100;
	int chaosThreshold = 30;
	ChatPlaysMode chatPlays = ChatPlaysMode::Off;
	float voteSeconds = 4.0f;
};

struct FeedItem {
	std::string text;
	uint32_t color;
	float age;
};

struct Popup {
	std::string text;
	float x, y; /* board cell coordinates */
	float age, life;
	uint32_t color;
	float scale;
};

struct Particle {
	float x, y, vx, vy;
	float age, life, size, rot, vrot;
	uint32_t color;
	int type = -1; /* >= 0: use the theme colour of this piece type */
};

struct Bubble {
	std::string text;
	std::string user;
	float x;
	float age, life;
	uint32_t color;
};

struct Banner {
	std::string title;
	std::string subtitle;
	uint32_t color;
	float age, life;
};

struct Wheel {
	bool active = false;
	float age = 0.0f;
	float duration = 4.0f;
	float tickTimer = 0.0f;
	float tickInterval = 0.05f;
	int shown = 0;
	int result = 0;
	bool resolved = false;
	std::string by;
	std::vector<int> options;
};

struct ViewerStats {
	int help = 0;
	int troll = 0;
	int fun = 0;
};

struct RankEntry {
	std::string name;
	int points;
};

class Game {
public:
	Game();

	void configure(const Settings &s);
	void reset();
	void tick(float dt);

	void press(Action a, bool down);
	void handle(const InboxMsg &msg);
	void panic();
	void spin(const std::string &by) { spinWheel(by); }

	/* -------- state read by the renderer -------- */
	Settings cfg;
	std::array<Cell, BoardW * BoardH> board{};
	Piece current;
	std::deque<Piece> queue;
	Piece hold;
	bool hasHold = false;
	bool holdUsed = false;
	bool paused = false;
	bool gameOver = false;
	bool chatMuted = false;
	bool autopilot = false;
	bool autopilotFromIdle = false;
	bool newBest = false;

	int64_t score = 0;
	int64_t best = 0;
	int lines = 0;
	int level = 1;
	int combo = -1;
	int shields = 0;
	int gamesPlayed = 0;
	float elapsed = 0.0f;
	float time = 0.0f; /* monotonic animation clock */
	float gameOverTimer = 0.0f;

	std::array<float, FX_COUNT> fx{};
	std::array<float, FX_COUNT> fxMax{};
	float flipAnim = 0.0f; /* 0..1 eased board rotation */
	float kick = 0.0f;     /* hard-drop bounce */
	float shakeImpulse = 0.0f;
	std::array<float, BoardH> rowFlash{};

	float hype = 0.0f; /* 0..1 */
	int chaos = 0;     /* counts up to cfg.chaosThreshold */
	float helpPower = 0.0f;
	float trollPower = 0.0f;
	float helpCard = 0.0f; /* seconds left to show the command card */

	std::deque<FeedItem> feed;
	std::vector<Popup> popups;
	std::vector<Particle> particles; /* board space */
	std::vector<Particle> confetti;  /* normalised screen space */
	std::vector<Bubble> bubbles;
	std::deque<Banner> banners;
	Wheel wheel;

	std::map<std::string, ViewerStats> viewers;
	std::vector<RankEntry> topHelpers;
	std::vector<RankEntry> topTrolls;

	/* chat plays */
	std::array<int, 7> votes{};
	std::map<std::string, bool> voters;
	float voteTimer = 0.0f;
	std::string lastVoteResult;
	float lastVoteResultAge = 99.0f;

	std::string chatStatus; /* written by the source */
	bool chatConnected = false;

	int ghostY() const;
	float fxRemaining(int effect) const { return fx[size_t(effect)]; }
	bool fxActive(int effect) const { return fx[size_t(effect)] > 0.0f; }
	static const char *commandLabel(int cmd);
	static Team commandTeam(int cmd);

private:
	std::mt19937 rng;
	std::vector<int> bag;
	std::array<bool, ActionCount> held{};
	std::array<float, ActionCount> repeatTimer{};
	float fallTimer = 0.0f;
	float lockTimer = 0.0f;
	int lockResets = 0;
	float idleTimer = 0.0f;
	float reactionGate = 0.0f;
	std::map<std::string, float> userCooldowns;
	bool ranksDirty = false;
	std::array<float, CMD_COUNT> globalCooldowns{};
	std::string lastMysteryGifter;
	float lastMysteryGiftAge = 99.0f;

	/* autopilot */
	bool aiPlanned = false;
	int aiRot = 0;
	int aiX = 0;
	float aiTimer = 0.0f;
	int aiSteps = 0;

	int nextFromBag();
	Piece makePiece(int type) const;
	void refillQueue();
	void spawn();
	bool collides(const Piece &p) const;
	bool tryMove(int dx, int dy);
	bool tryRotate(int dir);
	void softDrop(bool manual);
	void hardDrop();
	void lockPiece();
	void doHold();
	int clearLines(int *goldenRows);
	void explode(int cx, int cy);
	void addGarbage(int rows);
	void sweepRow();
	void topOut();
	void onLineClear(int cleared, int goldenRows, bool perfect);
	float fallInterval() const;
	void resetLock();

	void doAction(Action a, bool fromHuman);
	void stepRepeats(float dt);
	void stepAutopilot(float dt);
	void planAutopilot();

	void handleChat(const InboxMsg &msg);
	bool applyCommand(int cmd, const std::string &user);
	void startEffect(int effect, float seconds);
	void addFeed(const std::string &text, uint32_t color);
	void addBanner(const std::string &title, const std::string &subtitle, uint32_t color, float life = 3.5f);
	void addPopup(const std::string &text, uint32_t color, float scale = 1.0f);
	void addBubble(const std::string &text, const std::string &user, uint32_t color);
	void burst(float x, float y, uint32_t color, int count, float speed, int type = -1);
	void confettiBurst(int count);
	void credit(const std::string &user, Team team, int points);
	void rebuildRanks();
	void addChaos(int amount, const std::string &by);
	void addHype(float amount);
	void spinWheel(const std::string &by);
	void stepWheel(float dt);
	void stepVotes(float dt);
	void stepFx(float dt);
	std::string pick(std::initializer_list<const char *> lines);
	float rand01();
};

std::string sanitizeName(const std::string &name);

} // namespace sp
