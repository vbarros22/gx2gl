#ifndef GX2GL_STATS_H
#define GX2GL_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a frame spent inside gx2gl: how many calls of each kind, and the CPU
 * time they took in microseconds. finish_us is GX2GL_Present() waiting for
 * the GPU to finish the frame, vsync_us waiting for it to be on screen
 * before the next frame starts.
 */
typedef struct GX2GL_FrameStats
{
    uint32_t draws, draw_us;
    uint32_t buffer_uploads, buffer_bytes, buffer_us;
    uint32_t uniform_calls, uniform_us;
    uint32_t texture_binds, texture_bind_us;
    uint32_t texture_uploads, texture_upload_us;
    uint32_t texture_storage_us; /* of which (re)allocating texture storage */
    uint32_t texture_copy_us;    /* of which converting and copying texels */
    uint32_t mipmap_generations, mipmap_us;
    uint32_t shader_compiles, program_links, shader_us;
    uint32_t program_switches, program_switch_us;
    uint32_t clears, clear_us;
    uint32_t other_calls, other_us; /* every other GL call */
    uint32_t finish_us;
    uint32_t vsync_us; /* GX2GL_Present() waiting for the previous frame to be shown */
    /* The single slowest GL call of the frame, by its dispatch statement. */
    uint32_t slowest_us;
    const char *slowest_call;
    /* GX2DrawDone() inside gx2gl: total wait, and the longest one's place. */
    uint32_t gpu_waits, gpu_wait_us;
    uint32_t slowest_wait_us;
    const char *slowest_wait_file;
    int slowest_wait_line;
    /* Every GX2DrawDone() of the frame, in order (the first 32): where, how
     * long, and the surface it was waiting to use (0x0 when unknown). */
    uint32_t wait_log_count;
    struct {
        uint32_t us;
        int line;
        uint32_t width, height, format;
    } wait_log[32];
} GX2GL_FrameStats;

/* The counts of the last frame GX2GL_Present() finished. */
void GX2GL_GetFrameStats(GX2GL_FrameStats *stats);

/* Name of the gx2gl function that raised the most recent first GL error
 * (the one glGetError returns next), or "" if none was recorded. */
const char *GX2GL_GetLastErrorSite(void);

#ifdef __cplusplus
}
#endif

#endif
