/* gx2gl uniform diagnostics: matrices and loose uniforms, with and without a
 * geometry shader. Every panel draws one triangle in its own cell; a missing
 * (or black) triangle names the broken path.
 *
 *   M1  no GS,  VS translates with one mat4
 *   M2  no GS,  VS uses two mat4 (identity * translation): the second one's offset
 *   M3  GS,     VS translates with one mat4, pass-through GS
 *   M4  GS,     VS adds a vec4 offset, pass-through GS (vector, not matrix)
 *   M5  GS,     plain VS, GS translates with a mat4 only the GS declares
 *   M6  GS,     VS and GS share a mat4: GS translates, VS scales colour by [3][3]
 *
 * The background is mid grey so a black triangle (colour lost) is still visible.
 * Y toggles depth testing (with a depth clear) to check depth on the default
 * framebuffer: the triangles must stay visible either way.
 * Log: sd:/gx2gl-matrix-test.log
 */
#include "../common/example_util.h"

#include <gx2gl/present.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#include <vector>

#ifndef GX2GL_BUILD_ID
#define GX2GL_BUILD_ID "local"
#endif

using example::Log;
using example::Mat4;

static const char kPlainVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kOneMatrixVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uPlace;
void main() { vColor = aColor; gl_Position = uPlace * vec4(aPosition, 0.0, 1.0); }
)";
static const char kTwoMatrixVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uIdentity;
uniform mat4 uPlace;
void main() { vColor = aColor; gl_Position = uIdentity * uPlace * vec4(aPosition, 0.0, 1.0); }
)";
static const char kVectorVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec4 uShift;
void main() { vColor = aColor; gl_Position = vec4(aPosition, 0.0, 1.0) + uShift; }
)";
static const char kSharedVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uPlace;
void main() { vColor = aColor * uPlace[3][3]; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kFS[] = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";
static const char kGeometryFS[] = R"(
#version 330 core
in vec4 gColor;
out vec4 fragColor;
void main() { fragColor = gColor; }
)";
static const char kPassGS[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
void main() {
   for (int i = 0; i < 3; ++i) { gColor = vColor[i]; gl_Position = gl_in[i].gl_Position; EmitVertex(); }
   EndPrimitive();
}
)";
static const char kPlaceGS[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
uniform mat4 uPlace;
void main() {
   for (int i = 0; i < 3; ++i) { gColor = vColor[i]; gl_Position = uPlace * gl_in[i].gl_Position; EmitVertex(); }
   EndPrimitive();
}
)";

struct Panel {
   const char *name;
   GLuint program;
   int cell;
};

int main(int, char **)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-matrix-test.log");
   Log("gx2gl matrix test, build %s", GX2GL_BUILD_ID);

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }

   using example::CompileShader;
   using example::LinkProgram;
   const GLuint plain_vs = CompileShader("plain-vs", GL_VERTEX_SHADER, kPlainVS);
   const GLuint one_vs = CompileShader("one-matrix-vs", GL_VERTEX_SHADER, kOneMatrixVS);
   const GLuint two_vs = CompileShader("two-matrix-vs", GL_VERTEX_SHADER, kTwoMatrixVS);
   const GLuint vector_vs = CompileShader("vector-vs", GL_VERTEX_SHADER, kVectorVS);
   const GLuint shared_vs = CompileShader("shared-vs", GL_VERTEX_SHADER, kSharedVS);
   const GLuint fs = CompileShader("fs", GL_FRAGMENT_SHADER, kFS);
   const GLuint gfs = CompileShader("gs-fs", GL_FRAGMENT_SHADER, kGeometryFS);
   const GLuint pass_gs = CompileShader("pass-gs", GL_GEOMETRY_SHADER, kPassGS);
   const GLuint place_gs = CompileShader("place-gs", GL_GEOMETRY_SHADER, kPlaceGS);

   const Panel panels[] = {
      {"M1", LinkProgram("M1", one_vs, 0, fs, false), 0},
      {"M2", LinkProgram("M2", two_vs, 0, fs, false), 1},
      {"M3", LinkProgram("M3", one_vs, pass_gs, gfs, true), 2},
      {"M4", LinkProgram("M4", vector_vs, pass_gs, gfs, true), 3},
      {"M5", LinkProgram("M5", plain_vs, place_gs, gfs, true), 4},
      {"M6", LinkProgram("M6", shared_vs, place_gs, gfs, true), 5},
   };

   /* One triangle around the origin; each program moves it to its cell. */
   const float vertices[] = {-0.22f, -0.3f, 1, 0, 0, 1,   0.22f, -0.3f, 0, 1, 0, 1,   0.0f, 0.3f, 0, 0, 1, 1};
   GLuint vao, vbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);

   const Mat4 identity = Mat4::Identity();
   for (const Panel &panel : panels) {
      if (!panel.program)
         continue;
      const float x = -0.66f + 0.66f * (float)(panel.cell % 3);
      const float y = 0.45f - 0.9f * (float)(panel.cell / 3);
      const Mat4 place = Mat4::Translation(x, y, 0.0f);
      glUseProgram(panel.program);
      const GLint place_location = glGetUniformLocation(panel.program, "uPlace");
      const GLint identity_location = glGetUniformLocation(panel.program, "uIdentity");
      const GLint shift_location = glGetUniformLocation(panel.program, "uShift");
      if (place_location >= 0) glUniformMatrix4fv(place_location, 1, GL_FALSE, place.m);
      if (identity_location >= 0) glUniformMatrix4fv(identity_location, 1, GL_FALSE, identity.m);
      if (shift_location >= 0) glUniform4f(shift_location, x, y, 0.0f, 0.0f);
      GLfloat readback[16] = {};
      if (place_location >= 0) glGetUniformfv(panel.program, place_location, readback);
      Log("%s: uPlace %d uIdentity %d uShift %d; uPlace reads back [12]=%.2f [13]=%.2f [15]=%.2f",
          panel.name, place_location, identity_location, shift_location, readback[12],
          readback[13], readback[15]);
      example::LogGLError(panel.name);
   }

   bool depth = false;
   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      if (example::PollGamePad() & VPAD_BUTTON_Y) {
         depth = !depth;
         Log("frame %u: depth test %s", frame, depth ? "on" : "off");
      }
      glClearColor(0.35f, 0.35f, 0.4f, 1.0f);
      if (depth) {
         glEnable(GL_DEPTH_TEST);
         glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      } else {
         glDisable(GL_DEPTH_TEST);
         glClear(GL_COLOR_BUFFER_BIT);
      }
      glBindVertexArray(vao);
      for (const Panel &panel : panels) {
         if (!panel.program)
            continue;
         glUseProgram(panel.program);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         if (frame == 0) {
            glFinish();
            example::LogGLError(panel.name);
            Log("frame 0: %s drawn", panel.name);
         }
      }
      glBindVertexArray(0);
      GX2GL_Present();
      ++frame;
   }
   Log("exiting after %u frames", frame);
   GX2GL_DeleteContext(context);
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
