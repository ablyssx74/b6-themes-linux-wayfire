#include "FrameRenderer.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace B6 {

namespace {

struct TextMetrics {
	int width = 0, ascent = 0, descent = 0;
};

// Pango needs a cairo context to measure with; a one pixel surface will do
TextMetrics measure(const std::string& font, const std::string& text)
{
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* cr = cairo_create(surface);
	PangoLayout* layout = pango_cairo_create_layout(cr);
	PangoFontDescription* description = pango_font_description_from_string(font.c_str());
	pango_layout_set_font_description(layout, description);
	pango_layout_set_text(layout, text.c_str(), -1);

	TextMetrics m;
	PangoRectangle ink, logical;
	pango_layout_get_pixel_extents(layout, &ink, &logical);
	m.width = logical.width;
	PangoFontMetrics* metrics = pango_context_get_metrics(pango_layout_get_context(layout), description, nullptr);
	m.ascent = (pango_font_metrics_get_ascent(metrics) + PANGO_SCALE / 2) / PANGO_SCALE;
	m.descent = (pango_font_metrics_get_descent(metrics) + PANGO_SCALE / 2) / PANGO_SCALE;
	pango_font_metrics_unref(metrics);
	pango_font_description_free(description);
	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	return m;
}

// the letters of a button layout, split at the '|': buttons to the left and to the right of it, in order
void splitLayout(const XfwmTheme& theme, const FrameParams& p, std::vector<Button>& left, std::vector<Button>& right)
{
	const std::string& layout = theme.buttonLayout();
	size_t divider = layout.find('|');
	if (divider == std::string::npos)
		divider = layout.size();
	for (size_t i = 0; i < layout.size(); i++) {
		Button b = ButtonCount;
		switch (layout[i]) {
			case 'C': b = Close; break;
			case 'M': b = Maximize; break;
			case 'H': b = Hide; break;
			case 'S': b = Shade; break;
			case 'O': b = Menu; break;
			case 'T': b = Stick; break;
			default: break;
		}
		if (b == ButtonCount || !p.has[b])
			continue;
		if (i < divider)
			left.push_back(b);
		else if (i > divider)
			right.push_back(b);
	}
}

void blit(cairo_t* cr, const Piece& piece, int x, int y)
{
	if (piece.isNull())
		return;
	cairo_set_source_surface(cr, piece.surface.get(), x, y);
	cairo_paint(cr);
}

// tiles a picture over an area, the last tile cut off at the end of the area
void blitTiled(cairo_t* cr, const Piece& piece, const Rect& area, bool horizontal)
{
	if (piece.isNull() || area.empty())
		return;
	cairo_save(cr);
	cairo_rectangle(cr, area.x, area.y, area.w, area.h);
	cairo_clip(cr);
	if (horizontal) {
		for (int x = area.x; x <= area.right(); x += piece.width())
			blit(cr, piece, x, area.y);
	} else {
		for (int y = area.y; y <= area.bottom(); y += piece.height())
			blit(cr, piece, area.x, y);
	}
	cairo_restore(cr);
}

}	// namespace

FrameLayout computeLayout(const XfwmTheme& theme, const FrameParams& params)
{
	FrameLayout layout;
	const bool active = params.active;
	// a maximized window has the bar and nothing else: no sides, no bottom
	const int leftWidth = params.maximized ? 0 : theme.left(active).width();
	const int rightWidth = params.maximized ? 0 : theme.right(active).width();
	const int bottomHeight = params.maximized ? 0 : theme.bottom(active).height();
	const int titleHeight = theme.titleHeight();

	layout.size = {leftWidth + params.clientSize.w + rightWidth, titleHeight + params.clientSize.h + bottomHeight};
	layout.client = {leftWidth, titleHeight, params.clientSize.w, params.clientSize.h};

	const int width = layout.size.w;
	layout.topRightX = params.maximized ? width : width - theme.topRight(active).width();

	std::vector<Button> leftButtons, rightButtons;
	splitLayout(theme, params, leftButtons, rightButtons);
	const int buttonWidth = theme.buttonWidth();
	const int spacing = theme.buttonSpacing();

	// the tab: its two ends and the room for the title, which gets what is left of the width
	const int cap = theme.topLeft(active).width();
	const int before = theme.title(0, active).width() + theme.title(1, active).width();
	const int after = theme.title(3, active).width();
	const int roomForButtons = theme.buttonsInTab() && !rightButtons.empty()
		? std::max(0, (int)rightButtons.size() * (buttonWidth + spacing) - 2) : 0;
	const int fixed = cap + before + after + roomForButtons;
	const int available = std::max(0, layout.topRightX);

	const int wanted = params.title.empty() ? 0 : measure(params.font, params.title).width;
	int tabWidth = fixed + wanted;
	if (theme.fullWidthTitle() && tabWidth < available)
		tabWidth = available;
	if (tabWidth > available)
		tabWidth = std::max(fixed, available);

	layout.tab = {0, 0, tabWidth, titleHeight};
	layout.fillLeft = cap + before;
	layout.fillWidth = std::max(0, tabWidth - (cap + before + after));
	layout.restStart = tabWidth;
	layout.text = {layout.fillLeft, 0, std::max(0, tabWidth - fixed), titleHeight};

	// the buttons
	if (theme.buttonsInTab()) {
		int rx = tabWidth - theme.buttonOffsetRight();
		for (int i = (int)rightButtons.size() - 1; i >= 0; i--) {
			layout.buttons[rightButtons[i]] = {rx - buttonWidth, 0, buttonWidth, titleHeight};
			rx -= buttonWidth + spacing;
		}
		int lx = cap + theme.buttonOffset();
		for (Button b : leftButtons) {
			layout.buttons[b] = {lx, 0, buttonWidth, titleHeight};
			lx += buttonWidth + spacing;
		}
	} else {
		int x = layout.client.right() - theme.buttonOffset();
		for (int i = (int)rightButtons.size() - 1; i >= 0; i--) {
			layout.buttons[rightButtons[i]] = {x - buttonWidth + 1, 0, buttonWidth, titleHeight};
			x -= buttonWidth + spacing;
		}
		x = layout.client.x + theme.buttonOffset();
		for (Button b : leftButtons) {
			layout.buttons[b] = {x, 0, buttonWidth, titleHeight};
			x += buttonWidth + spacing;
		}
		// the title keeps clear of the buttons at the ends of the bar
		int textLeft = layout.text.x, textRight = layout.text.right() + 1;
		for (Button b : leftButtons)
			textLeft = std::max(textLeft, layout.buttons[b].right() + 1);
		for (Button b : rightButtons)
			textRight = std::min(textRight, layout.buttons[b].x);
		layout.text.x = textLeft;
		layout.text.w = std::max(0, std::max(textLeft, textRight) - textLeft);
	}

	// The pictures have see-through margins (the left border of b6 is 14 wide and only its inner 5 columns are drawn). They are not
	// part of the window: everything is moved so that the visible frame starts at 0,0.
	const Size full = layout.size;
	layout.themeSize = full;
	auto outer = [](const Piece& piece, int x, int y) { return piece.isNull() ? Rect{} : piece.opaque.translated(x, y); };
	Rect visible;
	visible = visible.united(outer(theme.topLeft(active), 0, 0));
	visible = visible.united(outer(theme.title(2, active), layout.fillLeft, 0));
	if (params.maximized) {
		const Piece& rest = theme.title(4, active);
		visible = visible.united({0, rest.opaque.y, width, rest.opaque.h});
	} else {
		visible = visible.united(outer(theme.topRight(active), layout.topRightX, 0));
		visible = visible.united(outer(theme.left(active), 0, layout.client.y));
		visible = visible.united(outer(theme.right(active), layout.client.right() + 1 - theme.right(active).opaque.x, layout.client.y));
		visible = visible.united(outer(theme.bottomLeft(active), 0, full.h - theme.bottomLeft(active).height()));
		visible = visible.united(outer(theme.bottomRight(active), width - theme.bottomRight(active).width(), full.h - theme.bottomRight(active).height()));
		visible = visible.united(outer(theme.bottom(active), theme.bottomLeft(active).width(), full.h - theme.bottom(active).height()));
	}
	layout.originX = visible.x;
	layout.originY = visible.y;
	layout.size = {visible.w, visible.h};
	const Rect client = layout.toDecoration(layout.client);
	layout.borders = {client.x, client.y, layout.size.w - client.right() - 1, layout.size.h - client.bottom() - 1};
	layout.titleBar = {0, 0, layout.size.w, client.y};
	return layout;
}

void paintFrame(cairo_t* cr, const XfwmTheme& theme, const FrameParams& params, const FrameLayout& layout)
{
	const bool active = params.active;
	const Piece& left = theme.left(active);
	const Piece& right = theme.right(active);
	const Piece& bottom = theme.bottom(active);
	const Piece& bottomLeft = theme.bottomLeft(active);
	const Piece& bottomRight = theme.bottomRight(active);
	const Rect& client = layout.client;
	const int width = layout.themeSize.w;		// the frame in the theme's own coordinates
	const int height = layout.themeSize.h;

	cairo_save(cr);
	cairo_translate(cr, -layout.originX, -layout.originY);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	// the sides and the bottom, then the bottom corners over them
	if (!params.maximized) {
		blitTiled(cr, left, {0, client.y, left.width(), client.h}, false);
		blitTiled(cr, right, {client.right() + 1 - right.opaque.x, client.y, right.width(), client.h}, false);
		const int bottomY = client.bottom() + 1 - bottom.opaque.y;
		blitTiled(cr, bottom, {bottomLeft.width(), bottomY, width - bottomLeft.width() - bottomRight.width(), bottom.height()}, true);
		blit(cr, bottomLeft, 0, height - bottomLeft.height());
		blit(cr, bottomRight, width - bottomRight.width(), height - bottomRight.height());
	}

	// the bar after the tab, and its right end
	const Piece& rest = theme.title(4, active);
	if (layout.restStart < layout.topRightX)
		blitTiled(cr, rest, {layout.restStart, 0, layout.topRightX - layout.restStart, rest.height()}, true);
	if (!params.maximized)
		blit(cr, theme.topRight(active), layout.topRightX, 0);

	// the tab
	int x = 0;
	blit(cr, theme.topLeft(active), x, 0);
	x += theme.topLeft(active).width();
	blit(cr, theme.title(0, active), x, 0);
	x += theme.title(0, active).width();
	blit(cr, theme.title(1, active), x, 0);
	const Piece& middle = theme.title(2, active);
	if (layout.fillWidth > 0)
		blitTiled(cr, middle, {layout.fillLeft, 0, layout.fillWidth, middle.height()}, true);
	const Piece& end = theme.title(3, active);
	blit(cr, end, layout.tab.w - end.width(), 0);

	// the title
	if (!params.title.empty() && layout.text.w > 0) {
		PangoLayout* text = pango_cairo_create_layout(cr);
		PangoFontDescription* description = pango_font_description_from_string(params.font.c_str());
		pango_layout_set_font_description(text, description);
		pango_layout_set_text(text, params.title.c_str(), -1);
		pango_layout_set_width(text, layout.text.w * PANGO_SCALE);
		pango_layout_set_ellipsize(text, PANGO_ELLIPSIZE_END);
		pango_layout_set_single_paragraph_mode(text, TRUE);

		PangoRectangle logical;
		pango_layout_get_pixel_extents(text, nullptr, &logical);
		const int used = logical.width;
		int shift;
		if (theme.alignment() == 1)
			shift = (layout.text.w - used) / 2;
		else if (theme.alignment() == 2)
			shift = layout.text.w - used - theme.titleHorizontalOffset();
		else
			shift = theme.titleHorizontalOffset();
		shift = std::min(std::max(shift, 0), std::max(0, layout.text.w - used));

		PangoFontMetrics* metrics = pango_context_get_metrics(pango_layout_get_context(text), description, nullptr);
		const int ascent = (pango_font_metrics_get_ascent(metrics) + PANGO_SCALE / 2) / PANGO_SCALE;
		const int descent = (pango_font_metrics_get_descent(metrics) + PANGO_SCALE / 2) / PANGO_SCALE;
		pango_font_metrics_unref(metrics);
		const double textHeight = ascent + descent;
		const int baseline = (int)std::floor((layout.titleBar.h - textHeight) / 2 + ascent + theme.titleOffset(active) + 0.5);
		const int layoutBaseline = pango_layout_get_baseline(text) / PANGO_SCALE;

		const XfwmTheme::Color color = theme.textColor(active);
		cairo_set_source_rgb(cr, color.r, color.g, color.b);
		cairo_move_to(cr, layout.text.x + shift, baseline - layoutBaseline);
		pango_cairo_show_layout(cr, text);
		pango_font_description_free(description);
		g_object_unref(text);
	}

	// the buttons
	for (int b = 0; b < ButtonCount; b++) {
		if (layout.buttons[b].empty() || !params.has[b])
			continue;
		ButtonState state;
		if (params.pressed == b)
			state = Pressed;
		else if (params.hovered == b)
			state = active ? Prelight : PrelightInactive;
		else
			state = active ? Active : Inactive;
		blit(cr, theme.button(Button(b), state, params.toggled[b]), layout.buttons[b].x, layout.buttons[b].y);
	}
	cairo_restore(cr);
}

}	// namespace B6
