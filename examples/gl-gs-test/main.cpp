/* gx2gl geometry shader test, written against the OpenGL API only.
 *
 * Three panels, left to right, on the TV (mirrored to the GamePad):
 *   A  plain vertex + fragment program (control)
 *   B  a triangle through a pass-through geometry shader that scales it with a
 *      uniform only the geometry shader declares (uGeometryScale = 1.0)
 *   C  four points expanded into squares by a geometry shader that reads its half
 *      size from a uniform buffer and its tint from a uniform the vertex shader
 *      also declares (uTint = white). Black squares mean the tint did not reach
 *      the geometry shader; no squares mean the buffer did not.
 *
 * B starts after ~2 s and C after ~4 s. Every step goes to sd:/gx2gl-gs-test.log,
 * reopened per line so it survives a GPU hang.
 */
#include <gl/gl.h>
#include <gx2gl/present.h>

#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>

#ifndef GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER
#define GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER 0x8A45
#endif

static std::string g_log_path;

static void Log(const char *format, ...)
{
   char line[2048];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);

   WHBLogPrintf("%s", line);
   if (g_log_path.empty())
      return;
   if (FILE *file = fopen(g_log_path.c_str(), "a")) {
      fprintf(file, "%s\n", line);
      fclose(file);
   }
}

static void LogGLError(const char *where)
{
   for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
      Log("  GL error 0x%04x after %s", error, where);
}

static const char kVertexSource[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec4 uTint;
void main() {
   vColor = aColor;
   // uTint.w is 1.0, so this keeps the position; it makes uTint a vertex
   // uniform too, which the geometry shader must then mirror.
   gl_Position = vec4(aPosition * uTint.w, 0.0, 1.0);
}
)";

static const char kFragmentSource[] = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

static const char kGeometryFragmentSource[] = R"(
#version 330 core
in vec4 gColor;
out vec4 fragColor;
void main() { fragColor = gColor; }
)";

static const char kPassThroughGeometrySource[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vColor[];
out vec4 gColor;
uniform float uGeometryScale;
void main() {
   vec4 center = (gl_in[0].gl_Position + gl_in[1].gl_Position + gl_in[2].gl_Position) / 3.0;
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i];
      gl_Position = center + (gl_in[i].gl_Position - center) * uGeometryScale;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kExpandGeometrySource[] = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec4 vColor[];
out vec4 gColor;
uniform vec4 uTint;
layout(std140) uniform SpriteData {
   vec4 halfSize;
};
void main() {
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0] * vec4(uTint.rgb, 1.0);
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize.xy, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static GLuint CompileShader(const char *name, GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);
   GLint ok = GL_FALSE;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   char log[2048] = {};
   glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
   Log("%s: compile %s%s%s", name, ok ? "ok" : "FAILED", log[0] ? ": " : "", log);
   LogGLError(name);
   return ok ? shader : 0;
}

static GLuint LinkProgram(const char *name, GLuint vs, GLuint gs, GLuint fs, bool needs_gs)
{
   if (!vs || !fs || (needs_gs && !gs))
      return 0;
   GLuint program = glCreateProgram();
   glAttachShader(program, vs);
   if (gs)
      glAttachShader(program, gs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint ok = GL_FALSE;
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   char log[2048] = {};
   glGetProgramInfoLog(program, sizeof(log), nullptr, log);
   Log("%s: link %s%s%s", name, ok ? "ok" : "FAILED", log[0] ? ": " : "", log);
   LogGLError(name);
   return ok ? program : 0;
}

struct Mesh {
   GLuint vao = 0;
   GLuint vbo = 0;
   GLsizei count = 0;
};

static Mesh CreateMesh(const float *vertices, GLsizei count)
{
   Mesh mesh;
   mesh.count = count;
   glGenVertexArrays(1, &mesh.vao);
   glBindVertexArray(mesh.vao);
   glGenBuffers(1, &mesh.vbo);
   glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
   glBufferData(GL_ARRAY_BUFFER, count * 6 * sizeof(float), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glBindVertexArray(0);
   return mesh;
}

/* x, y, r, g, b, a */
static const float kTriangleA[] = {
   -0.95f, -0.6f, 1, 0, 0, 1,
   -0.40f, -0.6f, 0, 1, 0, 1,
   -0.675f, 0.6f, 0, 0, 1, 1,
};
static const float kTriangleB[] = {
   -0.275f, -0.6f, 1, 0, 0, 1,
   0.275f, -0.6f, 0, 1, 0, 1,
   0.0f, 0.6f, 0, 0, 1, 1,
};
static const float kPointsC[] = {
   0.52f, -0.35f, 1, 1, 0, 1,
   0.82f, -0.35f, 0, 1, 1, 1,
   0.52f, 0.35f, 1, 0, 1, 1,
   0.82f, 0.35f, 1, 1, 1, 1,
};

int main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   if (WHBMountSdCard()) {
      g_log_path = std::string(WHBGetSdCardMountPath()) + "/gx2gl-gs-test.log";
      if (FILE *file = fopen(g_log_path.c_str(), "w"))
         fclose(file);
   }
   Log("gx2gl geometry shader test started");

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }
   Log("GL_VERSION %s", (const char *)glGetString(GL_VERSION));

   GLuint vs = CompileShader("vs", GL_VERTEX_SHADER, kVertexSource);
   GLuint fs = CompileShader("fs", GL_FRAGMENT_SHADER, kFragmentSource);
   GLuint gfs = CompileShader("gs-fs", GL_FRAGMENT_SHADER, kGeometryFragmentSource);
   GLuint gs_b = CompileShader("gs-B", GL_GEOMETRY_SHADER, kPassThroughGeometrySource);
   GLuint gs_c = CompileShader("gs-C", GL_GEOMETRY_SHADER, kExpandGeometrySource);

   GLuint program_a = LinkProgram("A", vs, 0, fs, false);
   GLuint program_b = LinkProgram("B", vs, gs_b, gfs, true);
   GLuint program_c = LinkProgram("C", vs, gs_c, gfs, true);

   GLint tint_a = program_a ? glGetUniformLocation(program_a, "uTint") : -1;
   GLint tint_b = program_b ? glGetUniformLocation(program_b, "uTint") : -1;
   GLint scale_b = program_b ? glGetUniformLocation(program_b, "uGeometryScale") : -1;
   GLint tint_c = program_c ? glGetUniformLocation(program_c, "uTint") : -1;
   Log("locations: A uTint=%d, B uTint=%d uGeometryScale=%d, C uTint=%d",
       tint_a, tint_b, scale_b, tint_c);

   GLuint sprite_ubo = 0;
   if (program_c) {
      GLuint block = glGetUniformBlockIndex(program_c, "SpriteData");
      GLint referenced = -1;
      glGetActiveUniformBlockiv(program_c, block, GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER,
                                &referenced);
      Log("C: SpriteData block index %u, referenced by GS %d", block, referenced);
      glUniformBlockBinding(program_c, block, 2);
      const float half_size[4] = {0.07f, 0.12f, 0.0f, 0.0f};
      glGenBuffers(1, &sprite_ubo);
      glBindBuffer(GL_UNIFORM_BUFFER, sprite_ubo);
      glBufferData(GL_UNIFORM_BUFFER, sizeof(half_size), half_size, GL_STATIC_DRAW);
      glBindBufferBase(GL_UNIFORM_BUFFER, 2, sprite_ubo);
      LogGLError("SpriteData setup");
   }

   for (GLuint program : {program_a, program_b, program_c}) {
      if (!program)
         continue;
      glUseProgram(program);
      GLint tint = glGetUniformLocation(program, "uTint");
      if (tint >= 0)
         glUniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);
   }
   if (program_b) {
      glUseProgram(program_b);
      glUniform1f(scale_b, 1.0f);
   }
   LogGLError("uniform setup");

   Mesh triangle_a = CreateMesh(kTriangleA, 3);
   Mesh triangle_b = CreateMesh(kTriangleB, 3);
   Mesh points_c = CreateMesh(kPointsC, 4);
   LogGLError("mesh setup");
   Log("setup done: A always, B from frame 120, C from frame 240");

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      /* Aroma's button combos (screenshots and the like) only see input the
       * app reads, so poll the GamePad even though the test ignores it.
       */
      VPADStatus vpad;
      VPADReadError vpad_error;
      VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_error);

      const bool first_b = frame == 120 && program_b;
      const bool first_c = frame == 240 && program_c;
      if (first_b)
         Log("frame %u: first B draw", frame);
      if (first_c)
         Log("frame %u: first C draw", frame);

      glClearColor(0.12f, 0.12f, 0.16f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      if (program_a) {
         glUseProgram(program_a);
         glBindVertexArray(triangle_a.vao);
         glDrawArrays(GL_TRIANGLES, 0, triangle_a.count);
      }
      if (frame >= 120 && program_b) {
         glUseProgram(program_b);
         glBindVertexArray(triangle_b.vao);
         glDrawArrays(GL_TRIANGLES, 0, triangle_b.count);
      }
      if (frame >= 240 && program_c) {
         glUseProgram(program_c);
         glBindVertexArray(points_c.vao);
         glDrawArrays(GL_POINTS, 0, points_c.count);
      }
      glBindVertexArray(0);
      if (frame == 0 || first_b || first_c) {
         glFinish();
         LogGLError("draw");
         Log("frame %u: finished on the GPU", frame);
      }
      GX2GL_Present();
      if (frame == 600)
         Log("frame 600: still running");
      ++frame;
   }

   Log("exiting after %u frames", frame);
   GX2GL_DeleteContext(context);
   Log("context deleted");
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
