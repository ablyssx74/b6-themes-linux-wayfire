/*
 * Lays out and paints the frame of a window from an xfwm4 theme: borders, title tab with its buttons, the bar beside it. The rules
 * are those of the Haiku port of the same themes (XfwmDecorator) and of the KWin plugin, for a window with one tab. Cairo and
 * Pango only.
 */
#ifndef B6_FRAME_RENDERER_H
#define B6_FRAME_RENDERER_H

#include "XfwmTheme.h"

#include <cairo.h>

#include <string>

namespace B6 {

struct Size {
	int w = 0, h = 0;
};

struct Margins {
	int left = 0, top = 0, right = 0, bottom = 0;
};

struct FrameParams {
	Size clientSize;
	bool active = true;
	std::string title;
	std::string font = "Sans 10";	// a Pango font description
	bool maximized = false;			// the frame is the bar only: no sides, no bottom
	bool has[ButtonCount] = {true, true, true, false, true, false};		// close, maximize, hide, shade, menu, stick
	bool toggled[ButtonCount] = {};	// maximized, shaded, on all desktops
	int hovered = -1;				// a Button, or -1
	int pressed = -1;
};

struct FrameLayout {
	// Everything but size, borders and titleBar is in the theme's own coordinates, where the pictures have see-through margins; the
	// decoration is the part of that which is not see-through, starting at origin. toDecoration() converts a rectangle.
	Size size;				// of the whole decoration
	Size themeSize;			// of the frame with the margins
	Margins borders;		// around the client, in the decoration
	int originX = 0, originY = 0;
	Rect titleBar;			// in the decoration: the bar, where the pointer moves the window
	Rect client;
	Rect tab;				// x and width of the tab, the height of the bar
	Rect text;				// where the title is drawn
	Rect buttons[ButtonCount];
	int topRightX = 0, restStart = 0, fillLeft = 0, fillWidth = 0;
	Rect toDecoration(const Rect& r) const { return r.translated(-originX, -originY); }
};

FrameLayout computeLayout(const XfwmTheme& theme, const FrameParams& params);
// draws into cr, which has the decoration's top left corner at 0,0
void paintFrame(cairo_t* cr, const XfwmTheme& theme, const FrameParams& params, const FrameLayout& layout);

}	// namespace B6

#endif
