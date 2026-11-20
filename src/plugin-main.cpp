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

#include <obs-module.h>
#include <plugin-support.h>

#include "chat-bridge.hpp"
#include "game.hpp"
#include "render.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("MMLTECH")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Stack Party";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Module.Description");
}

namespace {

#define T(key) obs_module_text(key)

/* Source id and the first hotkey names are kept from 1.x so existing scenes and bindings survive */
constexpr const char *SourceId = "obs_tetris_source";

struct HotkeyDef {
	sp::Action action;
	const char *name;
	const char *label;
};

const HotkeyDef Hotkeys[] = {
	{sp::Action::Left, "obs_tetris.left", "Hotkey.Left"},
	{sp::Action::Right, "obs_tetris.right", "Hotkey.Right"},
	{sp::Action::SoftDrop, "obs_tetris.down", "Hotkey.SoftDrop"},
	{sp::Action::RotateCW, "obs_tetris.rotate", "Hotkey.Rotate"},
	{sp::Action::RotateCCW, "obs_tetris.rotate_ccw", "Hotkey.RotateCCW"},
	{sp::Action::HardDrop, "obs_tetris.drop", "Hotkey.HardDrop"},
	{sp::Action::Hold, "obs_tetris.hold", "Hotkey.Hold"},
	{sp::Action::Pause, "obs_tetris.pause", "Hotkey.Pause"},
	{sp::Action::Reset, "obs_tetris.reset", "Hotkey.Reset"},
	{sp::Action::Autopilot, "obs_tetris.autopilot", "Hotkey.Autopilot"},
	{sp::Action::Panic, "obs_tetris.panic", "Hotkey.Panic"},
	{sp::Action::ToggleChat, "obs_tetris.chat_toggle", "Hotkey.ChatToggle"},
};

struct Inbox {
	std::mutex lock;
	std::vector<sp::InboxMsg> items;

	void push(sp::InboxMsg &&msg)
	{
		std::lock_guard<std::mutex> guard(lock);
		if (items.size() < 512)
			items.push_back(std::move(msg));
	}

	std::vector<sp::InboxMsg> take()
	{
		std::vector<sp::InboxMsg> out;
		std::lock_guard<std::mutex> guard(lock);
		out.swap(items);
		return out;
	}
};

struct StackSource {
	obs_source_t *source = nullptr;
	std::mutex lock; /* guards game + size */
	sp::Game game;
	sp::Batch batch; /* graphics thread only */
	uint32_t width = 720;
	uint32_t height = 1080;

	std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
	std::unique_ptr<sp::TwitchChat> twitch;
	std::unique_ptr<sp::HttpBridge> bridge;
	std::string channel;
	int bridgePort = 0;
	std::string bridgeKey;

	std::array<obs_hotkey_id, sp::ActionCount> hotkeys{};
	std::array<bool, sp::ActionCount> keyHeld{};
};

std::string stateJson(StackSource *s)
{
	std::lock_guard<std::mutex> guard(s->lock);
	const sp::Game &g = s->game;
	static const char *const fxNames[sp::FX_COUNT] = {"flip",  "mirror", "turbo", "slow", "dark",
							  "shake", "wobble", "disco", "hype", "chill"};
	static const char *const modes[3] = {"off", "anarchy", "democracy"};
	std::string j = "{";
	j += "\"score\":" + std::to_string(g.score);
	j += ",\"best\":" + std::to_string(std::max(g.best, g.score));
	j += ",\"lines\":" + std::to_string(g.lines);
	j += ",\"level\":" + std::to_string(g.level);
	j += std::string(",\"gameOver\":") + (g.gameOver ? "true" : "false");
	j += std::string(",\"paused\":") + (g.paused ? "true" : "false");
	j += std::string(",\"autopilot\":") + (g.autopilot ? "true" : "false");
	j += std::string(",\"chatPlays\":\"") + modes[int(g.cfg.chatPlays)] + "\"";
	j += ",\"shields\":" + std::to_string(g.shields);
	j += ",\"hype\":" + std::to_string(int(g.hype * 100.0f));
	j += ",\"chaos\":" + std::to_string(g.chaos);
	j += ",\"chaosThreshold\":" + std::to_string(g.cfg.chaosThreshold);
	j += ",\"effects\":[";
	bool first = true;
	for (int e = 0; e < sp::FX_COUNT; ++e) {
		if (!g.fxActive(e))
			continue;
		j += std::string(first ? "" : ",") + "{\"name\":\"" + fxNames[e] +
		     "\",\"seconds\":" + std::to_string(int(g.fxRemaining(e) + 0.5f)) + "}";
		first = false;
	}
	auto ranks = [](const std::vector<sp::RankEntry> &list) {
		std::string out = "[";
		for (size_t i = 0; i < list.size(); ++i)
			out += std::string(i ? "," : "") + "{\"name\":\"" + list[i].name +
			       "\",\"points\":" + std::to_string(list[i].points) + "}";
		return out + "]";
	};
	j += "],\"topHelpers\":" + ranks(g.topHelpers);
	j += ",\"topTrolls\":" + ranks(g.topTrolls);
	j += "}";
	return j;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

void onHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	auto *s = static_cast<StackSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	for (size_t i = 0; i < s->hotkeys.size(); ++i) {
		if (s->hotkeys[i] == id) {
			s->game.press(static_cast<sp::Action>(i), pressed);
			break;
		}
	}
}

int actionForKey(obs_key_t key)
{
	switch (key) {
	case OBS_KEY_LEFT:
	case OBS_KEY_A:
		return int(sp::Action::Left);
	case OBS_KEY_RIGHT:
	case OBS_KEY_D:
		return int(sp::Action::Right);
	case OBS_KEY_DOWN:
	case OBS_KEY_S:
		return int(sp::Action::SoftDrop);
	case OBS_KEY_UP:
	case OBS_KEY_W:
	case OBS_KEY_X:
		return int(sp::Action::RotateCW);
	case OBS_KEY_Z:
	case OBS_KEY_Q:
		return int(sp::Action::RotateCCW);
	case OBS_KEY_SPACE:
		return int(sp::Action::HardDrop);
	case OBS_KEY_C:
	case OBS_KEY_SHIFT:
		return int(sp::Action::Hold);
	case OBS_KEY_P:
	case OBS_KEY_ESCAPE:
		return int(sp::Action::Pause);
	default:
		return -1;
	}
}

/* Keyboard play through OBS' "Interact" window, no global hotkeys needed */
void onKey(void *data, const obs_key_event *event, bool keyUp)
{
	auto *s = static_cast<StackSource *>(data);
	const int action = actionForKey(obs_key_from_virtual_key(int(event->native_vkey)));
	if (action < 0)
		return;
	std::lock_guard<std::mutex> guard(s->lock);
	bool &held = s->keyHeld[size_t(action)];
	if (!keyUp && held)
		return; /* ignore OS auto-repeat, the game has its own */
	held = !keyUp;
	s->game.press(static_cast<sp::Action>(action), !keyUp);
}

void onFocus(void *data, bool focus)
{
	if (focus)
		return;
	auto *s = static_cast<StackSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	for (size_t i = 0; i < s->keyHeld.size(); ++i) {
		if (s->keyHeld[i])
			s->game.press(static_cast<sp::Action>(i), false);
		s->keyHeld[i] = false;
	}
}

/* ------------------------------------------------------------------ */
/* Source callbacks                                                    */
/* ------------------------------------------------------------------ */

const char *sourceName(void *)
{
	return T("Source.Name");
}

void sourceUpdate(void *data, obs_data_t *settings)
{
	auto *s = static_cast<StackSource *>(data);

	sp::Settings cfg;
	cfg.theme = int(obs_data_get_int(settings, "theme"));
	cfg.showGrid = obs_data_get_bool(settings, "show_grid");
	cfg.showGhost = obs_data_get_bool(settings, "show_ghost");
	cfg.transparent = obs_data_get_bool(settings, "transparent");
	cfg.showPanels = obs_data_get_bool(settings, "show_panels");
	cfg.startLevel = int(obs_data_get_int(settings, "start_level"));
	cfg.autoRestart = float(obs_data_get_int(settings, "auto_restart"));
	cfg.autopilotIdle = float(obs_data_get_int(settings, "autopilot_idle"));
	cfg.effectScale = float(obs_data_get_int(settings, "effect_scale")) / 100.0f;
	cfg.chatEnabled = obs_data_get_bool(settings, "chat_enabled");
	cfg.prefix = obs_data_get_string(settings, "prefix");
	cfg.userCooldown = float(obs_data_get_int(settings, "user_cooldown"));
	cfg.globalCooldown = float(obs_data_get_int(settings, "global_cooldown"));
	cfg.privilegedBypass = obs_data_get_bool(settings, "mods_bypass");
	cfg.allowTroll = obs_data_get_bool(settings, "allow_troll");
	cfg.allowHelper = obs_data_get_bool(settings, "allow_helper");
	cfg.allowFun = obs_data_get_bool(settings, "allow_fun");
	cfg.plainReactions = obs_data_get_bool(settings, "plain_reactions");
	cfg.welcomeFirst = obs_data_get_bool(settings, "welcome_first");
	cfg.bitsThreshold = int(obs_data_get_int(settings, "bits_threshold"));
	cfg.chaosThreshold = int(obs_data_get_int(settings, "chaos_threshold"));
	cfg.chatPlays = static_cast<sp::ChatPlaysMode>(std::clamp(int(obs_data_get_int(settings, "chat_plays")), 0, 2));
	cfg.voteSeconds = float(obs_data_get_int(settings, "vote_seconds"));

	{
		std::lock_guard<std::mutex> guard(s->lock);
		s->width = uint32_t(std::max<long long>(64, obs_data_get_int(settings, "width")));
		s->height = uint32_t(std::max<long long>(64, obs_data_get_int(settings, "height")));
		s->game.configure(cfg);
	}

	const std::string channel =
		cfg.chatEnabled ? sp::normalizeChannel(obs_data_get_string(settings, "twitch_channel")) : std::string();
	if (channel != s->channel) {
		s->channel = channel;
		s->twitch->start(channel); /* empty channel just stops */
	}

	const bool bridgeOn = obs_data_get_bool(settings, "bridge_enabled");
	const int port = bridgeOn ? int(obs_data_get_int(settings, "bridge_port")) : 0;
	const std::string key = obs_data_get_string(settings, "bridge_key");
	if (port != s->bridgePort || key != s->bridgeKey) {
		s->bridgePort = port;
		s->bridgeKey = key;
		if (port > 0)
			s->bridge->start(port, key);
		else
			s->bridge->stop();
	}
}

void *sourceCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new StackSource();
	s->source = source;
	std::weak_ptr<Inbox> inbox = s->inbox;
	auto sink = [inbox](sp::InboxMsg &&msg) {
		if (auto box = inbox.lock())
			box->push(std::move(msg));
	};
	s->twitch = std::make_unique<sp::TwitchChat>(sink);
	s->bridge = std::make_unique<sp::HttpBridge>(sink, [s]() { return stateJson(s); });

	s->game.best = obs_data_get_int(settings, "best_score");
	sourceUpdate(s, settings);
	{
		std::lock_guard<std::mutex> guard(s->lock);
		s->game.reset();
		s->game.gamesPlayed = 0;
	}

	s->hotkeys.fill(OBS_INVALID_HOTKEY_ID);
	for (const HotkeyDef &h : Hotkeys)
		s->hotkeys[size_t(h.action)] = obs_hotkey_register_source(source, h.name, T(h.label), onHotkey, s);
	return s;
}

void sourceDestroy(void *data)
{
	auto *s = static_cast<StackSource *>(data);
	s->twitch->stop();
	s->bridge->stop();
	obs_enter_graphics();
	s->batch.release();
	obs_leave_graphics();
	delete s;
}

void sourceSave(void *data, obs_data_t *settings)
{
	auto *s = static_cast<StackSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	obs_data_set_int(settings, "best_score", std::max(s->game.best, s->game.score));
}

void sourceDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 720);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_int(settings, "theme", 0);
	obs_data_set_default_bool(settings, "show_grid", true);
	obs_data_set_default_bool(settings, "show_ghost", true);
	obs_data_set_default_bool(settings, "transparent", false);
	obs_data_set_default_bool(settings, "show_panels", true);
	obs_data_set_default_int(settings, "start_level", 1);
	obs_data_set_default_int(settings, "auto_restart", 8);
	obs_data_set_default_int(settings, "autopilot_idle", 0);
	obs_data_set_default_int(settings, "effect_scale", 100);

	obs_data_set_default_bool(settings, "chat_enabled", true);
	obs_data_set_default_string(settings, "twitch_channel", "");
	obs_data_set_default_string(settings, "prefix", "!");
	obs_data_set_default_int(settings, "user_cooldown", 10);
	obs_data_set_default_int(settings, "global_cooldown", 20);
	obs_data_set_default_bool(settings, "mods_bypass", true);
	obs_data_set_default_bool(settings, "allow_troll", true);
	obs_data_set_default_bool(settings, "allow_helper", true);
	obs_data_set_default_bool(settings, "allow_fun", true);
	obs_data_set_default_bool(settings, "plain_reactions", true);
	obs_data_set_default_bool(settings, "welcome_first", true);
	obs_data_set_default_int(settings, "bits_threshold", 100);
	obs_data_set_default_int(settings, "chaos_threshold", 30);
	obs_data_set_default_int(settings, "chat_plays", 0);
	obs_data_set_default_int(settings, "vote_seconds", 4);

	obs_data_set_default_bool(settings, "bridge_enabled", false);
	obs_data_set_default_int(settings, "bridge_port", 8911);
	obs_data_set_default_string(settings, "bridge_key", "");
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

void inject(void *data, sp::InboxMsg &&msg)
{
	auto *s = static_cast<StackSource *>(data);
	if (s)
		s->inbox->push(std::move(msg));
}

sp::InboxMsg testMsg(sp::InboxMsg::Kind kind, int amount, const std::string &text = std::string())
{
	sp::InboxMsg m;
	m.kind = kind;
	m.user = "TestViewer";
	m.amount = amount;
	m.text = text;
	return m;
}

bool testSub(obs_properties_t *, obs_property_t *, void *data)
{
	inject(data, testMsg(sp::InboxMsg::Sub, 3));
	return false;
}

bool testGiftBomb(obs_properties_t *, obs_property_t *, void *data)
{
	inject(data, testMsg(sp::InboxMsg::MysteryGift, 5));
	return false;
}

bool testRaid(obs_properties_t *, obs_property_t *, void *data)
{
	inject(data, testMsg(sp::InboxMsg::Raid, 42));
	return false;
}

bool testBits(obs_properties_t *, obs_property_t *, void *data)
{
	inject(data, testMsg(sp::InboxMsg::Bits, 300, "Cheer300"));
	return false;
}

bool testChat(obs_properties_t *, obs_property_t *, void *data)
{
	static const char *const commands[] = {"flip",   "mirror", "turbo", "garbage", "dark", "shake",
					       "wobble", "curse",  "slow",  "bomb",    "long", "shield",
					       "disco",  "gg",     "pog",   "hype",    "lol"};
	static std::mt19937 rng(std::random_device{}());
	const size_t count = sizeof(commands) / sizeof(commands[0]);
	sp::InboxMsg m = testMsg(sp::InboxMsg::Chat, 0);
	m.command = commands[std::uniform_int_distribution<size_t>(0, count - 1)(rng)];
	m.bypassCooldown = true;
	inject(data, std::move(m));
	return false;
}

bool testWheel(obs_properties_t *, obs_property_t *, void *data)
{
	auto *s = static_cast<StackSource *>(data);
	if (!s)
		return false;
	std::lock_guard<std::mutex> guard(s->lock);
	s->game.spin("TESTVIEWER");
	return false;
}

bool panicButton(obs_properties_t *, obs_property_t *, void *data)
{
	auto *s = static_cast<StackSource *>(data);
	if (!s)
		return false;
	std::lock_guard<std::mutex> guard(s->lock);
	s->game.panic();
	return false;
}

bool refreshStatus(obs_properties_t *, obs_property_t *, void *)
{
	return true;
}

obs_properties_t *sourceProperties(void *data)
{
	auto *s = static_cast<StackSource *>(data);
	obs_properties_t *props = obs_properties_create();

	/* Look & feel */
	obs_properties_t *look = obs_properties_create();
	obs_properties_add_int(look, "width", T("Width"), 320, 7680, 10);
	obs_properties_add_int(look, "height", T("Height"), 240, 4320, 10);
	obs_property_t *theme =
		obs_properties_add_list(look, "theme", T("Theme"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < sp::themeCount(); ++i)
		obs_property_list_add_int(theme, sp::themeName(i), i);
	obs_properties_add_bool(look, "transparent", T("Transparent"));
	obs_properties_add_bool(look, "show_panels", T("ShowPanels"));
	obs_properties_add_bool(look, "show_grid", T("ShowGrid"));
	obs_properties_add_bool(look, "show_ghost", T("ShowGhost"));
	obs_properties_add_group(props, "group_look", T("Group.Look"), OBS_GROUP_NORMAL, look);

	/* Gameplay */
	obs_properties_t *play = obs_properties_create();
	obs_properties_add_int_slider(play, "start_level", T("StartLevel"), 1, 20, 1);
	obs_property_t *restart = obs_properties_add_int(play, "auto_restart", T("AutoRestart"), 0, 120, 1);
	obs_property_int_set_suffix(restart, " s");
	obs_property_t *idle = obs_properties_add_int(play, "autopilot_idle", T("AutopilotIdle"), 0, 3600, 5);
	obs_property_int_set_suffix(idle, " s");
	obs_property_set_long_description(idle, T("AutopilotIdle.Help"));
	obs_property_t *scale = obs_properties_add_int_slider(play, "effect_scale", T("EffectScale"), 25, 300, 25);
	obs_property_int_set_suffix(scale, " %");
	obs_properties_add_group(props, "group_play", T("Group.Gameplay"), OBS_GROUP_NORMAL, play);

	/* Chat */
	obs_properties_t *chat = obs_properties_create();
	std::string status = T("Status.Off");
	if (s) {
		bool connected = false;
		const std::string tw = s->twitch->status(&connected);
		const std::string br = s->bridge->status();
		if (!tw.empty() || !br.empty())
			status = tw + (tw.empty() || br.empty() ? "" : "  |  ") + br;
	}
	obs_properties_add_text(chat, "chat_status", status.c_str(), OBS_TEXT_INFO);
	obs_properties_add_button2(chat, "refresh_status", T("RefreshStatus"), refreshStatus, s);
	obs_properties_add_bool(chat, "chat_enabled", T("ChatEnabled"));
	obs_property_t *channel = obs_properties_add_text(chat, "twitch_channel", T("TwitchChannel"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(channel, T("TwitchChannel.Help"));
	obs_properties_add_text(chat, "prefix", T("Prefix"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(chat, "allow_troll", T("AllowTroll"));
	obs_properties_add_bool(chat, "allow_helper", T("AllowHelper"));
	obs_properties_add_bool(chat, "allow_fun", T("AllowFun"));
	obs_properties_add_bool(chat, "plain_reactions", T("PlainReactions"));
	obs_properties_add_bool(chat, "welcome_first", T("WelcomeFirst"));
	obs_property_t *ucd = obs_properties_add_int(chat, "user_cooldown", T("UserCooldown"), 0, 600, 1);
	obs_property_int_set_suffix(ucd, " s");
	obs_property_t *gcd = obs_properties_add_int(chat, "global_cooldown", T("GlobalCooldown"), 0, 600, 1);
	obs_property_int_set_suffix(gcd, " s");
	obs_properties_add_bool(chat, "mods_bypass", T("ModsBypass"));
	obs_property_t *bits = obs_properties_add_int(chat, "bits_threshold", T("BitsThreshold"), 0, 100000, 50);
	obs_property_set_long_description(bits, T("BitsThreshold.Help"));
	obs_property_t *chaos = obs_properties_add_int(chat, "chaos_threshold", T("ChaosThreshold"), 0, 1000, 1);
	obs_property_set_long_description(chaos, T("ChaosThreshold.Help"));
	obs_properties_add_group(props, "group_chat", T("Group.Chat"), OBS_GROUP_NORMAL, chat);

	/* Chat plays */
	obs_properties_t *cp = obs_properties_create();
	obs_property_t *mode =
		obs_properties_add_list(cp, "chat_plays", T("ChatPlays"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, T("ChatPlays.Off"), 0);
	obs_property_list_add_int(mode, T("ChatPlays.Anarchy"), 1);
	obs_property_list_add_int(mode, T("ChatPlays.Democracy"), 2);
	obs_property_t *vote = obs_properties_add_int_slider(cp, "vote_seconds", T("VoteSeconds"), 1, 30, 1);
	obs_property_int_set_suffix(vote, " s");
	obs_properties_add_group(props, "group_chatplays", T("Group.ChatPlays"), OBS_GROUP_NORMAL, cp);

	/* Bridge */
	obs_properties_t *br = obs_properties_create();
	obs_property_t *bon = obs_properties_add_bool(br, "bridge_enabled", T("BridgeEnabled"));
	obs_property_set_long_description(bon, T("BridgeEnabled.Help"));
	obs_properties_add_int(br, "bridge_port", T("BridgePort"), 1024, 65535, 1);
	obs_properties_add_text(br, "bridge_key", T("BridgeKey"), OBS_TEXT_PASSWORD);
	obs_properties_add_group(props, "group_bridge", T("Group.Bridge"), OBS_GROUP_NORMAL, br);

	/* Test buttons */
	obs_properties_t *test = obs_properties_create();
	obs_properties_add_button2(test, "test_chat", T("Test.Chat"), testChat, s);
	obs_properties_add_button2(test, "test_sub", T("Test.Sub"), testSub, s);
	obs_properties_add_button2(test, "test_giftbomb", T("Test.GiftBomb"), testGiftBomb, s);
	obs_properties_add_button2(test, "test_bits", T("Test.Bits"), testBits, s);
	obs_properties_add_button2(test, "test_raid", T("Test.Raid"), testRaid, s);
	obs_properties_add_button2(test, "test_wheel", T("Test.Wheel"), testWheel, s);
	obs_properties_add_button2(test, "panic", T("Test.Panic"), panicButton, s);
	obs_properties_add_group(props, "group_test", T("Group.Test"), OBS_GROUP_NORMAL, test);

	return props;
}

uint32_t sourceWidth(void *data)
{
	auto *s = static_cast<StackSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	return s->width;
}

uint32_t sourceHeight(void *data)
{
	auto *s = static_cast<StackSource *>(data);
	std::lock_guard<std::mutex> guard(s->lock);
	return s->height;
}

void sourceTick(void *data, float seconds)
{
	auto *s = static_cast<StackSource *>(data);
	std::vector<sp::InboxMsg> messages = s->inbox->take();

	bool connected = false;
	std::string status = s->twitch->status(&connected);
	if (status.empty())
		status = s->bridge->status();

	std::lock_guard<std::mutex> guard(s->lock);
	for (const sp::InboxMsg &msg : messages)
		s->game.handle(msg);
	s->game.chatStatus = status;
	s->game.chatConnected = connected || (!status.empty() && status.compare(0, 6, "BRIDGE") == 0 &&
					      status.find("BUSY") == std::string::npos);
	s->game.tick(seconds);
}

void sourceRender(void *data, gs_effect_t *)
{
	auto *s = static_cast<StackSource *>(data);
	{
		std::lock_guard<std::mutex> guard(s->lock);
		s->batch.begin();
		sp::renderGame(s->batch, s->game, float(s->width), float(s->height));
	}
	s->batch.draw();
}

obs_source_info makeSourceInfo()
{
	obs_source_info info = {};
	info.id = SourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION;
	info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
	info.get_name = sourceName;
	info.create = sourceCreate;
	info.destroy = sourceDestroy;
	info.update = sourceUpdate;
	info.get_defaults = sourceDefaults;
	info.get_properties = sourceProperties;
	info.get_width = sourceWidth;
	info.get_height = sourceHeight;
	info.video_tick = sourceTick;
	info.video_render = sourceRender;
	info.save = sourceSave;
	info.key_click = onKey;
	info.focus = onFocus;
	return info;
}

obs_source_info sourceInfo;

} // namespace

bool obs_module_load(void)
{
	if (!sp::netStartup())
		obs_log(LOG_WARNING, "network init failed: Twitch chat and bridge are unavailable");
	sourceInfo = makeSourceInfo();
	obs_register_source(&sourceInfo);
	obs_log(LOG_INFO, "Stack Party loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	sp::netShutdown();
	obs_log(LOG_INFO, "Stack Party unloaded");
}
