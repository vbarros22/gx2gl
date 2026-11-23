#ifndef GX2GL_GL_STATS_H
#define GX2GL_GL_STATS_H

#include "gx2gl/stats.h"

#ifdef __WIIU__
#include <coreinit/time.h>
#define GL_STATS_NOW() OSGetTime()
#define GL_STATS_US(t) ((uint32_t)OSTicksToMicroseconds(t))
typedef OSTime gl_stats_time_t;
#else
#define GL_STATS_NOW() 0
#define GL_STATS_US(t) 0u
typedef long long gl_stats_time_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern GX2GL_FrameStats g_gl_frame_stats;
void gl_stats_end_frame(uint32_t finish_us);
/* Waiting for the previous frame to be shown, in this frame. */
void gl_stats_set_vsync(uint32_t vsync_us);
#ifdef __cplusplus
}
#endif

/* Runs stmt, counting it in g_gl_frame_stats.count and its time in .us. */
#define GL_STATS_TIMED(count, us, stmt)                                     \
    do {                                                                    \
        gl_stats_time_t gl_stats_start_ = GL_STATS_NOW();                   \
        stmt;                                                               \
        g_gl_frame_stats.count++;                                           \
        uint32_t gl_stats_us_ = GL_STATS_US(GL_STATS_NOW() - gl_stats_start_); \
        g_gl_frame_stats.us += gl_stats_us_;                                \
        if (gl_stats_us_ > g_gl_frame_stats.slowest_us) {                   \
            g_gl_frame_stats.slowest_us = gl_stats_us_;                     \
            g_gl_frame_stats.slowest_call = #stmt;                          \
        }                                                                   \
    } while (0)

#ifdef __WIIU__
#include <gx2/event.h>
/* GX2DrawDone(), timed into g_gl_frame_stats; the macro below routes the
 * calls of every file that includes this header after the GX2 ones. */
/* The surface the next GX2DrawDone() waits to use, for the wait log. */
extern uint32_t g_gl_wait_tag_w, g_gl_wait_tag_h, g_gl_wait_tag_fmt;
static inline void gl_stats_draw_done(const char *file, int line)
{
    gl_stats_time_t start = GL_STATS_NOW();
    (GX2DrawDone)();
    uint32_t us = GL_STATS_US(GL_STATS_NOW() - start);
    g_gl_frame_stats.gpu_waits++;
    g_gl_frame_stats.gpu_wait_us += us;
    if (us > g_gl_frame_stats.slowest_wait_us) {
        g_gl_frame_stats.slowest_wait_us = us;
        g_gl_frame_stats.slowest_wait_file = file;
        g_gl_frame_stats.slowest_wait_line = line;
    }
    if (g_gl_frame_stats.wait_log_count < 32) {
        uint32_t i = g_gl_frame_stats.wait_log_count++;
        g_gl_frame_stats.wait_log[i].us = us;
        g_gl_frame_stats.wait_log[i].line = line;
        g_gl_frame_stats.wait_log[i].width = g_gl_wait_tag_w;
        g_gl_frame_stats.wait_log[i].height = g_gl_wait_tag_h;
        g_gl_frame_stats.wait_log[i].format = g_gl_wait_tag_fmt;
    }
    g_gl_wait_tag_w = g_gl_wait_tag_h = g_gl_wait_tag_fmt = 0;
}
#define GX2DrawDone() gl_stats_draw_done(__FILE__, __LINE__)
#endif

#endif
