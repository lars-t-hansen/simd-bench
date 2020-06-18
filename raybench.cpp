/* -*- mode: c++ -*- */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Author: Lars T Hansen, lth@acm.org / lhansen@mozilla.com
 */

/*
 * Ray tracer, largely out of Shirley & Marschner 3rd Ed.
 * Traces a scene and writes to a canvas.
 */

// What takes time here is antialiasing.  To allow this program to scale as a
// benchmark, we could use a smaller antialiasing grid (2x2 or 3x3, not 4x4).
//
// Also, partitioning speeds the program by a factor of 3.5 or so, so disabling
// that would make it more challenging still.

#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <sys/time.h>
#include <emscripten.h>
#include <wasm_simd128.h>

#ifdef SDL_OUTPUT
#  include <SDL/SDL.h>
#endif

using std::vector;

typedef float Float;
#define Sqrt sqrtf
#define Pow powf
#define Sin sinf
#define Cos cosf

// Partition the scene for faster tracing
#if !defined(PARTITIONING)
#  define PARTITIONING true
#endif

// Scene & tracing parameters
#ifndef HEIGHT
#  define HEIGHT 600
#endif

#ifndef WIDTH
#  define WIDTH 800;
#endif

#ifndef SHADOWS
#  define SHADOWS true
#endif

#ifndef REFLECTION
#  define REFLECTION 2
#endif

#ifndef ANTIALIAS
#  define ANTIALIAS true
#endif

static const uint32_t g_height = HEIGHT;
static const uint32_t g_width = WIDTH;

// Normally these configuration knobs would be constant, but for benchmarking they
// are made variable and affected by command line arguments.  See main().

static const bool g_partitioning = PARTITIONING;

static const bool g_shadows = SHADOWS;	                 // Compute object shadows

static const bool g_reflection = (REFLECTION != 0);           // Compute object reflections
static const uint32_t g_reflection_depth = REFLECTION;        //   to this depth

static const bool g_antialias = ANTIALIAS;                    // Antialias the image (expensive but very pretty)

// Viewport
static const Float g_left = -2;
static const Float g_right = 2;
static const Float g_top = 1.5;
static const Float g_bottom = -1.5;

// END CONFIGURATION

static void CRASH(const char* msg) {
    printf("CRASH: %s\n", msg);
    exit(1);
}

static void WARNING(const char* msg) {
    printf("WARNING: %s\n", msg);
}

static uint64_t timestamp() {
    struct timeval tp;
    gettimeofday(&tp, nullptr);
    return uint64_t(tp.tv_sec)*1000000 + tp.tv_usec;
}

static const Float SENTINEL = 1e32;
static const Float EPS = 0.00001;

static inline Float Max(Float a, Float b) {
    return a > b ? a : b;
}

static inline Float Min(Float a, Float b) {
    return a < b ? a : b;
}

#ifdef USE_SIMD

// In general we ignore the w lane but it may have garbage, it must be cleared
// if it might be incorporated in a result.

typedef v128_t Vec3;
typedef v128_t Bool3;

#define X(v) wasm_f32x4_extract_lane(v, 0)
#define Y(v) wasm_f32x4_extract_lane(v, 1)
#define Z(v) wasm_f32x4_extract_lane(v, 2)

#define Vec3Z() wasm_f32x4_const(0, 0, 0, 0)

#define Vec3C(x, y, z) wasm_f32x4_const(x, y, z, 0)

#define Vec3B(x, y, z) wasm_f32x4_make(x, y, z, 0)

#define V3P v128_t

static inline Vec3 add(V3P a, V3P b) {
    return wasm_f32x4_add(a, b);
}

static inline Vec3 addi(V3P a, Float c) {
    return wasm_f32x4_add(a, wasm_f32x4_splat(c));
}

static inline Vec3 sub(V3P a, V3P b) {
    return wasm_f32x4_sub(a, b);
}

static inline Vec3 subi(V3P a, Float c) {
    return wasm_f32x4_sub(a, wasm_f32x4_splat(c));
}

static inline Vec3 mul(V3P a, V3P b) {
    return wasm_f32x4_mul(a, b);
}

static inline Vec3 muli(V3P a, Float c) {
    return wasm_f32x4_mul(a, wasm_f32x4_splat(c));
}

static inline Vec3 divi(V3P a, Float c) {
    return wasm_f32x4_div(a, wasm_f32x4_splat(c));
}

static inline Vec3 inv(V3P a) {
    return wasm_f32x4_div(wasm_f32x4_splat(1), a);
}

static inline Vec3 neg(V3P a) {
    return wasm_f32x4_neg(a);
}

static inline Vec3 cross(V3P a, V3P b) {
#if 0
    return Vec3B(Y(a)*Z(b) - Z(a)*Y(b), Z(a)*X(b) - X(a)*Z(b), X(a)*Y(b) - Y(a)*X(b));
#else
    constexpr int x = 0;
    constexpr int y = 1;
    constexpr int z = 2;
    constexpr int w = 3;
    Vec3 tmp0 = wasm_v32x4_shuffle(a,a,y,z,x,w); // lhs of first mul
    Vec3 tmp1 = wasm_v32x4_shuffle(b,b,z,x,y,w); // rhs of first mul
    Vec3 tmp2 = wasm_v32x4_shuffle(a,a,z,x,y,w); // lhs of second mul
    Vec3 tmp3 = wasm_v32x4_shuffle(b,b,y,z,x,w); // rhs of second mul
    return wasm_f32x4_sub(wasm_f32x4_mul(tmp0,tmp1), wasm_f32x4_mul(tmp2,tmp3));
#endif
}

static inline Float dot(V3P a, V3P b) {
    Vec3 tmp = wasm_f32x4_mul(a, b);
    return X(tmp) + Y(tmp) + Z(tmp);
}

static inline Vec3 vmin(Vec3 a, Vec3 b) {
    return wasm_f32x4_min(a, b);
}

static inline Vec3 vmax(Vec3 a, Vec3 b) {
    return wasm_f32x4_max(a, b);
}

static inline Bool3 vpositive(Vec3 a) {
    return wasm_f32x4_ge(a, Vec3Z());
}

static inline Vec3 bitselect(Vec3 a, Vec3 b, Bool3 control) {
    return wasm_v128_bitselect(a, b, control);
}

#else

struct Vec3 {
    Float x_;
    Float y_;
    Float z_;

    // The zero init is a little dangerous because we don't get this with the
    // SIMD version, beware of subtle bugs.  Consider garbage-init here under a
    // DEBUG define.
    Vec3() : x_(0), y_(0), z_(0) {}

    Vec3(Float x, Float y, Float z)
	: x_(x)
	, y_(y)
	, z_(z)
    {}
};

struct Bool3 {
    bool x_, y_, z_;
    Bool3(bool x, bool y, bool z) : x_(x), y_(y), z_(z) {}
};

#define X(v) v.x_
#define Y(v) v.y_
#define Z(v) v.z_

static inline Vec3 Vec3Z() {
    return Vec3(0,0,0);
}

// Constant args
static inline Vec3 Vec3C(Float a, Float b, Float c) {
    return Vec3(a, b, c);
}

// Variable args
static inline Vec3 Vec3B(Float a, Float b, Float c) {
    return Vec3(a, b, c);
}

#define V3P const Vec3&

static inline Vec3 add(V3P a, V3P b) {
    return Vec3(a.x_+b.x_, a.y_+b.y_, a.z_+b.z_);
}

static inline Vec3 addi(V3P a, Float c) {
    return Vec3(a.x_+c, a.y_+c, a.z_+c);
}

static inline Vec3 sub(V3P a, V3P b) {
    return Vec3(a.x_-b.x_, a.y_-b.y_, a.z_-b.z_);
}

static inline Vec3 subi(V3P a, Float c) {
    return Vec3(a.x_-c, a.y_-c, a.z_-c);
}

static inline Vec3 mul(V3P a, V3P b) {
    return Vec3(a.x_*b.x_, a.y_*b.y_, a.z_*b.z_);
}

static inline Vec3 muli(V3P a, Float c) {
    return Vec3(a.x_*c, a.y_*c, a.z_*c);
}

static inline Vec3 divi(V3P a, Float c) {
    return Vec3(a.x_/c, a.y_/c, a.z_/c);
}

static inline Vec3 inv(V3P a) {
    return Vec3(1/a.x_, 1/a.y_, 1/a.z_);
}

static inline Vec3 neg(V3P a) {
    return Vec3(-a.x_, -a.y_, -a.z_);
}

static inline Vec3 cross(V3P a, V3P b) {
    return Vec3(a.y_*b.z_ - a.z_*b.y_, a.z_*b.x_ - a.x_*b.z_, a.x_*b.y_ - a.y_*b.x_);
}

static inline Float dot(V3P a, V3P b) {
    return a.x_*b.x_ + a.y_*b.y_ + a.z_*b.z_;
}

static inline Vec3 vmin(Vec3 a, Vec3 b) {
    return Vec3(Min(a.x_, b.x_), Min(a.y_, b.y_), Min(a.z_, b.z_));
}

static inline Vec3 vmax(Vec3 a, Vec3 b) {
    return Vec3(Max(a.x_, b.x_), Max(a.y_, b.y_), Max(a.z_, b.z_));
}

static inline Bool3 vpositive(Vec3 a) {
    return Bool3(a.x_ >= 0, a.y_ >= 0, a.z_ >= 0);
}

static inline Vec3 bitselect(Vec3 a, Vec3 b, Bool3 control) {
    return Vec3(control.x_ ? a.x_ : b.x_,
                control.y_ ? a.y_ : b.y_,
                control.z_ ? a.z_ : b.z_);
}

#endif // !USE_SIMD

static inline Float length(V3P a) {
    return Sqrt(dot(a,a));
}

static inline Vec3 normalize(V3P a) {
    return divi(a, length(a));
}

struct Material {
    Vec3 diffuse;
    Vec3 specular;
    Float shininess;
    Vec3 ambient;
    Float mirror;

    Material(V3P diffuse, V3P specular, Float shininess, V3P ambient, Float mirror)
	: diffuse(diffuse)
	, specular(specular)
	, shininess(shininess)
	, ambient(ambient)
	, mirror(mirror)
    {}

    Material()
	: diffuse(Vec3Z())
	, specular(Vec3Z())
	, shininess(0)
	, ambient(Vec3Z())
	, mirror(0)
    {}
};

struct Bounds {
    Vec3 mins;
    Vec3 maxs;

    Bounds(Vec3 mins, Vec3 maxs) : mins(mins), maxs(maxs) {}
};

class Surface
{
public:
    Material material;

    Surface(const Material& material)
	: material(material)
    {}

    virtual Surface* intersect(V3P eye, V3P ray, Float min, Float max, Float* distance) = 0;
    virtual Vec3 normal(V3P p) = 0;
    virtual Bounds bounds() = 0;
    virtual Vec3 center() = 0;
    virtual void debug(void (*print)(const char* s), uint32_t level) = 0;
};

class Volume : public Surface
{
    Bounds   bounds_;
    Surface* left_;
    Surface* right_;

public:
    Volume(Bounds bounds, Surface* left, Surface* right)
	: Surface(Material())
	, bounds_(bounds)
	, left_(left)
	, right_(right)
    {}

    Surface* intersect(V3P eye, V3P ray, Float min, Float max, Float* distance) {
	// Test volume intersection.
        Vec3 a = inv(ray);
        Vec3 a_times_mins_minus_eye = mul(a, sub(bounds_.mins, eye));
        Vec3 a_times_maxs_minus_eye = mul(a, sub(bounds_.maxs, eye));
        Bool3 a_ge_0 = vpositive(a);
        Vec3 mins = bitselect(a_times_mins_minus_eye, a_times_maxs_minus_eye, a_ge_0);
        Vec3 maxs = bitselect(a_times_maxs_minus_eye, a_times_mins_minus_eye, a_ge_0);
	Float tmin = 0, tmax = 0, tymin = 0, tymax = 0, tzmin = 0, tzmax = 0;
        tmin = X(mins);
        tmax = X(maxs);
        tymin = Y(mins);
        tymax = Y(maxs);
	if (tmin > tymax || tymin > tmax)
	    return nullptr;
	if (tymin > tmin)
	    tmin = tymin;
	if (tymax < tmax)
	    tmax = tymax;
        tzmin = Z(mins);
        tzmax = Z(maxs);
	if (tmin > tzmax || tzmin > tmax)
	    return nullptr;
	if (tzmin > tmin)
	    tmin = tzmin;
	if (tzmax < tmax)
	    tmax = tzmax;

	if (!(tmin < max && tmax > min))
	    return nullptr;

	// Then test object intersection.
	Float d1 = 0;
	Surface* r1 = left_->intersect(eye, ray, min, max, &d1);
	if (right_) {
	    Float d2 = 0;
	    Surface* r2 = right_->intersect(eye, ray, min, max, &d2);
	    if (r2 && (!r1 || d2 < d1)) {
		*distance = d2;
		return r2;
	    }
	}
	*distance = d1;
	return r1;
    }

    Bounds bounds() {
	return bounds_;
    }

    Vec3 normal(V3P p) {
	CRASH("Normal not implemented for Volume");
	return Vec3Z();
    }

    Vec3 center() {
	CRASH("Center not implemented for Volume");
	return Vec3Z();
    }

    void debug(void (*print)(const char* s), uint32_t level) {
	print("[");
	left_->debug(print, level+1);
	if (right_) {
	    print(",\n");
	    for ( uint32_t i=0 ; i < level ; i++ )
		print(" ");
	    right_->debug(print, level+1);
	}
	print("]");
    }
};

class Jumble : public Surface
{
    vector<Surface*> surfaces;

public:
    Jumble(vector<Surface*>& world)
	: Surface(Material())
    {
	surfaces.assign(world.begin(), world.end());
    }

    Surface* intersect(V3P eye, V3P ray, Float min, Float max, Float* distance) {
	Surface* min_obj = nullptr;
	Float min_dist = 1e100;

	for ( Surface* surface : surfaces ) {
	    Float dist = 0;
	    Surface* obj = surface->intersect(eye, ray, min, max, &dist);
	    if (obj) {
		if (dist < min_dist) {
		    min_obj = obj;
		    min_dist = dist;
		}
	    }
	}
	*distance = min_dist;
	return min_obj;
    }

    Vec3 normal(V3P p) {
	CRASH("Normal not implemented for Jumble");
	return Vec3Z();
    }

    Bounds bounds() {
	CRASH("Bounds not implemented for Jumble");
	return Bounds(Vec3Z(), Vec3Z());
    }

    Vec3 center() {
	CRASH("Center not implemented for Jumble");
	return Vec3Z();
    }

    void debug(void (*print)(const char* s), uint32_t level) {
    }
};

class Sphere : public Surface
{
    Vec3 center_;
    Float radius_;

public:
    Sphere(const Material& material, V3P center, Float radius)
	: Surface(material)
	, center_(center)
	, radius_(radius)
    {}

    Surface* intersect(V3P eye, V3P ray, Float min, Float max, Float* distance) {
	Float DdotD = dot(ray, ray);
	Vec3 EminusC = sub(eye, center_);
	Float B = dot(ray, EminusC);
	Float disc = B*B - DdotD*(dot(EminusC,EminusC) - radius_*radius_);
	if (disc < 0.0)
	    return nullptr;
	Float s1 = (-B + Sqrt(disc))/DdotD;
	Float s2 = (-B - Sqrt(disc))/DdotD;
	// Here return the smallest of s1 and s2 after filtering for _min and _max
	if (s1 < min || s1 > max)
	    s1 = SENTINEL;
	if (s2 < min || s2 > max)
	    s2 = SENTINEL;
	Float dist = Min(s1, s2);
	if (dist == SENTINEL)
	    return nullptr;
	*distance = dist;
	return this;
    }

    Vec3 normal(V3P p) {
	return divi(sub(p, center_), radius_);
    }

    Bounds bounds() {
	return Bounds(subi(center_, radius_), addi(center_, radius_));
    }

    Vec3 center() {
	return center_;
    }

    void debug(void (*print)(const char* s), uint32_t level) {
	char buf[256];
	sprintf(buf, "(S c=(%g,%g,%g) r=%g)", X(center_), Y(center_), Z(center_), radius_);
	print(buf);
    }
};

class Triangle : public Surface
{
    Vec3 v1;
    Vec3 v2;
    Vec3 v3;
    Vec3 norm;

public:
    Triangle(const Material& material, V3P v1, V3P v2, V3P v3)
	: Surface(material)
	, v1(v1)
	, v2(v2)
	, v3(v3)
	, norm(normalize(cross(sub(v2, v1), sub(v3, v1))))
    {}

    Surface* intersect(V3P eye, V3P ray, Float min, Float max, Float* distance) {
	// TODO: observe that values that do not depend on g, h, and i can be precomputed
	// and stored with the triangle (for a given eye position), at some (possibly significant)
	// space cost.  Notably the numerator of "t" is invariant, as are many factors of the
	// numerator of "gamma".
	Float a = X(v1) - X(v2);
	Float b = Y(v1) - Y(v2);
	Float c = Z(v1) - Z(v2);
	Float d = X(v1) - X(v3);
	Float e = Y(v1) - Y(v3);
	Float f = Z(v1) - Z(v3);
	Float g = X(ray);
	Float h = Y(ray);
	Float i = Z(ray);
	Float j = X(v1) - X(eye);
	Float k = Y(v1) - Y(eye);
	Float l = Z(v1) - Z(eye);
	Float M = a*(e*i - h*f) + b*(g*f - d*i) + c*(d*h - e*g);
	Float t = -((f*(a*k - j*b) + e*(j*c - a*l) + d*(b*l - k*c))/M);
	if (t < min || t > max)
	    return nullptr;
	Float gamma = (i*(a*k - j*b) + h*(j*c - a*l) + g*(b*l - k*c))/M;
	if (gamma < 0 || gamma > 1.0)
	    return nullptr;
	Float beta = (j*(e*i - h*f) + k*(g*f - d*i) + l*(d*h - e*g))/M;
	if (beta < 0.0 || beta > 1.0 - gamma)
	    return nullptr;
	*distance = t;
	return this;
    }

    Vec3 normal(V3P p) {
	return norm;
    }

    Bounds bounds() {
	return Bounds(vmin(v1, vmin(v2, v3)),
                      vmax(v1, vmax(v2, v3)));
    }

    Vec3 center() {
	return divi(add(v1, add(v2, v3)), 3);
    }

    void debug(void (*print)(const char* s), uint32_t level) {
	char buf[1024];
	sprintf(buf, "[T (%g,%g,%g) (%g,%g,%g) (%g,%g,%g)]", X(v1), Y(v1), Z(v1), X(v2), Y(v2), Z(v2), X(v3), Y(v3), Z(v3));
	print(buf);
    }
};

// "Color" is a Vec3 representing RGB scaled by 256.
// "RGBA" is a uint32_t representing r,g,b,a in the range 0..255

Vec3 colorFromRGB(uint32_t r, uint32_t g, uint32_t b) {
    return Vec3B(r/256.0, g/256.0, b/256.0);
}

uint32_t rgbaFromColor(V3P color)
{
    return (255<<24) | (uint32_t(255*Z(color))<<16) | (uint32_t(255*Y(color))<<8) | uint32_t(255*X(color));
}

void componentsFromRgba(uint32_t rgba, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a)
{
    *r = rgba;
    *g = (rgba >> 8);
    *b = (rgba >> 16);
    *a = (rgba >> 24);
}

class Bitmap
{
    uint32_t height;
    uint32_t width;
    uint32_t* const data;

public:
    Bitmap(uint32_t height, uint32_t width, V3P color)
	: height(height)
	, width(width)
	, data(new uint32_t[height*width])
    {
	if (!data)
	    CRASH("Failed to allocate array");

	uint32_t c = rgbaFromColor(color);
	for ( uint32_t i=0, l=width*height ; i < l ; i++ )
	    data[i] = c;
    }

    // For debugging only
    uint32_t ref(uint32_t y, uint32_t x) {
	return data[(height-y)*width + x];
    }

    // Not a hot function
    void setColor(uint32_t y, uint32_t x, V3P v) {
	data[(height-y)*width + x] = rgbaFromColor(v);
    }
};


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static Surface* setStage(Vec3* eye, Vec3* light, Vec3* background);
static void trace(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim, V3P eye, V3P light, V3P background, Surface* world, Bitmap* bits);

int main(int argc, char** argv)
{
    Vec3 eye;
    Vec3 light;
    Vec3 background;
    Surface* world;

    {
#ifdef RUNTIME
	uint64_t then = timestamp();
#endif
	world = setStage(&eye, &light, &background);
#ifdef RUNTIME
	uint64_t now = timestamp();
	printf("Setup time: %g ms\n", (now-then) / 1000.0);
#endif
    }

    Bitmap bits(g_height, g_width, colorFromRGB(152, 251, 152));

    {
#ifdef RUNTIME
	uint64_t then = timestamp();
#endif
	trace(0, g_height, 0, g_width, eye, light, background, world, &bits);
#ifdef RUNTIME
	uint64_t now = timestamp();
	printf("Render time: %g ms\n", (now-then) / 1000.0);
#endif
    }

#ifdef PPMX_STDOUT
    printf("P6 %d %d 255\n", g_width, g_height);
#endif

#ifdef SDL_OUTPUT
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *screen = SDL_SetVideoMode(g_width, g_height, 32, SDL_SWSURFACE);

    if (SDL_MUSTLOCK(screen))
	SDL_LockSurface(screen);
#endif

#if defined(SDL_OUTPUT) || defined(PPMX_STDOUT)
    for (uint32_t y = 0; y < g_height ; y++ ) {
	for (uint32_t x = 0; x < g_width; x++) {
	    uint8_t r, g, b, a;
# ifdef SDL_OUTPUT
	    componentsFromRgba(bits.ref(y, x), &r, &g, &b, &a);
            *((Uint32*)screen->pixels + (g_height-1-y) * g_width + x) = SDL_MapRGBA(screen->format, r, g, b, a);
# endif
# ifdef PPMX_STDOUT
	    componentsFromRgba(bits.ref(g_height-1-y, x), &r, &g, &b, &a);
            printf("!%x!%x!%x", r, g, b);
# endif
	}
    }
#endif

#ifdef SDL_OUTPUT
    if (SDL_MUSTLOCK(screen))
	SDL_UnlockSurface(screen);
#endif

#ifdef PPMX_STDOUT
    printf("\n");
#endif
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static Vec3 g_eye;
static Vec3 g_background;
static Vec3 g_light;
static Surface* g_world;
static Bitmap* g_bits;

static void traceWithoutAntialias(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim);
static void traceWithAntialias(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim);
static Vec3 raycolor(V3P eye, V3P ray, Float t0, Float t1, uint32_t depth);

static void trace(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim, V3P eye, V3P light, V3P background, Surface* world, Bitmap* bits)
{
    // Easiest to keep these in globals.
    g_eye = eye;
    g_light = light;
    g_background = background;
    g_world = world;
    g_bits = bits;
    if (g_antialias)
	traceWithAntialias(ymin, ylim, xmin, xlim);
    else
	traceWithoutAntialias(ymin, ylim, xmin, xlim);
}

static void traceWithoutAntialias(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim)
{
    for ( uint32_t h=ymin ; h < ylim ; h++ ) {
	for ( uint32_t w=xmin ; w < xlim ; w++ ) {
	    Float u = g_left + (g_right - g_left)*(w + 0.5)/g_width;
	    Float v = g_bottom + (g_top - g_bottom)*(h + 0.5)/g_height;
	    Vec3 ray = Vec3B(u, v, -Z(g_eye));
	    Vec3 col = raycolor(g_eye, ray, 0, SENTINEL, g_reflection_depth);
	    g_bits->setColor(h, w, col);
	}
    }
}

static const Float random_numbers[] = {
    0.495,0.840,0.636,0.407,0.026,0.547,0.223,0.349,0.033,0.643,0.558,0.481,0.039,
    0.175,0.169,0.606,0.638,0.364,0.709,0.814,0.206,0.346,0.812,0.603,0.969,0.888,
    0.294,0.824,0.410,0.467,0.029,0.706,0.314
};

static void traceWithAntialias(uint32_t ymin, uint32_t ylim, uint32_t xmin, uint32_t xlim)
{
    uint32_t k = 0;
    for ( uint32_t h=ymin ; h < ylim ; h++ ) {
	for ( uint32_t w=xmin ; w < xlim ; w++ ) {
	    // Simple stratified sampling, cf Shirley&Marschner ch 13 and a fast "random" function.
	    const uint32_t n = 4;
	    //var k = h % 32;
	    uint32_t rand = k % 2;
	    Vec3 c = Vec3Z();
	    k++;
	    for ( uint32_t p=0 ; p < n ; p++ ) {
		for ( uint32_t q=0 ; q < n ; q++ ) {
		    Float jx = random_numbers[rand]; rand=rand+1;
		    Float jy = random_numbers[rand]; rand=rand+1;
		    Float u = g_left + (g_right - g_left)*(w + (p + jx)/n)/g_width;
		    Float v = g_bottom + (g_top - g_bottom)*(h + (q + jy)/n)/g_height;
		    Vec3 ray = Vec3B(u, v, -Z(g_eye));
		    c = add(c, raycolor(g_eye, ray, 0.0, SENTINEL, g_reflection_depth));
		}
	    }
	    g_bits->setColor(h, w, divi(c, n*n));
	}
    }
}

// Clamping c is not necessary provided the three color components by
// themselves never add up to more than 1, and shininess == 0 or shininess >= 1.
//
// TODO: lighting intensity is baked into the material here, but we probably want
// to factor that out and somehow attenuate light with distance from the light source,
// for diffuse and specular lighting.

static Vec3 raycolor(V3P eye, V3P ray, Float t0, Float t1, uint32_t depth)
{
    Float dist;
    Surface* obj = g_world->intersect(eye, ray, t0, t1, &dist);

    if (obj) {
	Material& m = obj->material;
	Vec3 p = add(eye, muli(ray, dist));
	Vec3 n1 = obj->normal(p);
	Vec3 l1 = normalize(sub(g_light, p));
	Vec3 c = m.ambient;
	Surface* min_obj = nullptr;

	if (g_shadows) {
	    Float tmp;
	    min_obj = g_world->intersect(add(p, muli(l1, EPS)), l1, EPS, SENTINEL, &tmp);
	}
	if (!min_obj) {
	    const Float diffuse = Max(0.0, dot(n1,l1));
	    const Vec3 v1 = normalize(neg(ray));
	    const Vec3 h1 = normalize(add(v1, l1));
	    const Float specular = Pow(Max(0.0, dot(n1, h1)), m.shininess);
	    c = add(c, add(muli(m.diffuse, diffuse), muli(m.specular, specular)));
	    if (g_reflection) {
		if (depth > 0 && m.mirror != 0.0) {
		    const Vec3 r = sub(ray, muli(n1, 2.0*dot(ray, n1)));
		    c = add(c, muli(raycolor(add(p, muli(r, EPS)), r, EPS, SENTINEL, depth-1), m.mirror));
		}
	    }
	}
	return c;
    }
    return g_background;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Colors: http://kb.iu.edu/data/aetf.html

static const Vec3 paleGreen = colorFromRGB(152, 251, 152);
static const Vec3 darkGray = colorFromRGB(169, 169, 169);
static const Vec3 yellow = colorFromRGB(256, 256, 0);
static const Vec3 red = colorFromRGB(256, 0, 0);
static const Vec3 blue = colorFromRGB(0, 0, 256);

// Not restricted to a rectangle, actually
static void rectangle(vector<Surface*>& world, const Material& m, V3P v1, V3P v2, V3P v3, V3P v4)
{
    world.push_back(new Triangle(m, v1, v2, v3));
    world.push_back(new Triangle(m, v1, v3, v4));
}

// Vertices are for front and back faces, both counterclockwise as seen
// from the outside.
// Not restricted to a cube, actually.
static void cube(vector<Surface*>& world, const Material& m, V3P v1, V3P v2, V3P v3, V3P v4, V3P v5, V3P v6, V3P v7, V3P v8)
{
    rectangle(world, m, v1, v2, v3, v4);  // front
    rectangle(world, m, v2, v5, v8, v3);  // right
    rectangle(world, m, v6, v1, v4, v7);  // left
    rectangle(world, m, v5, v6, v7, v8);  // back
    rectangle(world, m, v4, v3, v8, v7);  // top
    rectangle(world, m, v6, v1, v2, v5);  // bottom
}

static constexpr Float MAXBOUND = 1e100;
static constexpr Float MINBOUND = -MAXBOUND;

static Bounds computeBounds(vector<Surface*>& surfaces)
{
    Vec3 mins = Vec3C(MAXBOUND, MAXBOUND, MAXBOUND);
    Vec3 maxs = Vec3C(MINBOUND, MINBOUND, MINBOUND);
    for ( Surface *s : surfaces ) {
	Bounds b = s->bounds();
        mins = vmin(mins, b.mins);
        maxs = vmax(maxs, b.maxs);
    }
    return Bounds(mins, maxs);
}

// This is not quite right, cf the Jumble object.  The bug could be here, or in
// the intersection algorithm.

static Surface* partition(vector<Surface*>& surfaces, Bounds bounds, uint32_t axis)
{
    Surface* left=nullptr;
    Surface* right=nullptr;
    if (surfaces.size() == 1) {
	left = surfaces[0];
	right = nullptr;
    }
    else if (surfaces.size() == 2) {
	left = surfaces[0];
	right = surfaces[1];
    }
    else {
	// We really should choose the "best" partitioning here, ie the most
	// even.  Instead we pick the first that works, and if we can't
	// partition among any axis we put the objects in a bag together and
	// trace them all.  There are other strategies.
	uint32_t safety = 4;
	vector<Surface*> lobj;
	vector<Surface*> robj;
	for (;;) {
	    if (!--safety) {
		WARNING("Degenerate parition");
		return new Jumble(surfaces);
	    }
	    Float mid = 0;
	    Float center = 0;
	    for ( Surface* s : surfaces) {
		switch (axis) {
		  case 0:
		    mid = (X(bounds.maxs) + X(bounds.mins)) / 2;
		    center = X(s->center());
		    break;
		  case 1:
		    mid = (Y(bounds.maxs) + Y(bounds.mins)) / 2;
		    center = Y(s->center());
		    break;
		  case 2:
		    mid = (Z(bounds.maxs) + Z(bounds.mins)) / 2;
		    center = Z(s->center());
		    break;
		}
		if (center <= mid)
		    lobj.push_back(s);
		else
		    robj.push_back(s);
	    }
	    axis = (axis + 1) % 3;
	    if (robj.size() && lobj.size())
		break;
	    lobj.clear();
	    robj.clear();
	}
	left = lobj.size() == 1 ? lobj[0] : partition(lobj, computeBounds(lobj), axis);
	right = robj.size() == 1 ? robj[0] : partition(robj, computeBounds(robj), axis);
    }
    return new Volume(bounds, left, right);
}

static Surface* setStage(Vec3* eye, Vec3* light, Vec3* background)
{
    Material m1(Vec3C(0.1, 0.2, 0.2), Vec3C(0.3, 0.6, 0.6), 10, Vec3C(0.05, 0.1, 0.1),  0);
    Material m2(Vec3C(0.3, 0.3, 0.2), Vec3C(0.6, 0.6, 0.4), 10, Vec3C(0.1,  0.1, 0.05), 0);
    Material m3(Vec3C(0.1, 0.0, 0.0), Vec3C(0.8, 0.0, 0.0), 10, Vec3C(0.1,  0.0, 0.0),  0);
    Material m4(muli(darkGray,0.4),  muli(darkGray,0.3), 100, muli(darkGray,0.3),    0.5);
    Material m5(muli(paleGreen,0.4), muli(paleGreen,0.4), 10, muli(paleGreen,0.2),   1.0);
    Material m6(muli(yellow,0.6),    Vec3C(0, 0, 0),        0, muli(yellow,0.4),      0);
    Material m7(muli(red,0.6),       Vec3C(0, 0, 0),        0, muli(red,0.4),         0);
    Material m8(muli(blue,0.6),      Vec3C(0, 0, 0),        0, muli(blue,0.4),        0);

    vector<Surface*> world;

    world.push_back(new Sphere(m1, Vec3C(-1, 1, -9), 1));
    world.push_back(new Sphere(m2, Vec3C(1.5, 1, 0), 0.75));
    world.push_back(new Triangle(m1, Vec3C(-1,0,0.75), Vec3C(-0.75,0,0), Vec3C(-0.75,1.5,0)));
    world.push_back(new Triangle(m3, Vec3C(-2,0,0), Vec3C(-0.5,0,0), Vec3C(-0.5,2,0)));
    rectangle(world, m4, Vec3C(-5,0,5), Vec3C(5,0,5), Vec3C(5,0,-40), Vec3C(-5,0,-40));
    cube(world, m5, Vec3C(1, 1.5, 1.5), Vec3C(1.5, 1.5, 1.25), Vec3C(1.5, 1.75, 1.25), Vec3C(1, 1.75, 1.5),
	 Vec3C(1.5, 1.5, 0.5), Vec3C(1, 1.5, 0.75), Vec3C(1, 1.75, 0.75), Vec3C(1.5, 1.75, 0.5));
    for ( uint32_t i=0 ; i < 30 ; i++ )
	world.push_back(new Sphere(m6, Vec3B((-0.6+(i*0.2)), (0.075+(i*0.05)), (1.5-(i*Cos(i/30.0)*0.5))), 0.075));
    for ( uint32_t i=0 ; i < 60 ; i++ )
	world.push_back(new Sphere(m7, Vec3B((1+0.3*Sin(i*(3.14/16))), (0.075+(i*0.025)), (1+0.3*Cos(i*(3.14/16)))), 0.025));
    for ( uint32_t i=0 ; i < 60 ; i++ )
	world.push_back(new Sphere(m8, Vec3B((1+0.3*Sin(i*(3.14/16))), (0.075+((i+8)*0.025)), (1+0.3*Cos(i*(3.14/16)))), 0.025));

    *eye        = Vec3C(0.5, 0.75, 5);
    *light      = Vec3B(g_left-1, g_top, 2);
    *background = colorFromRGB(25, 25, 112);

    if (g_partitioning)
	return partition(world, computeBounds(world), 0);

    return new Jumble(world);
}
