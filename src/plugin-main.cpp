/*
OBS Tetris Source
Minimal playable Tetris source for OBS Studio.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <functional>
#include <cstdio>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
constexpr int BoardW = 10;
constexpr int BoardH = 20;
constexpr int PieceSize = 4;
constexpr uint32_t Empty = 0x00000000;

static const std::array<std::array<const char *, 4>, 7> Pieces = {{
	{{"....", "####", "....", "...."}}, // I
	{{".#..", ".###", "....", "...."}}, // T
	{{".##.", ".##.", "....", "...."}}, // O
	{{".##.", "##..", "....", "...."}}, // S
	{{"##..", ".##.", "....", "...."}}, // Z
	{{"#...", "###.", "....", "...."}}, // J
	{{"..#.", "###.", "....", "...."}}, // L
}};

static const std::array<uint32_t, 7> Colors = {
	0x25d6ffff, 0xa855f7ff, 0xfacc15ff, 0x22c55eff, 0xef4444ff, 0x3b82f6ff, 0xf97316ff,
};

struct Piece {
	int type = 0;
	int rot = 0;
	int x = 3;
	int y = -1;
};

struct TetrisSource {
	obs_source_t *source = nullptr;
	uint32_t width = 720;
	uint32_t height = 1080;
	float fallSeconds = 0.65f;
	bool showGrid = true;
	bool paused = false;
	bool gameOver = false;
	int score = 0;
	int lines = 0;
	int level = 1;
	float fallAccum = 0.0f;
	float inputAccum = 0.0f;
	float elapsedSeconds = 0.0f;
	std::array<uint32_t, BoardW * BoardH> board{};
	Piece current;
	Piece next;
	std::mt19937 rng{std::random_device{}()};
	std::mutex lock;
	obs_hotkey_id leftHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id rightHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id downHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id rotateHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id dropHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id pauseHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id resetHotkey = OBS_INVALID_HOTKEY_ID;
};

static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(a);
}

static inline bool pieceCell(const Piece &p, int px, int py)
{
	int x = px;
	int y = py;
	switch (p.rot & 3) {
	case 1:
		x = py;
		y = PieceSize - 1 - px;
		break;
	case 2:
		x = PieceSize - 1 - px;
		y = PieceSize - 1 - py;
		break;
	case 3:
		x = PieceSize - 1 - py;
		y = px;
		break;
	default:
		break;
	}
	return Pieces[p.type][y][x] == '#';
}

static bool collides(const TetrisSource *s, const Piece &p)
{
	for (int py = 0; py < PieceSize; ++py) {
		for (int px = 0; px < PieceSize; ++px) {
			if (!pieceCell(p, px, py))
				continue;
			const int bx = p.x + px;
			const int by = p.y + py;
			if (bx < 0 || bx >= BoardW || by >= BoardH)
				return true;
			if (by >= 0 && s->board[size_t(by * BoardW + bx)] != Empty)
				return true;
		}
	}
	return false;
}

static Piece randomPiece(TetrisSource *s)
{
	std::uniform_int_distribution<int> dist(0, 6);
	Piece p;
	p.type = dist(s->rng);
	p.rot = 0;
	p.x = 3;
	p.y = -1;
	return p;
}

static void resetGame(TetrisSource *s)
{
	s->board.fill(Empty);
	s->score = 0;
	s->lines = 0;
	s->level = 1;
	s->fallSeconds = 0.65f;
	s->fallAccum = 0.0f;
	s->elapsedSeconds = 0.0f;
	s->paused = false;
	s->gameOver = false;
	s->current = randomPiece(s);
	s->next = randomPiece(s);
}

static void spawnPiece(TetrisSource *s)
{
	s->current = s->next;
	s->current.x = 3;
	s->current.y = -1;
	s->current.rot = 0;
	s->next = randomPiece(s);
	if (collides(s, s->current))
		s->gameOver = true;
}

static void clearLines(TetrisSource *s)
{
	int cleared = 0;
	for (int y = BoardH - 1; y >= 0; --y) {
		bool full = true;
		for (int x = 0; x < BoardW; ++x)
			full = full && s->board[size_t(y * BoardW + x)] != Empty;
		if (!full)
			continue;
		++cleared;
		for (int yy = y; yy > 0; --yy) {
			for (int x = 0; x < BoardW; ++x)
				s->board[size_t(yy * BoardW + x)] = s->board[size_t((yy - 1) * BoardW + x)];
		}
		for (int x = 0; x < BoardW; ++x)
			s->board[size_t(x)] = Empty;
		++y;
	}
	if (cleared > 0) {
		static const int points[] = {0, 100, 300, 500, 800};
		s->score += points[cleared] * s->level;
		s->lines += cleared;
		s->level = 1 + s->lines / 10;
		s->fallSeconds = std::max(0.08f, 0.65f - float(s->level - 1) * 0.055f);
	}
}

static void lockPiece(TetrisSource *s)
{
	for (int py = 0; py < PieceSize; ++py) {
		for (int px = 0; px < PieceSize; ++px) {
			if (!pieceCell(s->current, px, py))
				continue;
			const int bx = s->current.x + px;
			const int by = s->current.y + py;
			if (bx >= 0 && bx < BoardW && by >= 0 && by < BoardH)
				s->board[size_t(by * BoardW + bx)] = Colors[s->current.type];
		}
	}
	clearLines(s);
	spawnPiece(s);
}

static bool tryMove(TetrisSource *s, int dx, int dy)
{
	if (s->paused || s->gameOver)
		return false;
	Piece moved = s->current;
	moved.x += dx;
	moved.y += dy;
	if (collides(s, moved))
		return false;
	s->current = moved;
	return true;
}

static void softDrop(TetrisSource *s)
{
	if (!tryMove(s, 0, 1) && !s->paused && !s->gameOver)
		lockPiece(s);
}

static void rotatePiece(TetrisSource *s)
{
	if (s->paused || s->gameOver)
		return;
	Piece r = s->current;
	r.rot = (r.rot + 1) & 3;
	for (int kick : {0, -1, 1, -2, 2}) {
		Piece test = r;
		test.x += kick;
		if (!collides(s, test)) {
			s->current = test;
			return;
		}
	}
}

static void hardDrop(TetrisSource *s)
{
	if (s->paused || s->gameOver)
		return;
	while (tryMove(s, 0, 1))
		s->score += 2;
	lockPiece(s);
}

static void drawRect(float x, float y, float w, float h, uint32_t color)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!effect)
		return;

	vec4 c = {};
	c.x = float((color >> 24) & 0xff) / 255.0f;
	c.y = float((color >> 16) & 0xff) / 255.0f;
	c.z = float((color >> 8) & 0xff) / 255.0f;
	c.w = float(color & 0xff) / 255.0f;
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &c);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(effect, "Solid"))
		gs_draw_sprite(nullptr, 0, uint32_t(w), uint32_t(h));
	gs_matrix_pop();
}

static void drawCell(float x, float y, float size, uint32_t color)
{
	drawRect(x, y, size, size, rgba(15, 23, 42, 235));
	drawRect(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, color);
	drawRect(x + 5.0f, y + 5.0f, size - 10.0f, std::max(2.0f, size * 0.16f), rgba(255, 255, 255, 54));
}


static const uint8_t *glyphRows(char c)
{
	static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
	static const uint8_t colon[7] = {0, 4, 4, 0, 4, 4, 0};
	static const uint8_t dash[7] = {0, 0, 0, 31, 0, 0, 0};
	static const uint8_t zero[7] = {14, 17, 19, 21, 25, 17, 14};
	static const uint8_t one[7] = {4, 12, 4, 4, 4, 4, 14};
	static const uint8_t two[7] = {14, 17, 1, 2, 4, 8, 31};
	static const uint8_t three[7] = {30, 1, 1, 14, 1, 1, 30};
	static const uint8_t four[7] = {2, 6, 10, 18, 31, 2, 2};
	static const uint8_t five[7] = {31, 16, 16, 30, 1, 1, 30};
	static const uint8_t six[7] = {14, 16, 16, 30, 17, 17, 14};
	static const uint8_t seven[7] = {31, 1, 2, 4, 8, 8, 8};
	static const uint8_t eight[7] = {14, 17, 17, 14, 17, 17, 14};
	static const uint8_t nine[7] = {14, 17, 17, 15, 1, 1, 14};
	static const uint8_t a[7] = {14, 17, 17, 31, 17, 17, 17};
	static const uint8_t c_[7] = {14, 17, 16, 16, 16, 17, 14};
	static const uint8_t e[7] = {31, 16, 16, 30, 16, 16, 31};
	static const uint8_t g[7] = {14, 17, 16, 23, 17, 17, 15};
	static const uint8_t i[7] = {14, 4, 4, 4, 4, 4, 14};
	static const uint8_t l[7] = {16, 16, 16, 16, 16, 16, 31};
	static const uint8_t m[7] = {17, 27, 21, 21, 17, 17, 17};
	static const uint8_t n[7] = {17, 25, 21, 19, 17, 17, 17};
	static const uint8_t o[7] = {14, 17, 17, 17, 17, 17, 14};
	static const uint8_t p[7] = {30, 17, 17, 30, 16, 16, 16};
	static const uint8_t r[7] = {30, 17, 17, 30, 20, 18, 17};
	static const uint8_t s_[7] = {15, 16, 16, 14, 1, 1, 30};
	static const uint8_t t[7] = {31, 4, 4, 4, 4, 4, 4};
	static const uint8_t u[7] = {17, 17, 17, 17, 17, 17, 14};
	static const uint8_t v[7] = {17, 17, 17, 17, 17, 10, 4};

	switch (c) {
	case '0': return zero; case '1': return one; case '2': return two; case '3': return three; case '4': return four;
	case '5': return five; case '6': return six; case '7': return seven; case '8': return eight; case '9': return nine;
	case ':': return colon; case '-': return dash; case 'A': return a; case 'C': return c_; case 'E': return e;
	case 'G': return g; case 'I': return i; case 'L': return l; case 'M': return m; case 'N': return n;
	case 'O': return o; case 'P': return p; case 'R': return r; case 'S': return s_; case 'T': return t;
	case 'U': return u; case 'V': return v; default: return space;
	}
}

static float textWidth(const char *text, float scale)
{
	float width = 0.0f;
	for (const char *p = text; *p; ++p)
		width += (*p == ' ') ? 4.0f * scale : 6.0f * scale;
	return width;
}

static void drawText(const char *text, float x, float y, float scale, uint32_t color)
{
	float cx = x;
	for (const char *p = text; *p; ++p) {
		if (*p == ' ') {
			cx += 4.0f * scale;
			continue;
		}
		const uint8_t *rows = glyphRows(*p);
		for (int row = 0; row < 7; ++row) {
			for (int col = 0; col < 5; ++col) {
				if (rows[row] & (1 << (4 - col)))
					drawRect(cx + float(col) * scale, y + float(row) * scale, scale, scale, color);
			}
		}
		cx += 6.0f * scale;
	}
}

static void drawTextCentered(const char *text, float cx, float y, float scale, uint32_t color)
{
	drawText(text, cx - textWidth(text, scale) * 0.5f, y, scale, color);
}

static std::string formatTime(float seconds)
{
	const int total = std::max(0, int(seconds));
	const int minutes = total / 60;
	const int secs = total % 60;
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
	return std::string(buffer);
}

static void drawHudLine(const char *label, const std::string &value, float x, float y, float w, float scale)
{
	drawText(label, x, y, scale, rgba(148, 163, 184, 255));
	drawText(value.c_str(), x, y + scale * 9.0f, scale * 1.28f, rgba(248, 250, 252, 255));
	drawRect(x, y + scale * 18.5f, w, 1.0f, rgba(51, 65, 85, 255));
}

static void withSourceLock(void *data, const std::function<void(TetrisSource *)> &fn)
{
	auto *src = static_cast<TetrisSource *>(data);
	std::lock_guard<std::mutex> guard(src->lock);
	fn(src);
}

static void hotkeyLeft(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { tryMove(s, -1, 0); });
}

static void hotkeyRight(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { tryMove(s, 1, 0); });
}

static void hotkeyDown(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { softDrop(s); });
}

static void hotkeyRotate(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { rotatePiece(s); });
}

static void hotkeyDrop(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { hardDrop(s); });
}

static void hotkeyPause(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { s->paused = !s->paused; });
}

static void hotkeyReset(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		withSourceLock(data, [](TetrisSource *s) { resetGame(s); });
}

static const char *tetris_get_name(void *)
{
	return obs_module_text("OBSTetrisSource");
}

static void tetris_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<TetrisSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	s->width = uint32_t(obs_data_get_int(settings, "width"));
	s->height = uint32_t(obs_data_get_int(settings, "height"));
	s->showGrid = obs_data_get_bool(settings, "show_grid");
}

static void *tetris_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new TetrisSource();
	s->source = source;
	tetris_update(s, settings);
	resetGame(s);

	s->leftHotkey = obs_hotkey_register_source(source, "obs_tetris.left", "Tetris: Move Left", hotkeyLeft, s);
	s->rightHotkey = obs_hotkey_register_source(source, "obs_tetris.right", "Tetris: Move Right", hotkeyRight, s);
	s->downHotkey = obs_hotkey_register_source(source, "obs_tetris.down", "Tetris: Soft Drop", hotkeyDown, s);
	s->rotateHotkey = obs_hotkey_register_source(source, "obs_tetris.rotate", "Tetris: Rotate", hotkeyRotate, s);
	s->dropHotkey = obs_hotkey_register_source(source, "obs_tetris.drop", "Tetris: Hard Drop", hotkeyDrop, s);
	s->pauseHotkey = obs_hotkey_register_source(source, "obs_tetris.pause", "Tetris: Pause", hotkeyPause, s);
	s->resetHotkey = obs_hotkey_register_source(source, "obs_tetris.reset", "Tetris: Reset", hotkeyReset, s);
	return s;
}

static void tetris_destroy(void *data)
{
	delete static_cast<TetrisSource *>(data);
}

static void tetris_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 720);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_bool(settings, "show_grid", true);
}

static obs_properties_t *tetris_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int(props, "width", "Width", 320, 3840, 10);
	obs_properties_add_int(props, "height", "Height", 480, 2160, 10);
	obs_properties_add_bool(props, "show_grid", "Show subtle grid");
	return props;
}

static uint32_t tetris_width(void *data)
{
	return static_cast<TetrisSource *>(data)->width;
}

static uint32_t tetris_height(void *data)
{
	return static_cast<TetrisSource *>(data)->height;
}

static void tetris_tick(void *data, float seconds)
{
	auto *s = static_cast<TetrisSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	if (s->paused || s->gameOver)
		return;
	s->elapsedSeconds += seconds;
	s->fallAccum += seconds;
	if (s->fallAccum >= s->fallSeconds) {
		s->fallAccum = 0.0f;
		softDrop(s);
	}
}

static void drawMiniPiece(const Piece &p, float ox, float oy, float cell)
{
	for (int py = 0; py < PieceSize; ++py) {
		for (int px = 0; px < PieceSize; ++px) {
			if (pieceCell(p, px, py))
				drawCell(ox + float(px) * cell, oy + float(py) * cell, cell, Colors[p.type]);
		}
	}
}

static void tetris_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<TetrisSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);

	const float w = float(s->width);
	const float h = float(s->height);
	drawRect(0, 0, w, h, rgba(2, 6, 23, 255));

	const float margin = std::max(16.0f, w * 0.035f);
	const float hudW = std::clamp(w * 0.25f, 150.0f, 250.0f);
	const float nextW = std::clamp(w * 0.18f, 110.0f, 190.0f);
	const float boardMaxW = w - margin * 4.0f - hudW - nextW;
	const float boardMaxH = h - margin * 2.0f;
	const float cell = std::floor(std::max(8.0f, std::min(boardMaxW / BoardW, boardMaxH / BoardH)));
	const float boardW = cell * BoardW;
	const float boardH = cell * BoardH;
	const float by = (h - boardH) * 0.5f;
	const float hudX = margin;
	const float bx = hudX + hudW + margin;
	const float nextX = bx + boardW + margin;

	drawRect(hudX, by, hudW, boardH, rgba(15, 23, 42, 255));
	drawRect(hudX + 8, by + 8, hudW - 16, boardH - 16, rgba(2, 6, 23, 130));
	drawTextCentered("TETRIS", hudX + hudW * 0.5f, by + 24.0f, std::max(3.0f, hudW / 70.0f), rgba(34, 211, 238, 255));

	const float hudScale = std::max(2.2f, hudW / 92.0f);
	float lineY = by + 86.0f;
	drawHudLine("SCORE", std::to_string(s->score), hudX + 22.0f, lineY, hudW - 44.0f, hudScale);
	lineY += hudScale * 26.0f;
	drawHudLine("TIME", formatTime(s->elapsedSeconds), hudX + 22.0f, lineY, hudW - 44.0f, hudScale);
	lineY += hudScale * 26.0f;
	drawHudLine("LEVEL", std::to_string(s->level), hudX + 22.0f, lineY, hudW - 44.0f, hudScale);
	lineY += hudScale * 26.0f;
	drawHudLine("LINES", std::to_string(s->lines), hudX + 22.0f, lineY, hudW - 44.0f, hudScale);

	const float helpScale = std::max(1.6f, hudW / 130.0f);
	float helpY = by + boardH - helpScale * 47.0f;
	drawText("HOTKEYS", hudX + 22.0f, helpY, helpScale, rgba(148, 163, 184, 255));
	drawText("MOVE", hudX + 22.0f, helpY + helpScale * 10.0f, helpScale, rgba(100, 116, 139, 255));
	drawText("ROTATE", hudX + 22.0f, helpY + helpScale * 20.0f, helpScale, rgba(100, 116, 139, 255));
	drawText("DROP", hudX + 22.0f, helpY + helpScale * 30.0f, helpScale, rgba(100, 116, 139, 255));

	drawRect(bx - 8, by - 8, boardW + 16, boardH + 16, rgba(30, 41, 59, 255));
	drawRect(bx, by, boardW, boardH, rgba(15, 23, 42, 255));

	if (s->showGrid) {
		for (int x = 1; x < BoardW; ++x)
			drawRect(bx + float(x) * cell - 0.5f, by, 1.0f, boardH, rgba(148, 163, 184, 32));
		for (int y = 1; y < BoardH; ++y)
			drawRect(bx, by + float(y) * cell - 0.5f, boardW, 1.0f, rgba(148, 163, 184, 32));
	}

	for (int y = 0; y < BoardH; ++y) {
		for (int x = 0; x < BoardW; ++x) {
			const uint32_t color = s->board[size_t(y * BoardW + x)];
			if (color != Empty)
				drawCell(bx + float(x) * cell, by + float(y) * cell, cell, color);
		}
	}

	for (int py = 0; py < PieceSize; ++py) {
		for (int px = 0; px < PieceSize; ++px) {
			if (!pieceCell(s->current, px, py))
				continue;
			const int x = s->current.x + px;
			const int y = s->current.y + py;
			if (y >= 0)
				drawCell(bx + float(x) * cell, by + float(y) * cell, cell, Colors[s->current.type]);
		}
	}

	drawRect(nextX, by, nextW, boardH, rgba(15, 23, 42, 255));
	drawRect(nextX + 10, by + 10, nextW - 20, 150, rgba(30, 41, 59, 255));
	drawTextCentered("NEXT", nextX + nextW * 0.5f, by + 28.0f, std::max(2.0f, nextW / 72.0f), rgba(148, 163, 184, 255));
	drawMiniPiece(s->next, nextX + nextW * 0.5f - cell * 1.1f, by + 76.0f, std::max(14.0f, cell * 0.52f));

	if (s->paused || s->gameOver) {
		const char *title = s->gameOver ? "GAME OVER" : "PAUSED";
		const char *subtitle = s->gameOver ? "RESET TO PLAY" : "PAUSE TO RESUME";
		drawRect(bx, by + boardH * 0.36f, boardW, boardH * 0.26f, rgba(2, 6, 23, 224));
		drawTextCentered(title, bx + boardW * 0.5f, by + boardH * 0.425f, std::max(3.0f, cell * 0.12f), s->gameOver ? rgba(248, 113, 113, 255) : rgba(250, 204, 21, 255));
		drawTextCentered(subtitle, bx + boardW * 0.5f, by + boardH * 0.535f, std::max(2.0f, cell * 0.065f), rgba(226, 232, 240, 255));
	}
}


static obs_source_info tetris_source_info = {};
}

bool obs_module_load(void)
{
	tetris_source_info.id = "obs_tetris_source";
	tetris_source_info.type = OBS_SOURCE_TYPE_INPUT;
	tetris_source_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	tetris_source_info.get_name = tetris_get_name;
	tetris_source_info.create = tetris_create;
	tetris_source_info.destroy = tetris_destroy;
	tetris_source_info.update = tetris_update;
	tetris_source_info.get_defaults = tetris_defaults;
	tetris_source_info.get_properties = tetris_properties;
	tetris_source_info.get_width = tetris_width;
	tetris_source_info.get_height = tetris_height;
	tetris_source_info.video_tick = tetris_tick;
	tetris_source_info.video_render = tetris_render;

	obs_register_source(&tetris_source_info);
	obs_log(LOG_INFO, "OBS Tetris loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "OBS Tetris unloaded");
}
