/*
 * Window frames from xfwm4 themes for Wayfire.
 *
 * The structure (a scene node that draws the frame and takes the pointer, a decorator object on the toplevel that sets its margins,
 * a plugin that decorates the views) is that of Wayfire's own decoration plugin (plugins/decor, MIT license, Copyright (c) 2018 Iliya
 * Bozhinov and the Wayfire contributors); the drawing and the layout of the frame are those of b6-themes (src/).
 */

#include <wayfire/core.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>

#include <linux/input-event-codes.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

#include "FrameRenderer.h"
#include "XfwmTheme.h"

// ---------------------------------------------------------------------------------------------------------------------------------
// themes

namespace
{
wf::option_wrapper_t<std::string> opt_theme{"xfwm-decoration/theme"};
wf::option_wrapper_t<std::string> opt_themes_path{"xfwm-decoration/themes_path"};
wf::option_wrapper_t<std::string> opt_font{"xfwm-decoration/font"};
wf::option_wrapper_t<std::string> opt_window_color{"xfwm-decoration/window_color"};
wf::option_wrapper_t<std::string> opt_double_click{"xfwm-decoration/double_click"};

std::vector<std::string> theme_roots()
{
    std::vector<std::string> roots;
    const std::string paths = opt_themes_path;
    std::stringstream path(paths);
    std::string item;
    while (std::getline(path, item, ':'))
    {
        if (!item.empty())
        {
            roots.push_back(item);
        }
    }

    if (const char *home = getenv("HOME"))
    {
        roots.push_back(std::string(home) + "/.local/share/b6-themes");
    }

    roots.push_back("/usr/local/share/b6-themes");
    roots.push_back("/usr/share/b6-themes");
    return roots;
}

std::shared_ptr<B6::XfwmTheme> load_theme()
{
    static std::map<std::string, std::shared_ptr<B6::XfwmTheme>> cache;
    const std::string name = opt_theme;
    const std::string color = opt_window_color;
    int window = -1;
    if ((color.size() == 7) && (color[0] == '#'))
    {
        window = (int)strtol(color.c_str() + 1, nullptr, 16);
    }

    for (const auto& root : theme_roots())
    {
        const std::string directory = root + "/" + name;
        const std::string key = directory + color;
        auto found = cache.find(key);
        if (found != cache.end())
        {
            return found->second;
        }

        auto theme = std::make_shared<B6::XfwmTheme>();
        theme->setWindowColor(window);
        if (theme->load(directory))
        {
            cache[key] = theme;
            return theme;
        }
    }

    LOGE("xfwm-decoration: no theme \"", name, "\" found (b6-themes/<name>/themerc)");
    return nullptr;
}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the frame of one window

class xfwm_decoration_node_t : public wf::scene::node_t, public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> _view;
    wf::signal::connection_t<wf::view_title_changed_signal> title_set =
        [=] (wf::view_title_changed_signal *ev)
    {
        mark_dirty();
    };

    std::shared_ptr<B6::XfwmTheme> theme;
    B6::FrameParams params;
    B6::FrameLayout layout;
    wf::owned_texture_t tex;
    bool dirty = true;

    bool grabbed = false;
    int grab_button = -1;       // a B6::Button pressed with the pointer, or -1
    bool grab_move = false;     // pressed on the title bar: moving starts when the pointer does
    std::optional<std::chrono::steady_clock::time_point> last_title_press;
    bool double_click_at_release = false;
    std::optional<wf::pointf_t> current_input;

  public:
    wf::dimensions_t size;
    wf::regionf_t cached_region;

    xfwm_decoration_node_t(wayfire_toplevel_view view, std::shared_ptr<B6::XfwmTheme> theme) :
        node_t(false), theme(theme)
    {
        this->_view = view->weak_from_this();
        view->connect(&title_set);
    }

    void set_theme(std::shared_ptr<B6::XfwmTheme> new_theme)
    {
        theme = new_theme;
        mark_dirty();
        refresh();
    }

    void mark_dirty()
    {
        dirty = true;
        wf::scene::damage_node(shared_from_this(), get_bounding_box());
    }

    wf::pointf_t get_offset()
    {
        return {(double)-layout.borders.left, (double)-layout.borders.top};
    }

    // what the frame looks like for a window in this state
    B6::FrameParams make_params(bool maximized, B6::Size client)
    {
        B6::FrameParams p;
        p.clientSize = client;
        p.maximized = maximized;
        p.font = opt_font;
        p.hovered = params.hovered;
        p.pressed = params.pressed;
        if (auto view = _view.lock())
        {
            p.active    = view->activated;
            p.title     = view->get_title();
            p.has[B6::Close]    = true;
            p.has[B6::Maximize] = true;
            p.has[B6::Hide]     = !view->parent;
            p.has[B6::Shade]    = p.has[B6::Stick] = p.has[B6::Menu] = false;
            p.toggled[B6::Maximize] = maximized;
        }

        return p;
    }

    static B6::FrameLayout probe(const B6::XfwmTheme& theme, bool maximized)
    {
        B6::FrameParams p;
        p.maximized  = maximized;
        // large: the pictures of a theme (b5+) overhang a small client and the borders would grow with them
        p.clientSize = {1024, 1024};
        return B6::computeLayout(theme, p);
    }

    // the frame is laid out again for the window's size and state
    void refresh()
    {
        auto view = _view.lock();
        if (!view || !theme)
        {
            return;
        }

        const bool maximized = !view->toplevel()->current().fullscreen &&
            (view->toplevel()->current().tiled_edges == wf::TILED_EDGES_ALL);
        view->damage();
        size = wf::dimensions(view->get_geometry());
        const auto borders = probe(*theme, maximized).borders;
        B6::Size client{std::max(1, size.width - borders.left - borders.right),
            std::max(1, size.height - borders.top - borders.bottom)};
        params = make_params(maximized, client);
        layout = B6::computeLayout(*theme, params);
        dirty  = true;

        cached_region.clear();
        if (!view->toplevel()->current().fullscreen)
        {
            auto add = [&] (const B6::Rect& r)
            {
                if (!r.empty())
                {
                    cached_region |= wf::geometry_t{(double)r.x, (double)r.y, (double)r.w, (double)r.h};
                }
            };

            // the bar (all of it when the theme has a bar beside the tab, else only the tab), and the sides
            const auto active = params.active;
            const bool bar_beside_tab = !theme->title(4, active).opaque.empty();
            add(bar_beside_tab || maximized ? B6::Rect{0, 0, layout.size.w, layout.borders.top} :
                B6::Rect{layout.toDecoration(layout.tab)});
            add({0, layout.borders.top, layout.borders.left, layout.size.h - layout.borders.top});
            add({layout.size.w - layout.borders.right, layout.borders.top, layout.borders.right,
                layout.size.h - layout.borders.top});
            add({0, layout.size.h - layout.borders.bottom, layout.size.w, layout.borders.bottom});
        }

        view->damage();
    }

    wf::decoration_margins_t margins(const wf::toplevel_state_t& state)
    {
        if (state.fullscreen || !theme)
        {
            return {0, 0, 0, 0};
        }

        const auto b = probe(*theme, state.tiled_edges == wf::TILED_EDGES_ALL).borders;
        return wf::decoration_margins_t{.left = (double)b.left, .right = (double)b.right, .bottom = (double)b.bottom,
            .top = (double)b.top};
    }

    // --- drawing ---

    void rasterize()
    {
        if (!theme || (layout.size.w <= 0) || (layout.size.h <= 0))
        {
            return;
        }

        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, layout.size.w, layout.size.h);
        cairo_t *cr = cairo_create(surface);
        B6::paintFrame(cr, *theme, params, layout);
        cairo_destroy(cr);
        tex = wf::owned_texture_t{surface};
        cairo_surface_destroy(surface);
        dirty = false;
    }

    void render(const wf::scene::render_instruction_t& data)
    {
        if (dirty)
        {
            rasterize();
        }

        if (tex.get_texture() != nullptr)
        {
            auto origin = get_offset();
            wf::geometry_t geometry{origin.x, origin.y, (double)layout.size.w, (double)layout.size.h};
            data.pass->add_texture(tex.get_texture(), data.target, geometry, data.damage);
        }
    }

    class decoration_render_instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<xfwm_decoration_node_t> self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        decoration_render_instance_t(xfwm_decoration_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = std::dynamic_pointer_cast<xfwm_decoration_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_surface_damage);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto our_region = self->get_bounding_box();
            wf::regionf_t our_damage = damage & our_region;
            if (!our_damage.empty())
            {
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = std::move(our_damage),
                });
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            self->render(data);
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output = nullptr) override
    {
        instances.push_back(std::make_unique<decoration_render_instance_t>(this, push_damage));
    }

    wf::geometry_t get_bounding_box() override
    {
        auto origin = get_offset();
        return wf::geometry_t{origin.x, origin.y, (double)layout.size.w, (double)layout.size.h};
    }

    // --- the pointer ---

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        if (auto view = _view.lock())
        {
            wf::pointf_t local = at - get_offset();
            if (cached_region.contains_pointf(local) && view->is_mapped())
            {
                return wf::scene::input_node_t{
                    .node = this,
                    .local_coords = local,
                };
            }
        }

        return {};
    }

    pointer_interaction_t& pointer_interaction() override
    {
        return *this;
    }

    touch_interaction_t& touch_interaction() override
    {
        return *this;
    }

    static bool inside(const B6::Rect& r, const wf::pointf_t& p)
    {
        return !r.empty() && (p.x >= r.x) && (p.x < r.x + r.w) && (p.y >= r.y) && (p.y < r.y + r.h);
    }

    int button_at(const wf::pointf_t& p) const
    {
        for (int b = 0; b < B6::ButtonCount; b++)
        {
            if (params.has[b] && inside(layout.toDecoration(layout.buttons[b]), p))
            {
                return b;
            }
        }

        return -1;
    }

    // the edges to resize by when the pointer is at p (decoration coordinates); 0 for the title bar and out of the frame
    uint32_t resize_edges_at(const wf::pointf_t& p) const
    {
        if (params.maximized)
        {
            return 0;
        }

        const int corner = 20;
        uint32_t edges   = 0;
        const bool in_title = p.y < layout.borders.top;
        if (p.x < layout.borders.left)
        {
            edges |= WLR_EDGE_LEFT;
        }

        if (p.x >= layout.size.w - layout.borders.right)
        {
            edges |= WLR_EDGE_RIGHT;
        }

        if (p.y >= layout.size.h - layout.borders.bottom)
        {
            edges |= WLR_EDGE_BOTTOM;
        }

        if (in_title && (p.y < 3))
        {
            edges |= WLR_EDGE_TOP;
        }

        // near the end of a side or of the bottom: the corner
        if ((edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) && !(edges & WLR_EDGE_BOTTOM) && !in_title &&
            (p.y >= layout.size.h - corner))
        {
            edges |= WLR_EDGE_BOTTOM;
        }

        if ((edges & WLR_EDGE_BOTTOM) && !(edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)))
        {
            if (p.x < corner)
            {
                edges |= WLR_EDGE_LEFT;
            } else if (p.x >= layout.size.w - corner)
            {
                edges |= WLR_EDGE_RIGHT;
            }
        }

        return edges;
    }

    void set_hover(int button)
    {
        if (params.hovered != button)
        {
            params.hovered = button;
            mark_dirty();
        }
    }

    void set_pressed(int button)
    {
        if (params.pressed != button)
        {
            params.pressed = button;
            mark_dirty();
        }
    }

    void update_cursor(const wf::pointf_t& p)
    {
        const uint32_t edges = resize_edges_at(p);
        wf::get_core().set_cursor(edges > 0 ? wlr_xcursor_get_resize_name((wlr_edges)edges) : "default");
    }

    void handle_pointer_enter(wf::pointf_t point) override
    {
        point -= get_offset();
        current_input = point;
        set_hover(button_at(point));
        update_cursor(point);
    }

    void handle_pointer_leave() override
    {
        current_input.reset();
        set_hover(-1);
        if (grabbed)
        {
            grabbed = false;
            grab_button = -1;
            grab_move   = false;
            set_pressed(-1);
        }
    }

    void handle_pointer_motion(wf::pointf_t to, uint32_t) override
    {
        to -= get_offset();
        current_input = to;
        set_hover(button_at(to));
        update_cursor(to);

        // the pointer moves on after being pressed on the title bar: the window moves with it
        if (grabbed && grab_move)
        {
            grabbed   = false;
            grab_move = false;
            double_click_at_release = false;
            if (auto view = _view.lock())
            {
                wf::get_core().default_wm->move_request(view);
            }
        }
    }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        const bool pressed = ev.state == WL_POINTER_BUTTON_STATE_PRESSED;
        const wf::pointf_t where = current_input.value_or(wf::pointf_t{-1, -1});
        auto view = _view.lock();
        if (!view)
        {
            return;
        }

        if (pressed)
        {
            const uint32_t edges = resize_edges_at(where);
            const int button     = button_at(where);
            if ((ev.button == BTN_LEFT) && (button < 0) && (edges > 0))
            {
                wf::get_core().default_wm->resize_request(view, edges);
                return;
            }

            if (button >= 0)
            {
                grabbed     = true;
                grab_button = button;
                set_pressed(button);
            } else if (inside(B6::Rect{0, 0, layout.size.w, layout.borders.top}, where))
            {
                grabbed   = true;
                grab_move = true;
                const auto now = std::chrono::steady_clock::now();
                if (last_title_press &&
                    (std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_title_press).count() < 300))
                {
                    double_click_at_release = true;
                    last_title_press.reset();
                } else
                {
                    last_title_press = now;
                }
            }

            return;
        }

        // released
        if (double_click_at_release)
        {
            double_click_at_release = false;
            grabbed   = false;
            grab_move = false;
            if (ev.button == BTN_LEFT)
            {
                title_double_click(view);
            }

            return;
        }

        if (grabbed && (grab_button >= 0))
        {
            const int pressed_button = grab_button;
            grab_button = -1;
            grabbed     = false;
            set_pressed(-1);
            if ((ev.button == BTN_LEFT) && (button_at(where) == pressed_button))
            {
                button_action(view, pressed_button);
            }
        }

        grabbed   = false;
        grab_move = false;
    }

    void toggle_maximize(std::shared_ptr<wf::toplevel_view_interface_t> view)
    {
        if (view->pending_tiled_edges() == wf::TILED_EDGES_ALL)
        {
            wf::get_core().default_wm->tile_request(view, 0);
        } else
        {
            wf::get_core().default_wm->tile_request(view, wf::TILED_EDGES_ALL);
        }
    }

    void title_double_click(std::shared_ptr<wf::toplevel_view_interface_t> view)
    {
        const std::string action = opt_double_click;
        if (action == "minimize")
        {
            if (!view->parent)
            {
                wf::get_core().default_wm->minimize_request(view, true);
            }
        } else if (action == "maximize")
        {
            toggle_maximize(view);
        }
    }

    void button_action(std::shared_ptr<wf::toplevel_view_interface_t> view, int button)
    {
        switch (button)
        {
          case B6::Close:
            view->close();
            break;

          case B6::Maximize:
            toggle_maximize(view);
            break;

          case B6::Hide:
            wf::get_core().default_wm->minimize_request(view, true);
            break;

          default:
            break;
        }
    }

    void handle_touch_down(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {}

    void handle_touch_up(uint32_t time_ms, int finger_id, wf::pointf_t lift_off_position) override
    {}

    void handle_touch_motion(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {}
};

// ---------------------------------------------------------------------------------------------------------------------------------
// the decorator: the margins of a toplevel, and its frame

class xfwm_decorator_t : public wf::custom_data_t
{
    wayfire_toplevel_view view;
    std::shared_ptr<xfwm_decoration_node_t> deco;

    wf::signal::connection_t<wf::view_activated_state_signal> on_view_activated;
    wf::signal::connection_t<wf::view_geometry_changed_signal> on_view_geometry_changed;
    wf::signal::connection_t<wf::view_fullscreen_signal> on_view_fullscreen;

  public:
    xfwm_decorator_t(wayfire_toplevel_view view, std::shared_ptr<B6::XfwmTheme> theme)
    {
        this->view = view;
        deco = std::make_shared<xfwm_decoration_node_t>(view, theme);
        deco->refresh();
        wf::scene::add_back(view->get_surface_root_node(), deco);

        view->connect(&on_view_activated);
        view->connect(&on_view_geometry_changed);
        view->connect(&on_view_fullscreen);

        on_view_activated = [this] (auto)
        {
            deco->refresh();
        };

        on_view_geometry_changed = [this] (auto)
        {
            deco->refresh();
        };

        on_view_fullscreen = [this] (auto)
        {
            deco->refresh();
        };
    }

    ~xfwm_decorator_t()
    {
        wf::scene::remove_child(deco);
    }

    void set_theme(std::shared_ptr<B6::XfwmTheme> theme)
    {
        deco->set_theme(theme);
    }

    wf::decoration_margins_t get_margins(const wf::toplevel_state_t& state)
    {
        return deco->margins(state);
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------
// the plugin

class wayfire_xfwm_decoration : public wf::plugin_interface_t
{
    wf::view_matcher_t ignore_views{"xfwm-decoration/ignore_views"};
    wf::view_matcher_t forced_views{"xfwm-decoration/forced_views"};

    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx =
        [=] (wf::txn::new_transaction_signal *ev)
    {
        for (const auto& obj : ev->tx->get_objects())
        {
            if (auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj))
            {
                if (auto deco = toplevel->get_data<xfwm_decorator_t>())
                {
                    toplevel->pending().margins = deco->get_margins(toplevel->pending());
                    continue;
                }

                if (toplevel->current().mapped || !toplevel->pending().mapped)
                {
                    continue;
                }

                auto view = wf::find_view_for_toplevel(toplevel);
                wf::dassert(view != nullptr, "Mapping a toplevel means there must be a corresponding view!");
                if (should_decorate_view(view))
                {
                    adjust_new_decorations(view);
                }
            }
        }
    };

    wf::signal::connection_t<wf::view_decoration_state_updated_signal> on_decoration_state_changed =
        [=] (wf::view_decoration_state_updated_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    // maximizing and restoring change the margins (a maximized window is the bar alone)
    wf::signal::connection_t<wf::view_tiled_signal> on_view_tiled =
        [=] (wf::view_tiled_signal *ev)
    {
        update_view_decoration(ev->view);
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            if (is_toplevel_decorated(toplevel->toplevel()))
            {
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }
    };

    std::function<void()> on_theme_changed = [=] ()
    {
        auto theme = load_theme();
        if (!theme)
        {
            return;
        }

        for (auto& view : wf::get_core().get_all_views())
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                if (auto deco = toplevel->toplevel()->get_data<xfwm_decorator_t>())
                {
                    deco->set_theme(theme);
                    wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
                }
            }
        }
    };

  public:
    void init() override
    {
        wf::get_core().connect(&on_decoration_state_changed);
        wf::get_core().tx_manager->connect(&on_new_tx);
        wf::get_core().connect(&on_view_tiled);
        opt_theme.set_callback(on_theme_changed);
        opt_font.set_callback(on_theme_changed);
        opt_window_color.set_callback(on_theme_changed);

        for (auto& view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }
    }

    void fini() override
    {
        for (auto view : wf::get_core().get_all_views())
        {
            if (auto toplevel = wf::toplevel_cast(view))
            {
                remove_decoration(toplevel);
                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }
    }

    bool should_decorate_view(wayfire_toplevel_view view)
    {
        return !ignore_views.matches(view) && (forced_views.matches(view) || view->should_be_decorated());
    }

    void adjust_new_decorations(wayfire_toplevel_view view)
    {
        auto theme = load_theme();
        if (!theme)
        {
            return;
        }

        auto toplevel = view->toplevel();
        toplevel->store_data(std::make_unique<xfwm_decorator_t>(view, theme));
        auto deco     = toplevel->get_data<xfwm_decorator_t>();
        auto& pending = toplevel->pending();
        pending.margins = deco->get_margins(pending);

        if (!pending.fullscreen && !pending.tiled_edges)
        {
            pending.geometry = wf::expand_geometry_by_margins(pending.geometry, pending.margins);
            if (view->get_output())
            {
                pending.geometry = wf::clamp(pending.geometry, view->get_output()->workarea->get_workarea());
            }
        }
    }

    void remove_decoration(wayfire_toplevel_view view)
    {
        view->toplevel()->erase_data<xfwm_decorator_t>();
        auto& pending = view->toplevel()->pending();
        if (!pending.fullscreen && !pending.tiled_edges)
        {
            pending.geometry = wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
        }

        pending.margins = {0, 0, 0, 0};
    }

    bool is_toplevel_decorated(const std::shared_ptr<wf::toplevel_t>& toplevel)
    {
        return toplevel->has_data<xfwm_decorator_t>();
    }

    void update_view_decoration(wayfire_view view)
    {
        if (auto toplevel = wf::toplevel_cast(view))
        {
            const bool wants_decoration = should_decorate_view(toplevel);
            if (wants_decoration != is_toplevel_decorated(toplevel->toplevel()))
            {
                if (wants_decoration)
                {
                    adjust_new_decorations(toplevel);
                } else
                {
                    remove_decoration(toplevel);
                }

                wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
            }
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_xfwm_decoration);
