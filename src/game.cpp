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

#include "game.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>

namespace sp {

namespace {

constexpr uint32_t ColTroll = 0xff6b8bff;
constexpr uint32_t ColHelp = 0x5eea9aff;
constexpr uint32_t ColFun = 0xffc94dff;
constexpr uint32_t ColSys = 0x7dd3fcff;
constexpr uint32_t ColGold = 0xffd34dff;
constexpr uint32_t ColPink = 0xff7ad9ff;

constexpr float LockDelay = 0.5f;
constexpr int MaxLockResets = 15;
constexpr float DasDelay = 0.16f;
constexpr float ArrRate = 0.05f;
constexpr float SoftDropRate = 0.035f;
constexpr size_t MaxParticles = 1500;
constexpr size_t MaxConfetti = 700;
constexpr size_t MaxFeed = 6;
constexpr size_t MaxQueue = 9;

/* ------------------------------------------------------------------ */
/* Piece shapes                                                        */
/* ------------------------------------------------------------------ */

struct ShapeTable {
	int size[PIECE_COUNT];
	int count[PIECE_COUNT];
	Offset cells[PIECE_COUNT][4][5];
};

const ShapeTable &shapes()
{
	static const ShapeTable table = [] {
		static const char *const defs[PIECE_COUNT][4] = {
			{"....", "####", "....", "...."}, /* I */
			{".#.", "###", "...", nullptr},   /* T */
			{"##", "##", nullptr, nullptr},   /* O */
			{".##", "##.", "...", nullptr},   /* S */
			{"##.", ".##", "...", nullptr},   /* Z */
			{"#..", "###", "...", nullptr},   /* J */
			{"..#", "###", "...", nullptr},   /* L */
			{".#.", "###", ".#.", nullptr},   /* plus */
			{"#.#", "###", "...", nullptr},   /* U */
			{"#..", "##.", ".##", nullptr},   /* W */
			{"###", ".#.", ".#.", nullptr},   /* big T */
			{"#", nullptr, nullptr, nullptr}, /* bomb */
		};
		ShapeTable t{};
		for (int p = 0; p < PIECE_COUNT; ++p) {
			const int n = int(std::strlen(defs[p][0]));
			Offset base[5] = {};
			int c = 0;
			for (int y = 0; y < n; ++y)
				for (int x = 0; x < n; ++x)
					if (defs[p][y][x] == '#' && c < 5)
						base[c++] = {x, y};
			t.size[p] = n;
			t.count[p] = c;
			for (int r = 0; r < 4; ++r) {
				for (int i = 0; i < c; ++i) {
					Offset o = base[i];
					for (int k = 0; k < r; ++k)
						o = {n - 1 - o.y, o.x};
					t.cells[p][r][i] = o;
				}
			}
		}
		return t;
	}();
	return table;
}

/* ------------------------------------------------------------------ */
/* Command table                                                       */
/* ------------------------------------------------------------------ */

struct CommandDef {
	const char *names;
	const char *label;
	Team team;
	float cooldownScale;
};

const CommandDef Commands[CMD_COUNT] = {
	{"flip|upsidedown|australia", "FLIP", Team::Troll, 1.5f},
	{"mirror|swap|opposite", "MIRROR", Team::Troll, 1.2f},
	{"turbo|fast|speed|nitro", "TURBO", Team::Troll, 1.0f},
	{"garbage|trash|junk", "GARBAGE", Team::Troll, 1.0f},
	{"dark|lightsout|lights", "LIGHTS OUT", Team::Troll, 1.5f},
	{"shake|quake|earthquake", "QUAKE", Team::Troll, 0.6f},
	{"wobble|jelly|wiggle", "WOBBLE", Team::Troll, 1.0f},
	{"curse|cursed|weird", "CURSE", Team::Troll, 1.0f},
	{"slow|chill|chillpill", "CHILL PILL", Team::Helper, 1.0f},
	{"bomb|boom|kaboom", "BOMB", Team::Helper, 1.2f},
	{"long|line|straight|longboi", "LONG ONE", Team::Helper, 0.8f},
	{"shield|protect", "SHIELD", Team::Helper, 1.5f},
	{"sweep|clean|broom", "SWEEP", Team::Helper, 2.5f},
	{"disco|party|rave", "DISCO", Team::Fun, 1.0f},
	{"hype", "HYPE", Team::Fun, 0.0f},
	{"gg", "GG", Team::Fun, 0.0f},
	{"pog|poggers", "POG", Team::Fun, 0.0f},
	{"lol|lmao|kekw", "LOL", Team::Fun, 0.0f},
	{"rip|f", "RIP", Team::Fun, 0.0f},
	{"love|heart|<3", "<3", Team::Fun, 0.0f},
	{"wow|omg", "WOW", Team::Fun, 0.0f},
	{"sp|stackparty|stackhelp", "HELP", Team::Fun, 1.5f},
	{"left|l", "LEFT", Team::Control, 0.0f},
	{"right|r", "RIGHT", Team::Control, 0.0f},
	{"rotate|rot|cw|turn|up", "ROTATE", Team::Control, 0.0f},
	{"ccw", "ROTATE CCW", Team::Control, 0.0f},
	{"down|d", "DOWN", Team::Control, 0.0f},
	{"drop|slam", "DROP", Team::Control, 0.0f},
	{"hold|h", "HOLD", Team::Control, 0.0f},
	{"", "JACKPOT", Team::System, 0.0f},
	{"", "NOTHING", Team::System, 0.0f},
};

const std::unordered_map<std::string, int> &commandLookup()
{
	static const std::unordered_map<std::string, int> lookup = [] {
		std::unordered_map<std::string, int> m;
		for (int i = 0; i < CMD_COUNT; ++i) {
			std::string names = Commands[i].names;
			size_t start = 0;
			while (start < names.size()) {
				size_t end = names.find('|', start);
				if (end == std::string::npos)
					end = names.size();
				if (end > start)
					m.emplace(names.substr(start, end - start), i);
				start = end + 1;
			}
		}
		return m;
	}();
	return lookup;
}

bool isReaction(int cmd)
{
	return cmd >= CMD_GG && cmd <= CMD_WOW;
}

Action controlAction(int cmd)
{
	switch (cmd) {
	case CMD_LEFT:
		return Action::Left;
	case CMD_RIGHT:
		return Action::Right;
	case CMD_ROTATE:
		return Action::RotateCW;
	case CMD_CCW:
		return Action::RotateCCW;
	case CMD_DOWN:
		return Action::SoftDrop;
	case CMD_DROP:
		return Action::HardDrop;
	default:
		return Action::Hold;
	}
}

std::string toLower(std::string s)
{
	for (char &c : s)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0;
	size_t b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

std::vector<std::string> splitWords(const std::string &s, size_t maxWords)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size() && out.size() < maxWords) {
		while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
			++i;
		size_t j = i;
		while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])))
			++j;
		if (j > i)
			out.push_back(s.substr(i, j - i));
		i = j;
	}
	return out;
}

std::string withUser(const char *pattern, const std::string &user)
{
	std::string out = pattern;
	const size_t pos = out.find("%s");
	if (pos != std::string::npos)
		out.replace(pos, 2, "@" + user);
	return out;
}

/* Plain chat messages such as "gg" or "lol" become floating reactions */
int plainReaction(const std::string &lower)
{
	if (lower.size() > 16)
		return -1;
	const std::vector<std::string> words = splitWords(lower, 2);
	if (words.empty())
		return -1;
	const std::string &w = words[0];
	if (w == "gg" || w == "ggs" || w == "ggwp")
		return CMD_GG;
	if (w == "pog" || w == "poggers" || w == "pogchamp" || w == "pogu")
		return CMD_POG;
	if (w == "lol" || w == "lmao" || w == "kekw" || w == "lul" || w == "lmfao" || w == "xd")
		return CMD_LOL;
	if (w == "rip" || (w == "f" && words.size() == 1))
		return CMD_RIP;
	if (w == "<3" || w == "<333")
		return CMD_LOVE;
	if (w == "wow" || w == "omg" || w == "woah" || w == "whoa")
		return CMD_WOW;
	return -1;
}

const uint32_t ConfettiColors[] = {0xff5c8aff, 0xffd34dff, 0x5eea9aff, 0x6cc6ffff, 0xc084fcff, 0xff9f43ff, 0xffffffff};

} // namespace

/* ------------------------------------------------------------------ */
/* Public helpers                                                      */
/* ------------------------------------------------------------------ */

int pieceCells(int type, int rot, Offset out[5])
{
	const ShapeTable &t = shapes();
	const int c = t.count[type];
	for (int i = 0; i < c; ++i)
		out[i] = t.cells[type][rot & 3][i];
	return c;
}

bool isCursed(int type)
{
	return type >= PIECE_PLUS && type <= PIECE_BIGT;
}

std::string sanitizeName(const std::string &name)
{
	std::string out;
	for (char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '_')
			out.push_back(char(std::toupper(u)));
		if (out.size() >= 15)
			break;
	}
	return out.empty() ? std::string("VIEWER") : out;
}

const char *Game::commandLabel(int cmd)
{
	if (cmd < 0 || cmd >= CMD_COUNT)
		return "?";
	return Commands[cmd].label;
}

Team Game::commandTeam(int cmd)
{
	if (cmd < 0 || cmd >= CMD_COUNT)
		return Team::System;
	return Commands[cmd].team;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

Game::Game() : rng(std::random_device{}())
{
	reset();
	gamesPlayed = 0;
}

void Game::configure(const Settings &s)
{
	const bool wasDemocracy = cfg.chatPlays == ChatPlaysMode::Democracy;
	cfg = s;
	cfg.startLevel = std::clamp(cfg.startLevel, 1, 20);
	cfg.voteSeconds = std::clamp(cfg.voteSeconds, 1.0f, 30.0f);
	cfg.effectScale = std::clamp(cfg.effectScale, 0.25f, 3.0f);
	cfg.prefix = toLower(trim(cfg.prefix));
	if (cfg.prefix.empty())
		cfg.prefix = "!";
	if (wasDemocracy && cfg.chatPlays != ChatPlaysMode::Democracy) {
		votes.fill(0);
		voters.clear();
	}
	if (cfg.chaosThreshold <= 0)
		chaos = 0;
	level = std::max(level, cfg.startLevel);
}

void Game::reset()
{
	board.fill(Cell{});
	queue.clear();
	bag.clear();
	hasHold = false;
	holdUsed = false;
	score = 0;
	lines = 0;
	level = cfg.startLevel;
	combo = -1;
	elapsed = 0.0f;
	paused = false;
	gameOver = false;
	newBest = false;
	gameOverTimer = 0.0f;
	fallTimer = 0.0f;
	lockTimer = 0.0f;
	lockResets = 0;
	shields = 0;
	for (int i = 0; i < FX_COUNT; ++i) {
		if (i != FX_CHILL)
			fx[size_t(i)] = 0.0f;
	}
	rowFlash.fill(0.0f);
	aiPlanned = false;
	idleTimer = 0.0f;
	if (autopilotFromIdle) {
		autopilot = false;
		autopilotFromIdle = false;
	}
	++gamesPlayed;
	refillQueue();
	spawn();
}

float Game::rand01()
{
	return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

std::string Game::pick(std::initializer_list<const char *> options)
{
	const size_t n = options.size();
	const size_t idx = std::uniform_int_distribution<size_t>(0, n - 1)(rng);
	return *(options.begin() + idx);
}

/* ------------------------------------------------------------------ */
/* Pieces & board                                                      */
/* ------------------------------------------------------------------ */

int Game::nextFromBag()
{
	if (bag.empty()) {
		for (int i = 0; i < ClassicPieceCount; ++i)
			bag.push_back(i);
		std::shuffle(bag.begin(), bag.end(), rng);
	}
	const int v = bag.back();
	bag.pop_back();
	return v;
}

Piece Game::makePiece(int type) const
{
	Piece p;
	p.type = type;
	p.rot = 0;
	p.x = (BoardW - shapes().size[type]) / 2;
	p.y = -1;
	p.golden = false;
	return p;
}

void Game::refillQueue()
{
	while (queue.size() < size_t(QueueSize))
		queue.push_back(makePiece(nextFromBag()));
}

void Game::spawn()
{
	refillQueue();
	const Piece next = queue.front();
	queue.pop_front();
	refillQueue();
	current = makePiece(next.type);
	current.golden = next.golden;
	holdUsed = false;
	fallTimer = 0.0f;
	lockTimer = 0.0f;
	lockResets = 0;
	aiPlanned = false;
	aiSteps = 0;
	if (collides(current))
		topOut();
}

bool Game::collides(const Piece &p) const
{
	Offset cells[5];
	const int n = pieceCells(p.type, p.rot, cells);
	for (int i = 0; i < n; ++i) {
		const int bx = p.x + cells[i].x;
		const int by = p.y + cells[i].y;
		if (bx < 0 || bx >= BoardW || by >= BoardH)
			return true;
		if (by >= 0 && board[size_t(by * BoardW + bx)].kind != CELL_EMPTY)
			return true;
	}
	return false;
}

int Game::ghostY() const
{
	Piece g = current;
	for (;;) {
		Piece n = g;
		n.y += 1;
		if (collides(n))
			return g.y;
		g = n;
	}
}

void Game::resetLock()
{
	Piece below = current;
	below.y += 1;
	if (collides(below) && lockResets < MaxLockResets) {
		lockTimer = 0.0f;
		++lockResets;
	}
}

bool Game::tryMove(int dx, int dy)
{
	if (paused || gameOver)
		return false;
	Piece m = current;
	m.x += dx;
	m.y += dy;
	if (collides(m))
		return false;
	current = m;
	if (dx != 0)
		resetLock();
	return true;
}

bool Game::tryRotate(int dir)
{
	if (paused || gameOver)
		return false;
	if (current.type == PIECE_O || current.type == PIECE_BOMB || current.type == PIECE_PLUS)
		return true;
	static const Offset kicks[] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-2, 0}, {2, 0}, {-1, -1}, {1, -1}};
	Piece r = current;
	r.rot = (r.rot + dir + 4) & 3;
	for (const Offset &k : kicks) {
		Piece t = r;
		t.x += k.x;
		t.y += k.y;
		if (!collides(t)) {
			current = t;
			resetLock();
			return true;
		}
	}
	return false;
}

void Game::softDrop(bool manual)
{
	if (tryMove(0, 1)) {
		if (manual)
			score += 1;
		fallTimer = 0.0f;
	}
}

void Game::hardDrop()
{
	if (paused || gameOver)
		return;
	int dist = 0;
	while (tryMove(0, 1))
		++dist;
	score += 2 * dist;
	kick = 1.0f;
	lockPiece();
}

void Game::doHold()
{
	if (holdUsed || paused || gameOver)
		return;
	if (!hasHold) {
		hold = makePiece(current.type);
		hold.golden = current.golden;
		hasHold = true;
		spawn();
	} else {
		const Piece stored = hold;
		hold = makePiece(current.type);
		hold.golden = current.golden;
		current = makePiece(stored.type);
		current.golden = stored.golden;
		fallTimer = 0.0f;
		lockTimer = 0.0f;
		lockResets = 0;
		aiPlanned = false;
		if (collides(current))
			topOut();
	}
	holdUsed = true;
}

void Game::lockPiece()
{
	Offset cells[5];
	const int n = pieceCells(current.type, current.rot, cells);

	if (current.type == PIECE_BOMB) {
		explode(current.x + cells[0].x, current.y + cells[0].y);
		if (!gameOver)
			spawn();
		return;
	}

	bool above = false;
	for (int i = 0; i < n; ++i) {
		const int bx = current.x + cells[i].x;
		const int by = current.y + cells[i].y;
		if (by < 0) {
			above = true;
			continue;
		}
		Cell &c = board[size_t(by * BoardW + bx)];
		c.kind = current.golden ? CELL_GOLD : CELL_BLOCK;
		c.type = uint8_t(current.type);
	}
	kick = std::max(kick, 0.45f);

	int goldenRows = 0;
	const int cleared = clearLines(&goldenRows);
	bool perfect = cleared > 0;
	if (perfect) {
		for (const Cell &c : board) {
			if (c.kind != CELL_EMPTY) {
				perfect = false;
				break;
			}
		}
	}
	onLineClear(cleared, goldenRows, perfect);

	if (above && cleared == 0) {
		topOut();
		return;
	}
	spawn();
}

int Game::clearLines(int *goldenRows)
{
	std::array<bool, BoardH> clear{};
	int count = 0;
	int forced = 0;
	for (int y = 0; y < BoardH; ++y) {
		bool full = true;
		bool gold = false;
		for (int x = 0; x < BoardW; ++x) {
			const Cell &c = board[size_t(y * BoardW + x)];
			full = full && c.kind != CELL_EMPTY;
			gold = gold || c.kind == CELL_GOLD;
		}
		if (full || gold) {
			clear[size_t(y)] = true;
			++count;
			if (!full)
				++forced;
		}
	}
	if (goldenRows)
		*goldenRows = forced;
	if (count == 0)
		return 0;

	for (int y = 0; y < BoardH; ++y) {
		if (!clear[size_t(y)])
			continue;
		rowFlash[size_t(y)] = 1.0f;
		for (int x = 0; x < BoardW; ++x) {
			const Cell &c = board[size_t(y * BoardW + x)];
			if (c.kind == CELL_EMPTY)
				continue;
			if (c.kind == CELL_GOLD)
				burst(float(x) + 0.5f, float(y) + 0.5f, ColGold, 7, 6.0f);
			else if (c.kind == CELL_GARBAGE)
				burst(float(x) + 0.5f, float(y) + 0.5f, 0x9aa3b2ffu, 3, 6.0f);
			else
				burst(float(x) + 0.5f, float(y) + 0.5f, 0u, 4, 6.0f, c.type);
		}
	}

	std::array<Cell, BoardW * BoardH> next{};
	int dst = BoardH - 1;
	for (int y = BoardH - 1; y >= 0; --y) {
		if (clear[size_t(y)])
			continue;
		for (int x = 0; x < BoardW; ++x)
			next[size_t(dst * BoardW + x)] = board[size_t(y * BoardW + x)];
		--dst;
	}
	board = next;
	return count;
}

void Game::onLineClear(int cleared, int goldenRows, bool perfect)
{
	if (cleared <= 0) {
		combo = -1;
		return;
	}
	++combo;
	static const int points[] = {0, 100, 300, 500, 800, 1200, 1600};
	const int mult = level * (fxActive(FX_HYPE) ? 2 : 1);
	score += int64_t(points[std::min(cleared, 6)]) * mult;
	if (combo > 0)
		score += int64_t(50) * combo * mult;
	if (perfect)
		score += int64_t(2000) * mult;
	score += int64_t(250) * goldenRows * mult;

	lines += cleared;
	const int newLevel = std::min(99, cfg.startLevel + lines / 10);
	if (newLevel > level) {
		level = newLevel;
		addPopup("LEVEL UP!", ColSys, 1.0f);
	}

	if (goldenRows > 0) {
		addPopup("GOLDEN CLEAR!", ColGold, 1.2f);
		confettiBurst(80);
	}
	switch (std::min(cleared, 4)) {
	case 1:
		if (goldenRows == 0)
			addPopup(pick({"NICE", "CLEAN", "TIDY"}), 0xffffffff, 0.8f);
		break;
	case 2:
		addPopup("DOUBLE!", ColHelp, 1.0f);
		break;
	case 3:
		addPopup("TRIPLE!!", ColFun, 1.15f);
		shakeImpulse = std::max(shakeImpulse, 0.4f);
		break;
	default:
		addPopup("QUAD STACK!!!", ColPink, 1.35f);
		shakeImpulse = std::max(shakeImpulse, 0.8f);
		confettiBurst(90);
		break;
	}
	if (combo >= 1)
		addPopup("COMBO X" + std::to_string(combo + 1), ColSys, 0.9f);
	if (perfect) {
		addPopup("PERFECT CLEAR!!!", ColGold, 1.4f);
		confettiBurst(200);
	}
	addHype(0.02f * float(cleared));
}

void Game::explode(int cx, int cy)
{
	int destroyed = 0;
	for (int y = cy - 2; y <= cy + 2; ++y) {
		for (int x = cx - 2; x <= cx + 2; ++x) {
			if (x < 0 || x >= BoardW || y < 0 || y >= BoardH)
				continue;
			if (std::abs(x - cx) + std::abs(y - cy) > 2)
				continue;
			Cell &c = board[size_t(y * BoardW + x)];
			if (c.kind != CELL_EMPTY) {
				++destroyed;
				if (c.kind == CELL_BLOCK)
					burst(float(x) + 0.5f, float(y) + 0.5f, 0u, 5, 9.0f, c.type);
				else
					burst(float(x) + 0.5f, float(y) + 0.5f,
					      c.kind == CELL_GOLD ? ColGold : 0x9aa3b2ffu, 5, 9.0f);
			}
			c = Cell{};
		}
	}
	burst(float(cx) + 0.5f, float(cy) + 0.5f, 0xffb347ff, 40, 14.0f);
	burst(float(cx) + 0.5f, float(cy) + 0.5f, 0xfff1a8ff, 20, 7.0f);
	shakeImpulse = 1.0f;
	kick = 1.0f;
	score += int64_t(25) * destroyed * level;
	addPopup(destroyed > 6 ? "MEGA KABOOM!" : "KABOOM!", 0xffb347ff, 1.25f);
}

void Game::addGarbage(int rows)
{
	for (int r = 0; r < rows && !gameOver; ++r) {
		bool overflow = false;
		for (int x = 0; x < BoardW; ++x)
			overflow = overflow || board[size_t(x)].kind != CELL_EMPTY;
		for (int y = 0; y < BoardH - 1; ++y)
			for (int x = 0; x < BoardW; ++x)
				board[size_t(y * BoardW + x)] = board[size_t((y + 1) * BoardW + x)];
		const int hole = std::uniform_int_distribution<int>(0, BoardW - 1)(rng);
		for (int x = 0; x < BoardW; ++x) {
			Cell &c = board[size_t((BoardH - 1) * BoardW + x)];
			c.kind = x == hole ? CELL_EMPTY : CELL_GARBAGE;
			c.type = 0;
		}
		if (collides(current)) {
			current.y -= 1;
			if (collides(current))
				overflow = true;
		}
		if (overflow)
			topOut();
	}
	aiPlanned = false;
	kick = std::max(kick, 0.6f);
}

void Game::sweepRow()
{
	for (int x = 0; x < BoardW; ++x) {
		const Cell &c = board[size_t((BoardH - 1) * BoardW + x)];
		if (c.kind != CELL_EMPTY) {
			burst(float(x) + 0.5f, float(BoardH) - 0.5f, ColHelp, 4, 6.0f);
		}
	}
	for (int y = BoardH - 1; y > 0; --y)
		for (int x = 0; x < BoardW; ++x)
			board[size_t(y * BoardW + x)] = board[size_t((y - 1) * BoardW + x)];
	for (int x = 0; x < BoardW; ++x)
		board[size_t(x)] = Cell{};
	rowFlash[size_t(BoardH - 1)] = 1.0f;
	aiPlanned = false;
}

void Game::topOut()
{
	if (gameOver)
		return;
	gameOver = true;
	gameOverTimer = 0.0f;
	if (score > best) {
		best = score;
		newBest = score > 0;
	}
	shakeImpulse = std::max(shakeImpulse, 0.6f);
	addFeed("GAME OVER - SCORE " + std::to_string(score), ColTroll);
	if (newBest) {
		addBanner("NEW HIGH SCORE!", std::to_string(score) + " POINTS", ColGold, 4.0f);
		confettiBurst(220);
	}
}

float Game::fallInterval() const
{
	float base = 0.8f * std::pow(0.83f, float(level - 1));
	base = std::max(base, 0.04f);
	if (fxActive(FX_TURBO))
		base = std::max(base * 0.22f, 0.025f);
	if (fxActive(FX_SLOW))
		base *= 2.6f;
	return base;
}

/* ------------------------------------------------------------------ */
/* Simulation                                                          */
/* ------------------------------------------------------------------ */

void Game::stepFx(float dt)
{
	for (size_t i = 0; i < size_t(FX_COUNT); ++i)
		fx[i] = std::max(0.0f, fx[i] - dt);

	const float flipTarget = fxActive(FX_FLIP) ? 1.0f : 0.0f;
	flipAnim += (flipTarget - flipAnim) * std::min(1.0f, dt * 6.0f);
	kick = std::max(0.0f, kick - dt * 5.0f);
	shakeImpulse = std::max(0.0f, shakeImpulse - dt * 1.8f);
	for (float &f : rowFlash)
		f = std::max(0.0f, f - dt * 2.5f);
	hype = std::max(0.0f, hype - dt * 0.006f);
	const float decay = std::exp(-dt * 0.04f);
	helpPower *= decay;
	trollPower *= decay;
	helpCard = std::max(0.0f, helpCard - dt);
}

void Game::tick(float dt)
{
	dt = std::clamp(dt, 0.0f, 0.1f);
	time += dt;

	stepFx(dt);

	/* visual particles keep running even when paused */
	for (Particle &p : particles) {
		p.age += dt;
		p.vy += 28.0f * dt;
		p.vx *= std::exp(-dt * 1.5f);
		p.x += p.vx * dt;
		p.y += p.vy * dt;
		p.rot += p.vrot * dt;
	}
	particles.erase(std::remove_if(particles.begin(), particles.end(),
				       [](const Particle &p) { return p.age >= p.life; }),
			particles.end());
	for (Particle &p : confetti) {
		p.age += dt;
		p.vy += 0.35f * dt;
		p.vx *= std::exp(-dt * 0.8f);
		p.x += (p.vx + std::sin(p.age * 3.0f + p.size * 40.0f) * 0.04f) * dt;
		p.y += p.vy * dt;
		p.rot += p.vrot * dt;
	}
	confetti.erase(std::remove_if(confetti.begin(), confetti.end(),
				      [](const Particle &p) { return p.age >= p.life || p.y > 1.1f; }),
		       confetti.end());
	for (Popup &p : popups) {
		p.age += dt;
		p.y -= dt * 1.4f;
	}
	popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup &p) { return p.age >= p.life; }),
		     popups.end());
	for (Bubble &b : bubbles)
		b.age += dt;
	bubbles.erase(std::remove_if(bubbles.begin(), bubbles.end(), [](const Bubble &b) { return b.age >= b.life; }),
		      bubbles.end());
	if (!banners.empty()) {
		banners.front().age += dt;
		if (banners.front().age >= banners.front().life)
			banners.pop_front();
	}
	for (FeedItem &f : feed)
		f.age += dt;
	while (!feed.empty() && feed.front().age > 16.0f)
		feed.pop_front();

	reactionGate -= dt;
	lastMysteryGiftAge += dt;
	lastVoteResultAge += dt;
	for (float &c : globalCooldowns)
		c = std::max(0.0f, c - dt);
	for (auto it = userCooldowns.begin(); it != userCooldowns.end();) {
		it->second -= dt;
		if (it->second <= 0.0f)
			it = userCooldowns.erase(it);
		else
			++it;
	}
	if (ranksDirty) {
		rebuildRanks();
		ranksDirty = false;
	}

	stepWheel(dt);

	if (gameOver) {
		gameOverTimer += dt;
		if (cfg.autoRestart > 0.0f && gameOverTimer >= cfg.autoRestart)
			reset();
		return;
	}
	if (paused)
		return;

	elapsed += dt;
	idleTimer += dt;
	if (cfg.autopilotIdle > 0.0f && !autopilot && idleTimer >= cfg.autopilotIdle) {
		autopilot = true;
		autopilotFromIdle = true;
		aiPlanned = false;
		addFeed("AUTOPILOT ON - CHAT, GO TROLL THE BOT!", ColSys);
	}

	stepVotes(dt);
	stepRepeats(dt);
	if (gameOver)
		return;
	if (autopilot)
		stepAutopilot(dt);
	if (gameOver)
		return;

	fallTimer += dt;
	const float interval = fallInterval();
	int guard = 0;
	while (fallTimer >= interval && guard++ < 25) {
		fallTimer -= interval;
		if (!tryMove(0, 1)) {
			fallTimer = 0.0f;
			break;
		}
	}

	Piece below = current;
	below.y += 1;
	if (collides(below)) {
		lockTimer += dt;
		if (lockTimer >= (fxActive(FX_TURBO) ? LockDelay * 0.6f : LockDelay))
			lockPiece();
	} else {
		lockTimer = 0.0f;
	}
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

void Game::press(Action a, bool down)
{
	const size_t idx = size_t(a);
	held[idx] = down;
	if (!down)
		return;
	if (a == Action::Left || a == Action::Right)
		repeatTimer[idx] = DasDelay;
	else if (a == Action::SoftDrop)
		repeatTimer[idx] = SoftDropRate;

	idleTimer = 0.0f;
	const bool gameplay = a == Action::Left || a == Action::Right || a == Action::SoftDrop ||
			      a == Action::RotateCW || a == Action::RotateCCW || a == Action::HardDrop ||
			      a == Action::Hold;
	if (gameplay && autopilot) {
		autopilot = false;
		autopilotFromIdle = false;
		addFeed("STREAMER TOOK BACK CONTROL", ColSys);
	}
	doAction(a, true);
}

void Game::doAction(Action a, bool fromHuman)
{
	if (fxActive(FX_MIRROR)) {
		if (a == Action::Left)
			a = Action::Right;
		else if (a == Action::Right)
			a = Action::Left;
		else if (a == Action::RotateCW)
			a = Action::RotateCCW;
		else if (a == Action::RotateCCW)
			a = Action::RotateCW;
	}

	if (gameOver && fromHuman && a == Action::HardDrop) {
		reset();
		return;
	}

	switch (a) {
	case Action::Left:
		tryMove(-1, 0);
		break;
	case Action::Right:
		tryMove(1, 0);
		break;
	case Action::SoftDrop:
		softDrop(fromHuman);
		break;
	case Action::RotateCW:
		tryRotate(1);
		break;
	case Action::RotateCCW:
		tryRotate(-1);
		break;
	case Action::HardDrop:
		hardDrop();
		break;
	case Action::Hold:
		doHold();
		break;
	case Action::Pause:
		if (!gameOver)
			paused = !paused;
		break;
	case Action::Reset:
		reset();
		break;
	case Action::Autopilot:
		autopilot = !autopilot;
		autopilotFromIdle = false;
		aiPlanned = false;
		addFeed(autopilot ? "AUTOPILOT ON - CHAT, GO TROLL THE BOT!" : "AUTOPILOT OFF", ColSys);
		break;
	case Action::Panic:
		panic();
		break;
	case Action::ToggleChat:
		chatMuted = !chatMuted;
		addFeed(chatMuted ? "CHAT EFFECTS PAUSED" : "CHAT EFFECTS ARE BACK!", ColSys);
		break;
	default:
		break;
	}
}

void Game::stepRepeats(float dt)
{
	static const Action repeatable[] = {Action::Left, Action::Right, Action::SoftDrop};
	for (Action a : repeatable) {
		const size_t idx = size_t(a);
		if (!held[idx])
			continue;
		repeatTimer[idx] -= dt;
		int guard = 0;
		while (repeatTimer[idx] <= 0.0f && guard++ < 10) {
			doAction(a, true);
			repeatTimer[idx] += a == Action::SoftDrop ? SoftDropRate : ArrRate;
		}
		if (gameOver)
			return;
	}
}

void Game::panic()
{
	for (int i = 0; i < FX_COUNT; ++i)
		fx[size_t(i)] = 0.0f;
	fx[FX_CHILL] = 60.0f;
	fxMax[FX_CHILL] = 60.0f;
	wheel.active = false;
	votes.fill(0);
	voters.clear();
	addBanner("CHILL MODE", "CHAT EFFECTS PAUSED FOR 60S", ColSys, 3.0f);
	addFeed("STREAMER PRESSED THE CHILL BUTTON", ColSys);
}

/* ------------------------------------------------------------------ */
/* Autopilot                                                           */
/* ------------------------------------------------------------------ */

void Game::planAutopilot()
{
	double bestScore = -1e18;
	int bestRot = current.rot;
	int bestX = current.x;
	const int rotations = (current.type == PIECE_O || current.type == PIECE_BOMB || current.type == PIECE_PLUS) ? 1
														    : 4;

	for (int r = 0; r < rotations; ++r) {
		for (int x = -3; x < BoardW + 1; ++x) {
			Piece p = current;
			p.rot = (current.rot + r) & 3;
			p.x = x;
			if (collides(p))
				continue;
			while (true) {
				Piece n = p;
				n.y += 1;
				if (collides(n))
					break;
				p = n;
			}

			std::array<uint8_t, BoardW * BoardH> grid{};
			for (size_t i = 0; i < grid.size(); ++i)
				grid[i] = board[i].kind != CELL_EMPTY ? 1 : 0;
			Offset cells[5];
			const int n = pieceCells(p.type, p.rot, cells);
			double penalty = 0.0;
			int blasted = 0;
			for (int i = 0; i < n; ++i) {
				const int bx = p.x + cells[i].x;
				const int by = p.y + cells[i].y;
				if (by < 0) {
					penalty += 1000.0;
					continue;
				}
				if (p.type == PIECE_BOMB) {
					for (int yy = by - 2; yy <= by + 2; ++yy)
						for (int xx = bx - 2; xx <= bx + 2; ++xx)
							if (xx >= 0 && xx < BoardW && yy >= 0 && yy < BoardH &&
							    std::abs(xx - bx) + std::abs(yy - by) <= 2 &&
							    grid[size_t(yy * BoardW + xx)]) {
								grid[size_t(yy * BoardW + xx)] = 0;
								++blasted;
							}
				} else {
					grid[size_t(by * BoardW + bx)] = 1;
				}
			}

			int full = 0;
			for (int y = 0; y < BoardH; ++y) {
				bool rowFull = true;
				for (int xx = 0; xx < BoardW && rowFull; ++xx)
					rowFull = grid[size_t(y * BoardW + xx)] != 0;
				if (rowFull) {
					++full;
					for (int yy = y; yy > 0; --yy)
						for (int xx = 0; xx < BoardW; ++xx)
							grid[size_t(yy * BoardW + xx)] =
								grid[size_t((yy - 1) * BoardW + xx)];
					for (int xx = 0; xx < BoardW; ++xx)
						grid[size_t(xx)] = 0;
				}
			}

			int heights[BoardW] = {};
			int holes = 0;
			for (int xx = 0; xx < BoardW; ++xx) {
				bool seen = false;
				for (int y = 0; y < BoardH; ++y) {
					if (grid[size_t(y * BoardW + xx)]) {
						if (!seen)
							heights[xx] = BoardH - y;
						seen = true;
					} else if (seen) {
						++holes;
					}
				}
			}
			int aggregate = 0;
			int bumpiness = 0;
			for (int xx = 0; xx < BoardW; ++xx) {
				aggregate += heights[xx];
				if (xx > 0)
					bumpiness += std::abs(heights[xx] - heights[xx - 1]);
			}
			const double value = -0.510066 * aggregate + 0.760666 * full - 0.35663 * holes -
					     0.184483 * bumpiness + 0.2 * blasted - penalty;
			if (value > bestScore) {
				bestScore = value;
				bestRot = p.rot;
				bestX = p.x;
			}
		}
	}
	aiRot = bestRot;
	aiX = bestX;
	aiPlanned = true;
	aiSteps = 0;
}

void Game::stepAutopilot(float dt)
{
	if (!aiPlanned)
		planAutopilot();
	aiTimer -= dt;
	if (aiTimer > 0.0f)
		return;
	aiTimer = std::max(0.035f, 0.11f - float(level) * 0.004f);
	++aiSteps;
	if (aiSteps > 30) {
		/* the bot is confused (probably mirrored by chat): just slam it */
		doAction(Action::HardDrop, false);
		return;
	}
	if (current.rot != aiRot)
		doAction(Action::RotateCW, false);
	else if (current.x < aiX)
		doAction(Action::Right, false);
	else if (current.x > aiX)
		doAction(Action::Left, false);
	else
		doAction(Action::HardDrop, false);
}

/* ------------------------------------------------------------------ */
/* Chat & events                                                       */
/* ------------------------------------------------------------------ */

void Game::handle(const InboxMsg &msg)
{
	const std::string name = sanitizeName(msg.user);
	switch (msg.kind) {
	case InboxMsg::Chat:
	case InboxMsg::Bits:
		handleChat(msg);
		break;
	case InboxMsg::Sub: {
		const bool resub = msg.amount > 1;
		addBanner(resub ? "RESUB X" + std::to_string(msg.amount) + "!" : "NEW SUB!",
			  "@" + name + " - THANK YOU!", ColGold, 4.0f);
		for (Piece &p : queue) {
			if (!p.golden && p.type != PIECE_BOMB) {
				p.golden = true;
				break;
			}
		}
		shields = std::min(shields + 1, 5);
		addHype(0.25f);
		confettiBurst(140);
		credit(name, Team::Helper, 10);
		addFeed(withUser("%s SUBBED: GOLDEN PIECE + SHIELD!", name), ColGold);
		break;
	}
	case InboxMsg::GiftSub: {
		if (name == lastMysteryGifter && lastMysteryGiftAge < 20.0f)
			break; /* already celebrated by the mystery gift */
		const std::string to = msg.text.empty() ? std::string() : " -> @" + sanitizeName(msg.text);
		addBanner("GIFT SUB!", "@" + name + to, ColGold, 3.5f);
		for (Piece &p : queue) {
			if (!p.golden && p.type != PIECE_BOMB) {
				p.golden = true;
				break;
			}
		}
		shields = std::min(shields + 1, 5);
		addHype(0.2f);
		confettiBurst(110);
		credit(name, Team::Helper, 10);
		addFeed(withUser("%s GIFTED A SUB: GOLDEN PIECE!", name), ColGold);
		break;
	}
	case InboxMsg::MysteryGift: {
		const int count = std::max(1, msg.amount);
		lastMysteryGifter = name;
		lastMysteryGiftAge = 0.0f;
		addBanner("@" + name + " GIFTED " + std::to_string(count) + (count == 1 ? " SUB!" : " SUBS!"),
			  "GOLDEN PIECES INCOMING", ColGold, 4.5f);
		int golds = std::min(count, 3);
		for (Piece &p : queue) {
			if (golds <= 0)
				break;
			if (!p.golden && p.type != PIECE_BOMB) {
				p.golden = true;
				--golds;
			}
		}
		shields = std::min(shields + count, 5);
		addHype(0.2f * float(count));
		confettiBurst(std::min(600, 150 + count * 30));
		credit(name, Team::Helper, 10 * count);
		addFeed(withUser("%s STARTED A GIFT STORM!", name), ColGold);
		break;
	}
	case InboxMsg::Raid: {
		const int viewers = std::max(0, msg.amount);
		addBanner("RAID INCOMING!", "@" + name + " + " + std::to_string(viewers) + " RAIDERS", ColPink, 5.0f);
		startEffect(FX_HYPE, 20.0f);
		startEffect(FX_DISCO, 10.0f);
		confettiBurst(300);
		credit(name, Team::Fun, 20);
		addFeed("WELCOME RAIDERS! SPIN THAT WHEEL!", ColPink);
		spinWheel(name);
		break;
	}
	case InboxMsg::Follow:
		addBanner("NEW FOLLOWER!", "@" + name, ColSys, 3.0f);
		confettiBurst(60);
		credit(name, Team::Fun, 2);
		addFeed(withUser("THANKS FOR THE FOLLOW %s!", name), ColSys);
		break;
	}
}

void Game::handleChat(const InboxMsg &msg)
{
	if (!cfg.chatEnabled)
		return;
	const std::string name = sanitizeName(msg.user);
	const std::string text = trim(msg.text);
	const std::string lower = toLower(text);
	const int bits = msg.amount;
	const bool bitsTrigger = bits > 0 && cfg.bitsThreshold > 0 && bits >= cfg.bitsThreshold;

	if (msg.firstMessage && cfg.welcomeFirst) {
		addBanner("WELCOME!", "@" + name + " SAID HI FOR THE FIRST TIME", ColSys, 3.0f);
		confettiBurst(45);
		credit(name, Team::Fun, 1);
	}
	if (bits > 0) {
		addBanner("@" + name + " CHEERED " + std::to_string(bits) + " BITS!", "THANK YOU!", ColPink, 3.5f);
		confettiBurst(std::min(500, 40 + bits / 5));
		credit(name, Team::Fun, std::max(1, bits / 50));
		if (cfg.chaosThreshold > 0 && cfg.bitsThreshold > 0)
			addChaos(std::max(1, bits * 5 / cfg.bitsThreshold), name);
	}

	/* look for a command in the first few words */
	int cmd = -1;
	if (!msg.command.empty()) {
		std::string token = toLower(trim(msg.command));
		if (token.compare(0, cfg.prefix.size(), cfg.prefix) == 0)
			token = token.substr(cfg.prefix.size());
		const auto &lookup = commandLookup();
		const auto it = lookup.find(token);
		if (it != lookup.end())
			cmd = it->second;
	}
	const std::vector<std::string> words = splitWords(lower, 4);
	for (size_t i = 0; i < words.size() && cmd < 0; ++i) {
		const std::string &w = words[i];
		if (w.size() <= cfg.prefix.size() || w.compare(0, cfg.prefix.size(), cfg.prefix) != 0)
			continue;
		std::string token = w.substr(cfg.prefix.size());
		if (token == "vote" && i + 1 < words.size())
			token = words[i + 1];
		const auto &lookup = commandLookup();
		const auto it = lookup.find(token);
		if (it != lookup.end()) {
			cmd = it->second;
			break;
		}
	}
	if (cmd < 0 && cfg.plainReactions)
		cmd = plainReaction(lower);

	const bool muted = chatMuted || fxActive(FX_CHILL);

	if (cmd < 0) {
		if (bitsTrigger && !muted && cfg.allowTroll) {
			static const int trolls[] = {CMD_FLIP, CMD_MIRROR, CMD_TURBO,  CMD_GARBAGE,
						     CMD_DARK, CMD_SHAKE,  CMD_WOBBLE, CMD_CURSE};
			const int count = std::min(3, bits / cfg.bitsThreshold);
			for (int i = 0; i < count; ++i) {
				const size_t idx = std::uniform_int_distribution<size_t>(0, 7)(rng);
				applyCommand(trolls[idx], name);
			}
		}
		return;
	}

	const Team team = commandTeam(cmd);
	if (team == Team::System)
		return;

	if (team == Team::Control) {
		if (cfg.chatPlays == ChatPlaysMode::Off || muted || gameOver || paused)
			return;
		if (cfg.chatPlays == ChatPlaysMode::Anarchy) {
			const std::string key = name + "#ctl";
			if (userCooldowns.count(key) && !msg.privileged)
				return;
			userCooldowns[key] = 0.35f;
			const Action a = controlAction(cmd);
			if (a == Action::SoftDrop) {
				for (int i = 0; i < 2; ++i)
					doAction(a, false);
			} else {
				doAction(a, false);
			}
		} else {
			if (voters.count(name))
				return;
			voters[name] = true;
			votes[size_t(cmd - CMD_LEFT)] += 1;
		}
		return;
	}

	if (team == Team::Troll && !cfg.allowTroll)
		return;
	if (team == Team::Helper && !cfg.allowHelper)
		return;
	if (team == Team::Fun && !cfg.allowFun)
		return;

	if (isReaction(cmd) || cmd == CMD_HYPE) {
		const std::string key = name + "#fun";
		if (userCooldowns.count(key) || reactionGate > 0.0f)
			return;
		userCooldowns[key] = cmd == CMD_HYPE ? 5.0f : 2.0f;
		reactionGate = 0.12f;
		applyCommand(cmd, name);
		return;
	}

	if (muted)
		return;

	const bool bypass = msg.bypassCooldown || bitsTrigger || (msg.privileged && cfg.privilegedBypass);
	if (!bypass) {
		if (globalCooldowns[size_t(cmd)] > 0.0f)
			return;
		if (userCooldowns.count(name))
			return;
	}
	if (applyCommand(cmd, name)) {
		globalCooldowns[size_t(cmd)] = cfg.globalCooldown * Commands[cmd].cooldownScale;
		if (cfg.userCooldown > 0.0f)
			userCooldowns[name] = cfg.userCooldown;
		addChaos(1, name);
	}
}

bool Game::applyCommand(int cmd, const std::string &user)
{
	const Team team = commandTeam(cmd);
	const bool playing = !gameOver;

	if (team == Team::Troll && shields > 0 && playing) {
		--shields;
		addFeed(withUser("%s TRIED ", user) + commandLabel(cmd) + " - SHIELD BLOCKED IT!", ColHelp);
		addPopup("BLOCKED!", ColHelp, 1.1f);
		burst(float(BoardW) * 0.5f, float(BoardH) * 0.5f, ColHelp, 30, 10.0f);
		credit(user, Team::Troll, 1);
		return true;
	}

	switch (cmd) {
	case CMD_FLIP:
		if (!playing)
			return false;
		startEffect(FX_FLIP, 12.0f);
		addFeed(withUser(
				pick({"%s FLIPPED THE BOARD", "%s: AUSTRALIA MODE", "%s TURNED IT UPSIDE DOWN"}).c_str(),
				user),
			ColTroll);
		credit(user, Team::Troll, 3);
		return true;
	case CMD_MIRROR:
		if (!playing)
			return false;
		startEffect(FX_MIRROR, 12.0f);
		addFeed(withUser(pick({"%s SWAPPED LEFT AND RIGHT", "%s: OPPOSITE DAY!", "%s REVERSED THE CONTROLS"})
					 .c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 3);
		return true;
	case CMD_TURBO:
		if (!playing)
			return false;
		fx[FX_SLOW] = 0.0f;
		startEffect(FX_TURBO, 8.0f);
		addFeed(withUser(pick({"%s HIT THE NITRO", "%s: GOTTA GO FAST", "%s PRESSED THE TURBO BUTTON"}).c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 2);
		return true;
	case CMD_GARBAGE:
		if (!playing)
			return false;
		addGarbage(1);
		addFeed(withUser(pick({"%s DUMPED SOME TRASH", "%s: HERE, HOLD THIS", "%s SENT A GIFT (IT IS GARBAGE)"})
					 .c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 2);
		return true;
	case CMD_DARK:
		if (!playing)
			return false;
		startEffect(FX_DARK, 6.0f);
		addFeed(withUser(pick({"%s TURNED THE LIGHTS OFF", "%s: WHO FORGOT THE POWER BILL?",
				       "%s ATE THE LIGHTBULB"})
					 .c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 3);
		return true;
	case CMD_SHAKE:
		if (!playing)
			return false;
		startEffect(FX_SHAKE, 5.0f);
		addFeed(withUser(pick({"%s CAUSED AN EARTHQUAKE", "%s STOMPED REALLY HARD"}).c_str(), user), ColTroll);
		credit(user, Team::Troll, 1);
		return true;
	case CMD_WOBBLE:
		if (!playing)
			return false;
		startEffect(FX_WOBBLE, 10.0f);
		addFeed(withUser(pick({"%s MADE EVERYTHING WOBBLY", "%s: JELLY MODE", "%s SHOOK THE TABLE"}).c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 2);
		return true;
	case CMD_CURSE: {
		if (!playing || queue.size() >= MaxQueue)
			return false;
		const int type = PIECE_PLUS + std::uniform_int_distribution<int>(0, 3)(rng);
		queue.push_front(makePiece(type));
		addFeed(withUser(pick({"%s CURSED THE NEXT PIECE", "%s SUMMONED A WEIRD ONE", "%s: GOOD LUCK WITH THAT"})
					 .c_str(),
				 user),
			ColTroll);
		credit(user, Team::Troll, 2);
		return true;
	}
	case CMD_SLOW:
		if (!playing)
			return false;
		fx[FX_TURBO] = 0.0f;
		startEffect(FX_SLOW, 10.0f);
		addFeed(withUser(pick({"%s GAVE A CHILL PILL", "%s SLOWED DOWN TIME", "%s: DEEP BREATHS"}).c_str(),
				 user),
			ColHelp);
		credit(user, Team::Helper, 2);
		return true;
	case CMD_BOMB:
		if (!playing || queue.size() >= MaxQueue)
			return false;
		queue.push_front(makePiece(PIECE_BOMB));
		addFeed(withUser(pick({"%s SENT A BOMB", "%s: FIRE IN THE HOLE!", "%s DELIVERED EXPLOSIVES"}).c_str(),
				 user),
			ColHelp);
		credit(user, Team::Helper, 3);
		return true;
	case CMD_LONG:
		if (!playing || queue.size() >= MaxQueue)
			return false;
		queue.push_front(makePiece(PIECE_I));
		addFeed(withUser(pick({"%s SENT THE LONG ONE", "%s: STRAIGHT PIECE INCOMING",
				       "%s ANSWERED YOUR PRAYERS"})
					 .c_str(),
				 user),
			ColHelp);
		credit(user, Team::Helper, 2);
		return true;
	case CMD_SHIELD:
		if (shields >= 3)
			return false;
		++shields;
		addFeed(withUser("%s RAISED A SHIELD", user), ColHelp);
		credit(user, Team::Helper, 3);
		return true;
	case CMD_SWEEP: {
		if (!playing)
			return false;
		bool any = false;
		for (int x = 0; x < BoardW; ++x)
			any = any || board[size_t((BoardH - 1) * BoardW + x)].kind != CELL_EMPTY;
		if (!any)
			return false;
		sweepRow();
		addFeed(withUser(pick({"%s SWEPT THE FLOOR", "%s CLEANED UP THE MESS"}).c_str(), user), ColHelp);
		credit(user, Team::Helper, 4);
		return true;
	}
	case CMD_DISCO:
		startEffect(FX_DISCO, 12.0f);
		confettiBurst(70);
		addFeed(withUser(pick({"%s STARTED A DISCO", "%s: PARTY TIME!", "%s DROPPED THE BEAT"}).c_str(), user),
			ColFun);
		credit(user, Team::Fun, 2);
		return true;
	case CMD_HYPE:
		addHype(0.07f);
		addBubble("HYPE!", user, ColPink);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_GG:
		addBubble("GG", user, ColHelp);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_POG:
		addBubble("POG", user, ColFun);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_LOL:
		addBubble("LOL", user, ColSys);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_RIP:
		addBubble("RIP", user, 0xb8c0ccff);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_LOVE:
		addBubble("\x01\x01\x01", user, ColPink);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_WOW:
		addBubble("WOW", user, ColGold);
		credit(user, Team::Fun, 1);
		return true;
	case CMD_HELP:
		helpCard = 12.0f;
		return true;
	case CMD_JACKPOT:
		score += int64_t(1000) * level;
		addPopup("JACKPOT +" + std::to_string(1000 * level), ColGold, 1.3f);
		confettiBurst(150);
		addFeed("THE WHEEL PAID OUT A JACKPOT!", ColGold);
		return true;
	case CMD_NOTHING:
		addPopup("...NOTHING :)", 0xffffffff, 1.0f);
		addFeed("THE WHEEL CHOSE... ABSOLUTELY NOTHING", ColFun);
		return true;
	default:
		break;
	}
	return false;
}

void Game::startEffect(int effect, float seconds)
{
	const float s = seconds * cfg.effectScale;
	float &v = fx[size_t(effect)];
	v = std::min(v + s, s * 2.0f);
	fxMax[size_t(effect)] = std::max(v, s);
	if (effect == FX_SHAKE)
		shakeImpulse = std::max(shakeImpulse, 0.5f);
}

void Game::addFeed(const std::string &text, uint32_t color)
{
	feed.push_back({text, color, 0.0f});
	while (feed.size() > MaxFeed)
		feed.pop_front();
}

void Game::addBanner(const std::string &title, const std::string &subtitle, uint32_t color, float life)
{
	if (banners.size() >= 6)
		banners.pop_back();
	banners.push_back({title, subtitle, color, 0.0f, life});
}

void Game::addPopup(const std::string &text, uint32_t color, float scale)
{
	if (popups.size() >= 6)
		popups.erase(popups.begin());
	const float y = float(BoardH) * 0.38f + float(popups.size()) * 1.6f;
	popups.push_back({text, float(BoardW) * 0.5f, y, 0.0f, 1.6f, color, scale});
}

void Game::addBubble(const std::string &text, const std::string &user, uint32_t color)
{
	if (bubbles.size() >= 24)
		bubbles.erase(bubbles.begin());
	bubbles.push_back({text, user, 0.06f + rand01() * 0.88f, 0.0f, 3.6f, color});
}

void Game::burst(float x, float y, uint32_t color, int count, float speed, int type)
{
	for (int i = 0; i < count && particles.size() < MaxParticles; ++i) {
		const float a = rand01() * 6.2831853f;
		const float v = speed * (0.35f + rand01() * 0.65f);
		Particle p;
		p.x = x;
		p.y = y;
		p.vx = std::cos(a) * v;
		p.vy = std::sin(a) * v - speed * 0.4f;
		p.age = 0.0f;
		p.life = 0.5f + rand01() * 0.6f;
		p.size = 0.12f + rand01() * 0.22f;
		p.rot = rand01() * 6.28f;
		p.vrot = (rand01() - 0.5f) * 14.0f;
		p.color = color;
		p.type = type;
		particles.push_back(p);
	}
}

void Game::confettiBurst(int count)
{
	const size_t palette = sizeof(ConfettiColors) / sizeof(ConfettiColors[0]);
	for (int i = 0; i < count && confetti.size() < MaxConfetti; ++i) {
		Particle p;
		p.x = rand01();
		p.y = -0.05f - rand01() * 0.25f;
		p.vx = (rand01() - 0.5f) * 0.25f;
		p.vy = 0.08f + rand01() * 0.25f;
		p.age = 0.0f;
		p.life = 3.5f + rand01() * 2.5f;
		p.size = 0.006f + rand01() * 0.008f;
		p.rot = rand01() * 6.28f;
		p.vrot = (rand01() - 0.5f) * 12.0f;
		p.color = ConfettiColors[std::uniform_int_distribution<size_t>(0, palette - 1)(rng)];
		confetti.push_back(p);
	}
}

void Game::credit(const std::string &user, Team team, int points)
{
	if (user.empty() || points <= 0)
		return;
	ViewerStats &v = viewers[user];
	if (team == Team::Helper) {
		v.help += points;
		helpPower += float(points);
	} else if (team == Team::Troll) {
		v.troll += points;
		trollPower += float(points);
	} else {
		v.fun += points;
	}
	ranksDirty = true;
}

void Game::rebuildRanks()
{
	auto top = [this](bool helpers) {
		std::vector<RankEntry> all;
		all.reserve(viewers.size());
		for (const auto &kv : viewers) {
			const int pts = helpers ? kv.second.help : kv.second.troll;
			if (pts > 0)
				all.push_back({kv.first, pts});
		}
		const size_t n = std::min<size_t>(3, all.size());
		std::partial_sort(all.begin(), all.begin() + std::ptrdiff_t(n), all.end(),
				  [](const RankEntry &a, const RankEntry &b) { return a.points > b.points; });
		all.resize(n);
		return all;
	};
	topHelpers = top(true);
	topTrolls = top(false);
}

void Game::addChaos(int amount, const std::string &by)
{
	if (cfg.chaosThreshold <= 0 || amount <= 0)
		return;
	chaos += amount;
	if (chaos >= cfg.chaosThreshold && !wheel.active && !gameOver) {
		chaos = 0;
		spinWheel(by);
	}
	chaos = std::min(chaos, cfg.chaosThreshold);
}

void Game::addHype(float amount)
{
	hype += amount;
	if (hype >= 1.0f) {
		hype = 0.0f;
		startEffect(FX_HYPE, 20.0f);
		addBanner("HYPE MODE!", "DOUBLE POINTS - CHAT IS ON FIRE", ColPink, 3.5f);
		confettiBurst(160);
		addFeed("CHAT UNLOCKED HYPE MODE: SCORE X2!", ColPink);
	}
}

void Game::spinWheel(const std::string &by)
{
	if (wheel.active || chatMuted || fxActive(FX_CHILL))
		return;
	wheel.options.clear();
	if (cfg.allowTroll) {
		for (int c = CMD_FLIP; c <= CMD_CURSE; ++c)
			wheel.options.push_back(c);
	}
	if (cfg.allowHelper) {
		for (int c = CMD_SLOW; c <= CMD_SWEEP; ++c)
			wheel.options.push_back(c);
	}
	wheel.options.push_back(CMD_DISCO);
	wheel.options.push_back(CMD_JACKPOT);
	wheel.options.push_back(CMD_NOTHING);
	std::shuffle(wheel.options.begin(), wheel.options.end(), rng);
	wheel.active = true;
	wheel.age = 0.0f;
	wheel.duration = 3.6f + rand01() * 1.2f;
	wheel.tickTimer = 0.0f;
	wheel.tickInterval = 0.04f;
	wheel.shown = 0;
	wheel.result = std::uniform_int_distribution<int>(0, int(wheel.options.size()) - 1)(rng);
	wheel.resolved = false;
	wheel.by = by;
	addFeed("THE CHAOS WHEEL IS SPINNING!", ColFun);
}

void Game::stepWheel(float dt)
{
	if (!wheel.active)
		return;
	wheel.age += dt;
	if (!wheel.resolved) {
		const float progress = std::clamp(wheel.age / wheel.duration, 0.0f, 1.0f);
		wheel.tickTimer -= dt;
		if (wheel.tickTimer <= 0.0f) {
			wheel.shown = (wheel.shown + 1) % int(wheel.options.size());
			wheel.tickInterval = 0.04f + progress * progress * 0.38f;
			wheel.tickTimer = wheel.tickInterval;
		}
		if (wheel.age >= wheel.duration) {
			wheel.resolved = true;
			wheel.shown = wheel.result;
			const int cmd = wheel.options[size_t(wheel.result)];
			confettiBurst(50);
			if (!applyCommand(cmd, wheel.by))
				addFeed(std::string("THE WHEEL CHOSE ") + commandLabel(cmd) + "... BUT IT FIZZLED",
					ColFun);
		}
	} else if (wheel.age >= wheel.duration + 2.2f) {
		wheel.active = false;
	}
}

void Game::stepVotes(float dt)
{
	if (cfg.chatPlays != ChatPlaysMode::Democracy)
		return;
	voteTimer += dt;
	if (voteTimer < cfg.voteSeconds)
		return;
	voteTimer = 0.0f;
	int bestIdx = -1;
	int bestVotes = 0;
	for (size_t i = 0; i < votes.size(); ++i) {
		if (votes[i] > bestVotes || (votes[i] == bestVotes && bestVotes > 0 && rand01() < 0.5f)) {
			bestVotes = votes[i];
			bestIdx = int(i);
		}
	}
	if (bestIdx >= 0 && !chatMuted && !fxActive(FX_CHILL)) {
		const int cmd = CMD_LEFT + bestIdx;
		const Action a = controlAction(cmd);
		const int repeats = a == Action::SoftDrop ? 3 : 1;
		for (int i = 0; i < repeats; ++i)
			doAction(a, false);
		lastVoteResult =
			std::string("CHAT CHOSE ") + commandLabel(cmd) + " (" + std::to_string(bestVotes) + ")";
		lastVoteResultAge = 0.0f;
	}
	votes.fill(0);
	voters.clear();
}

} // namespace sp
