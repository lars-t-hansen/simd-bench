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
#define CUTOFF 1000

unsigned iterations[HEIGHT][WIDTH];

void mandel() {
    for ( unsigned Py=0 ; Py < HEIGHT; Py++ ) {
        float y0 = SCALE(Py, HEIGHT, -1, 1);
        for ( unsigned Px=0 ; Px < WIDTH; Px++ ) {
            float x0 = SCALE(Px, WIDTH, -2.5, 1);
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

void mandel_simd() {
    for ( unsigned Py=0 ; Py < HEIGHT; Py++ ) {
        v128_t y0 = wasm_f32x4_splat(SCALE(Py, HEIGHT, -1, 1));
        for ( unsigned Px=0 ; Px < WIDTH; Px+=4 ) {
            v128_t x0 = wasm_f32x4_splat(SCALE(Px, WIDTH, -2.5, 1));
            v128_t x = wasm_f32x4_const(0, 0, 0, 0);
            v128_t y = wasm_f32x4_const(0, 0, 0, 0);
            v128_t active = wasm_i32x4_const(-1, -1, -1, -1);
            v128_t counter = wasm_i32x4_const(CUTOFF, CUTOFF, CUTOFF, CUTOFF);
            for(;;) {
                v128_t x_sq = wasm_f32x4_mul(x, x);
                v128_t y_sq = wasm_f32x4_mul(y, y);
                v128_t sum_sq = wasm_f32x4_add(x_sq, y_sq);
                active = wasm_v128_and(active, wasm_f32x4_le(sum_sq, wasm_f32x4_const(4, 4, 4, 4)));
                active = wasm_v128_and(active, wasm_i32x4_ge(counter, wasm_i32x4_const(0,0,0,0)));
                if (!wasm_i32x4_any_true(active))
                    break;
                v128_t tmp = wasm_f32x4_add(wasm_f32x4_sub(x_sq, y_sq), x0);
                v128_t xy = wasm_f32x4_mul(x, y);
                y = wasm_f32x4_add(wasm_f32x4_add(xy, xy), y0);
                x = tmp;
                counter = wasm_i32x4_add(counter, active);
            }
            counter = wasm_i32x4_sub(wasm_i32x4_const(CUTOFF, CUTOFF, CUTOFF, CUTOFF), counter);
            iterations[Py][Px] = wasm_i32x4_extract_lane(counter, 0);
            iterations[Py][Px+1] = wasm_i32x4_extract_lane(counter, 1);
            iterations[Py][Px+2] = wasm_i32x4_extract_lane(counter, 2);
            iterations[Py][Px+3] = wasm_i32x4_extract_lane(counter, 3);
        }
    }
}

uint64_t timestamp() {
    struct timeval tp;
    gettimeofday(&tp, nullptr);
    return uint64_t(tp.tv_sec)*1000000 + tp.tv_usec;
}

int main(int argc, char** argv) {
    uint64_t then = timestamp();
    mandel_simd();
    uint64_t now = timestamp();
    fprintf(stderr, "Rendering time: %g ms\n", (now-then) / 1000.0);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *screen = SDL_SetVideoMode(WIDTH, HEIGHT, 32, SDL_SWSURFACE);

    if (SDL_MUSTLOCK(screen))
	SDL_LockSurface(screen);

    for (uint32_t y = 0; y < HEIGHT ; y++ ) {
	for (uint32_t x = 0; x < WIDTH; x++) {
	    uint8_t r, g, b, a = 0;
            if (iterations[y][x] < CUTOFF) {
                r = g = b = 255;
            } else {
                r = g = b = 0;
            }
	    *((Uint32*)screen->pixels + (HEIGHT-y-1) * WIDTH + x) = SDL_MapRGBA(screen->format, r, g, b, a);
	}
    }

    if (SDL_MUSTLOCK(screen))
	SDL_UnlockSurface(screen);

    return 0;
}
