/* gx2gl mipmap diagnostics. Each row draws one texture as squares of
 * 256, 128, 64, 32, 16 and 8 pixels (the texture is 64x64), so the GPU
 * picks mip levels 0, 0, 0, 1, 2 and 3 for them.
 *
 *   row 1: levels uploaded with glTexImage2D, each a solid colour:
 *          0 red, 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan, 6 white.
 *          Expected: red red red green blue yellow.
 *   row 2: a black and white checkerboard with glGenerateMipmap and
 *          GL_LINEAR_MIPMAP_NEAREST. Expected: checkers in the big squares,
 *          fading to grey in the small ones.
 *   row 3: the same checkerboard with GL_LINEAR (no mipmaps): checkers
 *          everywhere (aliased in the small ones). The reference.
 *   row 4: row 1's texture with GL_TEXTURE_BASE_LEVEL 1: green green green
 *          blue yellow magenta.
 *
 * Log: sd:/gx2gl-mip-test.log
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
out vec2 vTexCoord;
void main() { vTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
static const char kFS[] = R"(
#version 330 core
in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 fragColor;
void main() { fragColor = texture(uTexture, vTexCoord); }
)";

static const int kSize = 64;

static GLuint MakeLevelColourTexture()
{
   static const uint8_t colours[7][4] = {
      {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255},
      {255, 0, 255, 255}, {0, 255, 255, 255}, {255, 255, 255, 255}};
   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   for (int level = 0, size = kSize; size >= 1; ++level, size >>= 1) {
      std::vector<uint8_t> texels((size_t)size * size * 4);
      for (size_t i = 0; i < texels.size(); i += 4)
         for (int c = 0; c < 4; ++c) texels[i + c] = colours[level][c];
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, texels.data());
   }
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   example::LogGLError("level colour texture");
   return texture;
}

static GLuint MakeCheckerTexture(bool mipmaps)
{
   std::vector<uint8_t> texels(kSize * kSize * 4);
   for (int y = 0; y < kSize; ++y)
      for (int x = 0; x < kSize; ++x) {
         uint8_t v = (((x / 8) + (y / 8)) & 1) ? 255 : 0;
         uint8_t *t = &texels[(y * kSize + x) * 4];
         t[0] = t[1] = t[2] = v;
         t[3] = 255;
      }
   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                texels.data());
   if (mipmaps) {
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
   } else {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   }
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   example::LogGLError(mipmaps ? "checker with mipmaps" : "checker without mipmaps");
   return texture;
}

int main(int, char **)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   example::OpenLog("gx2gl-mip-test.log");
   Log("gx2gl mip test, build %s", GX2GL_BUILD_ID);

   GX2GL_Context context = GX2GL_CreateContext();
   if (!context) {
      Log("GX2GL_CreateContext FAILED");
      WHBProcShutdown();
      return 0;
   }

   GLint viewport[4];
   glGetIntegerv(GL_VIEWPORT, viewport);
   const float screen_w = (float)viewport[2], screen_h = (float)viewport[3];
   Log("viewport %d x %d", viewport[2], viewport[3]);

   const GLuint program = example::LinkProgram(
      "mip", example::CompileShader("vs", GL_VERTEX_SHADER, kVS), 0,
      example::CompileShader("fs", GL_FRAGMENT_SHADER, kFS), false);

   const GLuint level_colours = MakeLevelColourTexture();
   const GLuint checker_mips = MakeCheckerTexture(true);
   const GLuint checker_plain = MakeCheckerTexture(false);
   GLuint level_colours_base1;
   {
      level_colours_base1 = MakeLevelColourTexture();
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
      example::LogGLError("base level 1");
   }

   /* Squares in pixels, laid out left to right with 16px gaps. */
   static const int sizes[] = {256, 128, 64, 32, 16, 8};
   std::vector<float> vertices;
   for (int row = 0; row < 4; ++row) {
      float x = 40.0f;
      const float top = 20.0f + (float)row * 262.0f;
      for (int size : sizes) {
         const float x0 = x / screen_w * 2.0f - 1.0f;
         const float x1 = (x + (float)size) / screen_w * 2.0f - 1.0f;
         const float y0 = 1.0f - top / screen_h * 2.0f;
         const float y1 = 1.0f - (top + (float)size) / screen_h * 2.0f;
         const float quad[6][4] = {{x0, y0, 0, 0}, {x1, y0, 1, 0}, {x1, y1, 1, 1},
                                   {x0, y0, 0, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}};
         for (auto &v : quad) vertices.insert(vertices.end(), v, v + 4);
         x += (float)size + 16.0f;
      }
   }

   GLuint vao, vbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(),
                GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);

   glUseProgram(program);
   glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
   const GLuint textures[4] = {level_colours, checker_mips, checker_plain,
                               level_colours_base1};

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      example::PollGamePad();
      glClearColor(0.35f, 0.35f, 0.4f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glBindVertexArray(vao);
      for (int row = 0; row < 4; ++row) {
         glBindTexture(GL_TEXTURE_2D, textures[row]);
         glDrawArrays(GL_TRIANGLES, row * 36, 36);
      }
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
