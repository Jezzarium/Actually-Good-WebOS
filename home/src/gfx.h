#pragma once

// Drawing primitives on GLES2: every shape is a rounded rectangle drawn by one
// shader, filled with a gradient, a texture, frosted glass or a soft shadow.

#include <SDL_opengles2.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <string>

struct Color {
    float r, g, b, a;
};

// 0xRRGGBB plus alpha.
inline Color rgb(uint32_t hex, float a = 1) {
    return {((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, a};
}

struct Rect {
    float x, y, w, h;

    // Scaled about its centre.
    Rect scaled(float s) const {
        return {x + w * (1 - s) / 2, y + h * (1 - s) / 2, w * s, h * s};
    }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
};

// pixels: RGBA, 8 bits per channel, rows top first.
Texture upload_rgba(const void *pixels, int w, int h);
void destroy(Texture &t);

// White text, to be tinted when drawn.
Texture render_text(TTF_Font *font, const std::string &text);

class Painter {
public:
    bool init(int screen_w, int screen_h);

    // The blurred wallpaper that glass shows through.
    void set_backdrop(const Texture &blurred) { backdrop_ = blurred; }
    // Rotates everything drawn after it about each shape's centre, until reset to 0.
    void set_rotation(float radians);

    void fill(Rect r, float radius, Color top, Color bottom, float opacity = 1);
    // A soft white highlight centred at (sx, sy), in 0..1 of the rect, drawn
    // over whatever is already there (tile and icon alike).
    void shine(Rect r, float radius, float sx, float sy, float strength);
    void image(Rect r, float radius, const Texture &t, Color tint = {1, 1, 1, 1}, float opacity = 1);
    // Text texture at its natural size with its top-left at (x, y).
    void text(float x, float y, const Texture &t, Color color, float opacity = 1);
    // Frosted glass: the blurred wallpaper behind r, mixed toward tint by tint.a.
    void glass(Rect r, float radius, Color tint, float opacity = 1);
    // Soft shadow of the rounded rect r, spread over blur pixels on each side.
    void shadow(Rect r, float radius, float blur, Color color, float opacity = 1);

private:
    enum Mode { kFill = 0, kTexture = 1, kGlass = 2, kShadow = 3, kShine = 4 };
    void draw(Mode mode, Rect quad, Rect shape, float radius, Color c0, Color c1, GLuint texture,
              float opacity, float blur = 0, float sx = 0, float sy = 0, float shine = 0);

    GLuint program_ = 0;
    GLint u_quad_, u_rect_, u_screen_, u_radius_, u_mode_, u_c0_, u_c1_, u_tex_, u_opacity_,
        u_blur_, u_shine_, u_angle_;
    float angle_ = 0;
    Texture backdrop_;
};
