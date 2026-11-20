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

#include "render.hpp"
#include "game.hpp"

#include <obs-module.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sp {

/* ------------------------------------------------------------------ */
/* Colour helpers (colours are 0xRRGGBBAA)                             */
/* ------------------------------------------------------------------ */

namespace {

constexpr float Pi = 3.14159265f;

inline float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline uint32_t withAlpha(uint32_t c, float a)
{
	const float base = float(c & 0xffu);
	const uint32_t al = uint32_t(base * clamp01(a) + 0.5f);
	return (c & 0xffffff00u) | al;
}

inline uint32_t setAlpha(uint32_t c, uint32_t a)
{
	return (c & 0xffffff00u) | (a & 0xffu);
}

inline uint32_t mix(uint32_t a, uint32_t b, float t)
{
	t = clamp01(t);
	uint32_t out = 0;
	for (int shift = 0; shift < 32; shift += 8) {
		const float ca = float((a >> shift) & 0xffu);
		const float cb = float((b >> shift) & 0xffu);
		out |= uint32_t(ca + (cb - ca) * t + 0.5f) << shift;
	}
	return out;
}

inline uint32_t lighten(uint32_t c, float t)
{
	return mix(c, c | 0xffffff00u, t);
}

inline uint32_t darken(uint32_t c, float t)
{
	return mix(c, c & 0x000000ffu, t);
}

uint32_t hsv(float h, float s, float v, uint32_t a = 255)
{
	h = h - std::floor(h);
	const float i = std::floor(h * 6.0f);
	const float f = h * 6.0f - i;
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - f * s);
	const float t = v * (1.0f - (1.0f - f) * s);
	float r = v, g = t, bl = p;
	switch (int(i) % 6) {
	case 0:
		r = v;
		g = t;
		bl = p;
		break;
	case 1:
		r = q;
		g = v;
		bl = p;
		break;
	case 2:
		r = p;
		g = v;
		bl = t;
		break;
	case 3:
		r = p;
		g = q;
		bl = v;
		break;
	case 4:
		r = t;
		g = p;
		bl = v;
		break;
	default:
		r = v;
		g = p;
		bl = q;
		break;
	}
	return (uint32_t(r * 255.0f) << 24) | (uint32_t(g * 255.0f) << 16) | (uint32_t(bl * 255.0f) << 8) | (a & 0xffu);
}

inline float easeOutBack(float t)
{
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	const float u = t - 1.0f;
	return 1.0f + c3 * u * u * u + c1 * u * u;
}

inline float easeInOut(float t)
{
	return t * t * (3.0f - 2.0f * t);
}

/* ------------------------------------------------------------------ */
/* 5x7 pixel font                                                      */
/* ------------------------------------------------------------------ */

struct GlyphDef {
	unsigned char ch;
	const char *rows[7];
};

const GlyphDef FontDefs[] = {
	{'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
	{'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
	{'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
	{'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
	{'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
	{'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
	{'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
	{'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
	{'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
	{'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
	{'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
	{'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
	{'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
	{'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
	{'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
	{'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
	{'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
	{'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
	{'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
	{'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
	{'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
	{'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
	{'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
	{'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
	{'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
	{'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
	{'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
	{'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
	{'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
	{'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
	{'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
	{'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
	{'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
	{'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
	{'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
	{'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
	{'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
	{'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
	{'.', {".....", ".....", ".....", ".....", ".....", ".....", "..#.."}},
	{',', {".....", ".....", ".....", ".....", ".....", "..#..", ".#..."}},
	{':', {".....", "..#..", "..#..", ".....", "..#..", "..#..", "....."}},
	{';', {".....", "..#..", "..#..", ".....", "..#..", "..#..", ".#..."}},
	{'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
	{'"', {".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....."}},
	{'-', {".....", ".....", ".....", ".###.", ".....", ".....", "....."}},
	{'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
	{'*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."}},
	{'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
	{'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
	{'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
	{'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
	{'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
	{')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
	{'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
	{']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
	{'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
	{'%', {"##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"}},
	{'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
	{'@', {".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".####"}},
	{'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
	{'^', {"..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."}},
	{'~', {".....", ".....", ".#...", "#.#.#", "...#.", ".....", "....."}},
	{'|', {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
	{'$', {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."}},
	{1, {".....", ".#.#.", "#####", "#####", ".###.", "..#..", "....."}}, /* heart */
	{2, {"..#..", "..#..", "#####", ".###.", ".#.#.", "#...#", "....."}}, /* star */
	{3, {"#####", "#####", "#####", "#####", ".###.", "..#..", "....."}}, /* shield */
};

struct Run {
	uint8_t row;
	uint8_t x0;
	uint8_t len;
};

struct Glyph {
	uint8_t count = 0;
	Run runs[24];
};

const Glyph *glyphFor(unsigned char c)
{
	static const std::vector<Glyph> table = [] {
		std::vector<Glyph> t(128);
		for (const GlyphDef &def : FontDefs) {
			Glyph g;
			for (uint8_t row = 0; row < 7; ++row) {
				const char *r = def.rows[row];
				uint8_t x = 0;
				while (x < 5) {
					if (r[x] != '#') {
						++x;
						continue;
					}
					uint8_t start = x;
					while (x < 5 && r[x] == '#')
						++x;
					if (g.count < 24)
						g.runs[g.count++] = {row, start, uint8_t(x - start)};
				}
			}
			t[def.ch] = g;
		}
		return t;
	}();
	if (c >= 128)
		c = '?';
	c = static_cast<unsigned char>(std::toupper(c));
	return &table[c];
}

constexpr float GlyphAdvance = 6.0f;
constexpr float SpaceAdvance = 4.0f;

void drawTextRaw(Batch &b, const std::string &text, float x, float y, float px, uint32_t color)
{
	float cx = x;
	for (char ch : text) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c == ' ') {
			cx += SpaceAdvance * px;
			continue;
		}
		const Glyph *g = glyphFor(c);
		for (uint8_t i = 0; i < g->count; ++i) {
			const Run &r = g->runs[i];
			b.rect(cx + float(r.x0) * px, y + float(r.row) * px, float(r.len) * px, px, color);
		}
		cx += GlyphAdvance * px;
	}
}

/* ------------------------------------------------------------------ */
/* Themes                                                              */
/* ------------------------------------------------------------------ */

struct Theme {
	const char *name;
	uint32_t bgTop, bgBottom;
	uint32_t panel, edge, boardBg, grid;
	uint32_t text, dim, accent, accent2, shadow;
	uint32_t pieces[11];
	uint32_t garbage, gold, bomb;
};

const Theme Themes[] = {
	{"Neon Night",
	 0x0b0620ff,
	 0x1c0c40ff,
	 0x150d30e6,
	 0x4b2f99ff,
	 0x09051af2,
	 0xffffff12,
	 0xf5f3ffff,
	 0xa79bdcff,
	 0xff4fd8ff,
	 0x3ee8ffff,
	 0x00000099,
	 {0xff4fa3ff, 0xa3ff4fff, 0x3ee8ffff, 0xffa53eff, 0xb26bffff, 0xffe14fff, 0x2ee6b0ff, 0x8f3bffff, 0x7a5cffff,
	  0xa04dffff, 0x6f2fd6ff},
	 0x5d587aff,
	 0xffd34dff,
	 0x26232fff},
	{"Cozy Cafe",
	 0x2b1d16ff,
	 0x4a3226ff,
	 0x3a281ee6,
	 0x8a6248ff,
	 0x231711f2,
	 0xfff4e01a,
	 0xfff4e0ff,
	 0xd2b49aff,
	 0xf4a261ff,
	 0x9bd18bff,
	 0x00000088,
	 {0xe9c46aff, 0x9bd18bff, 0xf28482ff, 0x84a9f2ff, 0xf6bd60ff, 0xc3a6e8ff, 0x84dcc6ff, 0x7d5a8aff, 0x6d597aff,
	  0x8e6c9eff, 0x5e4b6bff},
	 0x7a6658ff,
	 0xffd166ff,
	 0x3a2a22ff},
	{"Retro Arcade",
	 0x000000ff,
	 0x0a0a1eff,
	 0x0b0b1ce6,
	 0x2d2dffff,
	 0x000000f2,
	 0x2d2dff26,
	 0xffffffff,
	 0x8a8affff,
	 0xff2d2dff,
	 0xffff2dff,
	 0x000000aa,
	 {0xff2d2dff, 0x2dff5aff, 0x2d9dffff, 0xffff2dff, 0xff2dffff, 0x2dffffff, 0xff9a2dff, 0x9d2dffff, 0xb02dffff,
	  0x7a2dffff, 0xc42dffff},
	 0x55557aff,
	 0xffd700ff,
	 0x1c1c1cff},
	{"Candy Pop",
	 0xffe1f0ff,
	 0xdfe9ffff,
	 0xffffffd9,
	 0xf5a3d0ff,
	 0xfff8fcf0,
	 0x3a2a5a14,
	 0x3a2a5aff,
	 0x8a7aa8ff,
	 0xff5fa2ff,
	 0x4fa8ffff,
	 0xffffffaa,
	 {0xff8fb8ff, 0x8fd3ffff, 0xffd36eff, 0x8ee89aff, 0xc6a0ffff, 0xffa98aff, 0x6fdcd5ff, 0x9b72cfff, 0x8a5fc2ff,
	  0xa77fd9ff, 0x7c52b8ff},
	 0xb9b4c9ff,
	 0xffc83dff,
	 0x4a3a5aff},
	{"Midnight Lo-fi",
	 0x0f1b2dff,
	 0x1f2d4dff,
	 0x16233ce6,
	 0x3d5a80ff,
	 0x0b1423f2,
	 0xffffff10,
	 0xe8eefcff,
	 0x8fa3c7ff,
	 0xf7a8c4ff,
	 0x9fd8cbff,
	 0x00000088,
	 {0xf7a8c4ff, 0x9fd8cbff, 0xffd8a8ff, 0xa8c5f7ff, 0xc9b6f2ff, 0xf2c6b6ff, 0xb6f2d8ff, 0x8c7ae6ff, 0x7b6bd1ff,
	  0x9a86f0ff, 0x6c5cc0ff},
	 0x4b5a75ff,
	 0xffd27aff,
	 0x1a2233ff},
};

constexpr int ThemeCount = int(sizeof(Themes) / sizeof(Themes[0]));

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

void roundRect(Batch &b, float x, float y, float w, float h, float r, uint32_t c)
{
	r = std::min(r, std::min(w, h) * 0.5f);
	if (r < 1.0f || w <= 0.0f || h <= 0.0f) {
		b.rect(x, y, w, h, c);
		return;
	}
	b.rect(x + r, y, w - 2.0f * r, h, c);
	b.rect(x, y + r, r, h - 2.0f * r, c);
	b.rect(x + w - r, y + r, r, h - 2.0f * r, c);
	const float cx[4] = {x + r, x + w - r, x + w - r, x + r};
	const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
	const float start[4] = {Pi, 1.5f * Pi, 0.0f, 0.5f * Pi};
	constexpr int Segments = 5;
	for (int k = 0; k < 4; ++k) {
		for (int s = 0; s < Segments; ++s) {
			const float a0 = start[k] + (0.5f * Pi) * float(s) / float(Segments);
			const float a1 = start[k] + (0.5f * Pi) * float(s + 1) / float(Segments);
			b.tri(cx[k], cy[k], cx[k] + std::cos(a0) * r, cy[k] + std::sin(a0) * r,
			      cx[k] + std::cos(a1) * r, cy[k] + std::sin(a1) * r, c, c, c);
		}
	}
}

void circle(Batch &b, float cx, float cy, float r, uint32_t c, int segments = 14)
{
	for (int s = 0; s < segments; ++s) {
		const float a0 = 2.0f * Pi * float(s) / float(segments);
		const float a1 = 2.0f * Pi * float(s + 1) / float(segments);
		b.tri(cx, cy, cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r,
		      cy + std::sin(a1) * r, c, c, c);
	}
}

void panel(Batch &b, const Theme &th, float x, float y, float w, float h, float u)
{
	roundRect(b, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, u * 0.4f, withAlpha(th.edge, 0.85f));
	roundRect(b, x, y, w, h, u * 0.35f, th.panel);
}

void bar(Batch &b, float x, float y, float w, float h, float fill, uint32_t bg, uint32_t fg)
{
	b.rect(x, y, w, h, bg);
	b.rect(x, y, w * clamp01(fill), h, fg);
}

struct DrawCtx {
	Batch &b;
	const Game &g;
	const Theme &th;
	float u;
	float t;
	bool disco;
};

uint32_t pieceColor(const DrawCtx &c, int type, float x, float y)
{
	uint32_t base = c.th.pieces[std::clamp(type, 0, 10)];
	if (type == PIECE_BOMB)
		base = c.th.bomb;
	if (c.disco)
		base = mix(base, hsv((x + y) * 0.07f + c.t * 0.6f, 0.75f, 1.0f), 0.65f);
	return base;
}

void drawBlock(const DrawCtx &c, float x, float y, float s, uint32_t base, float alpha)
{
	const float inset = std::max(1.0f, s * 0.07f);
	c.b.rect(x, y, s, s, withAlpha(darken(base, 0.5f), alpha));
	c.b.rect(x + inset, y + inset, s - 2.0f * inset, s - 2.0f * inset, withAlpha(base, alpha));
	c.b.rect(x + inset, y + inset, s - 2.0f * inset, s * 0.16f, withAlpha(lighten(base, 0.45f), alpha));
	c.b.rect(x + inset, y + inset, s * 0.12f, s - 2.0f * inset, withAlpha(lighten(base, 0.25f), alpha));
	c.b.rect(x + inset, y + s - inset - s * 0.12f, s - 2.0f * inset, s * 0.12f,
		 withAlpha(darken(base, 0.22f), alpha));
}

void drawCursedEyes(const DrawCtx &c, float x, float y, float s, float alpha, float seed)
{
	const float blink = std::fmod(c.t + seed, 3.7f) < 0.12f ? 0.25f : 1.0f;
	const float ew = s * 0.2f;
	const float eh = s * 0.24f * blink;
	const float ey = y + s * 0.34f + (s * 0.24f - eh) * 0.5f;
	const float look = std::sin(c.t * 1.7f + seed) * s * 0.04f;
	c.b.rect(x + s * 0.22f, ey, ew, eh, withAlpha(0xffffffff, alpha));
	c.b.rect(x + s * 0.58f, ey, ew, eh, withAlpha(0xffffffff, alpha));
	if (blink > 0.5f) {
		c.b.rect(x + s * 0.27f + look, ey + eh * 0.35f, ew * 0.5f, eh * 0.5f, withAlpha(0x1a1028ff, alpha));
		c.b.rect(x + s * 0.63f + look, ey + eh * 0.35f, ew * 0.5f, eh * 0.5f, withAlpha(0x1a1028ff, alpha));
	}
}

void drawBomb(const DrawCtx &c, float x, float y, float s, float alpha)
{
	const float cx = x + s * 0.5f;
	const float cy = y + s * 0.56f;
	circle(c.b, cx, cy, s * 0.42f, withAlpha(darken(c.th.bomb, 0.3f), alpha));
	circle(c.b, cx, cy, s * 0.36f, withAlpha(c.th.bomb, alpha));
	circle(c.b, cx - s * 0.12f, cy - s * 0.12f, s * 0.08f, withAlpha(0xffffff99, alpha), 8);
	c.b.rect(cx - s * 0.06f, y + s * 0.04f, s * 0.12f, s * 0.14f, withAlpha(0x8a7a6aff, alpha));
	const float flicker = 0.6f + 0.4f * std::sin(c.t * 37.0f) * std::sin(c.t * 23.0f);
	circle(c.b, cx, y + s * 0.04f, s * (0.08f + 0.06f * flicker), withAlpha(0xffb347ff, alpha), 8);
	circle(c.b, cx, y + s * 0.04f, s * 0.05f, withAlpha(0xfff6c0ff, alpha), 6);
}

void drawCellAt(const DrawCtx &c, float x, float y, float s, int kind, int type, float bx, float by, float alpha)
{
	if (kind == CELL_GARBAGE) {
		const uint32_t base = c.disco ? mix(c.th.garbage, hsv(c.t * 0.5f + bx * 0.1f, 0.5f, 0.9f), 0.4f)
					      : c.th.garbage;
		drawBlock(c, x, y, s, base, alpha);
		c.b.rect(x + s * 0.3f, y + s * 0.3f, s * 0.4f, s * 0.4f, withAlpha(darken(base, 0.35f), alpha));
		return;
	}
	if (kind == CELL_GOLD) {
		const float shimmer = 0.5f + 0.5f * std::sin(c.t * 6.0f + bx * 0.8f + by * 0.5f);
		drawBlock(c, x, y, s, mix(c.th.gold, 0xfff8dcff, shimmer * 0.45f), alpha);
		const float sp = std::fmod(c.t * 1.3f + bx * 0.37f + by * 0.21f, 1.0f);
		if (sp < 0.25f) {
			const float k = sp / 0.25f;
			c.b.rect(x + s * 0.65f, y + s * 0.2f, s * 0.08f, s * 0.3f * k, withAlpha(0xffffffff, alpha));
		}
		return;
	}
	if (type == PIECE_BOMB) {
		drawBomb(c, x, y, s, alpha);
		return;
	}
	drawBlock(c, x, y, s, pieceColor(c, type, bx, by), alpha);
	if (isCursed(type))
		drawCursedEyes(c, x, y, s, alpha, bx * 1.3f + by * 0.7f);
}

void drawMiniPiece(const DrawCtx &c, const Piece &p, float cx, float cy, float cell, float alpha)
{
	Offset cells[5];
	const int n = pieceCells(p.type, 0, cells);
	int minX = 9, maxX = -9, minY = 9, maxY = -9;
	for (int i = 0; i < n; ++i) {
		minX = std::min(minX, cells[i].x);
		maxX = std::max(maxX, cells[i].x);
		minY = std::min(minY, cells[i].y);
		maxY = std::max(maxY, cells[i].y);
	}
	const float w = float(maxX - minX + 1) * cell;
	const float h = float(maxY - minY + 1) * cell;
	for (int i = 0; i < n; ++i) {
		const float x = cx - w * 0.5f + float(cells[i].x - minX) * cell;
		const float y = cy - h * 0.5f + float(cells[i].y - minY) * cell;
		drawCellAt(c, x, y, cell, p.golden ? CELL_GOLD : CELL_BLOCK, p.type, float(i), 0.0f, alpha);
	}
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

struct Layout {
	float u = 16.0f;
	float m = 8.0f;
	float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
	bool hasLeft = false;
	bool hasRight = false;
	float lx = 0.0f, lw = 0.0f;
	float rx = 0.0f, rw = 0.0f;
};

Layout computeLayout(float w, float h, bool panels)
{
	Layout L;
	L.m = std::max(6.0f, std::min(w, h) * 0.022f);
	const float availH = h - 2.0f * L.m;
	if (!panels) {
		L.u = std::min((w - 2.0f * L.m) / float(BoardW), availH / float(BoardH));
	} else if (w / std::max(1.0f, h) >= 0.95f) {
		/* landscape: stats | board | queue+feed */
		const float cols = 6.4f + float(BoardW) + 7.0f;
		L.u = std::min((w - 4.0f * L.m) / cols, availH / float(BoardH));
		L.hasLeft = L.hasRight = true;
	} else {
		/* portrait: board | everything */
		const float cols = float(BoardW) + 7.6f;
		L.u = std::min((w - 3.0f * L.m) / cols, availH / float(BoardH));
		L.hasRight = true;
	}
	L.u = std::max(4.0f, L.u >= 10.0f ? std::floor(L.u) : L.u);
	L.bw = L.u * float(BoardW);
	L.bh = L.u * float(BoardH);
	L.by = (h - L.bh) * 0.5f;

	if (L.hasLeft) {
		L.lw = L.u * 6.4f;
		L.rw = L.u * 7.0f;
		const float total = L.lw + L.bw + L.rw + 2.0f * L.m;
		L.lx = (w - total) * 0.5f;
		L.bx = L.lx + L.lw + L.m;
		L.rx = L.bx + L.bw + L.m;
	} else if (L.hasRight) {
		L.rw = L.u * 7.6f;
		const float total = L.bw + L.rw + L.m;
		L.bx = (w - total) * 0.5f;
		L.rx = L.bx + L.bw + L.m;
	} else {
		L.bx = (w - L.bw) * 0.5f;
	}
	return L;
}

/* ------------------------------------------------------------------ */
/* Panel sections                                                      */
/* ------------------------------------------------------------------ */

const char *fxName(int e)
{
	switch (e) {
	case FX_FLIP:
		return "UPSIDE DOWN";
	case FX_MIRROR:
		return "MIRRORED";
	case FX_TURBO:
		return "TURBO";
	case FX_SLOW:
		return "CHILL PILL";
	case FX_DARK:
		return "LIGHTS OUT";
	case FX_SHAKE:
		return "QUAKE";
	case FX_WOBBLE:
		return "WOBBLE";
	case FX_DISCO:
		return "DISCO";
	case FX_HYPE:
		return "HYPE X2";
	default:
		return "CHILL MODE";
	}
}

uint32_t fxColor(int e)
{
	switch (e) {
	case FX_SLOW:
		return 0x5eea9aff;
	case FX_DISCO:
		return 0xffc94dff;
	case FX_HYPE:
		return 0xff7ad9ff;
	case FX_CHILL:
		return 0x7dd3fcff;
	default:
		return 0xff6b8bff;
	}
}

std::string formatTime(float seconds)
{
	const int total = std::max(0, int(seconds));
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
	return buf;
}

std::string formatScore(int64_t v)
{
	std::string s = std::to_string(v);
	std::string out;
	const int n = int(s.size());
	for (int i = 0; i < n; ++i) {
		out.push_back(s[size_t(i)]);
		const int rest = n - i - 1;
		if (rest > 0 && rest % 3 == 0)
			out.push_back(',');
	}
	return out;
}

float fitPx(const std::string &text, float maxW, float px)
{
	const float w = textWidth(text, px);
	return w > maxW && w > 0.0f ? px * maxW / w : px;
}

struct Column {
	const DrawCtx &c;
	float x, w, y, bottom;
	float pad;

	float inner() const { return w - 2.0f * pad; }
	bool fits(float h) const { return y + h <= bottom; }
};

void sectionTitle(Column &col)
{
	const DrawCtx &c = col.c;
	const std::string title = "STACK PARTY";
	const float px = fitPx(title, col.inner(), c.u * 0.14f);
	const float tw = textWidth(title, px);
	float x = col.x + (col.w - tw) * 0.5f;
	for (size_t i = 0; i < title.size(); ++i) {
		const char ch = title[i];
		if (ch == ' ') {
			x += SpaceAdvance * px;
			continue;
		}
		const float bounce = std::sin(c.t * 4.0f + float(i) * 0.55f) * px * 1.2f;
		const uint32_t color = c.disco ? hsv(float(i) * 0.08f + c.t * 0.5f, 0.7f, 1.0f) : c.th.pieces[i % 7];
		drawText(c.b, std::string(1, ch), x, col.y + px * 1.5f + bounce, px, color, c.th.shadow);
		x += GlyphAdvance * px;
	}
	col.y += px * 7.0f + px * 4.0f;
}

void sectionScore(Column &col, bool compact)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float label = c.u * 0.075f;
	const float x = col.x + col.pad;
	drawText(c.b, "SCORE", x, col.y, label, c.th.dim);
	if (g.fxActive(FX_HYPE)) {
		const float pulse = 0.5f + 0.5f * std::sin(c.t * 10.0f);
		const std::string x2 = "X2";
		drawText(c.b, x2, col.x + col.w - col.pad - textWidth(x2, label), col.y, label,
			 mix(0xff7ad9ff, 0xffffffff, pulse));
	}
	col.y += label * 9.0f;
	const std::string score = formatScore(g.score);
	const float spx = fitPx(score, col.inner(), c.u * 0.16f);
	drawText(c.b, score, x, col.y, spx, c.th.text, c.th.shadow);
	col.y += spx * 7.0f + label * 5.0f;

	const float vpx = c.u * 0.1f;
	auto stat = [&](const char *name, const std::string &value, float sx, float sw) {
		drawText(c.b, name, sx, col.y, label, c.th.dim);
		drawText(c.b, value, sx, col.y + label * 9.0f, fitPx(value, sw, vpx), c.th.text, c.th.shadow);
	};
	const float half = col.inner() * 0.5f;
	stat("LEVEL", std::to_string(g.level), x, half);
	stat("LINES", std::to_string(g.lines), x + half, half);
	col.y += label * 9.0f + vpx * 7.0f + label * 4.0f;
	if (!compact || col.fits(label * 30.0f)) {
		stat("TIME", formatTime(g.elapsed), x, half);
		stat("BEST", formatScore(std::max(g.best, g.score)), x + half, half);
		col.y += label * 9.0f + vpx * 7.0f + label * 4.0f;
	}
	if (g.combo > 0) {
		const std::string combo = "COMBO X" + std::to_string(g.combo + 1);
		drawText(c.b, combo, x, col.y, label * 1.2f, 0x7dd3fcff, c.th.shadow);
		col.y += label * 11.0f;
	}
}

void sectionMeters(Column &col)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float label = c.u * 0.07f;
	const float x = col.x + col.pad;
	const float bh = c.u * 0.22f;
	const uint32_t trough = withAlpha(c.th.dim, 0.25f);

	drawText(c.b, "HYPE", x, col.y, label, c.th.dim);
	col.y += label * 9.0f;
	const float hypeFill = g.fxActive(FX_HYPE) ? g.fxRemaining(FX_HYPE) / std::max(1.0f, g.fxMax[FX_HYPE]) : g.hype;
	const uint32_t hypeCol = g.fxActive(FX_HYPE) ? hsv(c.t * 0.8f, 0.6f, 1.0f) : 0xff7ad9ff;
	bar(c.b, x, col.y, col.inner(), bh, hypeFill, trough, hypeCol);
	col.y += bh + label * 5.0f;

	if (g.cfg.chaosThreshold > 0) {
		drawText(c.b, "CHAOS WHEEL", x, col.y, label, c.th.dim);
		col.y += label * 9.0f;
		const float fill = float(g.chaos) / float(std::max(1, g.cfg.chaosThreshold));
		bar(c.b, x, col.y, col.inner(), bh, g.wheel.active ? 1.0f : fill, trough,
		    g.wheel.active ? hsv(c.t * 2.0f, 0.7f, 1.0f) : 0xffc94dff);
		col.y += bh + label * 5.0f;
	}

	drawText(c.b, "HELPERS", x, col.y, label, 0x5eea9aff);
	const std::string trolls = "TROLLS";
	drawText(c.b, trolls, col.x + col.w - col.pad - textWidth(trolls, label), col.y, label, 0xff6b8bff);
	col.y += label * 9.0f;
	const float total = g.helpPower + g.trollPower;
	const float ratio = total > 0.01f ? g.helpPower / total : 0.5f;
	c.b.rect(x, col.y, col.inner(), bh, 0xff6b8bff);
	c.b.rect(x, col.y, col.inner() * ratio, bh, 0x5eea9aff);
	c.b.rect(x + col.inner() * ratio - 1.0f, col.y - bh * 0.25f, 2.0f, bh * 1.5f, 0xffffffff);
	col.y += bh + label * 6.0f;
}

void sectionRanks(Column &col)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float label = c.u * 0.07f;
	const float x = col.x + col.pad;
	auto list = [&](const char *title, const std::vector<RankEntry> &ranks, uint32_t color) {
		if (!col.fits(label * 12.0f))
			return;
		drawText(c.b, title, x, col.y, label, color);
		col.y += label * 10.0f;
		if (ranks.empty()) {
			drawText(c.b, "- NOBODY YET -", x, col.y, label * 0.9f, withAlpha(c.th.dim, 0.7f));
			col.y += label * 10.0f;
		}
		for (size_t i = 0; i < ranks.size() && col.fits(label * 10.0f); ++i) {
			const std::string pts = std::to_string(ranks[i].points);
			const float ptsW = textWidth(pts, label);
			const std::string name = std::to_string(i + 1) + " " + ranks[i].name;
			drawText(c.b, name, x, col.y, fitPx(name, col.inner() - ptsW - label * 3.0f, label),
				 i == 0 ? c.th.text : c.th.dim);
			drawText(c.b, pts, col.x + col.w - col.pad - ptsW, col.y, label, color);
			col.y += label * 9.5f;
		}
		col.y += label * 4.0f;
	};
	list("TOP HELPERS", g.topHelpers, 0x5eea9aff);
	list("TOP TROLLS", g.topTrolls, 0xff6b8bff);
}

void sectionQueue(Column &col, bool compactHold)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float label = c.u * 0.075f;
	const float x = col.x + col.pad;

	if (compactHold) {
		/* NEXT and HOLD side by side */
		const float half = col.inner() * 0.5f;
		drawText(c.b, "NEXT", x, col.y, label, c.th.dim);
		drawText(c.b, "HOLD", x + half, col.y, label, c.th.dim);
		col.y += label * 10.0f;
		const float cell = c.u * 0.5f;
		if (!g.queue.empty())
			drawMiniPiece(c, g.queue.front(), x + half * 0.5f, col.y + cell * 1.3f, cell, 1.0f);
		if (g.hasHold)
			drawMiniPiece(c, g.hold, x + half * 1.5f, col.y + cell * 1.3f, cell, g.holdUsed ? 0.4f : 1.0f);
		col.y += cell * 2.8f;
		const float small = c.u * 0.32f;
		for (size_t i = 1; i < g.queue.size() && i < size_t(QueueSize); ++i)
			drawMiniPiece(c, g.queue[i], x + half * 0.5f + float(i - 1) * small * 3.4f - small * 1.7f,
				      col.y + small * 1.2f, small, 0.7f);
		col.y += small * 2.8f;
		return;
	}

	drawText(c.b, "NEXT", x, col.y, label, c.th.dim);
	col.y += label * 10.0f;
	const float cell = c.u * 0.55f;
	if (!g.queue.empty()) {
		const float cy = col.y + cell * 1.2f;
		c.b.rect(x, col.y - cell * 0.1f, col.inner(), cell * 2.6f, withAlpha(c.th.boardBg, 0.6f));
		drawMiniPiece(c, g.queue.front(), col.x + col.w * 0.5f, cy, cell, 1.0f);
		col.y += cell * 2.8f;
	}
	const float small = c.u * 0.38f;
	const size_t extra = std::min(g.queue.size(), size_t(QueueSize));
	if (extra > 1) {
		const float slot = col.inner() / float(extra - 1);
		for (size_t i = 1; i < extra; ++i)
			drawMiniPiece(c, g.queue[i], x + slot * (float(i) - 0.5f), col.y + small * 1.3f, small, 0.75f);
		col.y += small * 3.0f;
	}
	col.y += label * 2.0f;
	drawText(c.b, "HOLD", x, col.y, label, c.th.dim);
	col.y += label * 10.0f;
	if (g.hasHold)
		drawMiniPiece(c, g.hold, col.x + col.w * 0.5f, col.y + cell * 1.1f, cell, g.holdUsed ? 0.4f : 1.0f);
	else
		drawTextCentered(c.b, "EMPTY", col.x + col.w * 0.5f, col.y + cell * 0.8f, label,
				 withAlpha(c.th.dim, 0.5f));
	col.y += cell * 2.6f;
}

void sectionEffects(Column &col)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float label = c.u * 0.068f;
	const float x = col.x + col.pad;

	if (g.shields > 0 && col.fits(label * 10.0f)) {
		drawText(c.b, "SHIELDS", x, col.y, label, 0x5eea9aff);
		std::string icons(size_t(g.shields), '\x03');
		drawText(c.b, icons, col.x + col.w - col.pad - textWidth(icons, label), col.y, label, 0x5eea9aff);
		col.y += label * 10.0f;
	}
	for (int e = 0; e < FX_COUNT; ++e) {
		if (!g.fxActive(e) || !col.fits(label * 12.0f))
			continue;
		const uint32_t color = fxColor(e);
		const std::string secs = std::to_string(int(std::ceil(g.fxRemaining(e)))) + "S";
		drawText(c.b, fxName(e), x, col.y, label, color);
		drawText(c.b, secs, col.x + col.w - col.pad - textWidth(secs, label), col.y, label, c.th.dim);
		col.y += label * 8.5f;
		bar(c.b, x, col.y, col.inner(), label * 1.4f, g.fxRemaining(e) / std::max(0.01f, g.fxMax[size_t(e)]),
		    withAlpha(c.th.dim, 0.2f), color);
		col.y += label * 4.0f;
	}
}

void sectionChatPlays(Column &col)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	if (g.cfg.chatPlays == ChatPlaysMode::Off)
		return;
	const float label = c.u * 0.068f;
	const float x = col.x + col.pad;
	const bool democracy = g.cfg.chatPlays == ChatPlaysMode::Democracy;
	drawText(c.b, democracy ? "CHAT VOTES" : "CHAT PLAYS", x, col.y, label, c.th.accent2);
	const std::string mode = democracy ? "DEMOCRACY" : "ANARCHY";
	drawText(c.b, mode, col.x + col.w - col.pad - textWidth(mode, label * 0.85f), col.y, label * 0.85f,
		 c.th.accent);
	col.y += label * 10.0f;
	if (!democracy) {
		const std::string hint = g.cfg.prefix + "LEFT " + g.cfg.prefix + "RIGHT " + g.cfg.prefix + "ROT " +
					 g.cfg.prefix + "DROP";
		for (const std::string &line : wrapText(hint, col.inner(), label * 0.9f)) {
			drawText(c.b, line, x, col.y, label * 0.9f, c.th.dim);
			col.y += label * 9.0f;
		}
		col.y += label * 2.0f;
		return;
	}
	static const char *const names[7] = {"L", "R", "ROT", "CCW", "DN", "DROP", "HOLD"};
	int maxVotes = 1;
	for (int v : g.votes)
		maxVotes = std::max(maxVotes, v);
	const float slot = col.inner() / 7.0f;
	const float bh = c.u * 0.9f;
	for (int i = 0; i < 7; ++i) {
		const float fill = float(g.votes[size_t(i)]) / float(maxVotes);
		const float sx = x + slot * float(i);
		c.b.rect(sx + slot * 0.15f, col.y, slot * 0.7f, bh, withAlpha(c.th.dim, 0.18f));
		c.b.rect(sx + slot * 0.15f, col.y + bh * (1.0f - fill), slot * 0.7f, bh * fill, c.th.accent2);
		const float lpx = std::min(label * 0.7f, slot / (GlyphAdvance * float(std::strlen(names[i]))));
		drawTextCentered(c.b, names[i], sx + slot * 0.5f, col.y + bh + label * 2.0f, lpx, c.th.dim);
	}
	col.y += bh + label * 10.0f;
	bar(c.b, x, col.y, col.inner(), label * 1.2f, g.voteTimer / std::max(0.1f, g.cfg.voteSeconds),
	    withAlpha(c.th.dim, 0.2f), c.th.accent);
	col.y += label * 4.0f;
	if (g.lastVoteResultAge < 2.5f && col.fits(label * 10.0f)) {
		drawText(c.b, g.lastVoteResult, x, col.y, fitPx(g.lastVoteResult, col.inner(), label * 0.9f),
			 c.th.text);
		col.y += label * 10.0f;
	}
}

void sectionFeed(Column &col, float reserveBottom)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float px = c.u * 0.064f;
	const float x = col.x + col.pad;
	const float lineH = px * 9.0f;
	if (!col.fits(lineH * 3.0f + reserveBottom))
		return;
	drawText(c.b, "CHAT LIVE", x, col.y, px * 1.05f, c.th.dim);
	const float dotA = 0.5f + 0.5f * std::sin(c.t * 4.0f);
	circle(c.b, col.x + col.w - col.pad - px * 3.0f, col.y + px * 3.5f, px * 2.5f, withAlpha(0xff4d6dff, dotA), 10);
	col.y += lineH * 1.3f;

	/* newest at the top, as many as fit */
	for (auto it = g.feed.rbegin(); it != g.feed.rend(); ++it) {
		const std::vector<std::string> lines = wrapText(it->text, col.inner(), px);
		const float h = float(lines.size()) * lineH + px * 3.0f;
		if (!col.fits(h + reserveBottom))
			break;
		const float fade = it->age > 13.0f ? clamp01((16.0f - it->age) / 3.0f) : 1.0f;
		const float slide = clamp01(it->age * 5.0f);
		c.b.rect(x - col.pad * 0.5f, col.y - px, px * 0.8f, h - px * 2.0f, withAlpha(it->color, fade));
		float ly = col.y;
		for (const std::string &line : lines) {
			drawText(c.b, line, x + px * 1.5f + (1.0f - slide) * col.w * 0.3f, ly, px,
				 withAlpha(it->color, fade));
			ly += lineH;
		}
		col.y += h;
	}
}

void sectionStatus(Column &col)
{
	const DrawCtx &c = col.c;
	const Game &g = c.g;
	const float px = c.u * 0.058f;
	const float y = col.bottom - px * 9.0f;
	const float x = col.x + col.pad;
	std::string status = g.chatStatus.empty() ? "CHAT OFF" : g.chatStatus;
	uint32_t dot = g.chatConnected ? 0x5eea9aff : 0xffc94dff;
	if (g.chatStatus.empty())
		dot = 0x8a8a8aff;
	if (g.chatMuted) {
		status = "CHAT EFFECTS PAUSED";
		dot = 0xff6b8bff;
	} else if (g.fxActive(FX_CHILL)) {
		status = "CHILL MODE " + std::to_string(int(g.fxRemaining(FX_CHILL))) + "S";
		dot = 0x7dd3fcff;
	}
	circle(c.b, x + px * 2.5f, y + px * 3.5f, px * 2.2f, dot, 10);
	drawText(c.b, status, x + px * 7.0f, y, fitPx(status, col.inner() - px * 7.0f, px), c.th.dim);
}

/* ------------------------------------------------------------------ */
/* Board                                                               */
/* ------------------------------------------------------------------ */

void drawBoard(const DrawCtx &c, const Layout &L)
{
	const Game &g = c.g;
	Batch &b = c.b;
	const float u = L.u;
	const float cx = L.bx + L.bw * 0.5f;
	const float cy = L.by + L.bh * 0.5f;

	/* board transform: quake + kick + wobble + flip */
	float ox = 0.0f;
	float oy = g.kick * u * 0.12f;
	const float shakeAmp = (g.fxActive(FX_SHAKE) ? 0.22f : 0.0f) + g.shakeImpulse * 0.3f;
	if (shakeAmp > 0.0f) {
		ox += std::sin(c.t * 53.0f) * std::cos(c.t * 17.0f) * shakeAmp * u;
		oy += std::cos(c.t * 47.0f) * std::sin(c.t * 29.0f) * shakeAmp * u;
	}
	float angle = Pi * easeInOut(clamp01(g.flipAnim));
	float sx = 1.0f;
	float sy = 1.0f;
	if (g.fxActive(FX_WOBBLE)) {
		const float k = std::min(1.0f, g.fxRemaining(FX_WOBBLE));
		angle += std::sin(c.t * 2.7f) * 0.085f * k;
		sx += std::sin(c.t * 3.9f) * 0.035f * k;
		sy -= std::sin(c.t * 3.9f) * 0.035f * k;
	}
	const Affine xf = Affine::translate(cx + ox, cy + oy) * Affine::rotate(angle) * Affine::scale(sx, sy) *
			  Affine::translate(-cx, -cy);
	b.push(xf);

	/* frame */
	uint32_t frame = c.th.edge;
	if (g.fxActive(FX_HYPE) || c.disco)
		frame = hsv(c.t * 0.7f, 0.7f, 1.0f);
	else if (g.autopilot)
		frame = mix(c.th.edge, c.th.accent2, 0.5f + 0.5f * std::sin(c.t * 3.0f));
	roundRect(b, L.bx - u * 0.22f, L.by - u * 0.22f, L.bw + u * 0.44f, L.bh + u * 0.44f, u * 0.4f, frame);
	b.rect(L.bx, L.by, L.bw, L.bh, c.th.boardBg);
	if (c.disco) {
		for (int i = 0; i < 4; ++i) {
			const float a = c.t * 0.9f + float(i) * 1.57f;
			const float px = L.bx + L.bw * (0.5f + 0.5f * std::sin(a));
			b.tri(L.bx + L.bw * 0.5f, L.by, px - u * 1.5f, L.by + L.bh, px + u * 1.5f, L.by + L.bh,
			      withAlpha(hsv(float(i) * 0.25f + c.t * 0.3f, 0.8f, 1.0f), 0.18f), 0u, 0u);
		}
	}

	if (g.cfg.showGrid) {
		const uint32_t grid = c.th.grid;
		for (int x = 1; x < BoardW; ++x)
			b.rect(L.bx + float(x) * u - 0.5f, L.by, 1.0f, L.bh, grid);
		for (int y = 1; y < BoardH; ++y)
			b.rect(L.bx, L.by + float(y) * u - 0.5f, L.bw, 1.0f, grid);
	}

	/* lights out: a flashlight around the falling piece */
	const bool dark = g.fxActive(FX_DARK);
	Offset pc[5];
	const int pn = pieceCells(g.current.type, g.current.rot, pc);
	float pcx = 0.0f;
	float pcy = 0.0f;
	for (int i = 0; i < pn; ++i) {
		pcx += float(g.current.x + pc[i].x) + 0.5f;
		pcy += float(g.current.y + pc[i].y) + 0.5f;
	}
	if (pn > 0) {
		pcx /= float(pn);
		pcy /= float(pn);
	}
	const int gy = g.gameOver ? g.current.y : g.ghostY();
	const float ghostCy = pcy + float(gy - g.current.y);

	for (int y = 0; y < BoardH; ++y) {
		for (int x = 0; x < BoardW; ++x) {
			const Cell &cell = g.board[size_t(y * BoardW + x)];
			if (cell.kind == CELL_EMPTY)
				continue;
			float alpha = 1.0f;
			if (dark) {
				const float d1 = std::hypot(float(x) + 0.5f - pcx, float(y) + 0.5f - pcy);
				const float d2 = std::hypot(float(x) + 0.5f - pcx, float(y) + 0.5f - ghostCy);
				alpha = std::max(0.03f, clamp01(1.0f - (std::min(d1, d2) - 1.8f) / 1.6f));
			}
			drawCellAt(c, L.bx + float(x) * u, L.by + float(y) * u, u, cell.kind, cell.type, float(x),
				   float(y), alpha);
		}
	}

	for (int y = 0; y < BoardH; ++y) {
		const float f = g.rowFlash[size_t(y)];
		if (f > 0.0f)
			b.rect(L.bx, L.by + float(y) * u, L.bw, u, withAlpha(0xffffffff, f * 0.85f));
	}

	if (!g.gameOver) {
		/* ghost */
		if (g.cfg.showGhost && gy != g.current.y) {
			const uint32_t gc = g.current.golden ? c.th.gold : pieceColor(c, g.current.type, 0.0f, 0.0f);
			for (int i = 0; i < pn; ++i) {
				const int x = g.current.x + pc[i].x;
				const int y = gy + pc[i].y;
				if (y < 0)
					continue;
				const float px = L.bx + float(x) * u;
				const float py = L.by + float(y) * u;
				const float t = std::max(1.0f, u * 0.08f);
				b.rect(px + 1.0f, py + 1.0f, u - 2.0f, u - 2.0f, withAlpha(gc, 0.12f));
				b.rect(px + 1.0f, py + 1.0f, u - 2.0f, t, withAlpha(gc, 0.6f));
				b.rect(px + 1.0f, py + u - 1.0f - t, u - 2.0f, t, withAlpha(gc, 0.6f));
				b.rect(px + 1.0f, py + 1.0f, t, u - 2.0f, withAlpha(gc, 0.6f));
				b.rect(px + u - 1.0f - t, py + 1.0f, t, u - 2.0f, withAlpha(gc, 0.6f));
			}
		}
		/* falling piece */
		for (int i = 0; i < pn; ++i) {
			const int x = g.current.x + pc[i].x;
			const int y = g.current.y + pc[i].y;
			if (y < 0)
				continue;
			drawCellAt(c, L.bx + float(x) * u, L.by + float(y) * u, u,
				   g.current.golden ? CELL_GOLD : CELL_BLOCK, g.current.type, float(x), float(y), 1.0f);
		}
	}

	/* particles */
	for (const Particle &p : g.particles) {
		const float a = clamp01(1.0f - p.age / p.life);
		const uint32_t col = p.type >= 0 ? pieceColor(c, p.type, p.x, p.y) : p.color;
		b.rotRect(L.bx + p.x * u, L.by + p.y * u, p.size * u, p.size * u, p.rot, withAlpha(col, a));
	}

	/* popups live inside the board transform: when chat flips the board, they flip too */
	for (const Popup &p : g.popups) {
		const float k = clamp01(p.age / 0.25f);
		const float px = u * 0.13f * p.scale * (0.6f + 0.4f * easeOutBack(k));
		const float a = clamp01((p.life - p.age) / 0.4f);
		drawTextCentered(b, p.text, L.bx + p.x * u, L.by + p.y * u, fitPx(p.text, L.bw * 0.95f, px),
				 withAlpha(p.color, a), withAlpha(c.th.shadow, a));
	}

	b.pop();
}

void drawBoardOverlays(const DrawCtx &c, const Layout &L)
{
	const Game &g = c.g;
	Batch &b = c.b;
	const float u = L.u;
	const float cx = L.bx + L.bw * 0.5f;

	if (g.fxActive(FX_DARK)) {
		const float a = 0.35f + 0.25f * std::sin(c.t * 2.0f);
		drawTextCentered(b, "LIGHTS OUT", cx, L.by + u * 0.6f, u * 0.09f, withAlpha(c.th.dim, a));
	}
	if (g.fxActive(FX_MIRROR) && !g.gameOver) {
		const float a = 0.55f + 0.35f * std::sin(c.t * 5.0f);
		drawTextCentered(b, "<- CONTROLS SWAPPED ->", cx, L.by + L.bh - u * 0.9f,
				 fitPx("<- CONTROLS SWAPPED ->", L.bw * 0.9f, u * 0.075f), withAlpha(0xff6b8bff, a),
				 c.th.shadow);
	}
	if (g.autopilot && !g.gameOver) {
		const std::string label = "BOT IS PLAYING - TROLL IT!";
		const float px = fitPx(label, L.bw * 0.9f, u * 0.075f);
		const float w = textWidth(label, px) + px * 6.0f;
		const float a = 0.75f + 0.25f * std::sin(c.t * 3.0f);
		roundRect(b, cx - w * 0.5f, L.by + u * 0.35f, w, px * 11.0f, px * 3.0f,
			  withAlpha(c.th.boardBg, 0.85f * a));
		drawText(b, label, cx - textWidth(label, px) * 0.5f, L.by + u * 0.35f + px * 2.0f, px,
			 withAlpha(c.th.accent2, a));
	}

	if (g.paused && !g.gameOver) {
		b.rect(L.bx, L.by + L.bh * 0.38f, L.bw, L.bh * 0.2f, withAlpha(c.th.boardBg, 0.92f));
		drawTextCentered(b, "PAUSED", cx, L.by + L.bh * 0.42f, u * 0.17f, c.th.accent, c.th.shadow);
		drawTextCentered(b, "CHAT, BEHAVE", cx, L.by + L.bh * 0.51f, u * 0.075f, c.th.dim);
	}

	if (g.gameOver) {
		const float appear = clamp01(g.gameOverTimer * 3.0f);
		b.rect(L.bx, L.by, L.bw, L.bh, withAlpha(c.th.boardBg, 0.82f * appear));
		float y = L.by + L.bh * 0.22f;
		const float big = u * 0.18f;
		drawTextCentered(b, "GAME OVER", cx, y, fitPx("GAME OVER", L.bw * 0.9f, big),
				 withAlpha(0xff6b8bff, appear), c.th.shadow);
		y += big * 11.0f;
		const std::string score = formatScore(g.score);
		drawTextCentered(b, score, cx, y, fitPx(score, L.bw * 0.9f, u * 0.14f), withAlpha(c.th.text, appear),
				 c.th.shadow);
		y += u * 0.14f * 10.0f;
		if (g.newBest) {
			const uint32_t col = hsv(c.t * 0.8f, 0.6f, 1.0f);
			drawTextCentered(b, "NEW BEST!", cx, y, u * 0.1f, withAlpha(col, appear), c.th.shadow);
			y += u * 0.1f * 11.0f;
		}
		const float small = u * 0.075f;
		if (!g.topHelpers.empty()) {
			const std::string mvp = "MVP HELPER @" + g.topHelpers.front().name;
			drawTextCentered(b, mvp, cx, y, fitPx(mvp, L.bw * 0.92f, small), withAlpha(0x5eea9aff, appear));
			y += small * 11.0f;
		}
		if (!g.topTrolls.empty()) {
			const std::string troll = "TOP TROLL @" + g.topTrolls.front().name;
			drawTextCentered(b, troll, cx, y, fitPx(troll, L.bw * 0.92f, small),
					 withAlpha(0xff6b8bff, appear));
			y += small * 11.0f;
		}
		y += small * 4.0f;
		if (g.cfg.autoRestart > 0.0f) {
			const int left = std::max(0, int(std::ceil(g.cfg.autoRestart - g.gameOverTimer)));
			const std::string again = "NEW GAME IN " + std::to_string(left);
			drawTextCentered(b, again, cx, y, small, withAlpha(c.th.dim, appear));
			y += small * 11.0f;
		}
		drawTextCentered(b, "HARD DROP TO PLAY", cx, y, fitPx("HARD DROP TO PLAY", L.bw * 0.9f, small),
				 withAlpha(c.th.dim, appear * (0.6f + 0.4f * std::sin(c.t * 4.0f))));
	}
}

void drawWheel(const DrawCtx &c, const Layout &L)
{
	const Game &g = c.g;
	if (!g.wheel.active || g.wheel.options.empty())
		return;
	Batch &b = c.b;
	const float u = L.u;
	const float w = std::min(L.bw * 1.15f, u * 11.0f);
	const float h = u * 4.6f;
	const float x = L.bx + (L.bw - w) * 0.5f;
	const float pop = easeOutBack(clamp01(g.wheel.age * 4.0f));
	const float y = L.by + L.bh * 0.3f - (1.0f - pop) * u * 2.0f;
	const float cx = x + w * 0.5f;

	b.setAlpha(clamp01(pop));
	roundRect(b, x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, u * 0.5f, hsv(c.t * 0.9f, 0.7f, 1.0f));
	roundRect(b, x, y, w, h, u * 0.45f, 0x120a24f2);
	drawTextCentered(b, "CHAOS WHEEL", cx, y + u * 0.3f, u * 0.09f, 0xffc94dff, 0x00000099);

	const size_t n = g.wheel.options.size();
	const int shown = g.wheel.shown;
	const float rowY = y + u * 1.55f;
	const float slotH = u * 1.0f;
	b.rect(x + u * 0.4f, rowY + slotH - u * 0.1f, w - u * 0.8f, slotH * 1.1f, 0xffffff18);
	for (int k = -1; k <= 1; ++k) {
		const size_t idx = size_t((shown + k + int(n)) % int(n));
		const char *label = Game::commandLabel(g.wheel.options[idx]);
		const bool centre = k == 0;
		float px = centre ? u * 0.12f : u * 0.07f;
		px = fitPx(label, w * 0.85f, px);
		uint32_t col = centre ? 0xffffffff : 0xffffff66;
		if (centre && g.wheel.resolved)
			col = hsv(c.t * 2.0f, 0.6f, 1.0f);
		drawTextCentered(b, label, cx, rowY + slotH * float(k + 1) + (slotH - px * 7.0f) * 0.5f, px, col);
	}
	const std::string by = g.wheel.resolved ? "SPUN BY @" + g.wheel.by : "SPINNING...";
	drawTextCentered(b, by, cx, y + h - u * 0.55f, fitPx(by, w * 0.9f, u * 0.06f), 0xffffff99);
	b.setAlpha(1.0f);
}

void drawHelpCard(const DrawCtx &c, const Layout &L)
{
	const Game &g = c.g;
	if (g.helpCard <= 0.0f)
		return;
	Batch &b = c.b;
	const float u = L.u;
	const float a = clamp01(g.helpCard / 0.5f) * clamp01((12.0f - g.helpCard) * 4.0f);
	const float x = L.bx + u * 0.3f;
	const float w = L.bw - u * 0.6f;
	const float px = u * 0.07f;
	const std::string &p = g.cfg.prefix;

	struct Line {
		std::string text;
		uint32_t color;
	};
	std::vector<Line> lines;
	auto add = [&](const std::string &s, uint32_t col) {
		for (const std::string &l : wrapText(s, w - u * 0.6f, px))
			lines.push_back({l, col});
	};
	add("CHAT COMMANDS", 0xffc94dff);
	if (g.cfg.allowTroll)
		add("TROLL: " + p + "FLIP " + p + "MIRROR " + p + "TURBO " + p + "GARBAGE " + p + "DARK " + p +
			    "SHAKE " + p + "WOBBLE " + p + "CURSE",
		    0xff6b8bff);
	if (g.cfg.allowHelper)
		add("HELP: " + p + "SLOW " + p + "BOMB " + p + "LONG " + p + "SHIELD " + p + "SWEEP", 0x5eea9aff);
	if (g.cfg.allowFun)
		add("PARTY: " + p + "DISCO " + p + "HYPE " + p + "GG " + p + "POG " + p + "LOL " + p + "RIP " + p +
			    "WOW",
		    0xff7ad9ff);
	if (g.cfg.chatPlays != ChatPlaysMode::Off)
		add("PLAY: " + p + "LEFT " + p + "RIGHT " + p + "ROT " + p + "CCW " + p + "DOWN " + p + "DROP " + p +
			    "HOLD",
		    0x7dd3fcff);
	add("SUBS = GOLDEN PIECE, BITS = CHAOS, RAIDS = PARTY", 0xffd34dff);

	const float lineH = px * 9.5f;
	const float h = float(lines.size()) * lineH + u * 0.8f;
	const float y = L.by + (L.bh - h) * 0.5f;
	b.setAlpha(a);
	roundRect(b, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, u * 0.35f, c.th.edge);
	roundRect(b, x, y, w, h, u * 0.3f, setAlpha(c.th.boardBg, 0xf4));
	float ly = y + u * 0.4f;
	for (const Line &l : lines) {
		drawText(b, l.text, x + u * 0.3f, ly, px, l.color);
		ly += lineH;
	}
	b.setAlpha(1.0f);
}

void drawBanner(const DrawCtx &c, float w, float h, float u)
{
	const Game &g = c.g;
	if (g.banners.empty())
		return;
	Batch &b = c.b;
	const Banner &bn = g.banners.front();
	const float inT = clamp01(bn.age / 0.35f);
	const float outT = clamp01((bn.life - bn.age) / 0.35f);
	const float slide = (1.0f - easeOutBack(inT)) * -w + (1.0f - easeInOut(outT)) * w;
	const float titlePx = fitPx(bn.title, w * 0.9f, u * 0.2f);
	const float subPx = fitPx(bn.subtitle, w * 0.9f, u * 0.09f);
	const float bandH = titlePx * 7.0f + subPx * 7.0f + u * 1.2f;
	const float y = h * 0.3f - bandH * 0.5f;

	b.push(Affine::translate(slide, 0.0f));
	b.gradient(0.0f, y, w, bandH, withAlpha(bn.color, 0.92f), withAlpha(darken(bn.color, 0.45f), 0.92f));
	b.rect(0.0f, y - u * 0.08f, w, u * 0.08f, 0xffffffcc);
	b.rect(0.0f, y + bandH, w, u * 0.08f, 0xffffffcc);
	/* moving stripes */
	for (int i = 0; i < 12; ++i) {
		const float sx = std::fmod(float(i) * w / 6.0f + c.t * u * 4.0f, w * 2.0f) - w * 0.25f;
		b.tri(sx, y, sx + u * 0.8f, y, sx - u * 0.6f, y + bandH, 0xffffff1c, 0xffffff1c, 0xffffff00);
	}
	const float wob = std::sin(c.t * 9.0f) * u * 0.03f;
	drawTextCentered(b, bn.title, w * 0.5f, y + u * 0.45f + wob, titlePx, 0xffffffff, 0x00000088);
	drawTextCentered(b, bn.subtitle, w * 0.5f, y + u * 0.75f + titlePx * 7.0f, subPx, 0xffffffee, 0x00000066);
	b.pop();
}

void drawBubbles(const DrawCtx &c, float w, float h, float u)
{
	Batch &b = c.b;
	for (const Bubble &bu : c.g.bubbles) {
		const float k = bu.age / bu.life;
		const float a = clamp01((bu.life - bu.age) / 0.6f) * clamp01(bu.age * 6.0f);
		const float px = u * 0.12f;
		const float x = bu.x * w + std::sin(bu.age * 3.0f + bu.x * 20.0f) * u * 0.4f;
		const float y = h * (0.95f - k * 0.65f);
		const float tw = textWidth(bu.text, px);
		const float bw = tw + px * 6.0f;
		const float bh = px * 11.0f;
		const float scale = 0.7f + 0.3f * easeOutBack(clamp01(bu.age * 4.0f));
		b.push(Affine::translate(x, y) * Affine::scale(scale, scale));
		roundRect(b, -bw * 0.5f, -bh * 0.5f, bw, bh, bh * 0.45f, withAlpha(bu.color, 0.9f * a));
		b.tri(-px * 2.0f, bh * 0.5f - 1.0f, px * 2.0f, bh * 0.5f - 1.0f, -px * 3.0f, bh * 0.5f + px * 3.0f,
		      withAlpha(bu.color, 0.9f * a), withAlpha(bu.color, 0.9f * a), withAlpha(bu.color, 0.9f * a));
		drawText(b, bu.text, -tw * 0.5f, -px * 3.5f, px, withAlpha(0x14101eff, a));
		const std::string user = "@" + bu.user;
		const float upx = px * 0.5f;
		drawTextCentered(b, user, 0.0f, bh * 0.5f + px * 4.0f, upx, withAlpha(0xffffffff, a),
				 withAlpha(0x000000aa, a));
		b.pop();
	}
}

void drawConfetti(const DrawCtx &c, float w, float h)
{
	const float scale = std::max(w, h);
	for (const Particle &p : c.g.confetti) {
		const float a = clamp01((p.life - p.age) / 0.8f);
		const float flutter = std::fabs(std::sin(p.rot * 1.3f));
		c.b.rotRect(p.x * w, p.y * h, p.size * scale, p.size * scale * (0.35f + 0.65f * flutter), p.rot,
			    withAlpha(p.color, a));
	}
}

} // namespace

/* ------------------------------------------------------------------ */
/* Affine                                                              */
/* ------------------------------------------------------------------ */

Affine Affine::translate(float x, float y)
{
	Affine m;
	m.tx = x;
	m.ty = y;
	return m;
}

Affine Affine::rotate(float radians)
{
	Affine m;
	const float s = std::sin(radians);
	const float co = std::cos(radians);
	m.a = co;
	m.b = s;
	m.c = -s;
	m.d = co;
	return m;
}

Affine Affine::scale(float sx, float sy)
{
	Affine m;
	m.a = sx;
	m.d = sy;
	return m;
}

Affine Affine::operator*(const Affine &o) const
{
	Affine r;
	r.a = a * o.a + c * o.b;
	r.b = b * o.a + d * o.b;
	r.c = a * o.c + c * o.d;
	r.d = b * o.c + d * o.d;
	r.tx = a * o.tx + c * o.ty + tx;
	r.ty = b * o.tx + d * o.ty + ty;
	return r;
}

/* ------------------------------------------------------------------ */
/* Batch                                                               */
/* ------------------------------------------------------------------ */

void Batch::begin()
{
	verts.clear();
	stack.clear();
	current = Affine();
	alphaMul = 1.0f;
}

void Batch::vertex(float x, float y, uint32_t rgba)
{
	Vertex v;
	v.x = current.a * x + current.c * y + current.tx;
	v.y = current.b * x + current.d * y + current.ty;
	uint32_t alpha = rgba & 0xffu;
	if (alphaMul < 1.0f)
		alpha = uint32_t(float(alpha) * clamp01(alphaMul) + 0.5f);
	/* GPU vertex colours are stored as bytes R, G, B, A in memory */
	v.color = (alpha << 24) | (((rgba >> 8) & 0xffu) << 16) | (((rgba >> 16) & 0xffu) << 8) |
		  ((rgba >> 24) & 0xffu);
	verts.push_back(v);
}

void Batch::rect(float x, float y, float w, float h, uint32_t color)
{
	if (w <= 0.0f || h <= 0.0f || (color & 0xffu) == 0u)
		return;
	vertex(x, y, color);
	vertex(x + w, y, color);
	vertex(x + w, y + h, color);
	vertex(x, y, color);
	vertex(x + w, y + h, color);
	vertex(x, y + h, color);
}

void Batch::gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom)
{
	if (w <= 0.0f || h <= 0.0f)
		return;
	vertex(x, y, top);
	vertex(x + w, y, top);
	vertex(x + w, y + h, bottom);
	vertex(x, y, top);
	vertex(x + w, y + h, bottom);
	vertex(x, y + h, bottom);
}

void Batch::tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c0, uint32_t c1, uint32_t c2)
{
	vertex(x0, y0, c0);
	vertex(x1, y1, c1);
	vertex(x2, y2, c2);
}

void Batch::rotRect(float cx, float cy, float w, float h, float angle, uint32_t color)
{
	if ((color & 0xffu) == 0u)
		return;
	const float s = std::sin(angle);
	const float co = std::cos(angle);
	const float hw = w * 0.5f;
	const float hh = h * 0.5f;
	const float xs[4] = {-hw, hw, hw, -hw};
	const float ys[4] = {-hh, -hh, hh, hh};
	float px[4];
	float py[4];
	for (int i = 0; i < 4; ++i) {
		px[i] = cx + xs[i] * co - ys[i] * s;
		py[i] = cy + xs[i] * s + ys[i] * co;
	}
	vertex(px[0], py[0], color);
	vertex(px[1], py[1], color);
	vertex(px[2], py[2], color);
	vertex(px[0], py[0], color);
	vertex(px[2], py[2], color);
	vertex(px[3], py[3], color);
}

void Batch::push(const Affine &m)
{
	stack.push_back(current);
	current = current * m;
}

void Batch::pop()
{
	if (stack.empty())
		return;
	current = stack.back();
	stack.pop_back();
}

void Batch::draw()
{
	if (verts.empty())
		return;

	constexpr size_t Chunk = 6144; /* multiple of 3 */
	if (!vb) {
		gs_vb_data *data = gs_vbdata_create();
		data->num = Chunk;
		data->points = static_cast<vec3 *>(bzalloc(sizeof(vec3) * Chunk));
		data->colors = static_cast<uint32_t *>(bzalloc(sizeof(uint32_t) * Chunk));
		vb = gs_vertexbuffer_create(data, GS_DYNAMIC);
		if (!vb)
			return;
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "SolidColored");
	if (!tech)
		return;
	vec4 white;
	vec4_set(&white, 1.0f, 1.0f, 1.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &white);

	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	const size_t passes = gs_technique_begin(tech);
	for (size_t pass = 0; pass < passes; ++pass) {
		if (!gs_technique_begin_pass(tech, pass))
			continue;
		for (size_t offset = 0; offset < verts.size(); offset += Chunk) {
			const size_t n = std::min(Chunk, verts.size() - offset);
			gs_vb_data *data = gs_vertexbuffer_get_data(vb);
			for (size_t i = 0; i < n; ++i) {
				const Vertex &v = verts[offset + i];
				vec3_set(&data->points[i], v.x, v.y, 0.0f);
				data->colors[i] = v.color;
			}
			gs_vertexbuffer_flush(vb);
			gs_load_vertexbuffer(vb);
			gs_load_indexbuffer(nullptr);
			gs_draw(GS_TRIS, 0, uint32_t(n));
		}
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
	gs_load_vertexbuffer(nullptr);
	gs_blend_state_pop();
}

void Batch::release()
{
	if (vb) {
		gs_vertexbuffer_destroy(vb);
		vb = nullptr;
	}
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

float textWidth(const std::string &text, float px)
{
	float w = 0.0f;
	for (char ch : text)
		w += (ch == ' ' ? SpaceAdvance : GlyphAdvance) * px;
	return w > 0.0f ? w - px : 0.0f;
}

void drawText(Batch &b, const std::string &text, float x, float y, float px, uint32_t color, uint32_t shadow)
{
	if ((shadow & 0xffu) != 0u) {
		const float off = std::max(1.0f, px * 0.6f);
		drawTextRaw(b, text, x + off, y + off, px, withAlpha(shadow, float(color & 0xffu) / 255.0f));
	}
	drawTextRaw(b, text, x, y, px, color);
}

void drawTextCentered(Batch &b, const std::string &text, float cx, float y, float px, uint32_t color, uint32_t shadow)
{
	drawText(b, text, cx - textWidth(text, px) * 0.5f, y, px, color, shadow);
}

std::vector<std::string> wrapText(const std::string &text, float maxWidth, float px)
{
	std::vector<std::string> lines;
	const size_t maxChars = std::max<size_t>(1, size_t((maxWidth + px) / (GlyphAdvance * px)));
	std::string line;
	size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && text[i] == ' ')
			++i;
		size_t j = i;
		while (j < text.size() && text[j] != ' ')
			++j;
		if (j == i)
			break;
		std::string word = text.substr(i, j - i);
		i = j;
		while (word.size() > maxChars) {
			if (!line.empty()) {
				lines.push_back(line);
				line.clear();
			}
			lines.push_back(word.substr(0, maxChars));
			word = word.substr(maxChars);
		}
		const size_t needed = line.empty() ? word.size() : line.size() + 1 + word.size();
		if (needed > maxChars && !line.empty()) {
			lines.push_back(line);
			line = word;
		} else {
			line = line.empty() ? word : line + " " + word;
		}
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

int themeCount()
{
	return ThemeCount;
}

const char *themeName(int index)
{
	return Themes[std::clamp(index, 0, ThemeCount - 1)].name;
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void renderGame(Batch &b, const Game &g, float width, float height)
{
	const Theme &th = Themes[std::clamp(g.cfg.theme, 0, ThemeCount - 1)];
	const bool disco = g.fxActive(FX_DISCO) || g.fxActive(FX_HYPE);
	const Layout L = computeLayout(width, height, g.cfg.showPanels);
	DrawCtx c{b, g, th, L.u, g.time, disco};

	if (!g.cfg.transparent) {
		uint32_t top = th.bgTop;
		uint32_t bottom = th.bgBottom;
		if (disco) {
			top = mix(top, hsv(g.time * 0.1f, 0.7f, 0.35f), 0.5f);
			bottom = mix(bottom, hsv(g.time * 0.1f + 0.3f, 0.7f, 0.35f), 0.5f);
		}
		b.gradient(0.0f, 0.0f, width, height, top, bottom);
		/* slow drifting background blocks for a cosy vibe */
		for (int i = 0; i < 14; ++i) {
			const float fi = float(i);
			const float speed = 0.012f + std::fmod(fi * 0.37f, 1.0f) * 0.02f;
			const float yy = std::fmod(fi * 0.173f + g.time * speed, 1.2f) - 0.1f;
			const float xx = std::fmod(fi * 0.618f, 1.0f);
			const float s = L.u * (0.6f + std::fmod(fi * 0.29f, 1.0f) * 0.9f);
			b.rotRect(xx * width, yy * height, s, s, g.time * 0.2f + fi,
				  withAlpha(th.pieces[i % 7], 0.07f));
		}
	}

	const float pad = L.u * 0.4f;
	if (L.hasLeft) {
		panel(b, th, L.lx, L.by, L.lw, L.bh, L.u);
		Column col{c, L.lx, L.lw, L.by + pad, L.by + L.bh - pad, pad};
		sectionTitle(col);
		sectionScore(col, false);
		if (col.fits(L.u * 3.0f))
			sectionMeters(col);
		sectionRanks(col);
	}
	if (L.hasRight) {
		panel(b, th, L.rx, L.by, L.rw, L.bh, L.u);
		Column col{c, L.rx, L.rw, L.by + pad, L.by + L.bh - pad, pad};
		const float statusH = L.u * 0.058f * 11.0f;
		if (!L.hasLeft) {
			sectionTitle(col);
			sectionScore(col, true);
			sectionQueue(col, true);
			sectionEffects(col);
			sectionChatPlays(col);
			if (col.fits(L.u * 3.5f + statusH))
				sectionMeters(col);
			sectionFeed(col, statusH + L.u * 3.0f);
			col.bottom -= statusH;
			sectionRanks(col);
			col.bottom += statusH;
		} else {
			sectionQueue(col, false);
			sectionEffects(col);
			sectionChatPlays(col);
			sectionFeed(col, statusH);
		}
		sectionStatus(col);
	}

	drawBoard(c, L);
	drawBoardOverlays(c, L);
	drawHelpCard(c, L);
	drawWheel(c, L);
	drawBubbles(c, width, height, L.u);
	drawBanner(c, width, height, L.u);
	drawConfetti(c, width, height);
}

} // namespace sp
