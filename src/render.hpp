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

#include <cstdint>
#include <string>
#include <vector>

struct gs_vertex_buffer;

namespace sp {

class Game;

/* 2D affine transform: x' = a*x + c*y + tx, y' = b*x + d*y + ty */
struct Affine {
	float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

	static Affine translate(float x, float y);
	static Affine rotate(float radians);
	static Affine scale(float sx, float sy);
	Affine operator*(const Affine &o) const;
};

/*
 * Collects coloured triangles on the CPU and submits them to the GPU in a few large
 * draw calls (one per 6k vertices) instead of one draw call per rectangle.
 */
class Batch {
public:
	Batch() = default;
	Batch(const Batch &) = delete;
	Batch &operator=(const Batch &) = delete;

	void begin();
	void rect(float x, float y, float w, float h, uint32_t color);
	void gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
	void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c0, uint32_t c1, uint32_t c2);
	void rotRect(float cx, float cy, float w, float h, float angle, uint32_t color);

	void push(const Affine &m);
	void pop();
	void setAlpha(float a) { alphaMul = a; }
	float alpha() const { return alphaMul; }

	/* Must be called from the graphics thread */
	void draw();
	void release();

	size_t size() const { return verts.size(); }

private:
	struct Vertex {
		float x, y;
		uint32_t color;
	};

	void vertex(float x, float y, uint32_t rgba);

	std::vector<Vertex> verts;
	std::vector<Affine> stack;
	Affine current;
	float alphaMul = 1.0f;
	gs_vertex_buffer *vb = nullptr;
};

float textWidth(const std::string &text, float px);
void drawText(Batch &b, const std::string &text, float x, float y, float px, uint32_t color, uint32_t shadow = 0);
void drawTextCentered(Batch &b, const std::string &text, float cx, float y, float px, uint32_t color,
		      uint32_t shadow = 0);
std::vector<std::string> wrapText(const std::string &text, float maxWidth, float px);

int themeCount();
const char *themeName(int index);

void renderGame(Batch &b, const Game &g, float width, float height);

} // namespace sp
