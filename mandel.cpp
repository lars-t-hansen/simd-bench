#include <cstdio>
#include <sys/time.h>
#include <emscripten.h>
#include <wasm_simd128.h>
#include <SDL/SDL.h>

#define ROUNDUP4(x) (((x)+3)&~3)

#define SCALE(v, range, min, max) \
    float(min) + float(v) * (float((max) - (min)) / float(range))

#define WIDTH ROUNDUP4(unsigned(400*3.5))
#define HEIGHT (400*2)

// Classical view
#define CUTOFF 3000
#define MINY -1
#define MAXY 1
#define MINX -2.5
#define MAXX 1

unsigned iterations[HEIGHT][WIDTH];

#ifdef USE_SIMD
void mandel() {
    v128_t* addr = (v128_t*)&iterations[0][0];
    for ( float Py=0.0 ; Py < HEIGHT; Py++ ) {
        v128_t y0 = wasm_f32x4_splat(SCALE(Py, HEIGHT, MINY, MAXY));
        for ( float Px=0.0 ; Px < WIDTH; Px+=4 ) {
            v128_t x0 = wasm_f32x4_make(SCALE(Px,   WIDTH, MINX, MAXX),
                                        SCALE(Px+1, WIDTH, MINX, MAXX),
                                        SCALE(Px+2, WIDTH, MINX, MAXX),
                                        SCALE(Px+3, WIDTH, MINX, MAXX));
            v128_t x = wasm_f32x4_const(0, 0, 0, 0);
            v128_t y = wasm_f32x4_const(0, 0, 0, 0);
            v128_t active = wasm_i32x4_const(-1, -1, -1, -1);
            v128_t counter = wasm_i32x4_const(CUTOFF, CUTOFF, CUTOFF, CUTOFF);
            for(;;) {
                v128_t x_sq = wasm_f32x4_mul(x, x);
                v128_t y_sq = wasm_f32x4_mul(y, y);
                v128_t sum_sq = wasm_f32x4_add(x_sq, y_sq);
                active = wasm_v128_and(active, wasm_f32x4_le(sum_sq, wasm_f32x4_const(4, 4, 4, 4)));
                active = wasm_v128_and(active, wasm_i32x4_gt(counter, wasm_i32x4_const(0,0,0,0)));
                if (!wasm_i32x4_any_true(active))
                    break;
                v128_t tmp = wasm_f32x4_add(wasm_f32x4_sub(x_sq, y_sq), x0);
                v128_t xy = wasm_f32x4_mul(x, y);
                y = wasm_f32x4_add(wasm_f32x4_add(xy, xy), y0);
                x = tmp;
                counter = wasm_i32x4_add(counter, active);
            }
            counter = wasm_i32x4_sub(wasm_i32x4_const(CUTOFF, CUTOFF, CUTOFF, CUTOFF), counter);
            *addr++ = counter;
        }
    }
}
#else
void mandel() {
    for ( unsigned Py=0 ; Py < HEIGHT; Py++ ) {
        float y0 = SCALE(Py, HEIGHT, MINY, MAXY);
        for ( unsigned Px=0 ; Px < WIDTH; Px++ ) {
            float x0 = SCALE(Px, WIDTH, MINX, MAXX);
            float x = 0;
            float y = 0;
            unsigned iteration = 0;
            while (x*x + y*y <= 4 && iteration < CUTOFF) {
                float tmp = x*x - y*y + x0;
                y = 2*x*y + y0;
                x = tmp;
                iteration++;
            }
            iterations[Py][Px] = iteration;
        }
    }
}
#endif

uint64_t timestamp() {
    struct timeval tp;
    gettimeofday(&tp, nullptr);
    return uint64_t(tp.tv_sec)*1000000 + tp.tv_usec;
}

#ifdef SDLOUTPUT
// Supposedly the gradients used by the Wikipedia mandelbrot page

#define C(r,g,b) ((r << 16) | (g << 8) | b)
#define R(rgb) (rgb >> 16)
#define G(rgb) ((rgb >> 8) & 255);
#define B(rgb) (rgb & 255)

int32_t mapping[16] = {
    C(66, 30, 15),
    C(25, 7, 26),
    C(9, 1, 47),
    C(4, 4, 73),
    C(0, 7, 100),
    C(12, 44, 138),
    C(24, 82, 177),
    C(57, 125, 209),
    C(134, 181, 229),
    C(211, 236, 248),
    C(241, 233, 191),
    C(248, 201, 95),
    C(255, 170, 0),
    C(204, 128, 0),
    C(153, 87, 0),
    C(106, 52, 3)
};
#endif

int main(int argc, char** argv) {
#ifdef RUNTIME
    uint64_t then = timestamp();
#endif

    mandel();

#ifdef RUNTIME
    uint64_t now = timestamp();
    double runtime = (now - then) / 1000.0;
    printf("Rendering time "
# ifdef USE_SIMD
            "SIMD"
# else
            "scalar"
# endif
            ": %g ms\n", runtime);
#endif

#ifdef SDLOUTPUT
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *screen = SDL_SetVideoMode(WIDTH, HEIGHT, 32, SDL_SWSURFACE);

    if (SDL_MUSTLOCK(screen))
	SDL_LockSurface(screen);

    for (uint32_t y = 0; y < HEIGHT ; y++ ) {
	for (uint32_t x = 0; x < WIDTH; x++) {
	    uint8_t r, g, b, a = 0;
            if (iterations[y][x] < CUTOFF) {
                r = R(mapping[iterations[y][x] % 16]);
                g = G(mapping[iterations[y][x] % 16]);
                b = B(mapping[iterations[y][x] % 16]);
            } else {
                r = g = b = 0;
            }
	    *((Uint32*)screen->pixels + (HEIGHT-y-1) * WIDTH + x) = SDL_MapRGBA(screen->format, r, g, b, a);
	}
    }

    if (SDL_MUSTLOCK(screen))
	SDL_UnlockSurface(screen);
#endif

    return 0;
}
