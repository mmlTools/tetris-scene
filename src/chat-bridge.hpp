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

#include "game.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sp {

using InboxSink = std::function<void(InboxMsg &&)>;
using StateProvider = std::function<std::string()>;

bool netStartup();
void netShutdown();

/* "https://twitch.tv/SomeOne" / "#someone" / "SomeOne" -> "someone" */
std::string normalizeChannel(const std::string &input);

/*
 * Anonymous, read-only Twitch chat client (IRC over TCP, justinfan login).
 * No OAuth token is needed: it only reads chat, subs, bits and raids.
 */
class TwitchChat {
public:
	explicit TwitchChat(InboxSink sink);
	~TwitchChat();

	void start(const std::string &channel);
	void stop();
	std::string status(bool *connected) const;

private:
	void run(std::string channel);
	bool handleLine(intptr_t sock, const std::string &line, const std::string &channel);
	void setStatus(const std::string &text, bool connected);

	InboxSink sink;
	std::thread worker;
	std::atomic<bool> running{false};
	mutable std::mutex statusLock;
	std::string statusText;
	bool isConnected = false;
};

/*
 * Tiny localhost-only HTTP endpoint so Streamer.bot, Stream Deck, Touch Portal, SAMMI,
 * YouTube/Kick chat bots, ... can trigger gimmicks:
 *   GET /cmd?user=NAME&cmd=flip
 *   GET /cmd?user=NAME&text=!bomb
 *   GET /event?type=sub|gift|giftbomb|raid|bits|follow|first&user=NAME&amount=N
 *   GET /state  (JSON, for overlays)
 */
class HttpBridge {
public:
	HttpBridge(InboxSink sink, StateProvider state);
	~HttpBridge();

	void start(int port, const std::string &key);
	void stop();
	std::string status() const;

private:
	void run(int port, std::string key);
	void serve(intptr_t client, const std::string &key);
	void setStatus(const std::string &text);

	InboxSink sink;
	StateProvider state;
	std::thread worker;
	std::atomic<bool> running{false};
	mutable std::mutex statusLock;
	std::string statusText;
};

} // namespace sp
