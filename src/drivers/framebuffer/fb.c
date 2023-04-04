/* 
 * pongoOS - https://checkra.in
 * 
 * Copyright (C) 2019-2023 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */
#include <pongo.h>
#include "font8x8_basic.h"

uint32_t* gFramebuffer;
uint32_t* gFramebufferCopy;
uint32_t gWidth;
uint32_t gHeight;
uint32_t gRowPixels;
uint32_t y_cursor;
uint32_t x_cursor;
uint8_t scale_factor;
uint32_t bannerHeight = 0;
char overflow_mode = 0;
uint32_t basecolor = 0x41414141;
void screen_fill(uint32_t color) {
    for (int y = 0; y < gHeight; y++) {
        for (int x = 0; x < gWidth; x++) {
            gFramebuffer[x + y * gRowPixels] = color;
        }
    }
    cache_clean(gFramebuffer, gHeight * gRowPixels * 4);
}
void screen_fill_basecolor() {
    return screen_fill(basecolor);
}
void screen_clear_all(const char *cmd, char *args)
{
    y_cursor = bannerHeight;
    for (int y = bannerHeight; y < gHeight; y++) {
        for (int x = 0; x < gWidth; x++) {
            gFramebuffer[x + y * gRowPixels] = gFramebufferCopy[x + y * gRowPixels];
        }
    }
    cache_clean(gFramebuffer, gHeight * gRowPixels * 4);
}
void screen_clear_row()
{
    for (int y = 0; y < (1 + 8 * SCALE_FACTOR); y++) {
        for (int x = 0; x < gWidth; x++) {
            gFramebuffer[x + ((y + y_cursor) * gRowPixels)] = gFramebufferCopy[x + ((y + y_cursor) * gRowPixels)];
        }
    }
    cache_clean(&gFramebuffer[y_cursor * gRowPixels], (1 + 8 * SCALE_FACTOR) * gRowPixels * 4);
}
uint32_t color_compose(uint16_t components[3]) {
    return ((((uint32_t)components[3]) & 0xff) << 24) | ((((uint32_t)components[2]) & 0xff) << 16) | ((((uint32_t)components[1]) & 0xff) << 8)  | ((((uint32_t)components[0]) & 0xff) << 0); // works on ARGB8,8,8,8 only, i'll add ARGB5,9,9,9 eventually
}
uint32_t color_compose_v32(uint32_t components[3]) {
    return ((((uint32_t)components[3]) & 0xff) << 24) | ((((uint32_t)components[2]) & 0xff) << 16) | ((((uint32_t)components[1]) & 0xff) << 8)  | ((((uint32_t)components[0]) & 0xff) << 0); // works on ARGB8,8,8,8 only, i'll add ARGB5,9,9,9 eventually
}
void color_decompose(uint32_t color, uint16_t* components) {
    components[0] = color & 0xff;
    components[1] = (color >> 8) & 0xff;
    components[2] = (color >> 16) & 0xff;
    components[3] = (color >> 24) & 0xff;
}
uint16_t component_darken(float component, float factor) {
    float mul = (component * factor);
    uint32_t mulr = mul;
    if (mulr > 0xff) return 0xff; // clamp to 0xff as max (only works on ARGB8,8,8,8, will fix later)
    return mulr;
}
uint32_t color_darken(uint32_t color, float darkenfactor) {
    uint16_t components[4];
    color_decompose(color, components);
    components[0] = component_darken(components[0], darkenfactor);
    components[1] = component_darken(components[1], darkenfactor);
    components[2] = component_darken(components[2], darkenfactor);
    return color_compose(components);
}
uint32_t colors_average(uint32_t color1, uint32_t color2) {
    uint16_t components[4];
    uint16_t components1[4];
    color_decompose(color1, components);
    color_decompose(color2, components1);
    components[0] = (components[0] + components1[0]) >> 1;
    components[1] = (components[1] + components1[1]) >> 1;
    components[2] = (components[2] + components1[2]) >> 1;
    return color_compose(components);
}

uint32_t colors_mix_alpha(uint32_t color1, uint32_t color2) {
    uint16_t components[4];
    uint16_t components1[4];
    uint32_t componentsw[4];
    color_decompose(color1, components);
    color_decompose(color2, components1);
    componentsw[0] = (components[0] * components[3]);
    componentsw[1] = (components[1] * components[3]);
    componentsw[2] = (components[2] * components[3]);
    componentsw[0] += (components1[0] * components1[3]);
    componentsw[1] += (components1[1] * components1[3]);
    componentsw[2] += (components1[2] * components1[3]);
    uint32_t total_alpha = components[3] + components1[3];
    componentsw[0] /= total_alpha;
    componentsw[1] /= total_alpha;
    componentsw[2] /= total_alpha;
    componentsw[3] = 0xff;
    
    return color_compose_v32(componentsw);
}

void screen_putc(uint8_t c)
{
    if (!gFramebuffer) return;
    disable_interrupts();
    if (c == '\b') {
        if (x_cursor > 8 * SCALE_FACTOR) {
            x_cursor -= 8 * SCALE_FACTOR;
        } else {
            x_cursor = 0;
        }
        if (LEFT_MARGIN > x_cursor) {
            x_cursor = LEFT_MARGIN;
        }
        enable_interrupts();
        return;
    }
    if (c == '\n' || (x_cursor + (8 * SCALE_FACTOR)) > (gWidth - LEFT_MARGIN*2)) {
        if ((y_cursor + (12 * SCALE_FACTOR) + 16) > gHeight) {
            y_cursor = bannerHeight;
        } else {
            y_cursor += 1 + 8 * SCALE_FACTOR;
        }
        x_cursor = LEFT_MARGIN;
        screen_clear_row();
    }
    if (c == '\n') {
        enable_interrupts();
        return;
    }
    if (c == '\r') {
        x_cursor = LEFT_MARGIN;
        screen_clear_row();
        enable_interrupts();
        return;
    }
    x_cursor += 8 * SCALE_FACTOR;
    volatile uint32_t local_x_cursor = x_cursor;
    volatile uint32_t local_y_cursor = y_cursor;

    enable_interrupts();
    // @squiffy, whenever you'll see this: tbt libmoonshine
    for (int x = 0; x < (8 * SCALE_FACTOR); x++) {
        for (int y = 0; y < (8 * SCALE_FACTOR); y++) {
            if (font8x8_basic[c & 0x7f][y / SCALE_FACTOR] & (1 << (x / SCALE_FACTOR))) {
                uint32_t ind = (x + local_x_cursor) + ((y + local_y_cursor) * gRowPixels);
                uint32_t curcolor = basecolor;
                curcolor ^= 0xFFFFFFFF;
                gFramebuffer[ind] = curcolor;
            } else {
                uint32_t ind = (x + local_x_cursor) + ((y + local_y_cursor) * gRowPixels);
                uint32_t rcol = gFramebufferCopy[ind];
                gFramebuffer[ind] = colors_average(rcol, basecolor);
            }
        }
    }
    cache_clean(&gFramebuffer[y_cursor * gRowPixels], (1 + 8 * SCALE_FACTOR) * gRowPixels * 4);
}
void screen_write(const char* str)
{
    while (*str)
        screen_putc(*str++);
}
void screen_puts(const char* str)
{
    screen_write(str);
    screen_putc('\n');
}
void screen_mark_banner() {
    bannerHeight = y_cursor;
}

void screen_invert(const char *cmd, char *args) {
    for (int y = 0; y < gHeight; y++) {
        for (int x = 0; x < gWidth; x++) {
            gFramebuffer[x + y * gRowPixels] ^= 0xffffffff;
            gFramebufferCopy[x + y * gRowPixels] ^= 0xffffffff;
        }
    }
    basecolor ^= 0xffffffff;
    cache_clean(gFramebuffer, gHeight * gRowPixels * 4);
}

struct logo_position {
    uint32_t begin_x;
    uint32_t begin_y;
};
// x and y values are native coordinates on display
static void set_pixel(const struct logo_position *pos, const uint32_t x, const size_t y) {
    // gRowPixels: the amount of pixels in a row (width)
    uint32_t fb_index = ((pos->begin_y + y) * gRowPixels) + pos->begin_x + x;
    gFramebuffer[fb_index] ^= 0xFFFFFFFF;
}
// TODO: check for off-by-one errors
// also: do you really trust my floating point comparisons? or is it just magic?
//
// remember: x and y positions in here are real native coordinates on display
static void draw_logo(const struct logo_position *pos, const uint32_t size) {
    // top_radius/size = 8/128
    // This allows it to be scaled to any `size`,
    // while using position measurements based on pixels of 128x128 palera1n logo PNG
    const double top_radius = (size * 8 / 128);
    const double bottom_radius = (size * 36 / 128);

    // global center x TODO: rename?
    const double center_x = size / 2;

    // TOP CIRCLE
    for (uint32_t y = 0; y < top_radius; y++) {
        // the y coordinate of centerpoint of circle
        // TODO: rename? this is local not global
        const double center_y = top_radius;
        for (uint32_t x = 0; x < size; x++) {
            // circle
            if (((x - center_x) * (x - center_x)) + ((y - center_y) * (y - center_y)) <= (top_radius * top_radius)) {
                set_pixel(pos, x, y);
            }
        }
    }

    // TRANSITION
    const double radius_start = top_radius;
    const double radius_end = bottom_radius;

    const double transition_begin_y = top_radius;
    const double transition_end_y = size - bottom_radius;
    // difference in radii
    const double transition_radius_diff = radius_end - radius_start;
    // TODO: if size < 128, this might overwrite the same line multiple times, but who cares
    for (uint32_t y = transition_begin_y; y < transition_end_y; y++) {
        // the y value through the transition; remember that y is not starting at 0
        const double transition_pos = y - transition_begin_y;
        // how far through the transition we are
        const double transition_factor = transition_pos / (transition_end_y - transition_begin_y);
        //mvprintw(y, 0, "%f", transition_length);
        //continue;
        // could be const but that's confusing if you read it in English
        const double radius = radius_start + (transition_factor * transition_radius_diff);

        const double x_start = center_x - radius;
        const double x_end = center_x + radius;
        for (uint32_t x = x_start + 1; x < x_end; x++) { // TODO: why does this need to be + 1, probably because < instead of <=? since otherwise off by one on left side only
            set_pixel(pos, x, y);
        }
    }

    // BOTTOM CIRCLE
    for (uint32_t y = transition_end_y; y < size; y++) {
        // TODO: rename? this is local not global
        const double center_y = size - bottom_radius;
        for (uint32_t x = 0; x < size; x++) {
            if (((x - center_x) * (x - center_x)) + ((y - center_y) * (y - center_y)) <= (bottom_radius * bottom_radius)) {
                set_pixel(pos, x, y);
            }
        }
    }
}
void screen_init() {
    gRowPixels = gBootArgs->Video.v_rowBytes >> 2;
    uint16_t width = gWidth = gBootArgs->Video.v_width;
    uint16_t height = gHeight = gBootArgs->Video.v_height;
    uint64_t fbbase = gBootArgs->Video.v_baseAddr;
    uint64_t fbsize = gHeight * gRowPixels * 4;
    uint64_t fboff;
    if(is_16k())
    {
        fboff  = fbbase & 0x3fffULL;
        fbsize = (fbsize + fboff + 0x3fffULL) & ~0x3fffULL;
    }
    else
    {
        fboff  = fbbase & 0xfffULL;
        fbsize = (fbsize + fboff + 0xfffULL) & ~0xfffULL;
    }
    map_range(0xfb0000000ULL, fbbase - fboff, fbsize, 3, 1, true);
    gFramebuffer = (uint32_t*)(0xfb0000000ULL + fboff);
    gFramebufferCopy = (uint32_t*)alloc_contig(fbsize);

    height &= 0xfff0;
    scale_factor = 2;
    if (width > 800)
        scale_factor = 3;

    if (width > height) scale_factor = 1;

    uint32_t logo_scaler_factor = 2 * scale_factor;
    if (socnum == 0x8012) logo_scaler_factor = 1;

    struct logo_position log_pos = {
        .begin_x = (gRowPixels / 2) - (16 * logo_scaler_factor),
        .begin_y = (height / 2) - (16 * logo_scaler_factor),
    };

    draw_logo(&log_pos, 32 * logo_scaler_factor);
    
    memcpy(gFramebufferCopy, gFramebuffer, fbsize);

    basecolor = gFramebuffer[0];
    cache_clean(gFramebuffer, gHeight * gRowPixels * 4);
    command_register("fbclear", "clears the framebuffer output (minus banner)", screen_clear_all);
    command_register("fbinvert", "inverts framebuffer contents", screen_invert);
    scale_factor = 1;
}
