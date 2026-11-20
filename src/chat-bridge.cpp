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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "chat-bridge.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <utility>

namespace sp {

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
using socklen = int;
const socket_t BadSocket = INVALID_SOCKET;

void closeSocket(socket_t s)
{
	closesocket(s);
}

void setNonBlocking(socket_t s, bool enabled)
{
	u_long mode = enabled ? 1 : 0;
	ioctlsocket(s, FIONBIO, &mode);
}

bool inProgress()
{
	const int e = WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}

void noSigPipe(socket_t) {}

constexpr int SendFlags = 0;
#else
using socket_t = int;
using socklen = socklen_t;
const socket_t BadSocket = -1;

void closeSocket(socket_t s)
{
	close(s);
}

void setNonBlocking(socket_t s, bool enabled)
{
	const int flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
}

bool inProgress()
{
	const int e = errno;
	if (e == EINPROGRESS || e == EAGAIN)
		return true;
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	if (e == EWOULDBLOCK)
		return true;
#endif
	return false;
}

void noSigPipe(socket_t s)
{
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen(sizeof(one)));
#else
	(void)s;
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int SendFlags = MSG_NOSIGNAL;
#else
constexpr int SendFlags = 0;
#endif
#endif

inline socket_t fromHandle(intptr_t h)
{
	return static_cast<socket_t>(h);
}

inline intptr_t toHandle(socket_t s)
{
	return static_cast<intptr_t>(s);
}

/* 1 = ready, 0 = timeout, -1 = error */
int waitFor(socket_t s, bool write, int timeoutMs)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(s, &set);
	fd_set errors;
	FD_ZERO(&errors);
	FD_SET(s, &errors);
	timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	const int r = select(int(s) + 1, write ? nullptr : &set, write ? &set : nullptr, &errors, &tv);
	if (r < 0)
		return -1;
	if (r == 0)
		return 0;
	if (FD_ISSET(s, &errors) && !FD_ISSET(s, &set))
		return -1;
	return 1;
}

bool sendAll(socket_t s, const std::string &data)
{
	size_t sent = 0;
	int stalls = 0;
	while (sent < data.size()) {
		const int n = int(send(s, data.data() + sent, int(data.size() - sent), SendFlags));
		if (n > 0) {
			sent += size_t(n);
			continue;
		}
		if (n < 0 && inProgress() && stalls++ < 20 && waitFor(s, true, 100) >= 0)
			continue;
		return false;
	}
	return true;
}

int recvSome(socket_t s, char *buf, int len)
{
	return int(recv(s, buf, len, 0));
}

void sleepWhile(const std::atomic<bool> &running, int ms)
{
	for (int waited = 0; waited < ms && running.load(); waited += 100)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

socket_t connectTo(const char *host, const char *port, const std::atomic<bool> &running)
{
	addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *result = nullptr;
	if (getaddrinfo(host, port, &hints, &result) != 0 || !result)
		return BadSocket;

	socket_t s = BadSocket;
	for (addrinfo *ai = result; ai && running.load(); ai = ai->ai_next) {
		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s == BadSocket)
			continue;
		noSigPipe(s);
		setNonBlocking(s, true);
		bool ok = connect(s, ai->ai_addr, socklen(ai->ai_addrlen)) == 0;
		if (!ok && inProgress()) {
			for (int i = 0; i < 40 && running.load(); ++i) {
				const int r = waitFor(s, true, 250);
				if (r == 0)
					continue;
				if (r > 0) {
					int err = 0;
					socklen len = socklen(sizeof(err));
					getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &len);
					ok = err == 0;
				}
				break;
			}
		}
		if (ok)
			break;
		closeSocket(s);
		s = BadSocket;
	}
	freeaddrinfo(result);
	return s;
}

std::string upper(std::string s)
{
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

bool isAscii(const std::string &s)
{
	return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 128; });
}

/* ------------------------------------------------------------------ */
/* IRC parsing                                                         */
/* ------------------------------------------------------------------ */

std::string unescapeTag(const std::string &v)
{
	std::string out;
	out.reserve(v.size());
	for (size_t i = 0; i < v.size(); ++i) {
		if (v[i] != '\\' || i + 1 >= v.size()) {
			out.push_back(v[i]);
			continue;
		}
		const char n = v[++i];
		switch (n) {
		case 's':
			out.push_back(' ');
			break;
		case ':':
			out.push_back(';');
			break;
		case 'r':
		case 'n':
			break;
		default:
			out.push_back(n);
			break;
		}
	}
	return out;
}

struct IrcMessage {
	std::map<std::string, std::string> tags;
	std::string nick;
	std::string command;
	std::string params;
	std::string trailing;
};

IrcMessage parseIrc(const std::string &line)
{
	IrcMessage m;
	size_t pos = 0;
	if (!line.empty() && line[0] == '@') {
		const size_t end = line.find(' ');
		const std::string tags = line.substr(1, end == std::string::npos ? std::string::npos : end - 1);
		size_t start = 0;
		while (start < tags.size()) {
			size_t semi = tags.find(';', start);
			if (semi == std::string::npos)
				semi = tags.size();
			const std::string kv = tags.substr(start, semi - start);
			const size_t eq = kv.find('=');
			if (eq == std::string::npos)
				m.tags[kv] = "";
			else
				m.tags[kv.substr(0, eq)] = unescapeTag(kv.substr(eq + 1));
			start = semi + 1;
		}
		pos = end == std::string::npos ? line.size() : end + 1;
	}
	if (pos < line.size() && line[pos] == ':') {
		const size_t end = line.find(' ', pos);
		const std::string prefix =
			line.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
		m.nick = prefix.substr(0, prefix.find('!'));
		pos = end == std::string::npos ? line.size() : end + 1;
	}
	const size_t cmdEnd = line.find(' ', pos);
	m.command = line.substr(pos, cmdEnd == std::string::npos ? std::string::npos : cmdEnd - pos);
	if (cmdEnd != std::string::npos) {
		const std::string rest = line.substr(cmdEnd + 1);
		const size_t colon = rest.find(" :");
		if (rest.size() > 0 && rest[0] == ':') {
			m.trailing = rest.substr(1);
		} else if (colon != std::string::npos) {
			m.params = rest.substr(0, colon);
			m.trailing = rest.substr(colon + 2);
		} else {
			m.params = rest;
		}
	}
	return m;
}

std::string tag(const IrcMessage &m, const char *key)
{
	const auto it = m.tags.find(key);
	return it == m.tags.end() ? std::string() : it->second;
}

int tagInt(const IrcMessage &m, const char *key)
{
	const std::string v = tag(m, key);
	return v.empty() ? 0 : std::atoi(v.c_str());
}

std::string bestName(const std::string &display, const std::string &login)
{
	if (!display.empty() && isAscii(display))
		return display;
	return login.empty() ? display : login;
}

/* ------------------------------------------------------------------ */
/* HTTP helpers                                                        */
/* ------------------------------------------------------------------ */

int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

std::string urlDecode(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '+') {
			out.push_back(' ');
		} else if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
			out.push_back(char(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2])));
			i += 2;
		} else {
			out.push_back(s[i]);
		}
	}
	return out;
}

std::map<std::string, std::string> parseQuery(const std::string &q)
{
	std::map<std::string, std::string> out;
	size_t start = 0;
	while (start < q.size()) {
		size_t amp = q.find('&', start);
		if (amp == std::string::npos)
			amp = q.size();
		const std::string kv = q.substr(start, amp - start);
		const size_t eq = kv.find('=');
		if (eq == std::string::npos)
			out[urlDecode(kv)] = "";
		else
			out[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
		start = amp + 1;
	}
	return out;
}

std::string httpResponse(int code, const char *type, const std::string &body, bool cors)
{
	const char *reason = code == 200 ? "OK"
					 : (code == 204 ? "No Content" : (code == 403 ? "Forbidden" : "Not Found"));
	std::string r = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
	r += std::string("Content-Type: ") + type + "\r\n";
	r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	r += "Cache-Control: no-store\r\n";
	if (cors)
		r += "Access-Control-Allow-Origin: *\r\n";
	r += "Connection: close\r\n\r\n";
	r += body;
	return r;
}

const char *HelpPage = "Stack Party bridge is running.\n\n"
		       "GET /cmd?user=NAME&cmd=flip            run a chat command (no prefix needed)\n"
		       "GET /cmd?user=NAME&text=!bomb          run a raw chat message\n"
		       "    optional: &mod=1 (privileged) &bypass=1 (ignore cooldowns) &bits=N\n"
		       "GET /event?type=sub&user=NAME&amount=MONTHS\n"
		       "GET /event?type=gift&user=GIFTER&to=RECIPIENT\n"
		       "GET /event?type=giftbomb&user=GIFTER&amount=COUNT\n"
		       "GET /event?type=raid&user=NAME&amount=VIEWERS\n"
		       "GET /event?type=bits&user=NAME&amount=BITS[&text=!flip]\n"
		       "GET /event?type=follow&user=NAME\n"
		       "GET /event?type=first&user=NAME     (first-time chatter welcome)\n"
		       "GET /state                            game state as JSON\n\n"
		       "If a bridge key is set, add &key=YOURKEY to /cmd and /event.\n";

} // namespace

/* ------------------------------------------------------------------ */
/* Public helpers                                                      */
/* ------------------------------------------------------------------ */

bool netStartup()
{
#ifdef _WIN32
	WSADATA data;
	return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
	return true;
#endif
}

void netShutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

std::string normalizeChannel(const std::string &input)
{
	std::string s = input;
	const size_t slash = s.find_last_of('/');
	if (slash != std::string::npos)
		s = s.substr(slash + 1);
	std::string out;
	for (char c : s) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '_')
			out.push_back(char(std::tolower(u)));
	}
	return out.substr(0, 25);
}

/* ------------------------------------------------------------------ */
/* Twitch chat                                                         */
/* ------------------------------------------------------------------ */

TwitchChat::TwitchChat(InboxSink s) : sink(std::move(s)) {}

TwitchChat::~TwitchChat()
{
	stop();
}

void TwitchChat::start(const std::string &channel)
{
	stop();
	if (channel.empty())
		return;
	running = true;
	setStatus("CONNECTING TO #" + upper(channel), false);
	worker = std::thread([this, channel] { run(channel); });
}

void TwitchChat::stop()
{
	running = false;
	if (worker.joinable())
		worker.join();
	setStatus("", false);
}

std::string TwitchChat::status(bool *connected) const
{
	std::lock_guard<std::mutex> guard(statusLock);
	if (connected)
		*connected = isConnected;
	return statusText;
}

void TwitchChat::setStatus(const std::string &text, bool connected)
{
	std::lock_guard<std::mutex> guard(statusLock);
	statusText = text;
	isConnected = connected;
}

void TwitchChat::run(std::string channel)
{
	std::mt19937 rng(std::random_device{}());
	int backoff = 2;
	while (running.load()) {
		setStatus("CONNECTING TO #" + upper(channel), false);
		const socket_t s = connectTo("irc.chat.twitch.tv", "6667", running);
		if (s == BadSocket) {
			if (!running.load())
				break;
			setStatus("TWITCH UNREACHABLE - RETRYING", false);
			sleepWhile(running, backoff * 1000);
			backoff = std::min(backoff * 2, 60);
			continue;
		}

		const std::string nick =
			"justinfan" + std::to_string(std::uniform_int_distribution<int>(10000, 99999)(rng));
		bool ok = sendAll(s, "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n") &&
			  sendAll(s, "NICK " + nick + "\r\n") && sendAll(s, "JOIN #" + channel + "\r\n");

		std::string buffer;
		auto lastData = std::chrono::steady_clock::now();
		char chunk[4096];
		while (ok && running.load()) {
			const int ready = waitFor(s, false, 250);
			if (ready < 0)
				break;
			if (ready == 0) {
				if (std::chrono::steady_clock::now() - lastData > std::chrono::seconds(330))
					break; /* Twitch pings every ~5 minutes: the link is dead */
				continue;
			}
			const int n = recvSome(s, chunk, int(sizeof(chunk)));
			if (n <= 0) {
				if (n < 0 && inProgress())
					continue;
				break;
			}
			lastData = std::chrono::steady_clock::now();
			buffer.append(chunk, size_t(n));
			size_t pos;
			while (ok && (pos = buffer.find("\r\n")) != std::string::npos) {
				const std::string line = buffer.substr(0, pos);
				buffer.erase(0, pos + 2);
				if (!line.empty())
					ok = handleLine(toHandle(s), line, channel);
			}
			if (buffer.size() > 65536)
				buffer.clear();
			bool connected = false;
			status(&connected);
			if (connected)
				backoff = 2;
		}
		closeSocket(s);
		if (running.load()) {
			setStatus("RECONNECTING TO #" + upper(channel), false);
			sleepWhile(running, backoff * 1000);
			backoff = std::min(backoff * 2, 60);
		}
	}
}

bool TwitchChat::handleLine(intptr_t sock, const std::string &line, const std::string &channel)
{
	const socket_t s = fromHandle(sock);
	if (line.compare(0, 4, "PING") == 0)
		return sendAll(s, "PONG" + line.substr(4) + "\r\n");

	const IrcMessage m = parseIrc(line);
	if (m.command == "RECONNECT")
		return false;
	if (m.command == "ROOMSTATE" || m.command == "366") {
		setStatus("TWITCH #" + upper(channel), true);
		return true;
	}
	if (m.command == "NOTICE") {
		const std::string id = tag(m, "msg-id");
		if (id == "msg_channel_suspended" || id == "msg_banned")
			setStatus("TWITCH: CHANNEL UNAVAILABLE", false);
		return true;
	}

	if (m.command == "PRIVMSG") {
		InboxMsg msg;
		msg.kind = InboxMsg::Chat;
		msg.user = bestName(tag(m, "display-name"), m.nick);
		msg.text = m.trailing;
		if (msg.text.compare(0, 8,
				     "\x01"
				     "ACTION ") == 0) {
			msg.text = msg.text.substr(8);
			if (!msg.text.empty() && msg.text.back() == '\x01')
				msg.text.pop_back();
		}
		const std::string badges = tag(m, "badges");
		msg.privileged = tag(m, "mod") == "1" || badges.find("broadcaster/") != std::string::npos ||
				 badges.find("moderator/") != std::string::npos;
		msg.amount = tagInt(m, "bits");
		msg.firstMessage = tag(m, "first-msg") == "1";
		msg.bypassCooldown = !tag(m, "custom-reward-id").empty(); /* channel point reward with text */
		if (sink)
			sink(std::move(msg));
		return true;
	}

	if (m.command == "USERNOTICE") {
		const std::string id = tag(m, "msg-id");
		InboxMsg msg;
		msg.user = bestName(tag(m, "display-name"), tag(m, "login"));
		if (id == "sub" || id == "resub") {
			msg.kind = InboxMsg::Sub;
			msg.amount = std::max(1, tagInt(m, "msg-param-cumulative-months"));
		} else if (id == "subgift" || id == "anonsubgift") {
			msg.kind = InboxMsg::GiftSub;
			msg.text = bestName(tag(m, "msg-param-recipient-display-name"),
					    tag(m, "msg-param-recipient-user-name"));
		} else if (id == "submysterygift" || id == "anonsubmysterygift") {
			msg.kind = InboxMsg::MysteryGift;
			msg.amount = tagInt(m, "msg-param-mass-gift-count");
		} else if (id == "raid") {
			msg.kind = InboxMsg::Raid;
			msg.user = bestName(tag(m, "msg-param-displayName"), tag(m, "msg-param-login"));
			msg.amount = tagInt(m, "msg-param-viewerCount");
		} else {
			return true;
		}
		if (sink)
			sink(std::move(msg));
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* HTTP bridge                                                         */
/* ------------------------------------------------------------------ */

HttpBridge::HttpBridge(InboxSink s, StateProvider st) : sink(std::move(s)), state(std::move(st)) {}

HttpBridge::~HttpBridge()
{
	stop();
}

void HttpBridge::start(int port, const std::string &key)
{
	stop();
	if (port <= 0 || port > 65535)
		return;
	running = true;
	setStatus("BRIDGE STARTING");
	worker = std::thread([this, port, key] { run(port, key); });
}

void HttpBridge::stop()
{
	running = false;
	if (worker.joinable())
		worker.join();
	setStatus("");
}

std::string HttpBridge::status() const
{
	std::lock_guard<std::mutex> guard(statusLock);
	return statusText;
}

void HttpBridge::setStatus(const std::string &text)
{
	std::lock_guard<std::mutex> guard(statusLock);
	statusText = text;
}

void HttpBridge::run(int port, std::string key)
{
	const socket_t ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ls == BadSocket) {
		setStatus("BRIDGE ERROR");
		return;
	}
	int one = 1;
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), socklen(sizeof(one)));
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<unsigned short>(port));
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* never exposed to the network */
	if (bind(ls, reinterpret_cast<sockaddr *>(&addr), socklen(sizeof(addr))) != 0 || listen(ls, 16) != 0) {
		setStatus("BRIDGE PORT " + std::to_string(port) + " BUSY");
		closeSocket(ls);
		return;
	}
	setNonBlocking(ls, true);
	setStatus("BRIDGE 127.0.0.1:" + std::to_string(port));

	while (running.load()) {
		if (waitFor(ls, false, 250) <= 0)
			continue;
		const socket_t client = accept(ls, nullptr, nullptr);
		if (client == BadSocket)
			continue;
		noSigPipe(client);
		setNonBlocking(client, true);
		serve(toHandle(client), key);
		closeSocket(client);
	}
	closeSocket(ls);
}

void HttpBridge::serve(intptr_t handle, const std::string &key)
{
	const socket_t client = fromHandle(handle);
	std::string request;
	char chunk[2048];
	for (int i = 0; i < 12 && request.find("\r\n\r\n") == std::string::npos && request.size() < 8192; ++i) {
		const int ready = waitFor(client, false, 125);
		if (ready < 0)
			return;
		if (ready == 0)
			continue;
		const int n = recvSome(client, chunk, int(sizeof(chunk)));
		if (n <= 0)
			break;
		request.append(chunk, size_t(n));
	}

	const size_t lineEnd = request.find("\r\n");
	const std::string first = request.substr(0, lineEnd);
	const size_t sp1 = first.find(' ');
	const size_t sp2 = first.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos) {
		sendAll(client, httpResponse(404, "text/plain", "bad request\n", false));
		return;
	}
	const std::string method = first.substr(0, sp1);
	const std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
	if (method == "OPTIONS") {
		sendAll(client, httpResponse(204, "text/plain", "", true));
		return;
	}
	const size_t qm = target.find('?');
	const std::string path = target.substr(0, qm);
	std::map<std::string, std::string> q = parseQuery(qm == std::string::npos ? "" : target.substr(qm + 1));

	if (path == "/" || path.empty()) {
		sendAll(client, httpResponse(200, "text/plain; charset=utf-8", HelpPage, false));
		return;
	}
	if (path == "/state") {
		const std::string json = state ? state() : std::string("{}");
		sendAll(client, httpResponse(200, "application/json", json, true));
		return;
	}
	if (path != "/cmd" && path != "/event") {
		sendAll(client, httpResponse(404, "text/plain", "unknown endpoint\n", false));
		return;
	}
	if (!key.empty() && q["key"] != key) {
		sendAll(client, httpResponse(403, "application/json", "{\"ok\":false,\"error\":\"bad key\"}", false));
		return;
	}

	InboxMsg msg;
	msg.user = q["user"].empty() ? std::string("BRIDGE") : q["user"];
	msg.amount = std::atoi(q["amount"].c_str());
	bool ok = true;
	if (path == "/cmd") {
		msg.kind = InboxMsg::Chat;
		msg.command = q["cmd"];
		msg.text = q["text"];
		msg.privileged = q["mod"] == "1";
		msg.bypassCooldown = q["bypass"] == "1";
		msg.amount = std::atoi(q["bits"].c_str());
		ok = !msg.command.empty() || !msg.text.empty();
	} else {
		const std::string type = q["type"];
		if (type == "sub" || type == "resub") {
			msg.kind = InboxMsg::Sub;
			msg.amount = std::max(1, msg.amount);
		} else if (type == "gift" || type == "giftsub") {
			msg.kind = InboxMsg::GiftSub;
			msg.text = q["to"];
		} else if (type == "giftbomb" || type == "mystery") {
			msg.kind = InboxMsg::MysteryGift;
		} else if (type == "raid") {
			msg.kind = InboxMsg::Raid;
		} else if (type == "follow") {
			msg.kind = InboxMsg::Follow;
		} else if (type == "bits" || type == "cheer") {
			msg.kind = InboxMsg::Bits;
			msg.text = q["text"];
			msg.command = q["cmd"];
		} else if (type == "first") {
			msg.kind = InboxMsg::Chat;
			msg.firstMessage = true;
		} else {
			ok = false;
		}
	}
	if (ok && sink)
		sink(std::move(msg));
	sendAll(client,
		httpResponse(ok ? 200 : 404, "application/json",
			     ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"unknown command or event\"}", false));
}

} // namespace sp
