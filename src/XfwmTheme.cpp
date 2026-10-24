#include "XfwmTheme.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace B6 {

namespace {

bool fileExists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

Rect opaqueBounds(cairo_surface_t* surface)
{
	const int width = cairo_image_surface_get_width(surface), height = cairo_image_surface_get_height(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	const unsigned char* data = cairo_image_surface_get_data(surface);
	int left = width, top = height, right = -1, bottom = -1;
	for (int y = 0; y < height; y++) {
		const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
		for (int x = 0; x < width; x++) {
			if ((row[x] >> 24) == 0)
				continue;
			if (x < left) left = x;
			if (x > right) right = x;
			if (y < top) top = y;
			if (y > bottom) bottom = y;
		}
	}
	return right < 0 ? Rect{} : Rect{left, top, right - left + 1, bottom - top + 1};
}

std::shared_ptr<cairo_surface_t> own(cairo_surface_t* surface)
{
	return std::shared_ptr<cairo_surface_t>(surface, cairo_surface_destroy);
}

// The quoted strings of an XPM file, in order.
std::vector<std::string> xpmStrings(const std::string& path)
{
	std::ifstream file(path);
	std::stringstream buffer;
	buffer << file.rdbuf();
	const std::string text = buffer.str();
	std::vector<std::string> strings;
	size_t i = text.find('{');
	if (i == std::string::npos) return strings;
	while (i < text.size()) {
		if (text[i] == '"') {
			std::string s;
			for (i++; i < text.size() && text[i] != '"'; i++) {
				if (text[i] == '\\' && i + 1 < text.size()) i++;
				s += text[i];
			}
			strings.push_back(s);
		}
		i++;
	}
	return strings;
}

// "#RRGGBB" (or 12 bit per channel, "#RRRRGGGGBBBB") to a pixel; "None" is transparent
bool parseColor(const std::string& value, uint32_t& argb)
{
	if (value.empty()) return false;
	if (strcasecmp(value.c_str(), "none") == 0) {
		argb = 0;
		return true;
	}
	if (value[0] != '#') return false;
	const std::string hex = value.substr(1);
	unsigned r, g, b;
	if (hex.size() == 6 && sscanf(hex.c_str(), "%2x%2x%2x", &r, &g, &b) == 3) {
	} else if (hex.size() == 12 && sscanf(hex.c_str(), "%4x%4x%4x", &r, &g, &b) == 3) {
		r >>= 8; g >>= 8; b >>= 8;
	} else
		return false;
	argb = 0xff000000u | (r << 16) | (g << 8) | b;
	return true;
}

bool darkWindow(int rgb)
{
	if (rgb < 0) return false;
	const int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
	return (r * 299 + g * 587 + b * 114) / 1000 < 128;
}

// A color of the window scheme: the shade keeps its distance from the theme's own window color, scaled to the user's.
uint32_t shadeOf(uint32_t original, uint32_t reference, uint32_t base)
{
	auto channel = [](int o, int r, int b) {
		if (o <= r) return r > 0 ? b * o / r : b;
		return b + (255 - b) * (o - r) / (255 - r);
	};
	uint32_t out = 0xff000000u;
	for (int shift = 16; shift >= 0; shift -= 8)
		out |= (uint32_t)channel((original >> shift) & 0xff, (reference >> shift) & 0xff, (base >> shift) & 0xff) << shift;
	return out;
}

// the color of the theme's "s active_color_2", or 0
uint32_t findReference(const std::string& directory)
{
	for (const char* name : {"left-active.xpm", "bottom-active.xpm", "right-active.xpm"}) {
		for (const std::string& line : xpmStrings(directory + "/" + name)) {
			const size_t at = line.find(" s active_color_2");
			const size_t hash = line.find("c #");
			uint32_t argb = 0;
			if (at != std::string::npos && hash != std::string::npos && parseColor(line.substr(hash + 2, 7), argb))
				return argb;
		}
	}
	return 0;
}

cairo_surface_t* loadXpm(const std::string& path, int windowColor = -1, uint32_t reference = 0)
{
	const std::vector<std::string> strings = xpmStrings(path);
	if (strings.empty()) return nullptr;
	int width = 0, height = 0, colors = 0, cpp = 0;
	if (sscanf(strings[0].c_str(), "%d %d %d %d", &width, &height, &colors, &cpp) != 4 || width <= 0 || height <= 0 || cpp < 1 || cpp > 4)
		return nullptr;
	if ((int)strings.size() < 1 + colors + height) return nullptr;

	std::map<std::string, uint32_t> palette;
	for (int i = 0; i < colors; i++) {
		const std::string& line = strings[1 + i];
		if ((int)line.size() < cpp) return nullptr;
		const std::string key = line.substr(0, cpp);
		// "key c #RRGGBB s symbolic_name": the colour is the word after the 'c' key (the symbolic names are for the toolkit's colours)
		std::istringstream words(line.substr(cpp));
		std::string word, color, symbol;
		while (words >> word) {
			if (word == "c") {
				words >> color;
				continue;
			}
			if (word == "s") {
				words >> symbol;
				continue;
			}
			if (word == "m" || word == "g" || word == "g4") words >> word;
		}
		uint32_t argb = 0;
		if (!parseColor(color, argb)) argb = 0;
		if (argb && darkWindow(windowColor) && reference &&
			(symbol.find("_color_") != std::string::npos || symbol.find("_hilight_") != std::string::npos ||
				symbol.find("_mid_") != std::string::npos || symbol.find("_shadow_") != std::string::npos))
			argb = shadeOf(argb, reference, 0xff000000u | (uint32_t)windowColor);
		palette[key] = argb;
	}

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return nullptr;
	cairo_surface_flush(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	unsigned char* data = cairo_image_surface_get_data(surface);
	for (int y = 0; y < height; y++) {
		const std::string& row = strings[1 + colors + y];
		uint32_t* out = reinterpret_cast<uint32_t*>(data + y * stride);
		for (int x = 0; x < width; x++) {
			const size_t at = (size_t)x * cpp;
			out[x] = at + cpp <= row.size() ? palette[row.substr(at, cpp)] : 0;
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}

std::string trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && isspace((unsigned char)s[a])) a++;
	while (b > a && isspace((unsigned char)s[b - 1])) b--;
	return s.substr(a, b - a);
}

}	// namespace

bool XfwmTheme::loadPiece(Piece& piece, const std::string& baseName) const
{
	// xfwm4 takes a picture in either format; the PNG, when there is one, is the newer
	for (const char* extension : {".png", ".xpm"}) {
		const std::string path = fDirectory + "/" + baseName + extension;
		if (!fileExists(path))
			continue;
		cairo_surface_t* surface = strcmp(extension, ".png") == 0 ? cairo_image_surface_create_from_png(path.c_str()) : loadXpm(path, fWindow, fReference);
		if (!surface)
			continue;
		if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) {
			cairo_surface_destroy(surface);
			continue;
		}
		piece.surface = own(surface);
		piece.opaque = opaqueBounds(surface);
		return true;
	}
	return false;
}

bool XfwmTheme::load(const std::string& directory)
{
	fValid = false;
	fDirectory = directory;
	const size_t slash = directory.find_last_of('/');
	fName = slash == std::string::npos ? directory : directory.substr(slash + 1);
	if (!fileExists(directory + "/themerc"))
		return false;
	readThemerc(directory + "/themerc");
	fReference = darkWindow(fWindow) ? findReference(directory) : 0;

	static const char* const states[2] = {"active", "inactive"};
	for (int s = 0; s < 2; s++) {
		const std::string state = states[s];
		loadPiece(fTopLeft[s], "top-left-" + state);
		loadPiece(fTopRight[s], "top-right-" + state);
		for (int i = 0; i < 5; i++)
			loadPiece(fTitle[i][s], "title-" + std::to_string(i + 1) + "-" + state);
		loadPiece(fLeft[s], "left-" + state);
		loadPiece(fRight[s], "right-" + state);
		loadPiece(fBottom[s], "bottom-" + state);
		loadPiece(fBottomLeft[s], "bottom-left-" + state);
		loadPiece(fBottomRight[s], "bottom-right-" + state);
	}

	static const char* const names[ButtonCount] = {"close", "maximize", "hide", "shade", "menu", "stick"};
	static const char* const suffixes[StateCount] = {"-active", "-inactive", "-prelight", "-prelight-inactive", "-pressed"};
	for (int b = 0; b < ButtonCount; b++) {
		for (int s = 0; s < StateCount; s++) {
			loadPiece(fButton[b][s], std::string(names[b]) + suffixes[s]);
			loadPiece(fButtonToggled[b][s], std::string(names[b]) + "-toggled" + suffixes[s]);
		}
		// a theme without a picture for a state uses the one of the plain state
		for (int s = Inactive; s < StateCount; s++) {
			if (fButton[b][s].isNull())
				fButton[b][s] = fButton[b][s == PrelightInactive ? Inactive : Active];
			if (fButtonToggled[b][s].isNull() && !fButtonToggled[b][Active].isNull())
				fButtonToggled[b][s] = fButtonToggled[b][s == PrelightInactive ? Inactive : Active];
		}
	}

	if (fTitle[2][0].isNull() || fLeft[0].isNull() || fRight[0].isNull() || fBottom[0].isNull())
		return false;
	fTitleHeight = fTitle[2][0].height();
	if (!fButton[Close][Active].isNull())
		fButtonWidth = fButton[Close][Active].width();
	fValid = true;
	return true;
}

void XfwmTheme::readThemerc(const std::string& path)
{
	std::ifstream file(path);
	std::string line;
	auto color = [](const std::string& value) {
		Color c;
		unsigned r = 0, g = 0, b = 0;
		if (value.size() == 7 && value[0] == '#' && sscanf(value.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
			c.r = r / 255.0;
			c.g = g / 255.0;
			c.b = b / 255.0;
		}
		return c;
	};
	while (std::getline(file, line)) {
		line = trim(line);
		const size_t equals = line.find('=');
		if (line.empty() || line[0] == '#' || equals == std::string::npos)
			continue;
		const std::string key = trim(line.substr(0, equals)), value = trim(line.substr(equals + 1));
		const bool truth = value == "true";
		if (key == "active_text_color") fActiveText = color(value);
		else if (key == "inactive_text_color") fInactiveText = color(value);
		else if (key == "title_vertical_offset_active") fOffsetActive = atoi(value.c_str());
		else if (key == "title_vertical_offset_inactive") fOffsetInactive = atoi(value.c_str());
		else if (key == "title_horizontal_offset") fTitleOffsetX = atoi(value.c_str());
		else if (key == "title_alignment") fAlignment = value == "left" ? 0 : (value == "right" ? 2 : 1);
		else if (key == "full_width_title") fFullWidth = truth;
		else if (key == "button_layout") fButtonLayout = value;
		else if (key == "button_offset") fButtonOffset = atoi(value.c_str());
		else if (key == "button_offset_right") { fButtonOffsetRight = atoi(value.c_str()); fHasOffsetRight = true; }
		else if (key == "button_spacing") fButtonSpacing = atoi(value.c_str());
		else if (key == "buttons_in_tab") fButtonsInTab = truth;
	}
}

}	// namespace B6
