#include "gfx.h"

#include "log.h"

namespace {

const char *kVertex = R"(
attribute vec2 a_pos;      // unit quad, 0..1
uniform vec4 u_quad;       // geometry drawn, pixels
uniform vec4 u_rect;       // the shape, pixels (larger quad for shadows)
uniform vec2 u_screen;
uniform float u_angle;     // rotation about the shape's centre, radians
varying vec2 v_px;         // position relative to the shape's top-left
varying vec2 v_screen;     // position on screen
void main() {
    vec2 p = u_quad.xy + a_pos * u_quad.zw;
    v_px = p - u_rect.xy;
    vec2 c = u_rect.xy + u_rect.zw * 0.5;
    float s = sin(u_angle), k = cos(u_angle);
    p = c + mat2(k, s, -s, k) * (p - c);
    v_screen = p;
    vec2 ndc = p / u_screen * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

// highp throughout: uniforms shared with the vertex shader must match its
// precision, and Mali enforces that.
const char *kFragment = R"(
precision highp float;
uniform vec4 u_rect;
uniform vec2 u_screen;
uniform float u_radius;
uniform int u_mode;        // 0 fill, 1 texture, 2 glass, 3 shadow, 4 shine
uniform vec4 u_c0;         // fill top / texture tint / glass tint / shadow colour
uniform vec4 u_c1;         // fill bottom
uniform sampler2D u_tex;
uniform float u_opacity;
uniform float u_blur;
uniform vec3 u_shine;      // highlight centre (0..1 of the shape) and strength
varying vec2 v_px;
varying vec2 v_screen;

float rounded_rect() {
    vec2 hs = u_rect.zw * 0.5;
    vec2 q = abs(v_px - hs) - (hs - u_radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;
}

void main() {
    float d = rounded_rect();
    if (u_mode == 3) {
        float a = 1.0 - smoothstep(-u_blur, u_blur, d);
        gl_FragColor = vec4(u_c0.rgb, u_c0.a * a * a * u_opacity);
        return;
    }
    float cover = clamp(0.5 - d, 0.0, 1.0);
    if (u_mode == 4) {
        vec2 p = (v_px / u_rect.zw - u_shine.xy) * vec2(1.0, u_rect.w / u_rect.z * 1.6);
        gl_FragColor = vec4(1.0, 1.0, 1.0, exp(-dot(p, p) * 4.0) * u_shine.z * cover * u_opacity);
        return;
    }
    vec4 c;
    if (u_mode == 0) {
        c = mix(u_c0, u_c1, clamp(v_px.y / u_rect.w, 0.0, 1.0));
    } else if (u_mode == 1) {
        c = texture2D(u_tex, v_px / u_rect.zw) * u_c0;
    } else {
        vec3 behind = texture2D(u_tex, v_screen / u_screen).rgb;
        c = vec4(mix(behind, u_c0.rgb, u_c0.a), 1.0);
        c.rgb += smoothstep(-2.5, 0.0, d) * 0.10;  // light catching the rim
    }
    gl_FragColor = vec4(c.rgb, c.a * cover * u_opacity);
}
)";

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        LOG("shader: %s", log);
    }
    return s;
}

}  // namespace

Texture upload_rgba(const void *pixels, int w, int h) {
    Texture t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return t;
}

void destroy(Texture &t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t = {};
}

Texture render_text(TTF_Font *font, const std::string &text) {
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text.c_str(), SDL_Color{255, 255, 255, 255});
    if (!s) return {};
    // ABGR8888 is R, G, B, A in memory on little-endian: what GL_RGBA wants.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(s);
    if (!rgba) return {};
    Texture t;
    if (rgba->pitch == rgba->w * 4) {
        t = upload_rgba(rgba->pixels, rgba->w, rgba->h);
    }
    SDL_FreeSurface(rgba);
    return t;
}

bool Painter::init(int screen_w, int screen_h) {
    program_ = glCreateProgram();
    glAttachShader(program_, compile(GL_VERTEX_SHADER, kVertex));
    glAttachShader(program_, compile(GL_FRAGMENT_SHADER, kFragment));
    glBindAttribLocation(program_, 0, "a_pos");
    glLinkProgram(program_);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof log, nullptr, log);
        LOG("link: %s", log);
        return false;
    }
    u_quad_ = glGetUniformLocation(program_, "u_quad");
    u_rect_ = glGetUniformLocation(program_, "u_rect");
    u_screen_ = glGetUniformLocation(program_, "u_screen");
    u_radius_ = glGetUniformLocation(program_, "u_radius");
    u_mode_ = glGetUniformLocation(program_, "u_mode");
    u_c0_ = glGetUniformLocation(program_, "u_c0");
    u_c1_ = glGetUniformLocation(program_, "u_c1");
    u_tex_ = glGetUniformLocation(program_, "u_tex");
    u_opacity_ = glGetUniformLocation(program_, "u_opacity");
    u_blur_ = glGetUniformLocation(program_, "u_blur");
    u_shine_ = glGetUniformLocation(program_, "u_shine");
    u_angle_ = glGetUniformLocation(program_, "u_angle");

    static const float verts[] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glUseProgram(program_);
    glUniform2f(u_screen_, float(screen_w), float(screen_h));
    glUniform1i(u_tex_, 0);
    glViewport(0, 0, screen_w, screen_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void Painter::draw(Mode mode, Rect quad, Rect shape, float radius, Color c0, Color c1,
                   GLuint texture, float opacity, float blur, float sx, float sy, float shine) {
    if (opacity <= 0.001f) return;
    glUniform4f(u_quad_, quad.x, quad.y, quad.w, quad.h);
    glUniform4f(u_rect_, shape.x, shape.y, shape.w, shape.h);
    glUniform1f(u_radius_, radius);
    glUniform1i(u_mode_, mode);
    glUniform4f(u_c0_, c0.r, c0.g, c0.b, c0.a);
    glUniform4f(u_c1_, c1.r, c1.g, c1.b, c1.a);
    glUniform1f(u_opacity_, opacity);
    glUniform1f(u_blur_, blur);
    glUniform3f(u_shine_, sx, sy, shine);
    if (texture) glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Painter::set_rotation(float radians) {
    if (radians != angle_) glUniform1f(u_angle_, angle_ = radians);
}

void Painter::fill(Rect r, float radius, Color top, Color bottom, float opacity) {
    draw(kFill, r, r, radius, top, bottom, 0, opacity);
}

void Painter::shine(Rect r, float radius, float sx, float sy, float strength) {
    if (strength <= 0.001f) return;
    draw(kShine, r, r, radius, {1, 1, 1, 1}, {1, 1, 1, 1}, 0, 1, 0, sx, sy, strength);
}

void Painter::image(Rect r, float radius, const Texture &t, Color tint, float opacity) {
    if (!t.id) return;
    draw(kTexture, r, r, radius, tint, tint, t.id, opacity);
}

void Painter::text(float x, float y, const Texture &t, Color color, float opacity) {
    image({x, y, float(t.w), float(t.h)}, 0, t, color, opacity);
}

void Painter::glass(Rect r, float radius, Color tint, float opacity) {
    if (!backdrop_.id) return;
    draw(kGlass, r, r, radius, tint, tint, backdrop_.id, opacity);
}

void Painter::shadow(Rect r, float radius, float blur, Color color, float opacity) {
    Rect quad{r.x - blur, r.y - blur, r.w + 2 * blur, r.h + 2 * blur};
    draw(kShadow, quad, r, radius, color, color, 0, opacity, blur);
}
