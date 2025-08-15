// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "render.h"
#include "debug_render.h"
#include "utils.h"

#include "hb.h"
#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_rasterizer.h"
#include "skb_layout.h"
#include "skb_editor.h"
#include "skb_image_atlas.h"
#include "ime.h"

typedef struct testbed_context_t {
	example_t base;

	skb_font_collection_t* font_collection;
	skb_temp_alloc_t* temp_alloc;
	render_context_t* rc;

	skb_editor_t* editor;

	bool allow_char;
	view_t view;
	bool drag_view;
	bool drag_text;

	float atlas_scale;
	bool show_glyph_details;
	bool show_caret_details;
	bool show_baseline_details;

} testbed_context_t;

static void update_ime_rect(testbed_context_t* ctx)
{
	skb_text_selection_t edit_selection = skb_editor_get_current_selection(ctx->editor);
	skb_visual_caret_t caret_pos = skb_editor_get_visual_caret(ctx->editor, edit_selection.end_pos);

	skb_rect2i_t input_rect = {
		.x = (int32_t)(ctx->view.cx + caret_pos.x * ctx->view.scale),
		.y = (int32_t)(ctx->view.cy + caret_pos.y * ctx->view.scale),
		.width = (int32_t)(caret_pos.width * ctx->view.scale),
		.height = (int32_t)(caret_pos.height * ctx->view.scale),
	};
	ime_set_input_rect(input_rect);
}

static void ime_handler(ime_event_t event, const uint32_t* text, int32_t text_length, int32_t cursor, void* context)
{
	testbed_context_t* ctx = context;

	if (event == IME_EVENT_COMPOSITION)
		skb_editor_set_composition_utf32(ctx->editor, ctx->temp_alloc, text, text_length, cursor);
	else if (event == IME_EVENT_COMMIT)
		skb_editor_commit_composition_utf32(ctx->editor, ctx->temp_alloc, text, text_length);
	else if (event == IME_EVENT_CANCEL)
		skb_editor_clear_composition(ctx->editor, ctx->temp_alloc);

	update_ime_rect(ctx);
}

void testbed_destroy(void* ctx_ptr);
void testbed_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods);
void testbed_on_char(void* ctx_ptr, unsigned int codepoint);
void testbed_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods);
void testbed_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y);
void testbed_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods);
void testbed_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height);

#define LOAD_FONT_OR_FAIL(path, font_family) \
	if (!skb_font_collection_add_font(ctx->font_collection, path, font_family)) { \
		skb_debug_log("Failed to load " path "\n"); \
		goto error; \
	}

void* testbed_create(render_context_t* rc)
{
	assert(rc);

	testbed_context_t* ctx = skb_malloc(sizeof(testbed_context_t));
	memset(ctx, 0, sizeof(testbed_context_t));

	ctx->base.create = testbed_create;
	ctx->base.destroy = testbed_destroy;
	ctx->base.on_key = testbed_on_key;
	ctx->base.on_char = testbed_on_char;
	ctx->base.on_mouse_button = testbed_on_mouse_button;
	ctx->base.on_mouse_move = testbed_on_mouse_move;
	ctx->base.on_mouse_scroll = testbed_on_mouse_scroll;
	ctx->base.on_update = testbed_on_update;

	ctx->rc = rc;
	render_reset_atlas(rc, NULL);

	ctx->atlas_scale = 0.0f;
	ctx->show_glyph_details = false;
	ctx->show_caret_details = true;
	ctx->show_baseline_details = false;

	ctx->font_collection = skb_font_collection_create();
	assert(ctx->font_collection);

	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansArabic-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansJP-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansKR-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansDevanagari-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansBrahmi-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSerifBalinese-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansTamil-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansBengali-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansThai-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoColorEmoji-Regular.ttf", SKB_FONT_FAMILY_EMOJI);
//	LOAD_FONT_OR_FAIL("data/OpenMoji-color-glyf_colr_1.ttf", SKB_FONT_FAMILY_EMOJI);

	// These snippets have been useful for at some point in developing the library.
	// Leaving them here for future tests.
//	const char* bidiText = "یہ ایک )cargfi( ہے۔";
//	const char* bidiText = "Koffi";
//	const char* bidiText = "nǐn hǎo¿Qué tal?Привет你好안녕하세요こんにちは";
//	const char* bidiText = "a\u0308o\u0308u\u0308";
//	const char* bidiText = "\uE0B0\u2588Öy";
//	const char* bidiText = "एक गांव -- में मोहन नाम का लड़का रहता था। उसके पिताजी एक मामूली मजदूर थे";
//	const char* bidiText = "ᬓ ᬓᬸ ᬓᭀ ᬓᬿ";

//	const char* bidiText = "ᬓᭀ ᬓᬿ ہے۔ kofi یہ ایک";

//	const char* bidiText = "ᬓᭀ ᬓᬿ ہے۔ [kofi] یہ ایک";

//	const char* bidiText = "ᬓᭀ ᬓᬿ (ہے۔) [kofi] (یہ ایک)";

//	const char* bidiText = "ہے۔ kofi یہ ایک"; // rlt line
//	const char* bidiText = "asd ہے۔ kofi یہ ایک";
//	const char* bidiText = "سلام در حال تست";

//	const char* bidiText = "123سلام در حال تست";

//	const char* bidiText = "123.456";

//	const char* bidiText = "١١رس"; // arabic numerals

//	const char* bidiText = "såppa";

//	const char* bidiText = "لا"; // ligature
//	const char* bidiText = "این یک تست است"; // this is a test

//	const char* bidiText = "ltr این یک تست است"; // this is a test

//	const char* bidiText = "aa این یک تست\nاست"; // this is a test

//	const char* bidiText = "ہے۔ kofi یہ ایک";
//	const char* bidiText = "私はその人を常に先生と 呼んでいた。";
//	const char* bidiText = "วันนี้อากาศดี";
//	const char* bidiText = "今天天气晴朗。";
//	const char* bidiText ="Hamburgerfontstiv";

//	const char* bidiText = "🤣moikka 🥰💀✌️🌴🐢🐐🍄⚽🍻👑📸😬foo 👀🚨🏡🕊️🏆😻🌟🧿🍀🎨🍜 bar 🥳🧁🍰🎁🎂🎈🎺🎉🎊📧〽️🧿🌶️🔋 😂❤️😍😊🥺🙏💕😭😘👍😅👏😁";

//	const char* bidiText = "این یک 😬👀🚨 تست است"; // this is a test

//	const char* bidiText = "い😍";

//	const char* bidiText = "🤦🏼‍♂️ Ä था ᬓᬿ";

//	const char* bidiText = "A, B, C, kissa kävelee, tikapuita pitkin taivaaseen.";

//	const char* bidiText = "\nsorsa juo \r\n\r\nkaf  fia\n";
//	const char* bidiText = "sorsa juo \nkaffia thisiverylongwordandstuff and more";
//	const char* bidiText = "शकति शक्ति";
//	const char* bidiText = "हिन्दी हि न्दी";
//	const char* bidiText = "யாவற்றையும்"; // tamil, does not work correctly!
//	const char* bidiText = "ঝিল্লি ঝি ল্লি"; // bengali
//	const char* bidiText = "";

	const char* bidiText = "Hamburgerfontstiv 🤣🥰💀✌️🌴🐢🐐🍄⚽🍻👑📸 این یک تست است 😬👀🚨🏡🕊️🏆😻🌟私はその人を常に先生と 呼んでいた。";

	ctx->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(ctx->temp_alloc);

	skb_color_t ink_color = skb_rgba(64,64,64,255);

	const skb_attribute_t attributes[] = {
		skb_attribute_make_font(SKB_FONT_FAMILY_DEFAULT, 92.f, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL),
		skb_attribute_make_line_height(SKB_LINE_HEIGHT_METRICS_RELATIVE, 1.3f),
		skb_attribute_make_fill(ink_color),
	};

	const skb_attribute_t composition_attributes[] = {
		skb_attribute_make_font(SKB_FONT_FAMILY_DEFAULT, 92.f, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL),
		skb_attribute_make_line_height(SKB_LINE_HEIGHT_METRICS_RELATIVE, 1.3f),
		skb_attribute_make_fill(skb_rgba(0,128,192,255)),
		skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_DOTTED, 0.f, 1.f, skb_rgba(0,128,192,255)),
	};

	skb_editor_params_t edit_params = {
		.layout_params = {
			.lang = "zh-hans",
			.base_direction = SKB_DIRECTION_AUTO,
			.font_collection = ctx->font_collection,
			.layout_width = 1200.f,
			.text_wrap = SKB_WRAP_WORD_CHAR,
			.tab_stop_increment = 92.f * 2.f,
		},
		.text_attributes = attributes,
		.text_attributes_count = SKB_COUNTOF(attributes),
		.composition_attributes = composition_attributes,
		.composition_attributes_count = SKB_COUNTOF(composition_attributes),
	};

	ctx->editor = skb_editor_create(&edit_params);
	assert(ctx->editor);
	skb_editor_set_text_utf8(ctx->editor, ctx->temp_alloc, bidiText, -1);

	ctx->view = (view_t) { .cx = 400.f, .cy = 120.f, .scale = 1.f };

	ime_set_handler(ime_handler, ctx);

	update_ime_rect(ctx);

	return ctx;

error:
	testbed_destroy(ctx);
	return NULL;
}

void testbed_destroy(void* ctx_ptr)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_editor_destroy(ctx->editor);
	skb_font_collection_destroy(ctx->font_collection);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	memset(ctx, 0, sizeof(testbed_context_t));

	ime_cancel();
	ime_set_handler(NULL, NULL);

	skb_free(ctx);
}

void testbed_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	uint32_t edit_mods = 0;
	if (mods & GLFW_MOD_SHIFT)
		edit_mods |= SKB_MOD_SHIFT;
	if (mods & GLFW_MOD_CONTROL)
		edit_mods |= SKB_MOD_CONTROL;

	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		ctx->allow_char = true;
		if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
			// Paste
			const char* clipboard_text = glfwGetClipboardString(window);
			skb_editor_paste_utf8(ctx->editor, ctx->temp_alloc, clipboard_text, -1);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT) == 0)
			skb_editor_undo(ctx->editor, ctx->temp_alloc);
		if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT))
			skb_editor_redo(ctx->editor, ctx->temp_alloc);
		if (key == GLFW_KEY_LEFT)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_LEFT, edit_mods);
		if (key == GLFW_KEY_RIGHT)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_RIGHT, edit_mods);
		if (key == GLFW_KEY_UP)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_UP, edit_mods);
		if (key == GLFW_KEY_DOWN)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_DOWN, edit_mods);
		if (key == GLFW_KEY_HOME)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_HOME, edit_mods);
		if (key == GLFW_KEY_END)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_END, edit_mods);
		if (key == GLFW_KEY_BACKSPACE)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_BACKSPACE, edit_mods);
		if (key == GLFW_KEY_DELETE)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_DELETE, edit_mods);
		if (key == GLFW_KEY_ENTER)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_ENTER, edit_mods);

		update_ime_rect(ctx);
	}
	if (action == GLFW_PRESS) {
		ctx->allow_char = true;
		if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
			// Select all
			skb_editor_select_all(ctx->editor);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_TAB) {
			skb_editor_insert_codepoint(ctx->editor, ctx->temp_alloc, '\t');
		}
		if (key == GLFW_KEY_ESCAPE) {
			// Clear selection
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			if (skb_editor_get_selection_text_utf32_count(ctx->editor, selection) > 0)
				skb_editor_select_none(ctx->editor);
			else
				glfwSetWindowShouldClose(window, GL_TRUE);
		}
		if (key == GLFW_KEY_X && (mods & GLFW_MOD_CONTROL)) {
			// Cut
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			int32_t text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, NULL, -1);
			char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
			text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, text, text_len);
			text[text_len] = '\0';
			glfwSetClipboardString(window, text);
			SKB_TEMP_FREE(ctx->temp_alloc, text);
			skb_editor_cut(ctx->editor, ctx->temp_alloc);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
			// Copy
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			int32_t text_len = skb_editor_get_selection_text_utf8_count(ctx->editor, selection);
			char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
			text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, text, text_len);
			text[text_len] = '\0';
			glfwSetClipboardString(window, text);
			SKB_TEMP_FREE(ctx->temp_alloc, text);
			ctx->allow_char = false;
		}

		update_ime_rect(ctx);

		if (key == GLFW_KEY_F7) {
			ctx->show_baseline_details = !ctx->show_baseline_details;
		}
		if (key == GLFW_KEY_F8) {
			ctx->show_caret_details = !ctx->show_caret_details;
		}
		if (key == GLFW_KEY_F9) {
			ctx->show_glyph_details = !ctx->show_glyph_details;
		}
		if (key == GLFW_KEY_F10) {
			ctx->atlas_scale += 0.25f;
			if (ctx->atlas_scale > 1.01f)
				ctx->atlas_scale = 0.0f;
		}
	}
}

void testbed_on_char(void* ctx_ptr, unsigned int codepoint)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->allow_char)
		skb_editor_insert_codepoint(ctx->editor, ctx->temp_alloc, codepoint);
}

static skb_vec2_t transform_mouse_pos(testbed_context_t* ctx, float mouse_x, float mouse_y)
{
	return (skb_vec2_t) {
		.x = (mouse_x - ctx->view.cx) / ctx->view.scale,
		.y = (mouse_y - ctx->view.cy) / ctx->view.scale,
	};
}

void testbed_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	int32_t mouse_mods = 0;
	if (mods & GLFW_MOD_SHIFT)
		mouse_mods |= SKB_MOD_SHIFT;
	if (mods & GLFW_MOD_CONTROL)
		mouse_mods |= SKB_MOD_CONTROL;

	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) {
			if (!ctx->drag_view) {
				view_drag_start(&ctx->view, mouse_x, mouse_y);
				ctx->drag_view = true;
			}
		}
		if (action == GLFW_RELEASE) {
			if (ctx->drag_view) {
				ctx->drag_view = false;
			}
		}
	}

	if (button == GLFW_MOUSE_BUTTON_LEFT) {

		// caret hit testing
		if (action == GLFW_PRESS) {
			if (!ctx->drag_text) {
				ime_cancel();
				ctx->drag_text = true;
				skb_vec2_t pos = transform_mouse_pos(ctx, mouse_x, mouse_y);
				skb_editor_process_mouse_click(ctx->editor, pos.x, pos.y, mouse_mods, glfwGetTime());
			}
		}

		if (action == GLFW_RELEASE) {
			if (ctx->drag_text) {
				ctx->drag_text = false;
			}
		}
	}

	update_ime_rect(ctx);
}

void testbed_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->drag_view) {
		view_drag_move(&ctx->view, mouse_x, mouse_y);
		update_ime_rect(ctx);
	}

	if (ctx->drag_text) {
		skb_vec2_t pos = transform_mouse_pos(ctx, mouse_x, mouse_y);
		skb_editor_process_mouse_drag(ctx->editor, pos.x, pos.y);
		update_ime_rect(ctx);
	}
}

void testbed_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	const float zoom_speed = 0.2f;
	view_scroll_zoom(&ctx->view, mouse_x, mouse_y, delta_y * zoom_speed);
}


typedef struct draw_selection_context_t {
	float x;
	float y;
	skb_color_t color;
	render_context_t* renderer;
} draw_selection_context_t;

static void draw_selection_rect(skb_rect2_t rect, void* context)
{
	draw_selection_context_t* ctx = (draw_selection_context_t*)context;
	debug_render_filled_rect(ctx->renderer, ctx->x + rect.x, ctx->y + rect.y, rect.width, rect.height, ctx->color);
}

static skb_attribute_font_t get_font_attribute_from_editor_params(const skb_editor_params_t* edit_params)
{
	skb_attribute_span_t dummy_span = {
		.attributes = (skb_attribute_t*)edit_params->text_attributes,
		.attributes_count = edit_params->text_attributes_count,
	};
	return skb_attributes_get_font(&dummy_span);
}

void testbed_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	testbed_context_t* ctx = ctx_ptr;
	assert(ctx);

	{
		skb_temp_alloc_stats_t stats = skb_temp_alloc_stats(ctx->temp_alloc);
		debug_render_text(ctx->rc, (float)view_width - 20,20, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)stats.used / 1024.f, (float)stats.allocated / 1024.f);
		skb_temp_alloc_stats_t render_stats = skb_temp_alloc_stats(render_get_temp_alloc(ctx->rc));
		debug_render_text(ctx->rc, (float)view_width - 20,40, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Render Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)render_stats.used / 1024.f, (float)render_stats.allocated / 1024.f);
	}

	// Draw visual result
	render_push_transform(ctx->rc, ctx->view.cx, ctx->view.cy, ctx->view.scale);

	skb_color_t log_color = skb_rgba(32,128,192,255);
	skb_color_t caret_color = skb_rgba(255,128,128,255);
	skb_color_t caret_color_dark = skb_rgba(192,96,96,255);
	skb_color_t caret2_color = skb_rgba(128,128,255,255);
	skb_color_t caret_color_trans = skb_rgba(255,128,128,32);
	skb_color_t sel_color = skb_rgba(255,192,192,255);
	skb_color_t ink_color = skb_rgba(64,64,64,255);
	skb_color_t ink_color_trans = skb_rgba(32,32,32,128);

	skb_text_selection_t edit_selection = skb_editor_get_current_selection(ctx->editor);


	float layout_height = 0.f;
	float layout_width = 0.f;
	for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(ctx->editor); pi++) {
		const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(ctx->editor, pi);
		const skb_rect2_t layout_bounds = skb_layout_get_bounds(edit_layout);
		layout_height += layout_bounds.height;
		layout_width = skb_maxf(layout_width, layout_bounds.width);
	}

	const char* affinity_str[] = {
		"-",
		"TR", //SKB_AFFINITY_TRAILING,
		"LE", //SKB_AFFINITY_LEADING,
		"SOL", //SKB_AFFINITY_SOL, // Start of line
		"EOL", //SKB_AFFINITY_EOL, // End of line
	};

	{
		float ox = 0.f;
		float oy = 0.f;

		// line break boundaries
		const float line_break_width = skb_editor_get_params(ctx->editor)->layout_params.layout_width;
		debug_render_dashed_line(ctx->rc, ox, oy-50, ox, oy+layout_height+50, 6, ink_color_trans, -1.f);
		debug_render_dashed_line(ctx->rc, ox+line_break_width, oy+50, ox+line_break_width, oy+layout_height+50, 6, ink_color_trans, -1.f);

		if (skb_editor_get_selection_count(ctx->editor, edit_selection) > 0) {
			draw_selection_context_t sel_ctx = { .x = ox, .y = oy, .color = sel_color, .renderer = ctx->rc };
			skb_editor_get_selection_bounds(ctx->editor, edit_selection, draw_selection_rect, &sel_ctx);
		}

		for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(ctx->editor); pi++) {
			const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(ctx->editor, pi);
			const float edit_layout_y = skb_editor_get_paragraph_offset_y(ctx->editor, pi);
			const skb_layout_line_t* lines = skb_layout_get_lines(edit_layout);
			const int32_t lines_count = skb_layout_get_lines_count(edit_layout);
			const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(edit_layout);
			const skb_glyph_t* glyphs = skb_layout_get_glyphs(edit_layout);
			const skb_attribute_span_t* attrib_spans = skb_layout_get_attribute_spans(edit_layout);
			const skb_layout_params_t* layout_params = skb_layout_get_params(edit_layout);
			const int32_t decorations_count = skb_layout_get_decorations_count(edit_layout);
			const skb_decoration_t* decorations = skb_layout_get_decorations(edit_layout);

			// Draw underlines
			for (int32_t i = 0; i < decorations_count; i++) {
				const skb_decoration_t* decoration = &decorations[i];
				const skb_attribute_span_t* span = &attrib_spans[decoration->attribute_span_idx];
				const skb_attribute_decoration_t attr_decoration = span->attributes[decoration->attribute_idx].decoration;
				if (attr_decoration.position != SKB_DECORATION_THROUGHLINE) {
					render_draw_decoration(ctx->rc,
						ox + decoration->offset_x, oy + decoration->offset_y, decoration->length, decoration->pattern_offset, decoration->thickness,
						attr_decoration.style, attr_decoration.position, attr_decoration.color, SKB_RASTERIZE_ALPHA_SDF);
				}
			}

			for (int li = 0; li < lines_count; li++) {
				const skb_layout_line_t* line = &lines[li];

				float rox = ox + line->bounds.x;
				float roy = oy + edit_layout_y + line->baseline;

				float top_y = roy + line->ascender;
				float bot_y = roy + line->descender;
				float baseline_y = roy;

				// Line info
				debug_render_line(ctx->rc, rox - 25, baseline_y,rox,baseline_y, ink_color, -1.f);
				debug_render_text(ctx->rc, rox - 12, baseline_y - 4,13,RENDER_ALIGN_CENTER, ink_color, "L%d", li);

				if (skb_is_rtl(skb_layout_get_resolved_direction(edit_layout)))
					debug_render_text(ctx->rc, rox - 10, bot_y - 5.f,13,RENDER_ALIGN_END, log_color, "< RTL");
				else
					debug_render_text(ctx->rc, rox - 10, bot_y - 5.f,13,RENDER_ALIGN_END, log_color, "LTR >");

				// Draw glyphs
				float pen_x = ox + line->bounds.x;
				float run_start_x = pen_x;
				int32_t run_start_glyph_idx = line->glyph_range.start;
				skb_rect2_t run_bounds = skb_rect2_make_undefined();


				for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
					const skb_layout_run_t* run = &layout_runs[ri];
					const skb_attribute_span_t* attribute_span = &attrib_spans[run->attribute_span_idx];
					const skb_attribute_fill_t attr_fill = skb_attributes_get_fill(attribute_span);
					const skb_attribute_font_t attr_font = skb_attributes_get_font(attribute_span);
					for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++) {
						const skb_glyph_t* glyph = &glyphs[gi];

						float gx = ox + glyph->offset_x;
						float gy = oy + edit_layout_y + glyph->offset_y;

						if (ctx->show_glyph_details) {
							// Glyph pen position
							debug_render_tick(ctx->rc, gx, gy, 5.f, ink_color_trans, -1.f);

							// Glyph bounds
							skb_rect2_t bounds = skb_font_get_glyph_bounds(layout_params->font_collection, glyph->font_handle, glyph->gid, attr_font.size);
							debug_render_stroked_rect(ctx->rc, gx + bounds.x, gy + bounds.y, bounds.width, bounds.height, ink_color_trans, -1.f);

							// Visual index
							debug_render_text(ctx->rc, gx + bounds.x +2.f +0.5f, gy + bounds.y-8+0.5f,13, RENDER_ALIGN_START, ink_color, "%d", gi);

							// Keep track of run of glyphs that map to same text range.
							if (!skb_rect2_is_empty(bounds))
								run_bounds = skb_rect2_union(run_bounds, skb_rect2_translate(bounds, skb_vec2_make(gx,gy)));
						}

						if (run->type == SKB_CONTENT_RUN_UTF8 || run->type == SKB_CONTENT_RUN_UTF32) {
							// Text
							render_draw_glyph(ctx->rc, gx, gy,
								layout_params->font_collection, run->font_handle, glyph->gid, attr_font.size,
								attr_fill.color, SKB_RASTERIZE_ALPHA_SDF);
						}

						if (ctx->show_baseline_details) {
							const skb_text_property_t* text_properties = skb_layout_get_text_properties(edit_layout);
							const skb_text_direction_t dir = text_properties[glyph->text_range.start].direction;
							const uint8_t script = text_properties[glyph->text_range.start].script;
							skb_baseline_set_t baseline_set = skb_font_get_baseline_set(layout_params->font_collection, glyph->font_handle, dir, script, attr_font.size);
							skb_font_metrics_t metrics = skb_font_get_metrics(layout_params->font_collection, glyph->font_handle);

							const float rx = roundf(gx);
							const float ry = roundf(gy);

							debug_render_line(ctx->rc, rx, ry + metrics.ascender * attr_font.size, rx + glyph->advance_x * 0.5f, ry + metrics.ascender * attr_font.size, skb_rgba(0,0,0,255), -1.f);
							debug_render_line(ctx->rc, rx, ry + metrics.descender * attr_font.size, rx + glyph->advance_x * 0.5f, ry + metrics.descender * attr_font.size, skb_rgba(0,0,0,255), -1.f);

							debug_render_line(ctx->rc, rx, ry + baseline_set.alphabetic, rx + glyph->advance_x, ry + baseline_set.alphabetic, skb_rgba(255,64,0,255), -1.f);
							debug_render_line(ctx->rc, rx, ry + baseline_set.ideographic, rx + glyph->advance_x, ry + baseline_set.ideographic, skb_rgba(0,64,255,255), -1.f);
							debug_render_line(ctx->rc, rx, ry + baseline_set.hanging, rx + glyph->advance_x, ry + baseline_set.hanging, skb_rgba(0,192,255,255), -1.f);
							debug_render_line(ctx->rc, rx, ry + baseline_set.central, rx + glyph->advance_x, ry + baseline_set.central, skb_rgba(64,255,0,255), -1.f);
						}

						pen_x += glyph->advance_x;

						if (ctx->show_glyph_details) {
							const int32_t next_gi = gi + 1;
							if (next_gi > line->glyph_range.end || glyphs[next_gi].text_range.start != glyph->text_range.start) {
								// Glyph run bounds
								if ((next_gi - run_start_glyph_idx) > 1 && !skb_rect2_is_empty(run_bounds))
									debug_render_stroked_rect(ctx->rc, run_bounds.x - 4.f, run_bounds.y - 4.f, run_bounds.width + 8.f, run_bounds.height + 8.f, ink_color_trans, -1.f);

								// Logical id
								float run_end_x = pen_x;
								debug_render_stroked_rect(ctx->rc, run_start_x + 2.f + 0.5f, bot_y + 0.5f - 18, (run_end_x - run_start_x) - 4.f,  18.f, log_color, -1.f);
								if ((glyph->text_range.end - glyph->text_range.start) > 1)
									debug_render_text(ctx->rc, run_start_x + 5.f, bot_y - 5.f, 11, RENDER_ALIGN_START, log_color, "L%d - L%d", glyph->text_range.start, glyph->text_range.end-1);
								else
									debug_render_text(ctx->rc, run_start_x + 5.f, bot_y - 5.f,11,RENDER_ALIGN_START, log_color, "L%d", glyph->text_range.start);

								// Reset
								run_bounds = skb_rect2_make_undefined();
								run_start_x = pen_x;
								run_start_glyph_idx = gi + 1;
							}
						}
					}
				}

				if (ctx->show_caret_details) {
					float left_text_offset = 0.f;

					skb_caret_iterator_t caret_iter = skb_caret_iterator_make(edit_layout, li);

					float caret_x = 0.f;
					float caret_advance = 0.f;
					skb_caret_iterator_result_t left = {0};
					skb_caret_iterator_result_t right = {0};

					while (skb_caret_iterator_next(&caret_iter, &caret_x, &caret_advance, &left, &right)) {

						float cx = ox + caret_x;
						debug_render_line(ctx->rc, cx, bot_y, cx, top_y + 5, caret_color, -1.f);

						if (left.direction != right.direction) {
							debug_render_tri(ctx->rc, cx, top_y+5, cx-5, top_y+5, cx, top_y+5+5, caret2_color);
							debug_render_tri(ctx->rc, cx, top_y+5, cx+5, top_y+5, cx, top_y+5+5, caret_color);
							debug_render_text(ctx->rc, cx-3,top_y + 20 + left_text_offset, 11, RENDER_ALIGN_END, caret2_color, "%s%d", affinity_str[left.text_position.affinity], left.text_position.offset);
							debug_render_text(ctx->rc, cx+3,top_y + 20, 11,RENDER_ALIGN_START, caret_color, "%s%d", affinity_str[right.text_position.affinity], right.text_position.offset);
							left_text_offset = caret_advance < 40.f ? 15 : 0;
						} else {
							if (right.text_position.affinity == SKB_AFFINITY_TRAILING) { // || caret_iter.right.affinity == SKB_AFFINITY_EOL) {
								debug_render_tri(ctx->rc, cx, top_y+5, cx + (skb_is_rtl(right.direction) ? -5 : 5), top_y+5, cx, top_y+5+5, caret_color);
								debug_render_text(ctx->rc, cx+3,top_y + 20,11, RENDER_ALIGN_START, caret_color, "%s%d", affinity_str[right.text_position.affinity], right.text_position.offset);
								left_text_offset = caret_advance < 40.f ? 15 : 0;
							} else {
								debug_render_tri(ctx->rc, cx, top_y+5, cx + (skb_is_rtl(left.direction) ? -5 : 5), top_y+5, cx, top_y+5+5, caret2_color);
								debug_render_text(ctx->rc, cx-3,top_y + 20+left_text_offset, 11, RENDER_ALIGN_END, caret2_color, "%s%d", affinity_str[left.text_position.affinity], left.text_position.offset);
								left_text_offset = 0.f;
							}
						}
					}
				}
			}

			// Draw through lines
			for (int32_t i = 0; i < decorations_count; i++) {
				const skb_decoration_t* decoration = &decorations[i];
				const skb_attribute_span_t* span = &attrib_spans[decoration->attribute_span_idx];
				const skb_attribute_decoration_t attr_decoration = span->attributes[decoration->attribute_idx].decoration;
				if (attr_decoration.position == SKB_DECORATION_THROUGHLINE) {
					render_draw_decoration(ctx->rc,
						decoration->offset_x, decoration->offset_y, decoration->length, decoration->pattern_offset, decoration->thickness,
						attr_decoration.style, attr_decoration.position, attr_decoration.color, SKB_RASTERIZE_ALPHA_SDF);
				}
			}

		}

		// Caret & selection info
		{
			float cx = ox;

			// Caret
			cx = debug_render_text(ctx->rc, cx + 5,oy + layout_height + 30, 13, RENDER_ALIGN_START, caret_color_dark, "Caret: %s%d",  affinity_str[edit_selection.end_pos.affinity], edit_selection.end_pos.offset);
			cx = ox + ceilf((cx-ox+10.f)/40.f) * 40.f;

			// Caret location
			int32_t insert_idx = skb_editor_get_text_offset_at(ctx->editor, edit_selection.end_pos);
			skb_text_position_t insert_pos = {
				.offset = insert_idx,
				.affinity = SKB_AFFINITY_TRAILING,
			};
			int32_t line_idx = skb_editor_get_line_index_at(ctx->editor, insert_pos);
			int32_t col_idx = skb_editor_get_column_index_at(ctx->editor, insert_pos);

			cx = debug_render_text(ctx->rc, cx,oy + layout_height + 30, 13, RENDER_ALIGN_START, log_color, "Ln %d, Col %d", line_idx+1, col_idx+1);
			cx = ox + ceilf((cx-ox+10.f)/40.f) * 40.f;

			// Selection count
			const int32_t selection_count = skb_editor_get_selection_count(ctx->editor, edit_selection);
			if (selection_count > 0) {
				cx = debug_render_text(ctx->rc, cx,oy + layout_height + 30, 13, RENDER_ALIGN_START, ink_color, "Selection %d - %d, (%d chars)", edit_selection.start_pos.offset, edit_selection.end_pos.offset, selection_count);
				cx = ox + ceilf((cx-ox+10.f)/40.f) * 40.f;
			}

			cx = debug_render_text(ctx->rc, cx,oy + layout_height + 30, 13, RENDER_ALIGN_START, ink_color, "text_offset %d", edit_selection.end_pos.offset);
			cx = ox + ceilf((cx-ox+10.f)/40.f) * 40.f;
		}

		// Caret is generally drawn only when there is no selection.
		if (skb_editor_get_selection_count(ctx->editor, edit_selection) == 0) {

			// Visual caret
			skb_visual_caret_t caret_pos = skb_editor_get_visual_caret(ctx->editor, edit_selection.end_pos);

			float caret_slope = caret_pos.width / caret_pos.height;
			float caret_top_x = ox + caret_pos.x + caret_pos.width - caret_slope * 3.f;
			float caret_top_y = oy + caret_pos.y + 3.f;
			float caret_bot_x = ox + caret_pos.x + caret_slope * 3.f;
			float caret_bot_y = oy + caret_pos.y + caret_pos.height - 3.f;

			debug_render_line(ctx->rc, caret_top_x, caret_top_y, caret_bot_x, caret_bot_y, caret_color, 6.f);

			float as = skb_absf(caret_pos.height) / 10.f;
			float dx = skb_is_rtl(caret_pos.direction) ? -as : as;
			float tri_top_x = ox + caret_pos.x + caret_pos.width;
			float tri_top_y = oy + caret_pos.y;
			float tri_bot_x = tri_top_x - as * caret_slope;
			float tri_bot_y = tri_top_y + as;
			debug_render_tri(ctx->rc, tri_top_x, tri_top_y,
				tri_top_x + dx, tri_top_y,
				tri_bot_x, tri_bot_y,
				caret_color);

			// Caret affinity text
			float dir = (edit_selection.end_pos.affinity == SKB_AFFINITY_LEADING || edit_selection.end_pos.affinity == SKB_AFFINITY_SOL) ? -1.f : 1.f;
			bool caret_is_rtl = skb_editor_get_text_direction_at(ctx->editor, edit_selection.end_pos);
			if (caret_is_rtl) dir = -dir;
			debug_render_text(ctx->rc, caret_bot_x + dir*7.f + caret_slope * 23, caret_bot_y - 23, 11, dir > 0.f ? RENDER_ALIGN_START : RENDER_ALIGN_END, caret_color, affinity_str[edit_selection.end_pos.affinity]);
		}
	}

	// Draw logical string info
	{
		const skb_editor_params_t* edit_params = skb_editor_get_params(ctx->editor);
		const skb_attribute_font_t attr_font = get_font_attribute_from_editor_params(edit_params);

		float ox = 0.f;//ctx->view.cx;
		float oy = /*ctx->view.cy +*/ 30.f + layout_height + 80.f;
		float sz = 80.f;
		float font_scale = (sz * 0.5f) / attr_font.size;

		bool prev_is_emoji = false;
		uint8_t prev_script = 0;
		skb_font_handle_t font_handle = 0;

		int32_t caret_insert_idx = skb_editor_get_text_offset_at(ctx->editor, edit_selection.end_pos);

		int caret_selection_start_idx = -1;
		int caret_selection_end_idx = -1;
		if (skb_editor_get_selection_count(ctx->editor, edit_selection) > 0) {
			int caret_start_idx = skb_editor_get_text_offset_at(ctx->editor, edit_selection.start_pos);
			caret_selection_start_idx = skb_mini(caret_start_idx, caret_insert_idx);
			caret_selection_end_idx = skb_maxi(caret_start_idx, caret_insert_idx);
		}

		const int32_t edit_text_count = skb_editor_get_text_utf32(ctx->editor, NULL, 0);
		const int32_t edit_layout_count = skb_editor_get_paragraph_count(ctx->editor);

		for (int32_t pi = 0; pi < edit_layout_count; pi++) {
			const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(ctx->editor, pi);
			const int32_t edit_text_offset = skb_editor_get_paragraph_text_offset(ctx->editor, pi);
			const bool is_last_edit_line = pi == edit_layout_count - 1;

			const skb_layout_line_t* lines = skb_layout_get_lines(edit_layout);
			const int32_t lines_count = skb_layout_get_lines_count(edit_layout);
			const uint32_t* text = skb_layout_get_text(edit_layout);
			const skb_text_property_t* text_props = skb_layout_get_text_properties(edit_layout);

			for (int line_idx = 0; line_idx < lines_count; line_idx++) {
				const skb_layout_line_t* line = &lines[line_idx];
				const bool is_last_layout_line = line_idx == lines_count - 1;

				ox = 0.f;
				for (int32_t cp_idx = line->text_range.start; cp_idx < line->text_range.end; cp_idx++) {
					const uint32_t cp = text[cp_idx];

					// Selection
					if ((edit_text_offset + cp_idx) >= caret_selection_start_idx && (cp_idx + edit_text_offset) < caret_selection_end_idx)
						debug_render_filled_rect(ctx->rc, ox-1.f,oy-1.f,sz+2.f,sz+2.f,sel_color);

					// Glyph box
					debug_render_stroked_rect(ctx->rc,  ox+0.5f, oy+0.5f, sz, sz, log_color, -1.f);

					// Caret insert position
					if ((edit_text_offset + cp_idx) == caret_insert_idx) {
						debug_render_filled_rect(ctx->rc, ox+1.5f, oy+1.5f, sz-2, sz-2, caret_color_trans);
					}
					// Caret position
					if ((edit_text_offset + cp_idx) == edit_selection.end_pos.offset) {
						float cx = ox + 6.f;
						float dir = 1.f;
						if (edit_selection.end_pos.affinity == SKB_AFFINITY_EOL || edit_selection.end_pos.affinity == SKB_AFFINITY_LEADING) {
							cx += sz - 12.f;
							dir = -1.f;
						}

						debug_render_line(ctx->rc, cx,oy+6.f,cx,oy+sz-5.f,caret_color, 4.f);

						// Direction triangle
						bool caret_is_rtl = skb_editor_get_text_direction_at(ctx->editor, edit_selection.end_pos);
						float as = sz / 8.f;
						float dx = (caret_is_rtl ? -as : as);
						debug_render_tri(ctx->rc, cx, oy+4,
							cx + dx, oy+4,
							cx, oy+3+as,
							caret_color);

						debug_render_text(ctx->rc, cx + dir*5.f, oy+sz-7+0.5f, 11, dir > 0.f ? RENDER_ALIGN_START : RENDER_ALIGN_END, caret_color, affinity_str[edit_selection.end_pos.affinity]);
					}

					const uint8_t script = text_props[cp_idx].script;
					const bool is_emoji = (text_props[cp_idx].flags & SKB_TEXT_PROP_EMOJI);
					const uint8_t font_family = is_emoji ? SKB_FONT_FAMILY_EMOJI : attr_font.family;
					if (!font_handle || script != prev_script || is_emoji != prev_is_emoji) {
						if (skb_font_collection_match_fonts(ctx->font_collection, "", script, font_family, attr_font.weight, attr_font.style, attr_font.stretch, &font_handle, 1) == 0)
							font_handle = 0;
						prev_script = script;
						prev_is_emoji = is_emoji;
					}

					// Logical index
					debug_render_text(ctx->rc, ox+0.5f, oy-8+0.5f, 11, RENDER_ALIGN_START, log_color, "L%d", edit_text_offset + cp_idx);

					// Codepoint
					debug_render_text(ctx->rc, ox+4+0.5f, oy+14+0.5f,11,0, ink_color, "0x%X", cp);

					if (font_handle) {
						uint32_t gid = 0;
						hb_font_get_nominal_glyph(skb_font_get_hb_font(ctx->font_collection, font_handle), cp, &gid);

						// Draw glyph centered on the rect.
						skb_rect2_t bounds = skb_font_get_glyph_bounds(ctx->font_collection, font_handle, gid, attr_font.size * font_scale);

						float base_line = oy + sz * 0.75f;
						debug_render_line(ctx->rc, ox+4+0.5f, base_line+0.5f, ox + sz - 4+0.5f, base_line+0.5f, log_color, -1.f);

						float gx = ox + sz * 0.5f - bounds.width * 0.5f+0.5f;
						float gy = base_line+0.5f;

						render_draw_glyph(ctx->rc, gx, gy,
							ctx->font_collection, font_handle, gid, attr_font.size * font_scale,
							ink_color, SKB_RASTERIZE_ALPHA_MASK);

					} else {
						debug_render_text(ctx->rc, ox+10+0.5f, oy+sz*0.5f+0.5f, 13, RENDER_ALIGN_START, ink_color_trans, "<Empty>");
					}

					// Draw properties
					float lx = ox+4;
					float ly = oy + sz + 15;
					float rx = ox + sz-4;
					float ry = oy + sz + 15;

					if (text_props[cp_idx].flags & SKB_TEXT_PROP_GRAPHEME_BREAK) {
						debug_render_text(ctx->rc, rx-1.5f, ry+0.5f, 11, RENDER_ALIGN_END, caret_color, "GB");
						ry += 13.f;
					}

					if (text_props[cp_idx].flags & SKB_TEXT_PROP_WORD_BREAK) {
						debug_render_text(ctx->rc, rx-1.5f, ry+0.5f, 11,RENDER_ALIGN_END, ink_color_trans, "WB");
						ry += 13.f;
					}
					if (text_props[cp_idx].flags & SKB_TEXT_PROP_MUST_LINE_BREAK) {
						debug_render_text(ctx->rc, rx-1.5f, ry+0.5f, 11,RENDER_ALIGN_END, log_color, "LB!");
						ry += 13.f;
					}
					if (text_props[cp_idx].flags & SKB_TEXT_PROP_ALLOW_LINE_BREAK) {
						debug_render_text(ctx->rc, rx-1.5f, ry+0.5f, 11, RENDER_ALIGN_END, log_color, "LB?");
						ry += 13.f;
					}

					// Script
					debug_render_text(ctx->rc, lx+1.5f, ly+0.5f, 11, RENDER_ALIGN_START, log_color, "%c%c%c%c %s", SKB_UNTAG(skb_script_to_iso15924_tag(script)), (text_props[cp_idx].flags & SKB_TEXT_PROP_EMOJI) ? ":)" : "");
					ly += 13.f;
					// Direction
					debug_render_text(ctx->rc, lx+1.5f, ly+0.5f, 11, RENDER_ALIGN_START, log_color, skb_is_rtl(text_props[cp_idx].direction) ? "<R" : "L>");
					ly += 13.f;

					// Next block
					ox += sz + 4.f;
				}

				if (is_last_edit_line && is_last_layout_line) {
					// Caret at EOS
					if ((edit_text_offset + line->last_grapheme_offset) == edit_text_count) {
						debug_render_filled_rect(ctx->rc, ox+1.5f,oy+1.5f,sz-2,sz-2,caret_color_trans);
					}
				}

				// next line
				oy += sz * 2.f;
			}
		}
	}

	render_pop_transform(ctx->rc);

	// Draw atlas
	render_update_atlas(ctx->rc);
	debug_render_atlas_overlay(ctx->rc, 20.f, 50.f, ctx->atlas_scale, 1);

	// Draw info
	debug_render_text(ctx->rc, (float)view_width - 20.f, (float)view_height - 15.f, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,255),
		"F7: Baseline details %s   F8: Caret details %s   F9: Glyph details %s   F10: Atlas %.1f%%",
		ctx->show_baseline_details ? "ON" : "OFF",
		ctx->show_caret_details ? "ON" : "OFF",
		ctx->show_glyph_details ? "ON" : "OFF",
		ctx->atlas_scale * 100.f);
}
