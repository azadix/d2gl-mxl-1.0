/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
/*
	Part of this file is part of D2DX.
	https://github.com/bolrog/d2dx/blob/main/src/d2dx/UnitMotionPredictor.cpp
	https://github.com/bolrog/d2dx/blob/main/src/d2dx/WeatherMotionPredictor.cpp

	Copyright (C) 2021  Bolrog

	D2DX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	D2DX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with D2DX. If not, see <https://www.gnu.org/licenses/>.
*/

#include "pch.h"
#include "motion_prediction.h"
#include "d2/common.h"
#include "d2/funcs.h"
#include "d2/stubs.h"
#include "helpers.h"
#include "modules/hd_text.h"

#include <detours/detours.h>

namespace d2gl::modules {

// A draw position that is not drawn again for this many frames is forgotten
// (it only has to survive the frames between two camera updates).
constexpr uint32_t WORLD_ANCHOR_LIFETIME = 90;
constexpr size_t MAX_WORLD_ANCHORS = 60000;

static inline uint64_t text_position_key(int x, int y)
{
	return ((uint64_t)(uint16_t)(int16_t)x << 16) | (uint64_t)(uint16_t)(int16_t)y;
}

static inline uint64_t rect_position_key(int left, int top, int right, int bottom)
{
	return ((uint64_t)(uint16_t)(int16_t)left << 48) | ((uint64_t)(uint16_t)(int16_t)top << 32)
		| ((uint64_t)(uint16_t)(int16_t)right << 16) | (uint64_t)(uint16_t)(int16_t)bottom;
}

MotionPrediction::MotionPrediction()
{
	m_units.reserve(1024);
}

void MotionPrediction::toggle(bool active)
{
	if (!m_active && active) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)d2::drawUnit, isVerMax(V_110) || isVer(V_114d) ? d2::drawUnitStubESI : d2::drawUnitStubStack);
		if (isVerNot(V_109d) && isVerNot(V_110))
			DetourAttach(&(PVOID&)d2::drawMissile, d2::drawMissileStub);
		DetourAttach(&(PVOID&)d2::drawWeatherParticles, isVer(V_114d) ? d2::drawWeatherParticlesStub114d : d2::drawWeatherParticlesStub);
		DetourTransactionCommit();

		m_active = true;
		d2::patch_motion_prediction->toggle(m_active);
	} else if (m_active && !active) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&(PVOID&)d2::drawUnit, isVerMax(V_110) || isVer(V_114d) ? d2::drawUnitStubESI : d2::drawUnitStubStack);
		if (isVerNot(V_109d) && isVerNot(V_110))
			DetourDetach(&(PVOID&)d2::drawMissile, d2::drawMissileStub);
		DetourDetach(&(PVOID&)d2::drawWeatherParticles, isVer(V_114d) ? d2::drawWeatherParticlesStub114d : d2::drawWeatherParticlesStub);
		DetourTransactionCommit();

		m_active = false;
		d2::patch_motion_prediction->toggle(m_active);
	}
	m_global_offset = { 0, 0 };
	m_player_motion.offset = { 0, 0 };
	m_world_step = { 0, 0 };
	m_camera_step = { 0, 0 };
	m_step_measured = false;
	m_last_player_pos = { 0, 0 };
	m_last_player_unit = nullptr;
	m_text_keys_prev.clear();
	m_text_keys_cur.clear();
	m_rect_keys_prev.clear();
	m_rect_keys_cur.clear();
	m_text_state.clear();
	m_rect_state.clear();
	m_frame_rect_offsets.clear();
	m_image_pos_prev.clear();
	m_image_pos_cur.clear();
}

void MotionPrediction::update()
{
	if (!isAvailable()) {
		m_global_offset = { 0, 0 };
		m_player_motion.offset = { 0, 0 };
		return;
	}

	m_frame_time = d2::isEscMenuOpen() ? 0.0f : (float)(App.context->getFrameTime() / 1000.0);

	const auto frame_time_ms = (int64_t)(App.context->getAvgFrameTime() * (65536.0 / 1000.0));
	int32_t delta = (int32_t)glm::max((int64_t)INT_MIN, glm::min((int64_t)INT_MAX, frame_time_ms));

	m_player_motion.unit = d2::getPlayerUnit();
	setUnitMotion(&m_player_motion, delta);
	updateWorldScroll();

	const auto frame = App.context->getFrameCount() - 1;
	for (auto it = m_units.begin(); it != m_units.end();) {
		if (it->second.frame != frame)
			it = m_units.erase(it);
		else {
			if (it->second.unit = d2::findUnit(it->first)) {
				setUnitMotion(&it->second, delta);
				it++;
			} else
				it = m_units.erase(it);
		}
	}

	m_perspective = d2::isPerspective();
	m_global_offset = getUnitOffset(&m_player_motion);
	m_alt_item_offset_applied = false;
	m_loot_nameplate_valid = false;
}

glm::ivec2 MotionPrediction::getGlobalOffset(bool skip)
{
	if (App.game.draw_stage == DrawStage::World && (!skip || !m_perspective))
		return m_global_offset;

	return { 0, 0 };
}

glm::ivec2 MotionPrediction::getGlobalOffsetPerspective()
{
	if (App.game.draw_stage == DrawStage::World && m_draw_fn == D2DrawFn::PerspectiveImage)
		return m_global_offset;

	return { 0, 0 };
}

glm::ivec2 MotionPrediction::getUnitOffset(uint32_t type_id)
{
	if (isActive())
		return m_units[type_id].offset;

	return { 0, 0 };
}

void MotionPrediction::updateWorldScroll()
{
	// Fallback camera step from the player path (the game camera follows the
	// player). The exact step, including the frame it happens on, is measured
	// from the world sprites in getCameraStep().
	const auto player = m_player_motion.unit;
	const auto path = player ? d2::getUnitPath(player) : nullptr;
	const glm::ivec2 pos = path ? glm::ivec2((int32_t)path->x, (int32_t)path->y) : glm::ivec2(0, 0);

	m_world_step = { 0, 0 };

	if (player && player == m_last_player_unit && path) {
		const glm::vec2 delta = glm::vec2(pos - m_last_player_pos) / 65536.0f;
		const glm::vec2 scale_factors = { 32.0f / sqrtf(2.0f), 16.0f / sqrtf(2.0f) };
		const glm::vec2 screen_offset = scale_factors * glm::vec2(delta.x - delta.y, delta.x + delta.y);

		// Teleport/waypoint: a huge jump is not a camera scroll.
		if (glm::max(fabs(screen_offset.x), fabs(screen_offset.y)) < 64.0f)
			m_world_step = { -(int)roundf(screen_offset.x), -(int)roundf(screen_offset.y) };
		else {
			m_text_state.clear();
			m_rect_state.clear();
		}
	} else {
		// New game/session: drop everything learned about world anchors.
		m_text_state.clear();
		m_rect_state.clear();
	}

	m_last_player_unit = player;
	m_last_player_pos = pos;

	// Rotate the per frame draw position history used to tell world anchored
	// elements (they move with the camera) from screen space UI (it does not).
	m_text_keys_prev.swap(m_text_keys_cur);
	m_text_keys_cur.clear();
	m_rect_keys_prev.swap(m_rect_keys_cur);
	m_rect_keys_cur.clear();

	m_image_pos_prev.swap(m_image_pos_cur);
	m_image_pos_cur.clear();
	m_step_measured = false;
	m_frame_rect_offsets.clear();

	const uint32_t frame = App.context->getFrameCount();
	if (frame % 60 != 0)
		return;

	for (auto it = m_text_state.begin(); it != m_text_state.end();) {
		if (frame - it->second.frame > WORLD_ANCHOR_LIFETIME)
			it = m_text_state.erase(it);
		else
			++it;
	}
	for (auto it = m_rect_state.begin(); it != m_rect_state.end();) {
		if (frame - it->second.frame > WORLD_ANCHOR_LIFETIME)
			it = m_rect_state.erase(it);
		else
			++it;
	}
}

void MotionPrediction::measureCameraStep()
{
	m_step_measured = true;
	m_camera_step = { 0, 0 };
	m_step_valid = false;

	// Most images drawn during the world pass belong to static world objects, so
	// the most common displacement between the last two frames is exactly how
	// far the camera scrolled (and on which frame it did).
	if (m_image_pos_prev.empty() || m_image_pos_cur.empty())
		return;

	struct Disp {
		int16_t x, y;
		int16_t count;
	};
	Disp disp[96];
	int disp_count = 0;

	for (const auto& a : m_image_pos_prev) {
		for (const auto& b : m_image_pos_cur) {
			const int dx = b.x - a.x;
			const int dy = b.y - a.y;
			if (abs(dx) > 48 || abs(dy) > 48)
				continue;

			int i = 0;
			for (; i < disp_count; i++)
				if (disp[i].x == dx && disp[i].y == dy)
					break;

			if (i < disp_count)
				disp[i].count++;
			else if (disp_count < (int)(sizeof(disp) / sizeof(disp[0])))
				disp[disp_count++] = { (int16_t)dx, (int16_t)dy, 1 };
		}
	}

	// Take the most common displacement. Duplicate sprite positions are already
	// collapsed, so a rigid camera scroll wins clearly over the noise.
	int best = 0;
	for (int i = 1; i < disp_count; i++)
		if (disp[i].count > disp[best].count)
			best = i;

	if (disp_count && disp[best].count >= 2) {
		m_camera_step = { disp[best].x, disp[best].y };
		m_step_valid = true;
	}
}

glm::ivec2 MotionPrediction::getCameraStep()
{
	if (!m_step_measured)
		measureCameraStep();

	// Measured from the world sprites: exact, including a zero step (the camera
	// did not scroll on this frame). Otherwise fall back to the player path.
	return m_step_valid ? m_camera_step : m_world_step;
}

bool MotionPrediction::checkWorldAnchored(uint64_t key, const std::unordered_set<uint64_t>& previous, std::unordered_map<uint64_t, DrawState>& state, const uint64_t* expected, int expected_count, const uint64_t* nearby, int nearby_count)
{
	const uint32_t frame = App.context->getFrameCount();

	// Already classified: the frames between two camera updates keep the
	// previous result (the game only scrolls the camera in steps).
	const auto it = state.find(key);
	if (it != state.end() && it->second.frame + 1 >= frame) {
		it->second.frame = frame;
		return it->second.world;
	}

	bool world = false;

	// Centered HUD text moves a pixel or two when the value shown changes
	// width. Keep the classification of the neighbouring position in that case
	// instead of treating it as a brand new element.
	for (int i = 0; i < nearby_count && !world; i++) {
		const auto n = state.find(nearby[i]);
		if (n != state.end() && n->second.frame + 1 >= frame && n->second.world)
			world = true;
	}

	// Otherwise: drawn exactly where it was drawn last frame, shifted by the
	// amount the camera scrolled -> it is anchored to the world.
	if (!world) {
		for (int i = 0; i < expected_count && !world; i++)
			world = previous.count(expected[i]) > 0;
	}

	if (state.size() < MAX_WORLD_ANCHORS)
		state.insert_or_assign(key, DrawState{ frame, world });

	return world;
}

bool MotionPrediction::isWorldAnchoredText(int x, int y)
{
	if (!isAvailable())
		return false;

	const uint64_t key = text_position_key(x, y);
	m_text_keys_cur.insert(key);

	const glm::ivec2 step = getCameraStep();
	const bool moved = step != glm::ivec2(0, 0);

	uint64_t expected[(3 * 3)];
	int expected_count = 0;
	for (int ox = -1; ox <= 1; ox++)
		for (int oy = -1; oy <= 1; oy++)
			expected[expected_count++] = text_position_key(x - step.x + ox, y - step.y + oy);

	uint64_t nearby[(7 * 7)];
	int nearby_count = 0;
	for (int ox = -3; ox <= 3; ox++)
		for (int oy = -3; oy <= 3; oy++)
			nearby[nearby_count++] = text_position_key(x + ox, y + oy);

	return checkWorldAnchored(key, m_text_keys_prev, m_text_state, expected, moved ? expected_count : 0, nearby, nearby_count);
}

bool MotionPrediction::isWorldAnchoredRect(int left, int top, int right, int bottom)
{
	if (!isAvailable())
		return false;

	const uint64_t key = rect_position_key(left, top, right, bottom);
	m_rect_keys_cur.insert(key);

	const glm::ivec2 step = getCameraStep();
	const bool moved = step != glm::ivec2(0, 0);

	uint64_t expected[(3 * 3)];
	int expected_count = 0;
	for (int ox = -1; ox <= 1; ox++)
		for (int oy = -1; oy <= 1; oy++)
			expected[expected_count++] = rect_position_key(left - step.x + ox, top - step.y + oy, right - step.x + ox, bottom - step.y + oy);

	uint64_t nearby[(7 * 7)];
	int nearby_count = 0;
	for (int ox = -3; ox <= 3; ox++)
		for (int oy = -3; oy <= 3; oy++)
			nearby[nearby_count++] = rect_position_key(left + ox, top + oy, right + ox, bottom + oy);

	return checkWorldAnchored(key, m_rect_keys_prev, m_rect_state, expected, moved ? expected_count : 0, nearby, nearby_count);
}

glm::ivec2 MotionPrediction::getFrameRectOffset(int left, int top, int right, int bottom)
{
	if (!isAvailable())
		return { 0, 0 };

	const auto it = m_frame_rect_offsets.find(rect_position_key(left, top, right, bottom));
	return it != m_frame_rect_offsets.end() ? it->second : glm::ivec2(0, 0);
}

glm::ivec2 MotionPrediction::drawImage(int x, int y, D2DrawFn fn, uint32_t gamma, int draw_mode)
{
	glm::ivec2 pos = { x, y };
	m_draw_fn = fn;

	if (!isAvailable() || fn == D2DrawFn::PerspectiveImage)
		return pos;

	// Raw positions of the world sprites of this frame, used to measure how far
	// the camera scrolled since the previous frame.
	if (App.game.draw_stage == DrawStage::World && m_image_pos_cur.size() < 32) {
		const glm::ivec2 raw = { x, y };
		bool duplicate = false;
		for (const auto& pos : m_image_pos_cur) {
			if (pos == raw) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
			m_image_pos_cur.push_back(raw);
	}

	if (fn == D2DrawFn::Shadow || fn == D2DrawFn::ImageFast) {
		if (isHudMotionStage())
			return pos;
		if (glm::max(abs(pos.x - m_player_motion.screen_pos.x), abs(pos.y - m_player_motion.screen_pos.y)) < 16)
			return pos;
		else {
			for (auto& item : m_units) {
				if (glm::max(abs(item.second.screen_pos.x - pos.x), abs(item.second.screen_pos.y - pos.y)) < 16) {
					pos += getUnitOffset(&item.second);
					break;
				}
			}
		}
		return pos - m_global_offset;
	}

	if (d2::currently_drawing_unit) {
		if (isHudMotionStage())
			return pos;
		if (d2::currently_drawing_unit == d2::getPlayerUnit()) {
			m_player_motion.screen_pos = pos;
			return pos;
		} else {
			if (d2::currently_drawing_unit->dwType == d2::UnitType::Player || d2::currently_drawing_unit->dwType == d2::UnitType::Monster || d2::currently_drawing_unit->dwType == d2::UnitType::Missile) {
				const uint32_t type_id = d2::getUnitID(d2::currently_drawing_unit) | ((uint8_t)d2::currently_drawing_unit->dwType << 24);
				UnitMotion* unit_motion = &m_units[type_id];
				const uint32_t frame = App.context->getFrameCount();

				if (unit_motion->frame != frame) {
					unit_motion->unit = d2::currently_drawing_unit;
					unit_motion->frame = frame;
					unit_motion->screen_pos = pos;
				}
				if (fn == D2DrawFn::Image)
					pos += getUnitOffset(unit_motion);
			}
			return pos - m_global_offset;
		}
	} else {
		if (App.game.draw_stage == DrawStage::World && fn == D2DrawFn::Image && draw_mode == 3 && gamma == 0xFFFFFFFF)
			return pos - m_global_offset;
	}

	return pos;
}

glm::ivec2 MotionPrediction::drawLine(int start_x, int start_y)
{
	if (!isAvailable() || !d2::currently_drawing_weather_particles)
		return { 0, 0 };

	uint32_t particle_index = *d2::currently_drawing_weather_particle_index_ptr;
	if (particle_index == m_last_particle_index)
		particle_index += 256;

	const auto frame = App.context->getFrameCount();
	ParticleMotion& particle = m_particles[particle_index & 511];
	m_last_particle_index = particle_index;

	if (particle.frame != frame) {
		const glm::ivec2 pos = { start_x, start_y };
		const glm::ivec2 diff = pos - particle.last_pos;
		const int error = glm::max(abs(diff.x), abs(diff.y));

		if (frame - particle.frame > 2 || error > 100) {
			particle.last_pos = pos;
			particle.predicted_pos = pos;
			particle.velocity = { 0.0f, 0.0f };
		} else {
			if (error > 0) {
				particle.velocity = { 25.0f * diff.x, 25.0f * diff.y };
				particle.last_pos = pos;
			}
		}

		particle.predicted_pos += particle.velocity * m_frame_time;
		particle.offset = -glm::ivec2(particle.predicted_pos - particle.last_pos);
		particle.frame = frame;
	}

	return particle.offset;
}

glm::ivec2 MotionPrediction::drawSolidRect(int left, int top, int right, int bottom)
{
	if (!isAvailable())
		return { 0, 0 };

	// MXL draws some of its HUD (training hud stats, item labels, chat ...) as
	// black plates during the world/HUD pass. A plate that moves with the world
	// has to be shifted together with it, a screen space one must stay where
	// the game put it.
	const bool world_anchored = isWorldOffsetStage() && isWorldAnchoredRect(left, top, right, bottom);
	const bool screen_fixed = m_rect_keys_prev.count(rect_position_key(left, top, right, bottom)) > 0;
	const bool nameplate = !screen_fixed && isLootNameplateRect(left, top, right, bottom);

	glm::ivec2 offset = { 0, 0 };

	if (m_text_fn == D2DrawFn::NormalText && !isHudMotionStage()) {
		if (d2::headsup_text_unit && m_player_motion.unit != d2::headsup_text_unit) {
			offset = m_global_offset;
			if (d2::headsup_text_unit->dwType == d2::UnitType::Monster) {
				const uint32_t type_id = d2::getUnitID(d2::headsup_text_unit) | ((uint8_t)d2::headsup_text_unit->dwType << 24);
				offset -= getUnitOffset(&m_units[type_id]);
			}
		}
	}

	if (offset == glm::ivec2(0, 0) && (world_anchored || nameplate)) {
		m_loot_nameplate_l = left;
		m_loot_nameplate_t = top;
		m_loot_nameplate_r = right;
		m_loot_nameplate_b = bottom;
		m_loot_nameplate_valid = true;
		offset = m_global_offset;
	}

	// The box frame of a button/panel is drawn on top of this plate, remember
	// the offset so its border moves together with the background.
	if (offset != glm::ivec2(0, 0))
		m_frame_rect_offsets[rect_position_key(left, top, right, bottom)] = offset;

	return offset;
}

glm::ivec2 MotionPrediction::drawText(const wchar_t* str, int x, int y, D2DrawFn fn)
{
	glm::ivec2 pos = { x, y };
	if (!isAvailable() || !str)
		return pos;

	if (fn == m_text_fn && !isHudMotionStage()) {
		if (d2::headsup_text_unit && m_player_motion.unit != d2::headsup_text_unit) {
			if (d2::headsup_text_unit->dwType == d2::UnitType::Monster) {
				const uint32_t type_id = d2::getUnitID(d2::headsup_text_unit) | ((uint8_t)d2::headsup_text_unit->dwType << 24);
				const auto unit_offset = getUnitOffset(&m_units[type_id]);
				pos += unit_offset;
			}
			pos -= m_global_offset;
			return pos;
		}
	}

	if (fn == D2DrawFn::FramedText) {
		const auto unit = d2::getSelectedUnit();

		if (unit && m_active)
			pos -= m_global_offset;
		return pos;
	}

	// DrawRectangledText: offset unless altItemsTextMotion already shifted the rect.
	if (fn == D2DrawFn::RectangledText) {
		if (m_alt_item_offset_applied)
			m_alt_item_offset_applied = false;
		else
			pos -= m_global_offset;
		return pos;
	}

	if (fn == D2DrawFn::NormalText || fn == D2DrawFn::NormalTextEx) {
		const bool world_anchored = isWorldOffsetStage() && isWorldAnchoredText(x, y);
		if (!isChatLogText(x)) {
			const bool loot_name = textNearLootNameplate(x, y);
			const bool world_label = (App.game.draw_stage == DrawStage::World) && isWorldAnchoredUiLabel(x, y, str);
			if (loot_name || world_label || world_anchored) {
				if (m_alt_item_offset_applied)
					m_alt_item_offset_applied = false;
				else
					pos -= m_global_offset;
			}
		}
	}

	return pos;
}

void MotionPrediction::unitHoverTextMotion()
{
	if (m_active) {
		*d2::hover_text_x -= m_global_offset.x;
		*d2::hover_text_y -= m_global_offset.y;
	}
}

void MotionPrediction::altItemsTextMotion()
{
	if (!m_active || !d2::alt_item_pos)
		return;

	static int* x1 = nullptr;
	static int* x2 = nullptr;
	static int* y1 = nullptr;
	static int* y2 = nullptr;

	switch (helpers::getVersion()) {
		case Version::V_109d:
			x1 = (int*)((uintptr_t)d2::alt_item_pos + 0x0C);
			x2 = (int*)((uintptr_t)d2::alt_item_pos + 0x20);
			y1 = (int*)((uintptr_t)d2::alt_item_pos + 0x18);
			y2 = (int*)((uintptr_t)d2::alt_item_pos + 0x10);
			break;
		case Version::V_110:
			x1 = (int*)((uintptr_t)d2::alt_item_pos + 0x30);
			x2 = (int*)((uintptr_t)d2::alt_item_pos + 0x2C);
			y1 = (int*)((uintptr_t)d2::alt_item_pos + 0x14);
			y2 = (int*)((uintptr_t)d2::alt_item_pos + 0x24);
			break;
		case Version::V_114d:
			x1 = (int*)((uintptr_t)d2::alt_item_pos + 0x4C);
			x2 = (int*)((uintptr_t)d2::alt_item_pos + 0x2C);
			y1 = (int*)((uintptr_t)d2::alt_item_pos + 0x40);
			y2 = (int*)((uintptr_t)d2::alt_item_pos + 0x34);
			break;
		default:
			x1 = (int*)((uintptr_t)d2::alt_item_pos + 0x34);
			x2 = (int*)((uintptr_t)d2::alt_item_pos + 0x30);
			y1 = (int*)((uintptr_t)d2::alt_item_pos + 0x18);
			y2 = (int*)((uintptr_t)d2::alt_item_pos + 0x28);
	}
	*x1 -= m_global_offset.x;
	*y1 -= m_global_offset.y;
	*x2 -= m_global_offset.x;
	*y2 -= m_global_offset.y;
	m_alt_item_offset_applied = true;
}

bool MotionPrediction::isHudMotionStage()
{
	return App.game.draw_stage == DrawStage::HUD
		|| App.game.draw_stage == DrawStage::Map
		|| App.game.draw_stage == DrawStage::Map2
		|| App.game.draw_stage == DrawStage::Cursor
		|| App.game.draw_stage == DrawStage::CursorItem;
}

bool MotionPrediction::isWorldOffsetStage()
{
	// Stages where the world under the drawn element was shifted by the motion
	// prediction offset. The automap pass (Map/Map2) and the cursor pass are
	// drawn without it, so their text must not be shifted either.
	return App.game.draw_stage == DrawStage::World
		|| App.game.draw_stage == DrawStage::UI
		|| App.game.draw_stage == DrawStage::HUD;
}

bool MotionPrediction::isLootNameplateRect(int left, int top, int right, int bottom)
{
	if (d2::isEscMenuOpen())
		return false;
	if (App.game.draw_stage != DrawStage::World && App.game.draw_stage != DrawStage::UI && App.game.draw_stage != DrawStage::HUD)
		return false;

	const int width = right - left;
	const int height = bottom - top;
	if (height < 10 || height > 40 || width < 24 || width > 420)
		return false;
	if (width > (int)App.game.size.x / 2)
		return false;
	// Loot nameplates are wide relative to height; skip checkbox/panel squares.
	if (width < height * 2)
		return false;
	// Chat / message log backgrounds (left-aligned one-line strips).
	if (left <= 80 && height >= 10 && height <= 24)
		return false;
	if (height == 16 && (left == 10 || left == 11 || (*d2::screen_shift == SCREENPANEL_LEFT && left == (int)App.game.size.x / 2 + 10)))
		return false;
	// Hireling / summon HP under the left portraits.
	if (height <= 24 && width <= 100 && left < 130)
		return false;
	// Square-ish black plate behind merc/minion portraits (left HUD stack).
	if (left < 180 && top < 450 && width <= 110 && height >= 28 && width <= height + 20)
		return false;

	const int shift = *d2::screen_shift;
	const int mid_x = (int)App.game.size.x / 2;
	if (shift == SCREENPANEL_BOTH)
		return false;
	if (shift == SCREENPANEL_LEFT && left < mid_x)
		return false;
	if (shift == SCREENPANEL_RIGHT && left > mid_x)
		return false;

	return true;
}

bool MotionPrediction::textNearLootNameplate(int x, int y)
{
	if (!m_loot_nameplate_valid)
		return false;
	return x >= m_loot_nameplate_l - 24 && x <= m_loot_nameplate_r + 24
		&& y >= m_loot_nameplate_t - 8 && y <= m_loot_nameplate_b + 28;
}

bool MotionPrediction::isGroundLootLabel(int x, int y)
{
	return textNearLootNameplate(x, y);
}

bool MotionPrediction::isChatLogText(int x)
{
	if (modules::HDText::Instance().getTextSize() == 13)
		return true;
	if (x == 15 || x == 10 || x == 11)
		return true;
	if (*d2::screen_shift == SCREENPANEL_LEFT && x == (int)App.game.size.x / 2 + 15)
		return true;
	return false;
}

bool MotionPrediction::isWorldAnchoredUiLabel(int x, int y, const wchar_t* str)
{
	UNREFERENCED_PARAMETER(str);

	if (d2::isEscMenuOpen())
		return false;

	if (App.game.draw_stage != DrawStage::World)
		return false;

	const int shift = *d2::screen_shift;
	const int mid_x = (int)App.game.size.x / 2;
	if (shift == SCREENPANEL_BOTH)
		return false;
	if (shift == SCREENPANEL_LEFT && x < mid_x)
		return false;
	if (shift == SCREENPANEL_RIGHT && x > mid_x)
		return false;

	return true;
}

void MotionPrediction::setUnitMotion(UnitMotion* unit_motion, int32_t delta)
{
	const d2::Path* path = d2::getUnitPath(unit_motion->unit);
	const glm::ivec2 unit_pos = { (int32_t)path->x, (int32_t)path->y };

	glm::ivec2 pos_whole = { unit_pos.x >> 16, unit_pos.y >> 16 };
	glm::ivec2 last_pos_whole = { unit_motion->last_pos.x >> 16, unit_motion->last_pos.y >> 16 };
	glm::ivec2 predicted_pos_whole = { unit_motion->predicted_pos.x >> 16, unit_motion->predicted_pos.y >> 16 };

	int32_t last_pos_md = std::max(abs(pos_whole.x - last_pos_whole.x), abs(pos_whole.y - last_pos_whole.y));
	int32_t predicted_pos_md = std::max(abs(pos_whole.x - predicted_pos_whole.x), abs(pos_whole.y - predicted_pos_whole.y));

	if (last_pos_md > 2 || predicted_pos_md > 2) {
		unit_motion->predicted_pos = unit_pos;
		unit_motion->corrected_pos = unit_pos;
		unit_motion->last_pos = unit_pos;
		unit_motion->velocity = { 0, 0 };
	}

	const int32_t dx = unit_pos.x - unit_motion->last_pos.x;
	const int32_t dy = unit_pos.y - unit_motion->last_pos.y;

	unit_motion->dt_last_pos_change += delta;

	if (dx != 0 || dy != 0 || unit_motion->dt_last_pos_change >= (65536 / 25)) {
		unit_motion->corrected_pos.x = ((int64_t)unit_pos.x + unit_motion->last_pos.x) >> 1;
		unit_motion->corrected_pos.y = ((int64_t)unit_pos.y + unit_motion->last_pos.y) >> 1;

		unit_motion->velocity.x = 25 * dx;
		unit_motion->velocity.y = 25 * dy;

		unit_motion->last_pos = unit_pos;
		unit_motion->dt_last_pos_change = 0;
	}

	if (unit_motion->velocity.x != 0 || unit_motion->velocity.y != 0) {
		if (unit_motion->dt_last_pos_change < (65536 / 25)) {
			glm::ivec2 step = { (int32_t)(((int64_t)delta * unit_motion->velocity.x) >> 16), (int32_t)(((int64_t)delta * unit_motion->velocity.y) >> 16) };

			const int32_t correction = 7000;
			const int32_t one_minus_correction = 65536 - correction;

			unit_motion->predicted_pos.x = (int32_t)(((int64_t)unit_motion->predicted_pos.x * one_minus_correction + (int64_t)unit_motion->corrected_pos.x * correction) >> 16);
			unit_motion->predicted_pos.y = (int32_t)(((int64_t)unit_motion->predicted_pos.y * one_minus_correction + (int64_t)unit_motion->corrected_pos.y * correction) >> 16);

			unit_motion->predicted_pos.x += step.x;
			unit_motion->predicted_pos.y += step.y;

			unit_motion->corrected_pos.x += step.x;
			unit_motion->corrected_pos.y += step.y;
		}
	} else {
		// Unit stopped moving: let the predicted position fall back to the real
		// one, otherwise the offset stays frozen and everything drawn with it
		// (tiles, sprites, world anchored HUD) stays shifted for good.
		const int32_t correction = 12000;
		const int32_t one_minus_correction = 65536 - correction;

		unit_motion->predicted_pos.x = (int32_t)(((int64_t)unit_motion->predicted_pos.x * one_minus_correction + (int64_t)unit_pos.x * correction) >> 16);
		unit_motion->predicted_pos.y = (int32_t)(((int64_t)unit_motion->predicted_pos.y * one_minus_correction + (int64_t)unit_pos.y * correction) >> 16);
	}

	unit_motion->offset_update = true;
}

glm::ivec2 MotionPrediction::getUnitOffset(UnitMotion* unit_motion)
{
	if (unit_motion->offset_update) {
		const glm::vec2 offset = { (unit_motion->predicted_pos.x - unit_motion->last_pos.x) / 65536.0f, (unit_motion->predicted_pos.y - unit_motion->last_pos.y) / 65536.0f };
		const glm::vec2 scale_factors = { 32.0f / sqrtf(2.0f), 16.0f / sqrtf(2.0f) };
		const glm::vec2 screen_offset = scale_factors * glm::vec2(offset.x - offset.y, offset.x + offset.y) + 0.5f;
		unit_motion->offset = { (int)screen_offset.x, (int)screen_offset.y };
		unit_motion->offset_update = false;
	}
	return unit_motion->offset;
}

}