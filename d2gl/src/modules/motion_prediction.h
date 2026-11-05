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

#pragma once

#include <unordered_set>
#include <vector>

#include "d2/structs.h"

namespace d2gl::modules {

struct UnitMotion {
	uint32_t frame = 0;
	d2::UnitAny* unit = nullptr;
	glm::ivec2 screen_pos = { 0, 0 };
	glm::ivec2 offset = { 0, 0 };
	bool offset_update = false;

	glm::ivec2 last_pos = { 0, 0 };
	glm::ivec2 predicted_pos = { 0, 0 };
	glm::ivec2 corrected_pos = { 0, 0 };
	glm::ivec2 velocity = { 0, 0 };
	int64_t dt_last_pos_change = 0;
};

struct ParticleMotion {
	uint32_t frame = 0;
	glm::ivec2 offset = { 0, 0 };

	glm::ivec2 last_pos = { 0, 0 };
	glm::ivec2 predicted_pos = { 0, 0 };
	glm::vec2 velocity = { 0.0f, 0.0f };
};

class MotionPrediction {
	bool m_active = false;
	float m_frame_time = 0.0f;
	glm::ivec2 m_global_offset = { 0, 0 };

	std::unordered_map<uint32_t, UnitMotion> m_units;
	UnitMotion m_player_motion;
	bool m_perspective = false;

	D2DrawFn m_text_fn = D2DrawFn::None;
	D2DrawFn m_draw_fn = D2DrawFn::None;
	bool m_alt_item_offset_applied = false;
	bool m_loot_nameplate_valid = false;
	int m_loot_nameplate_l = 0;
	int m_loot_nameplate_t = 0;
	int m_loot_nameplate_r = 0;
	int m_loot_nameplate_b = 0;

	// World anchored HUD detection. Some MXL HUD elements (training stats,
	// filter labels, ground nameplates) are drawn during the HUD pass but are
	// positioned from a world anchor, so they must be shifted together with the
	// world. Screen space HUD keeps the same position from frame to frame.
	glm::ivec2 m_last_player_pos = { 0, 0 };
	d2::UnitAny* m_last_player_unit = nullptr;

	// Camera step (screen pixels) between the last two frames, either measured
	// from the world sprites (exact) or projected from the player path.
	glm::ivec2 m_world_step = { 0, 0 };
	glm::ivec2 m_camera_step = { 0, 0 };
	bool m_step_measured = false;
	bool m_step_valid = false;
	std::vector<glm::ivec2> m_image_pos_prev;
	std::vector<glm::ivec2> m_image_pos_cur;

	std::unordered_set<uint64_t> m_rect_keys_prev;
	std::unordered_set<uint64_t> m_rect_keys_cur;

	// Screen position -> is it anchored to the world (it moved with the camera
	// when it was first drawn at this position).
	struct DrawState {
		uint32_t frame = 0;
		bool world = false;
	};
	std::unordered_map<uint64_t, DrawState> m_rect_state;
	std::unordered_map<uint64_t, glm::ivec2> m_frame_rect_offsets;

	// Same, but followed per string (hash) and only promoted to world when the
	// text has been seen moving by exactly the camera step.
	struct TextAnchor {
		uint32_t frame = 0;
		glm::ivec2 pos = { 0, 0 };
		bool world = false;
	};
	std::unordered_map<uint32_t, TextAnchor> m_text_anchors;

	std::array<ParticleMotion, 512> m_particles;
	uint32_t m_last_particle_index = 0;

	MotionPrediction();
	~MotionPrediction() = default;

public:
	static MotionPrediction& Instance()
	{
		static MotionPrediction instance;
		return instance;
	}

	inline bool isActive() { return m_active; }
	void toggle(bool active = true);
	void update();

	glm::ivec2 getGlobalOffset(bool skip = false);
	glm::ivec2 getGlobalOffsetPerspective();
	glm::ivec2 getUnitOffset(uint32_t type_id);

	glm::ivec2 drawImage(int x, int y, D2DrawFn fn, uint32_t gamma = 0, int draw_mode = 0);
	glm::ivec2 drawLine(int start_x, int start_y);
	glm::ivec2 drawSolidRect(int left, int top, int right = 0, int bottom = 0);
	glm::ivec2 drawText(const wchar_t* str, int x, int y, D2DrawFn fn);

	void unitHoverTextMotion();
	void altItemsTextMotion();

	inline void textMotion(D2DrawFn fn) { m_text_fn = fn; }
	bool isLootNameplateRect(int left, int top, int right, int bottom);
	bool isGroundLootLabel(int x, int y);
	// Offset a box frame has to use so it stays aligned with the solid rect it
	// was drawn for (button/panel border).
	glm::ivec2 getFrameRectOffset(int left, int top, int right, int bottom);

private:
	void setUnitMotion(UnitMotion* unit_motion, int32_t delta);
	glm::ivec2 getUnitOffset(UnitMotion* unit_motion);
	void updateWorldScroll();
	void measureCameraStep();
	glm::ivec2 getCameraStep();
	bool isWorldAnchoredText(const wchar_t* str, int x, int y);
	bool isWorldAnchoredRect(int left, int top, int right, int bottom);
	bool checkWorldAnchored(uint64_t key, const std::unordered_set<uint64_t>& previous, std::unordered_map<uint64_t, DrawState>& state, const uint64_t* expected, int expected_count, const uint64_t* nearby, int nearby_count);
	bool isWorldAnchoredUiLabel(int x, int y, const wchar_t* str = nullptr);
	bool isHudMotionStage();
	bool isWorldOffsetStage();
	bool textNearLootNameplate(int x, int y);
	bool isChatLogText(int x);

	inline bool isAvailable() { return m_active && App.game.screen == GameScreen::InGame; }
};

}