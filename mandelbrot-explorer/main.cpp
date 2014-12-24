//
//  main.cpp
//  mandelbrot-explorer
//
//  Created by Michael Larson on 12/21/14.
//  Copyright (c) 2014 Sandstorm Software. All rights reserved.
//

#include <iostream>
#include <pthread.h>

//Using SDL and standard IO
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <OpenCL/cl.h>

#include <stdio.h>
#include <string>
#include <assert.h>

#define USE_BIGNUM

#ifdef USE_BIGNUM
#include "gmp.h"
#endif

//Screen dimension constants
const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 1024;
const int PALETTE_COUNT = 32;

typedef struct {
    int             done;
    double          center_x, center_y;
    unsigned        hy;
    unsigned        itermax;
    unsigned        xres;
    unsigned        yres;
    float           zoom;
    unsigned        flags;
    unsigned        *pixels;
} ScanLineInfo;

#define kUSE_BIGNUM 0x1

typedef struct WorkQueueEntry_t {
    volatile struct  WorkQueueEntry_t   *next;
    ScanLineInfo                        *scan_info;
} WorkQueueEntry;

struct {
    volatile unsigned           count;
    volatile WorkQueueEntry     *next;
    pthread_mutex_t             queue_lock;
    pthread_cond_t              cond;
} g_work_queue;

typedef struct ZoomView_t {
    struct ZoomView_t   *next;
    double              center_x, center_y, zoom;
    unsigned            color_gradient;
    unsigned            itermax;
    unsigned            flags;
    unsigned            xres, yres;
    unsigned            *pixels;
} ZoomView;

typedef struct {
    double          center_x, center_y;
    double          zoom;
    double          xres, yres;
    unsigned        itermax;
    unsigned        sample_count;
    int             pitch;
} CLWorkInfo;

typedef struct {
    double r;       // percent
    double g;       // percent
    double b;       // percent
} RGB_Color;

typedef struct {
    double h;       // angle in degrees
    double s;       // percent
    double v;       // percent
} HSV_Color;

typedef struct {
    unsigned        count;
    HSV_Color       control_colors[4];
    RGB_Color       *c;
} Palette;

enum {
    kRenderModeDouble,
#ifdef USE_BIGNUM
    kRenderModeBigNUM,
#endif
    kRenderModeOpenCL
};


unsigned            render_mode;
cl_device_id        device = NULL;
cl_context          context = 0;
cl_command_queue    queue = 0;

void *calcThread(void *ctx)
{
    while(1)
    {
        volatile WorkQueueEntry *entry = NULL;
        
        pthread_mutex_lock(&g_work_queue.queue_lock);
        pthread_cond_wait(&g_work_queue.cond, &g_work_queue.queue_lock);
        {
            if(g_work_queue.next)
            {
                entry = g_work_queue.next;
                
                g_work_queue.next = entry->next;
            }
        }
        pthread_mutex_unlock(&g_work_queue.queue_lock);
        
        if(entry)
        {
            ScanLineInfo *scan_info = entry->scan_info;
            
            double      cx, cy;
            double      x, xx;
            int         iteration;
            int         hx;
            double      zoom        = scan_info->zoom;
            double      center_x    = scan_info->center_x;
            double      center_y    = scan_info->center_y;
            double      xres        = scan_info->xres;
            double      yres        = scan_info->yres;
            double      hy          = scan_info->hy;
            unsigned    itermax     = scan_info->itermax;
            unsigned    *pixels     = scan_info->pixels;
#ifdef USE_BIGNUM
            bool        useBignum   = scan_info->flags & kUSE_BIGNUM;
#endif
            for (hx=0; hx<xres; hx++)
            {
                bool    done = false;
                double  y;
                
                cx = center_x + 3.0*((double)hx/xres-0.5)/zoom;
                cy = center_y + 3.0*(hy/yres-0.5)/zoom;
                x = 0.0; y=0.0;
                
#ifdef USE_BIGNUM
                if(useBignum)
                {
                    mpf_t   _x, _y;
                    mpf_t   _xx;
                    mpf_t   _cx, _cy;
                    mpf_t   _two, _tmp1, _tmp2;
                    
                    mpf_init(_x);
                    mpf_init(_y);
                    mpf_init(_xx);
                    mpf_init(_cx);
                    mpf_init(_cy);
                    mpf_init(_two);
                    mpf_init(_tmp1);
                    mpf_init(_tmp2);
                    
                    // cx = center_x + ((double)hx/xres-0.5)/zoom*3.0;
                    // cy = center_y + (hy/yres-0.5)/zoom*3.0;
                    // x = 0.0; y=0.0;
                    
                    mpf_set_d(_cx, center_x);
                    mpf_set_d(_tmp1, ((double)hx/xres-0.5)*3.0);
                    mpf_set_d(_tmp2, zoom);
                    mpf_div(_tmp1, _tmp1, _tmp2);
                    mpf_add(_cx, _cx, _tmp1);
                    
                    mpf_set_d(_cy, center_y);
                    mpf_set_d(_tmp1, ((double)hy/yres-0.5)*3.0);
                    mpf_set_d(_tmp2, zoom);
                    mpf_div(_tmp1, _tmp1, _tmp2);
                    mpf_add(_cy, _cy, _tmp1);
                    
                    mpf_set_d(_x, x);
                    mpf_set_d(_y, y);
                    mpf_set_d(_two, 2.0);
                    
                    for (iteration=1;!done && iteration<itermax;iteration++)
                    {
                        // xx = x*x-y*y+cx;
                        mpf_mul(_tmp1, _x, _x);
                        mpf_mul(_tmp2, _y, _y);
                        mpf_sub(_tmp1, _tmp1, _tmp2);
                        mpf_add(_xx, _tmp1, _cx);
                        
                        // y = 2.0*x*y+cy;
                        mpf_mul(_tmp1, _two, _x);
                        mpf_mul(_tmp1, _tmp1, _y);
                        mpf_add(_y, _tmp1, _cy);
                        
                        // x = xx;
                        mpf_set(_x, _xx);
                        
                        // x*x+y*y>100.0
                        mpf_mul(_tmp1, _x, _x);
                        mpf_mul(_tmp2, _y, _y);
                        mpf_add(_tmp1, _tmp1, _tmp2);
                        
                        if (mpf_get_d(_tmp1)>100.0)
                        {
                            done = true;
                        }
                    }
                    
                    if(done)
                    {
                        pixels[hx] = iteration;
                    }
                    else
                    {
                        pixels[hx] = 0;
                    }
                }
                else
#endif // #ifdef USE_BIGNUM
                {
                    for (iteration=1;!done && iteration<itermax;iteration++)
                    {
                        xx = x*x-y*y+cx;
                        y = 2.0*x*y+cy;
                        x = xx;
                        
                        if (x*x+y*y>100.0)
                        {
                            done = true;
                        }
                    }
                    
                    if(done)
                    {
                        pixels[hx] = iteration;
                    }
                    else
                    {
                        pixels[hx] = 0;
                    }
                }
            }
            
            scan_info->done = 1;
            
            pthread_mutex_lock(&g_work_queue.queue_lock);
            {
                g_work_queue.count--;
                free((void *)entry);
            }
            pthread_mutex_unlock(&g_work_queue.queue_lock);
        }
    }
    
    return NULL;
}

cl_device_id getCLDevice()
{
    cl_platform_id platforms[100];
    cl_uint platforms_n = 0;
    
    clGetPlatformIDs(100, platforms, &platforms_n);
    
    cl_device_id devices[100];
    cl_uint devices_n = 0;
    clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_GPU, 100, devices, &devices_n);
    
    char buffer[1024];
    for (int i=0; i<devices_n; i++)
    {
        clGetDeviceInfo(devices[i], CL_DEVICE_VENDOR, sizeof(buffer), buffer, NULL);
        
        if(!strcmp("NVIDIA", buffer))
        {
            clGetDeviceInfo(devices[i], CL_DEVICE_VENDOR, sizeof(buffer), buffer, NULL);
            
            return devices[i];
        }
    }
    
    return devices[0];
}

void cl_notify(const char *errinfo, const void *private_info, size_t cb, void *user_data)
{
    fprintf(stderr, "OpenCL Error (via pfn_notify): %s\n", errinfo);
}

HSV_Color RGB2HSV(RGB_Color in)
{
    HSV_Color   out;
    double      min, max, delta;
    
    min = in.r < in.g ? in.r : in.g;
    min = min  < in.b ? min  : in.b;
    
    max = in.r > in.g ? in.r : in.g;
    max = max  > in.b ? max  : in.b;
    
    out.v = max;                                // v
    delta = max - min;
    if( max > 0.0 ) { // NOTE: if Max is == 0, this divide would cause a crash
        out.s = (delta / max);                  // s
    } else {
        // if max is 0, then r = g = b = 0
        // s = 0, v is undefined
        out.s = 0.0;
        out.h = NAN;                            // its now undefined
        return out;
    }
    if( in.r >= max )                           // > is bogus, just keeps compilor happy
        out.h = ( in.g - in.b ) / delta;        // between yellow & magenta
    else
        if( in.g >= max )
            out.h = 2.0 + ( in.b - in.r ) / delta;  // between cyan & yellow
        else
            out.h = 4.0 + ( in.r - in.g ) / delta;  // between magenta & cyan
    
    out.h *= 60.0;                              // degrees
    
    if( out.h < 0.0 )
        out.h += 360.0;
    
    return out;
}

RGB_Color HSV2RGB(HSV_Color in)
{
    double      hh, p, q, t, ff;
    long        i;
    RGB_Color   out;
    
    if(in.s <= 0.0)
    {
        out.r = in.v;
        out.g = in.v;
        out.b = in.v;
        return out;
    }
    hh = in.h;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));
    
    switch(i)
    {
        case 0:
            out.r = in.v;
            out.g = t;
            out.b = p;
            break;
        case 1:
            out.r = q;
            out.g = in.v;
            out.b = p;
            break;
        case 2:
            out.r = p;
            out.g = in.v;
            out.b = t;
            break;
        case 3:
            out.r = p;
            out.g = q;
            out.b = in.v;
            break;
        case 4:
            out.r = t;
            out.g = p;
            out.b = in.v;
            break;
        case 5:
        default:
            out.r = in.v;
            out.g = p;
            out.b = q;
            break;
    }
    
    return out;
}

HSV_Color genBezierColor(float u, HSV_Color *cpts)
{
    HSV_Color   hsv;
    float       b0, b1, b2, b3;
    
    b0 = pow(1.0 - u, 3);
    b1 = 3.0 * u * pow(1.0 - u, 2);
    b2 = 3.0 * pow(u, 2) * (1.0 - u);
    b3 = pow(u, 3);
    
    hsv.h = b0 * cpts[0].h + b1 * cpts[1].h + b2 * cpts[2].h + b3 * cpts[3].h;
    hsv.s = b0 * cpts[0].s + b1 * cpts[1].s + b2 * cpts[2].s + b3 * cpts[3].s;
    hsv.v = b0 * cpts[0].v + b1 * cpts[1].v + b2 * cpts[2].v + b3 * cpts[3].v;
    
    return hsv;
}

RGB_Color genColorFromPalette(unsigned index, unsigned max, Palette *palette)
{
    if(index == 0)
    {
        RGB_Color   zero;
        
        zero.r = 0;
        zero.g = 0;
        zero.b = 0;
        
        return zero;
    }
    
    // deferred creation of palette if count is larger
    if(max > palette->count)
    {
        free(palette->c);
        
        palette->c = new RGB_Color [max];
        palette->count = max;
        
        for(int i=0; i<max; i++)
        {
            float u = (float)i / (float)max;
            HSV_Color hsv = genBezierColor(u, palette->control_colors);
            RGB_Color rgb = HSV2RGB(hsv);
            
            palette->c[i].r = rgb.r * 255.0;
            palette->c[i].g = rgb.g * 255.0;
            palette->c[i].b = rgb.b * 255.0;
        }
    }
    else if (max < palette->count)
    {
        float scale = (float)palette->count / (float)max;
        
        index = scale * index;
    }
    
    return palette->c[index];
}

int createPalettes(Palette *palettes)
{
    unsigned        palette_index;
    
    HSV_Color   zero;
    zero.h = 0.0;
    zero.s = 0.0;
    zero.v = 0.0;
    
    // greyscale
    palette_index = 0;
    
    for(int i=0; i<4; i++)
    {
        palettes[palette_index].control_colors[i] = zero;
    }
    
    palettes[palette_index].control_colors[0].v = 0.0;
    palettes[palette_index].control_colors[1].v = 1.0/3.0;
    palettes[palette_index].control_colors[2].v = 2.0/3.0;
    palettes[palette_index].control_colors[3].v = 1.0;
    
    palettes[palette_index].count = 256;
    palettes[palette_index].c     = new RGB_Color [256];
    
    for(int i=0; i<256; i++)
    {
        float u = (float)i / 255.0;
        HSV_Color hsv = genBezierColor(u, palettes[palette_index].control_colors);
        RGB_Color rgb = HSV2RGB(hsv);
        
        palettes[palette_index].c[i].r = rgb.r * 255.0;
        palettes[palette_index].c[i].g = rgb.g * 255.0;
        palettes[palette_index].c[i].b = rgb.b * 255.0;
    }
    
    palette_index++;
    
    // remaining
    for(int j=palette_index; j<PALETTE_COUNT; j++)
    {
        palettes[palette_index].count = 0;
        palette_index++;
    }
    
    palette_index = 0;
    
    return palette_index;
}

void drawFractalImage(SDL_Window* window, SDL_Surface *draw_surface, ZoomView *current_view, Palette *current_palette)
{
    SDL_Surface *screen_surface;
    
    SDL_LockSurface(draw_surface);
    
    for (int hy=0; hy<current_view->yres; hy++)
    {
        unsigned *dst_pixels = (unsigned *)draw_surface->pixels + (draw_surface->pitch >> 2) * hy;
        unsigned *src_pixels = &current_view->pixels[hy * current_view->xres];
        
        for (int hx=0; hx<current_view->xres; hx++)
        {
            RGB_Color rgb = genColorFromPalette(src_pixels[hx], current_view->itermax, current_palette);
            
            dst_pixels[hx] = SDL_MapRGBA(draw_surface->format,
                                         rgb.r,
                                         rgb.g,
                                         rgb.b,
                                         0);
        }
    }
    
    SDL_UnlockSurface(draw_surface);
    
    screen_surface = SDL_GetWindowSurface( window );
    
    SDL_SetSurfaceBlendMode(screen_surface, SDL_BLENDMODE_NONE);
    
    SDL_BlitSurface(draw_surface, NULL,
                    screen_surface, NULL);
}

bool runPaletteDialog(SDL_Window* window, SDL_Surface *draw_surface, ZoomView *current_view, Palette *current_palette)
{
    SDL_Surface     *screen_surface;
    SDL_Event       event;
    bool            finished;
    bool            update;
    float           value[4];
    unsigned        control_color;
    
    finished        = false;
    update          = true;
    control_color   = 0;
    
    for(int i=0; i<4; i++)
    {
        value[i] = current_palette->control_colors[i].v;
    }
    
    while(!finished)
    {
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_KEYDOWN:
                {
                    if(event.key.keysym.scancode == SDL_SCANCODE_P)
                    {
                        finished = 1;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_RIGHT)
                    {
                        value[control_color] += 0.1;
                        
                        if(value[control_color] > 1.0)
                        {
                            value[control_color] = 1.0;
                        }
                        
                        current_palette->control_colors[control_color].v = value[control_color];
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_LEFT)
                    {
                        value[control_color] -= 0.1;
                        
                        if(value[control_color] < 0.0)
                        {
                            value[control_color] = 0.0;
                        }
                        
                        current_palette->control_colors[control_color].v = value[control_color];
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_1)
                    {
                        control_color = 0;
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_2)
                    {
                        control_color = 1;
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_3)
                    {
                        control_color = 2;
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_4)
                    {
                        control_color = 3;
                        update = true;
                    }
                    
                    break;
                }
                    
                case SDL_MOUSEMOTION:
                    if(event.motion.state != SDL_PRESSED)
                    {
                        break;
                    }
                    
                case SDL_MOUSEBUTTONDOWN:
                {
                    float mouse_x = (double)event.button.x;
                    float mouse_y = (double)event.button.y;
                    
                    // figure out which circle it was in
                    update = true;
                    for(int control_circle=0; control_circle<4; control_circle++)
                    {
                        float cx = (2 * control_circle + 1) * current_view->xres / 8.0f;
                        float cy = current_view->yres / 8.0f;
                        
                        float x = cx - mouse_x;
                        float y = cy - mouse_y;
                        
                        float radius = current_view->xres / 12;
                        float dist = sqrtf(x*x + y*y);
                        
                        if(dist <= radius)
                        {
                            if(control_color != control_circle)
                            {
                                control_color = control_circle;
                                update = false;
                            }
                        }
                    }
                    
                    if(update)
                    {
                        float cx = (2 * control_color + 1) * current_view->xres / 8.0f;
                        float cy = current_view->yres / 8.0f;
                        
                        float x = cx - mouse_x;
                        float y = cy - mouse_y;
                        
                        float radius = current_view->xres / 12;
                        float dist = sqrtf(x*x + y*y);
                        
                        if(dist <= radius)
                        {
                            x /= dist;
                            y /= dist;
                            
                            float theta = acosf(x) * 180.0 / M_PI;
                            
                            if (y < 0)
                            {
                                theta = 360 - theta;
                            }
                            
                            current_palette->control_colors[control_color].h = theta;
                            current_palette->control_colors[control_color].s = dist / radius;
                            current_palette->control_colors[control_color].v = value[control_color];
                        }
                    }
                    
                    break;
                }
                    
                case SDL_QUIT:
                {
                    return true;
                }
            }
        }
        
        // draw base surface
        if(update)
        {
            SDL_LockSurface(draw_surface);
            
            HSV_Color   hsv;
            RGB_Color   rgb;
            float       dist;
            float       radius = current_view->xres / 12;
            
            current_palette->count = 256;
            for (int i=0; i<256; i++)
            {
                float   u = (float)i / 255.0;
                
                hsv = genBezierColor(u, current_palette->control_colors);
                rgb = HSV2RGB(hsv);
                
                current_palette->c[i].r = rgb.r * 255.0;
                current_palette->c[i].g = rgb.g * 255.0;
                current_palette->c[i].b = rgb.b * 255.0;
            }
            
            // draw image
            drawFractalImage(window, draw_surface, current_view, current_palette);
            
            // draw HSV palette
            for (int hy=0; hy<32; hy++)
            {
                unsigned *dst_pixels = (unsigned *)draw_surface->pixels + (draw_surface->pitch >> 2) * hy;
                
                for (int hx=0; hx<current_view->xres; hx++)
                {
                    float   u = (float)hx / (float)current_view->xres;
                    HSV_Color hsv = genBezierColor(u, current_palette->control_colors);
                    RGB_Color rgb = HSV2RGB(hsv);
                    
                    dst_pixels[hx] = SDL_MapRGBA(draw_surface->format,
                                                 rgb.r * 255.0,
                                                 rgb.g * 255.0,
                                                 rgb.b * 255.0,
                                                 0);
                }
            }
            
            // draw HSV control circles
            for(int control_circle=0; control_circle<4; control_circle++)
            {
                float   cx = (2 * control_circle + 1) * current_view->xres / 8.0f;
                float   cy = current_view->yres / 8.0f;
                
                for (int hy=0; hy<current_view->yres; hy++)
                {
                    unsigned *dst_pixels = (unsigned *)draw_surface->pixels + (draw_surface->pitch >> 2) * hy;
                    
                    for (int hx=0; hx<current_view->xres; hx++)
                    {
                        float  x = cx - hx;
                        float  y = cy - hy;
                        
                        dist = sqrtf(x*x + y*y);
                        
                        if(dist <= radius)
                        {
                            x /= dist;
                            y /= dist;
                            
                            float theta = acosf(x) * 180.0 / M_PI;
                            
                            if (y < 0)
                            {
                                theta = 360 - theta;
                            }
                            
                            hsv.h = theta;
                            hsv.s = dist / radius;
                            hsv.v = value[control_circle];
                            
                            rgb = HSV2RGB(hsv);
                            
                            dst_pixels[hx] = SDL_MapRGBA(draw_surface->format,
                                                         rgb.r * 255.0,
                                                         rgb.g * 255.0,
                                                         rgb.b * 255.0,
                                                         0);
                        }
                    }
                }
                
                
                // draw hsv value in circle
                hsv = current_palette->control_colors[control_circle];
                
                float x = cx + radius * cos(hsv.h / (180.0 / M_PI)) * hsv.s;
                float y = cy + radius * sin(hsv.h / (180.0 / M_PI)) * hsv.s;
                
                unsigned *dst_pixels = (unsigned *)draw_surface->pixels + (draw_surface->pitch >> 2) * (int)y;
                
                dst_pixels[(int)x] = SDL_MapRGBA(draw_surface->format,
                                                 0,
                                                 0,
                                                 0,
                                                 0);
                
                SDL_UnlockSurface(draw_surface);
                
                SDL_SetSurfaceBlendMode(draw_surface, SDL_BLENDMODE_NONE);
                
                // blt the draw surface to the screen
                screen_surface = SDL_GetWindowSurface( window );
                SDL_SetSurfaceBlendMode(screen_surface, SDL_BLENDMODE_NONE);
                
                SDL_BlitSurface(draw_surface, NULL,
                                screen_surface, NULL);
                
                update = false;
            }
        }
        
        SDL_UpdateWindowSurface( window );
    }
    
    return false;
}

void updateLoop(SDL_Window* window, SDL_Surface *draw_surface)
{
    SDL_Surface     *screen_surface;
    SDL_Event       event;
    ScanLineInfo    *scanline_info;
    ZoomView        *views;
    int             xres, yres;
    double          mouse_x, mouse_y;
    double          repeat;
    bool            update, redraw;
    bool            finished;
    bool            draw_lines;
    unsigned        zoom_index;
    unsigned        palette_index;
    Palette         *palettes;
    
    screen_surface  = NULL;
    
    xres       = SCREEN_WIDTH;
    yres       = SCREEN_HEIGHT;
    mouse_x     = 0.0;
    mouse_y     = 0.0;
    repeat      = 0.0;
    
    update          = true;
    redraw          = false;
    finished        = false;
    draw_lines      = true;
    render_mode     = kRenderModeOpenCL;
    palette_index   = 0;
    palettes        = new Palette [PALETTE_COUNT];
    
    views                               = new ZoomView [1024];
    zoom_index                          = 0;
    views[zoom_index].next              = NULL;
    views[zoom_index].xres              = SCREEN_WIDTH;
    views[zoom_index].yres              = SCREEN_HEIGHT;
    views[zoom_index].center_x          = -0.7;
    views[zoom_index].center_y          = 0.0;
    views[zoom_index].zoom              = 1.0;
    views[zoom_index].pixels            = new unsigned [SCREEN_WIDTH * SCREEN_HEIGHT];
    views[zoom_index].color_gradient    = 4;
    views[zoom_index].itermax           = 256;
    
    scanline_info = new ScanLineInfo [SCREEN_HEIGHT];
    
    g_work_queue.next = NULL;
    g_work_queue.count = 0;
    
    pthread_mutex_init(&g_work_queue.queue_lock, NULL);
    pthread_cond_init(&g_work_queue.cond, NULL);
    
    pthread_t threads[8];
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    for(int i=0; i<8; i++)
    {
        pthread_create(&threads[i], &attr, calcThread, NULL);
    }
    
    // init globals
    device  = getCLDevice();
    context = 0;
    queue   = 0;

    cl_program          program = 0;
    cl_kernel           kernel = 0;
    
    if(device)
    {
        cl_int _err = CL_INVALID_VALUE;
        
        context = clCreateContext(NULL, 1, &device, &cl_notify, NULL, &_err);
        
        const char *program_source[] = {
            "typedef struct {\n"
            "    double         center_x, center_y;\n"
            "    double         zoom;\n"
            "    double         xres, yres;\n"
            "    unsigned       itermax;\n"
            "    unsigned       sample_count;\n"
            "    int            pitch;\n"
            "} CLWorkInfo;\n"
            "\n"
            "__kernel void mandelbrot(__global CLWorkInfo *info, __global int *dst)\n",
            "{\n",
            "	int xi = get_global_id(0);\n",
            "	int yi = get_global_id(1);\n",
            "   int iteration;\n"
            "   int itermax = info->itermax;\n"
            "   int pitch = info->pitch;\n"
            "   int done = 0;\n"
            "   double x, y, xx, cx, cy;\n"
            "\n"
            "   cx = info->center_x + 3.0*(xi/info->xres-0.5f)/info->zoom;\n"
            "   cy = info->center_y + 3.0*(yi/info->yres-0.5f)/info->zoom;\n"
            "   x = 0.0; y=0.0;\n"
            "\n"
            "   for (iteration=1;!done && iteration<itermax;iteration++)\n"
            "   {\n"
            "       xx = x*x-y*y+cx;\n"
            "       y = 2.0*x*y+cy;\n"
            "       x = xx;\n"
            "\n"
            "       if (x*x+y*y>100.0)\n"
            "       {\n"
            "           done = true;\n"
            "       }\n"
            "   }\n"
            "\n"
            "   if (done)\n"
            "   {\n"
            "       dst[yi * pitch + xi] = iteration;\n"
            "   }\n"
            "   else\n"
            "   {\n"
            "       dst[yi * pitch + xi] = 0;\n"
            "   }\n"
            "}\n"
        };
        
        program = clCreateProgramWithSource(context, sizeof(program_source)/sizeof(*program_source), program_source, NULL, &_err);
        
        if (clBuildProgram(program, 1, &device, "", NULL, NULL) != CL_SUCCESS)
        {
            char *buffer;
            size_t buffer_size;
            
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &buffer_size);
            
            buffer = (char *)malloc(buffer_size + 1);
            
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, buffer_size, buffer, NULL);
            fprintf(stderr, "CL Compilation failed:\n%s", buffer);
            abort();
        }
        
        kernel = clCreateKernel(program, "mandelbrot", &_err);
        
        queue = clCreateCommandQueue(context, device, 0, &_err);
    }
    
    palette_index = createPalettes(palettes);
    
    while(!finished)
    {
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_KEYDOWN:
                {
                    if(event.key.keysym.scancode == SDL_SCANCODE_Z)
                    {
                        views[zoom_index+1] = views[zoom_index];
                        zoom_index++;
                        
                        views[zoom_index].zoom++;
                        views[zoom_index].pixels    = new unsigned [SCREEN_WIDTH * SCREEN_HEIGHT];
                        
                        if(event.key.repeat)
                        {
                            views[zoom_index].zoom *= 2;
                        }
                        
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_O)
                    {
                        if(zoom_index)
                        {
                            free(views[zoom_index].pixels);
                            zoom_index--;
                        }
                        
                        if(event.key.repeat)
                        {
                            if(zoom_index)
                            {
                                free(views[zoom_index].pixels);
                                zoom_index--;
                            }
                        }
                        
                        redraw = true;
                    }
#ifdef USE_BIGNUM
                    else if(event.key.keysym.scancode == SDL_SCANCODE_B)
                    {
                        render_mode = kRenderModeBigNUM;
                        
                        update = true;
                    }
#endif
                    else if(event.key.keysym.scancode == SDL_SCANCODE_C)
                    {
                        render_mode = kRenderModeOpenCL;
                        
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_D)
                    {
                        render_mode = kRenderModeDouble;
                        
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_G)
                    {
                        if(views[zoom_index].color_gradient < 8)
                            views[zoom_index].color_gradient++;
                        
                        redraw = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_H)
                    {
                        if(views[zoom_index].color_gradient > 2)
                            views[zoom_index].color_gradient--;
                        
                        redraw = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_I)
                    {
                        views[zoom_index].itermax *= 2;
                        
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_P)
                    {
                        finished = runPaletteDialog(window, draw_surface, &views[zoom_index], &palettes[palette_index]);
                        
                        if(!finished)
                        {
                            redraw = true;
                            update = true;
                        }
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_L)
                    {
                        draw_lines = !draw_lines;
                        
                        redraw = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_U)
                    {
                        if(views[zoom_index].itermax > 64)
                            views[zoom_index].itermax /= 2;
                        
                        update = true;
                    }
                    else if(event.key.keysym.scancode == SDL_SCANCODE_S)
                    {
                        char filename[1024];
                        FILE *fptr;
                        
                        sprintf(filename, "/tmp/Mandelbrot_%s%1.16g_%s%01.16g_%s%1.16g.tga",
                                (views[zoom_index].center_x < 0.0 ? "neg_" : ""), fabs(views[zoom_index].center_x),
                                (views[zoom_index].center_y < 0.0 ? "neg_" : ""), fabs(views[zoom_index].center_y),
                                (views[zoom_index].zoom < 0.0 ? "neg_" : ""), fabs(views[zoom_index].zoom));
                        
                        printf("Saving to file %s\n", filename);
                        
                        fptr = fopen(filename, "wb");
                        
                        if(fptr)
                        {
                            putc(0,fptr);
                            putc(0,fptr);
                            putc(2,fptr);                         /* uncompressed RGB */
                            putc(0,fptr); putc(0,fptr);
                            putc(0,fptr); putc(0,fptr);
                            putc(0,fptr);
                            putc(0,fptr); putc(0,fptr);           /* X origin */
                            putc(0,fptr); putc(0,fptr);           /* y origin */
                            putc((SCREEN_WIDTH & 0x00FF),fptr);
                            putc((SCREEN_WIDTH & 0xFF00) / 256,fptr);
                            putc((SCREEN_HEIGHT & 0x00FF),fptr);
                            putc((SCREEN_HEIGHT & 0xFF00) / 256,fptr);
                            putc(24,fptr);                        /* 24 bit bitmap */
                            putc(0,fptr);
                            
                            for (int hy=0; hy<yres; hy++)
                            {
                                unsigned *src_pixels = &views[zoom_index].pixels[hy * xres];
                                
                                for (int hx=0; hx<xres; hx++)
                                {
                                    if((hx == SCREEN_WIDTH / 2) ||
                                       (hy == SCREEN_HEIGHT / 2))
                                    {
                                        putc((int)(0x3f),fptr);
                                        putc((int)(0x3f),fptr);
                                        putc((int)(0x3f),fptr);
                                    }
                                    else
                                    {
                                        unsigned mask = (0xff << (8 - views[zoom_index].color_gradient)) & 0xff;
                                        unsigned color = src_pixels[hx] & mask;
                                        
                                        putc((int)(color),fptr);
                                        putc((int)(color),fptr);
                                        putc((int)(color),fptr);
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                    
                case SDL_MOUSEBUTTONDOWN:
                {
                    double mouse_x = (double)event.button.x;
                    double mouse_y = (double)event.button.y;
                    mouse_x = (((float)mouse_x)/((float)SCREEN_WIDTH)-0.5)/views[zoom_index].zoom*3.0;
                    mouse_y = (((float)mouse_y)/((float)SCREEN_HEIGHT)-0.5)/views[zoom_index].zoom*3.0;
                    
                    views[zoom_index+1] = views[zoom_index];
                    zoom_index++;
                    
                    views[zoom_index].center_x += mouse_x;
                    views[zoom_index].center_y += mouse_y;
                    views[zoom_index].pixels    = new unsigned [SCREEN_WIDTH * SCREEN_HEIGHT];
                    
                    update = true;
                    break;
                }
                    
                case SDL_QUIT:
                {
                    /* Set whatever flags are necessary to */
                    /* end the main game loop here */
                    finished = true;
                    break;
                }
            }
        }
        
        if(update)
        {
            if(render_mode == kRenderModeOpenCL)
            {
                CLWorkInfo  workInfo;
                cl_mem      input_buffer = 0;
                cl_mem      output_buffer = 0;
                cl_int      _err = CL_INVALID_VALUE;
                
                workInfo.center_x   = views[zoom_index].center_x;
                workInfo.center_y   = views[zoom_index].center_y;
                workInfo.zoom       = views[zoom_index].zoom;
                workInfo.xres       = xres;
                workInfo.yres       = yres;
                workInfo.itermax    = views[zoom_index].itermax;
                workInfo.pitch      = xres;
                
                cl_event    kernel_completion;
                size_t      global_work_size[2] = { (size_t)xres, (size_t)yres };
                size_t      pbuffer_size = yres * xres * sizeof(unsigned);
                
                input_buffer = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(CLWorkInfo), &workInfo, &_err);
                assert(input_buffer);
                output_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, pbuffer_size, NULL, &_err);
                assert(output_buffer);
                
                clSetKernelArg(kernel, 0, sizeof(input_buffer), &input_buffer);
                clSetKernelArg(kernel, 1, sizeof(output_buffer), &output_buffer);
                
                clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size, NULL, 0, NULL, &kernel_completion);
                clWaitForEvents(1, &kernel_completion);
                clReleaseEvent(kernel_completion);
                
                clEnqueueReadBuffer(queue, output_buffer, CL_TRUE, 0, pbuffer_size, views[zoom_index].pixels, 0, NULL, NULL);
                
                clReleaseMemObject(input_buffer);
                clReleaseMemObject(output_buffer);
            }
            else
            {
                for (int hy=0; hy<yres; hy++)
                {
                    scanline_info[hy].center_x      = views[zoom_index].center_x;
                    scanline_info[hy].center_y      = views[zoom_index].center_y;
                    scanline_info[hy].hy            = hy;
                    scanline_info[hy].itermax       = views[zoom_index].itermax;
                    scanline_info[hy].xres         = xres;
                    scanline_info[hy].yres         = yres;
                    scanline_info[hy].zoom          = views[zoom_index].zoom;
#ifdef USE_BIGNUM
                    if(render_mode == kRenderModeBigNUM)
                    {
                        scanline_info[hy].flags     |= kUSE_BIGNUM;
                    }
#endif
                    scanline_info[hy].pixels        = &views[zoom_index].pixels[hy * xres];
                    scanline_info[hy].done          = 0;
                    
                    volatile WorkQueueEntry *entry = (WorkQueueEntry *)malloc(sizeof(WorkQueueEntry));
                    
                    pthread_mutex_lock(&g_work_queue.queue_lock);
                    {
                        entry->scan_info = &scanline_info[hy];
                        entry->next = g_work_queue.next;
                        g_work_queue.next = entry;
                        g_work_queue.count++;
                        pthread_cond_signal(&g_work_queue.cond);
                    }
                    pthread_mutex_unlock(&g_work_queue.queue_lock);
                }
                
                while(g_work_queue.count)
                {
                    pthread_mutex_lock(&g_work_queue.queue_lock);
                    pthread_cond_signal(&g_work_queue.cond);
                    pthread_mutex_unlock(&g_work_queue.queue_lock);
                }
            }
        }
        
        if(update || redraw)
        {
            int         hx, hy;
            float       color_scale = 1.0;
            
            if(views[zoom_index].itermax > 255)
            {
                color_scale = 255.0 / (float)views[zoom_index].itermax;
            }
            
            // draw image
            drawFractalImage(window, draw_surface, &views[zoom_index], &palettes[palette_index]);
            
            if(draw_lines)
            {
                unsigned *surface_pixels;
                
                screen_surface = SDL_GetWindowSurface( window );
                surface_pixels = (unsigned *)screen_surface->pixels;
                
                unsigned draw_color = 0xffffffff;
                switch(render_mode)
                {
                    case kRenderModeDouble:
                        draw_color = 0xff;
                        break;
                        
#ifdef USE_BIGNUM
                    case kRenderModeBigNUM:
                        draw_color = 0xff00;
                        break;
#endif
                    case kRenderModeOpenCL:
                        draw_color = 0xff0000;
                        break;
                }
                
                unsigned *dst = &surface_pixels[(yres / 2) * (screen_surface->pitch >> 2)];
                for(hx=0; hx<xres; hx++)
                {
                    dst[hx] = draw_color;
                }
                
                dst = surface_pixels;
                for(hy=0; hy<yres; hy++)
                {
                    dst[xres / 2] = draw_color;
                    dst += (screen_surface->pitch >> 2);
                }
            }
            
            //Update the surface
            SDL_UpdateWindowSurface( window );
            
            update = false;
            redraw = false;
        }
    }
}

int main(int argc, const char * argv[])
{
    //The window we'll be rendering to
    SDL_Window* window = NULL;
    
    SDL_Surface *drawSurface = SDL_CreateRGBSurface(0,
                                                    SCREEN_WIDTH, SCREEN_HEIGHT,
                                                    32, 0x0, 0x0, 0x0, 0x0);
    
    //Initialize SDL
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 )
    {
        printf( "SDL could not initialize! SDL_Error: %s\n", SDL_GetError() );
    }
    else
    {
        //Create window
        window = SDL_CreateWindow( "Mandelbrot Explorer", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED,
                                  SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL
                                  | SDL_WINDOW_SHOWN );
        
        if( window == NULL )
        {
            printf( "Window could not be created! SDL Error: %s\n",
                   SDL_GetError() );
        }
        else
        {
            updateLoop(window, drawSurface);
        }
    }
    
    //Destroy window
    SDL_DestroyWindow( window );
    
    //Quit SDL subsystems
    SDL_Quit();
    
    return 0;
}
