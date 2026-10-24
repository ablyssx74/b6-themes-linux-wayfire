/*
 * Draws window frames from a theme to a PNG, without a compositor: the same layout and painting code the plugin uses.
 *   b6-preview <theme directory> <output.png> [scale]
 */
#include "FrameRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace B6;

namespace {

cairo_surface_t* drawCase(const XfwmTheme& theme, const FrameParams& params, const double desktop[3])
{
	const FrameLayout layout = computeLayout(theme, params);
	const int margin = 8;
	cairo_surface_t* sheet = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, layout.size.w + 2 * margin, layout.size.h + 2 * margin);
	cairo_t* cr = cairo_create(sheet);
	cairo_set_source_rgb(cr, desktop[0], desktop[1], desktop[2]);
	cairo_paint(cr);

	// the frame, then the window's own content over it, as the compositor does
	cairo_translate(cr, margin, margin);
	paintFrame(cr, theme, params, layout);
	const Rect client = layout.toDecoration(layout.client);
	cairo_set_source_rgb(cr, 0xf2 / 255.0, 0xf2 / 255.0, 0xf2 / 255.0);
	cairo_rectangle(cr, client.x, client.y, client.w, client.h);
	cairo_fill(cr);
	cairo_destroy(cr);
	return sheet;
}

}	// namespace

int main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <theme directory> <output.png> [scale]\n", argv[0]);
		return 2;
	}
	XfwmTheme theme;
	if (!theme.load(argv[1])) {
		fprintf(stderr, "cannot load the theme in %s\n", argv[1]);
		return 1;
	}
	const int scale = argc > 3 ? atoi(argv[3]) : 1;
	printf("theme %s: title height %d, button width %d, buttons \"%s\"%s\n", theme.name().c_str(), theme.titleHeight(), theme.buttonWidth(),
		theme.buttonLayout().c_str(), theme.buttonsInTab() ? " (in the tab)" : "");

	FrameParams base;
	base.clientSize = {420, 160};
	base.title = "Terminal";
	const double desktop[3] = {0x5a / 255.0, 0x7a / 255.0, 0x9a / 255.0};

	std::vector<cairo_surface_t*> cases;
	FrameParams p = base;
	cases.push_back(drawCase(theme, p, desktop));
	p = base; p.active = false; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.title = "A much longer window title that has to be cut off somewhere"; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.clientSize = {200, 120}; p.title = "Files — b6+r"; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.hovered = Close; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.hovered = Maximize; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.pressed = Close; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.pressed = Maximize; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.active = false; p.hovered = Close; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.title = ""; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.maximized = true; p.clientSize = {420, 120}; p.title = "Maximized window"; p.toggled[Maximize] = true; cases.push_back(drawCase(theme, p, desktop));
	p = base; p.has[Shade] = p.has[Stick] = true; p.toggled[Stick] = true; p.hovered = Hide; cases.push_back(drawCase(theme, p, desktop));

	int width = 0, height = 0;
	for (cairo_surface_t* c : cases) {
		width = std::max(width, cairo_image_surface_get_width(c));
		height += cairo_image_surface_get_height(c);
	}
	cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale);
	cairo_t* cr = cairo_create(out);
	cairo_scale(cr, scale, scale);
	cairo_set_source_rgb(cr, desktop[0], desktop[1], desktop[2]);
	cairo_paint(cr);
	int y = 0;
	for (cairo_surface_t* c : cases) {
		cairo_set_source_surface(cr, c, 0, y);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
		cairo_paint(cr);
		y += cairo_image_surface_get_height(c);
		cairo_surface_destroy(c);
	}
	cairo_destroy(cr);
	const bool ok = cairo_surface_write_to_png(out, argv[2]) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(out);
	if (!ok) {
		fprintf(stderr, "cannot write %s\n", argv[2]);
		return 1;
	}
	printf("wrote %s (%dx%d)\n", argv[2], width * scale, height * scale);
	return 0;
}
