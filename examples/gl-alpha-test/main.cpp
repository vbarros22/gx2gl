/* gx2gl transparency diagnostics, modelled on what Yamagi Quake II's GL3
 * renderer does for its HUD, water and explosions. Eight panels in two
 * rows; each says what it should look like.
 *
 *   B1 blending: a red square, then a green one at alpha 0.5 over it with
 *      GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA. Expected: olive (0.5, 0.5, 0).
 *   B2 blending, alpha from a uniform block float (like uni3D.alpha = 0.5),
 *      green over red. Expected: olive, same as B1.
 *   B3 discard: a texture whose alpha is a checkerboard of 0 and 255, the
 *      shader discards alpha <= 0.666. Expected: white checkers with the
 *      grey background showing through.
 *   B4 texture alpha shown as grey: alpha 64 / 128 / 192 / 255 in four
 *      vertical stripes. Expected: four grey stripes, darkest to white.
 *   B5 uniform block layout of uni3D (two mat4, then scroll, time, alpha,
 *      overbrightbits, particleFadeFactor, lightScaleForTurb, two pads);
 *      colour = (alpha, lightScaleForTurb, time) = (0.25, 0.5, 0.75).
 *      Expected: a medium blue-grey (64, 128, 191).
 *   B6 sin(): colour = 0.5 + 0.5 * sin(x * 6.28) across the square.
 *      Expected: a smooth dark-bright-dark horizontal gradient.
 *   B7 the 2D shader's code (discard, then outColor.rgb and .a written
 *      separately, pow with gamma 1) on B3's texture, with blending on.
 *      Expected: like B3.
 *   B8 GL_POINTS with gl_PointCoord (Quake II's particles), size 32.
 *      Expected: round dots.
 *
 * Log: sd:/gx2gl-alpha-test.log
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

static const char kVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 passTexCoord;
void main() { passTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kColourFS[] = R"(
#version 330 core
uniform vec4 uColour;
out vec4 fragColor;
void main() { fragColor = uColour; }
)";
static const char kUboAlphaFS[] = R"(
#version 330 core
layout (std140) uniform uni3D
{
   mat4 transProjView;
   mat4 transModel;
   float scroll;
   float time;
   float alpha;
   float overbrightbits;
   float particleFadeFactor;
   float lightScaleForTurb;
   float _pad_1;
   float _pad_2;
};
uniform vec4 uColour;
out vec4 outColor;
void main() { outColor.rgb = uColour.rgb; outColor.a = uColour.a * alpha; }
)";
static const char kDiscardFS[] = R"(
#version 330 core
in vec2 passTexCoord;
uniform sampler2D tex;
out vec4 fragColor;
void main() { vec4 texel = texture(tex, passTexCoord); if (texel.a <= 0.666) discard; fragColor = texel; }
)";
static const char kAlphaAsGreyFS[] = R"(
#version 330 core
in vec2 passTexCoord;
uniform sampler2D tex;
out vec4 fragColor;
void main() { float a = texture(tex, passTexCoord).a; fragColor = vec4(a, a, a, 1.0); }
)";
static const char kUboLayoutFS[] = R"(
#version 330 core
layout (std140) uniform uni3D
{
   mat4 transProjView;
   mat4 transModel;
   float scroll;
   float time;
   float alpha;
   float overbrightbits;
   float particleFadeFactor;
   float lightScaleForTurb;
   float _pad_1;
   float _pad_2;
};
out vec4 fragColor;
void main() { fragColor = vec4(alpha, lightScaleForTurb, time, 1.0); }
)";
static const char kSinFS[] = R"(
#version 330 core
in vec2 passTexCoord;
out vec4 fragColor;
void main() { float v = 0.5 + 0.5 * sin(passTexCoord.x * 6.2831853); fragColor = vec4(v, v, v, 1.0); }
)";
static const char k2DFS[] = R"(
#version 150
in vec2 passTexCoord; layout (std140) uniform uniCommon { float gamma; float intensity; float intensity2D; vec4 color; }; uniform sampler2D tex; out vec4 outColor; void main() { vec4 texel = texture(tex, passTexCoord); if(texel.a <= 0.666) discard; texel.rgb *= intensity2D; outColor.rgb = pow(texel.rgb, vec3(gamma)); outColor.a = texel.a; }
)";
static const char kPointVS[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
void main() { gl_PointSize = 32.0; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kPointFS[] = R"(
#version 330 core
out vec4 fragColor;
void main() {
   vec2 offsetFromCenter = 2.0 * (gl_PointCoord - vec2(0.5, 0.5));
   if (dot(offsetFromCenter, offsetFromCenter) > 1.0) discard;
   fragColor = vec4(1.0, 0.8, 0.2, 1.0);
}
)";

static GLuint MakeTexture(const std::vector<uint8_t> &texels, int size)
{
   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                texels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   return texture;
}

static GLuint MakeUbo(GLsizeiptr size, const void *data)
{
   GLuint ubo;
   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER, size, data, GL_DYNAMIC_DRAW);
   return ubo;
}

int main(int, char **)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-alpha-test.log");
   Log("gx2gl alpha test, build %s", GX2GL_BUILD_ID);

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }

   using example::CompileShader;
   using example::LinkProgram;
   const GLuint vs = CompileShader("vs", GL_VERTEX_SHADER, kVS);
   const GLuint colour = LinkProgram("B1 colour", vs, 0, CompileShader("colour-fs", GL_FRAGMENT_SHADER, kColourFS), false);
   const GLuint ubo_alpha = LinkProgram("B2 ubo alpha", vs, 0, CompileShader("ubo-alpha-fs", GL_FRAGMENT_SHADER, kUboAlphaFS), false);
   const GLuint discard = LinkProgram("B3 discard", vs, 0, CompileShader("discard-fs", GL_FRAGMENT_SHADER, kDiscardFS), false);
   const GLuint alpha_grey = LinkProgram("B4 alpha grey", vs, 0, CompileShader("alpha-grey-fs", GL_FRAGMENT_SHADER, kAlphaAsGreyFS), false);
   const GLuint ubo_layout = LinkProgram("B5 ubo layout", vs, 0, CompileShader("ubo-layout-fs", GL_FRAGMENT_SHADER, kUboLayoutFS), false);
   const GLuint sine = LinkProgram("B6 sin", vs, 0, CompileShader("sin-fs", GL_FRAGMENT_SHADER, kSinFS), false);
   const GLuint q2d = LinkProgram("B7 2D", vs, 0, CompileShader("2d-fs", GL_FRAGMENT_SHADER, k2DFS), false);
   const GLuint points = LinkProgram("B8 points", CompileShader("point-vs", GL_VERTEX_SHADER, kPointVS), 0, CompileShader("point-fs", GL_FRAGMENT_SHADER, kPointFS), false);

   /* Textures. */
   const int size = 32;
   std::vector<uint8_t> checker(size * size * 4), stripes(size * size * 4);
   for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
         uint8_t *c = &checker[(y * size + x) * 4];
         c[0] = c[1] = c[2] = 255;
         c[3] = (((x / 8) + (y / 8)) & 1) ? 255 : 0;
         uint8_t *s = &stripes[(y * size + x) * 4];
         static const uint8_t alphas[4] = {64, 128, 192, 255};
         s[0] = 255; s[1] = 0; s[2] = 0;
         s[3] = alphas[x / 8];
      }
   const GLuint checker_tex = MakeTexture(checker, size);
   const GLuint stripes_tex = MakeTexture(stripes, size);

   /* Uniform blocks, filled like Quake II's. */
   float uni3d[16 + 16 + 8] = {};
   for (int i = 0; i < 4; ++i) uni3d[i * 5] = uni3d[16 + i * 5] = 1.0f;
   uni3d[32] = 0.0f;   /* scroll */
   uni3d[33] = 0.75f;  /* time */
   uni3d[34] = 0.25f;  /* alpha (B5) */
   uni3d[35] = 1.0f;   /* overbrightbits */
   uni3d[36] = 1.0f;   /* particleFadeFactor */
   uni3d[37] = 0.5f;   /* lightScaleForTurb */
   const GLuint uni3d_layout = MakeUbo(sizeof(uni3d), uni3d);
   uni3d[34] = 0.5f;
   const GLuint uni3d_half = MakeUbo(sizeof(uni3d), uni3d);
   const float common[8] = {1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
   const GLuint uni_common = MakeUbo(sizeof(common), common);

   for (GLuint program : {ubo_alpha, ubo_layout}) {
      GLuint index = glGetUniformBlockIndex(program, "uni3D");
      if (index != GL_INVALID_INDEX) glUniformBlockBinding(program, index, 1);
   }
   {
      GLuint index = glGetUniformBlockIndex(q2d, "uniCommon");
      if (index != GL_INVALID_INDEX) glUniformBlockBinding(q2d, index, 2);
   }
   for (GLuint program : {discard, alpha_grey, q2d}) {
      glUseProgram(program);
      glUniform1i(glGetUniformLocation(program, "tex"), 0);
   }
   example::LogGLError("setup");

   /* Panel squares: 4 per row, 2 rows. */
   auto panel = [](int index, float inset, std::vector<float> &out) {
      const float x0 = -0.95f + 0.48f * (float)(index % 4) + inset;
      const float y0 = 0.9f - 0.9f * (float)(index / 4) - inset;
      const float w = 0.4f - 2.0f * inset, h = 0.7f - 2.0f * inset;
      const float q[6][4] = {{x0, y0, 0, 0}, {x0 + w, y0, 1, 0}, {x0 + w, y0 - h, 1, 1},
                             {x0, y0, 0, 0}, {x0 + w, y0 - h, 1, 1}, {x0, y0 - h, 0, 1}};
      for (auto &v : q) out.insert(out.end(), v, v + 4);
   };
   std::vector<float> vertices;
   for (int i = 0; i < 8; ++i) panel(i, 0.0f, vertices);   /* full squares: 6 verts each */
   for (int i = 0; i < 2; ++i) panel(i, 0.08f, vertices);  /* inner squares for B1, B2 */
   const int inner_first = 8 * 6;
   const int point_first = (int)(vertices.size() / 4);
   for (int i = 0; i < 4; ++i) {
      const float p[4] = {0.5f + 0.1f * (float)i, -0.4f + 0.1f * (float)(i % 2), 0, 0};
      vertices.insert(vertices.end(), p, p + 4);
   }

   GLuint vao, vbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);
   glEnable(GL_PROGRAM_POINT_SIZE);

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      example::PollGamePad();
      glClearColor(0.35f, 0.35f, 0.4f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindVertexArray(vao);
      glDisable(GL_BLEND);

      /* B1, B2: red base, then green at alpha 0.5. */
      glUseProgram(colour);
      glUniform4f(glGetUniformLocation(colour, "uColour"), 1, 0, 0, 1);
      glDrawArrays(GL_TRIANGLES, 0, 12);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glUniform4f(glGetUniformLocation(colour, "uColour"), 0, 1, 0, 0.5f);
      glDrawArrays(GL_TRIANGLES, inner_first, 6);
      glUseProgram(ubo_alpha);
      glBindBufferBase(GL_UNIFORM_BUFFER, 1, uni3d_half);
      glUniform4f(glGetUniformLocation(ubo_alpha, "uColour"), 0, 1, 0, 1);
      glDrawArrays(GL_TRIANGLES, inner_first + 6, 6);
      glDisable(GL_BLEND);

      /* B3 discard, B4 alpha as grey. */
      glUseProgram(discard);
      glBindTexture(GL_TEXTURE_2D, checker_tex);
      glDrawArrays(GL_TRIANGLES, 12, 6);
      glUseProgram(alpha_grey);
      glBindTexture(GL_TEXTURE_2D, stripes_tex);
      glDrawArrays(GL_TRIANGLES, 18, 6);

      /* B5 uniform block layout, B6 sin. */
      glUseProgram(ubo_layout);
      glBindBufferBase(GL_UNIFORM_BUFFER, 1, uni3d_layout);
      glDrawArrays(GL_TRIANGLES, 24, 6);
      glUseProgram(sine);
      glDrawArrays(GL_TRIANGLES, 30, 6);

      /* B7 the 2D shader with blending. */
      glEnable(GL_BLEND);
      glUseProgram(q2d);
      glBindBufferBase(GL_UNIFORM_BUFFER, 2, uni_common);
      glBindTexture(GL_TEXTURE_2D, checker_tex);
      glDrawArrays(GL_TRIANGLES, 36, 6);
      glDisable(GL_BLEND);

      /* B8 points. */
      glUseProgram(points);
      glDrawArrays(GL_POINTS, point_first, 4);

      if (frame == 0) {
         glFinish();
         example::LogGLError("frame 0");
         Log("frame 0 drawn");
      }
      GX2GL_Present();
      ++frame;
   }
   Log("exiting after %u frames", frame);
   GX2GL_DeleteContext(context);
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
