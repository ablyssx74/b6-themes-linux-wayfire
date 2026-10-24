/*
 * An xfwm4 theme: the pictures of a window frame (title bar, corners, borders, buttons) and the few values of its themerc that
 * matter to a decoration. Cairo only, no Wayfire, so the preview tool draws exactly what the plugin draws.
 *
 * (The same rules, drawn with Qt, are in the KWin plugin of b6-themes-linux-kde.)
 */
#ifndef B6_XFWM_THEME_H
#define B6_XFWM_THEME_H

#include <cairo.h>

#include <memory>
#include <cstdint>
#include <string>

namespace B6 {

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;
	bool empty() const { return w <= 0 || h <= 0; }
	int right() const { return x + w - 1; }
	int bottom() const { return y + h - 1; }
	Rect translated(int dx, int dy) const { return {x + dx, y + dy, w, h}; }
	Rect united(const Rect& other) const
	{
		if (empty()) return other;
		if (other.empty()) return *this;
		const int left = x < other.x ? x : other.x, top = y < other.y ? y : other.y;
		const int r = right() > other.right() ? right() : other.right(), b = bottom() > other.bottom() ? bottom() : other.bottom();
		return {left, top, r - left + 1, b - top + 1};
	}
};

// the letters of a themerc button_layout: C close, M maximize, H hide (minimize), S shade, O window menu, T stick (on all desktops)
enum Button { Close = 0, Maximize, Hide, Shade, Menu, Stick, ButtonCount };
enum ButtonState { Active = 0, Inactive, Prelight, PrelightInactive, Pressed, StateCount };

// one picture of the frame; "None" pixels of an XPM are transparent
struct Piece {
	std::shared_ptr<cairo_surface_t> surface;	// ARGB32, premultiplied
	Rect opaque;								// bounding box of the pixels that are not transparent

	bool isNull() const { return !surface; }
	int width() const { return surface ? cairo_image_surface_get_width(surface.get()) : 0; }
	int height() const { return surface ? cairo_image_surface_get_height(surface.get()) : 0; }
};

class XfwmTheme {
public:
	// The window color of the user's color scheme as 0xRRGGBB, or -1 for the theme as drawn. When it is dark, the colors the pictures
	// name (xfwm4's active_color_2, active_hilight_2 and so on) take their shade from it.
	void setWindowColor(int rgb) { fWindow = rgb; }
	bool load(const std::string& directory);
	bool isValid() const { return fValid; }
	const std::string& name() const { return fName; }

	// the frame, for the active (0) or the inactive (1) window
	const Piece& topLeft(bool active) const { return fTopLeft[active ? 0 : 1]; }
	const Piece& topRight(bool active) const { return fTopRight[active ? 0 : 1]; }
	// title-1 ... title-5: left end, left tile, the tab's tile under the text, right end, the bar after the tab
	const Piece& title(int index, bool active) const { return fTitle[index][active ? 0 : 1]; }
	const Piece& left(bool active) const { return fLeft[active ? 0 : 1]; }
	const Piece& right(bool active) const { return fRight[active ? 0 : 1]; }
	const Piece& bottom(bool active) const { return fBottom[active ? 0 : 1]; }
	const Piece& bottomLeft(bool active) const { return fBottomLeft[active ? 0 : 1]; }
	const Piece& bottomRight(bool active) const { return fBottomRight[active ? 0 : 1]; }
	// a button's picture; the toggled one is for a window that is maximized, shaded or on all desktops (xfwm4's "-toggled" files)
	const Piece& button(Button button, ButtonState state, bool toggled = false) const
	{
		return toggled && !fButtonToggled[button][state].isNull() ? fButtonToggled[button][state] : fButton[button][state];
	}

	int titleHeight() const { return fTitleHeight; }
	int buttonWidth() const { return fButtonWidth; }

	// themerc: colours are r, g, b in 0..1
	struct Color { double r = 0, g = 0, b = 0; };
	Color textColor(bool active) const { return active ? fActiveText : fInactiveText; }
	int titleOffset(bool active) const { return active ? fOffsetActive : fOffsetInactive; }
	int titleHorizontalOffset() const { return fTitleOffsetX; }
	int alignment() const { return fAlignment; }	// 0 left, 1 center, 2 right
	bool fullWidthTitle() const { return fFullWidth; }
	const std::string& buttonLayout() const { return fButtonLayout; }
	bool buttonsInTab() const { return fButtonsInTab; }
	int buttonOffset() const { return fButtonOffset; }
	int buttonOffsetRight() const { return fHasOffsetRight ? fButtonOffsetRight : fButtonOffset; }
	int buttonSpacing() const { return fButtonSpacing; }

private:
	bool loadPiece(Piece& piece, const std::string& baseName) const;
	void readThemerc(const std::string& path);

	int fWindow = -1;
	uint32_t fReference = 0;	// the theme's own color_2, what the window color stands for
	bool fValid = false;
	std::string fDirectory, fName;
	Piece fTopLeft[2], fTopRight[2], fTitle[5][2], fLeft[2], fRight[2], fBottom[2], fBottomLeft[2], fBottomRight[2];
	Piece fButton[ButtonCount][StateCount], fButtonToggled[ButtonCount][StateCount];

	int fTitleHeight = 24, fButtonWidth = 12;
	Color fActiveText, fInactiveText;
	int fOffsetActive = 0, fOffsetInactive = 0, fTitleOffsetX = 0;
	int fAlignment = 1;
	bool fFullWidth = true;
	std::string fButtonLayout = "O|HMC";
	bool fButtonsInTab = false;
	int fButtonOffset = 0, fButtonOffsetRight = 0, fButtonSpacing = 0;
	bool fHasOffsetRight = false;
};

}	// namespace B6

#endif
