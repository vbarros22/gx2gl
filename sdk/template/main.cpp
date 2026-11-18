/* Minimal gx2gl app with a geometry shader: a triangle that the geometry shader
 * shrinks and spins a little every frame. Start from here.
 */
#include <gl/gl.h>
#include <gx2gl/present.h>

#include <vpad/input.h>
#include <whb/proc.h>

static const char kVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;
out VS_OUT { vec3 color; } vs_out;
void main() {
   vs_out.color = aColor;
   gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

static const char kGeometryShader[] = R"(
#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in VS_OUT { vec3 color; } gs_in[];
out vec3 gColor;
uniform float uTime;   // only the geometry shader uses it
void main() {
   float s = sin(uTime), c = cos(uTime);
   for (int i = 0; i < 3; ++i) {
      vec2 p = gl_in[i].gl_Position.xy * 0.8;
      gColor = gs_in[i].color;
      gl_Position = vec4(c * p.x - s * p.y, s * p.x + c * p.y, 0.0, 1.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kFragmentShader[] = R"(
#version 330 core
in vec3 gColor;
out vec4 fragColor;
void main() { fragColor = vec4(gColor, 1.0); }
)";

static GLuint Compile(GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);
   return shader;
}

int main(int, char **)
{
   WHBProcInit();
   GX2GL_Context context = GX2GL_CreateContext();

   GLuint program = glCreateProgram();
   glAttachShader(program, Compile(GL_VERTEX_SHADER, kVertexShader));
   glAttachShader(program, Compile(GL_GEOMETRY_SHADER, kGeometryShader));
   glAttachShader(program, Compile(GL_FRAGMENT_SHADER, kFragmentShader));
   glLinkProgram(program);   // check GL_LINK_STATUS / glGetProgramInfoLog in real code

   const float vertices[] = {-0.6f, -0.5f, 1, 0, 0,   0.6f, -0.5f, 0, 1, 0,   0.0f, 0.6f, 0, 0, 1};
   GLuint vao, vbo;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)0);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);

   glUseProgram(program);
   const GLint time_location = glGetUniformLocation(program, "uTime");
   float time = 0.0f;
   while (WHBProcIsRunning()) {
      /* Reading the GamePad lets Aroma's button combos (screenshots) work. */
      VPADStatus vpad;
      VPADReadError error;
      VPADRead(VPAD_CHAN_0, &vpad, 1, &error);

      time += 1.0f / 60.0f;
      glClearColor(0.1f, 0.1f, 0.14f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glUniform1f(time_location, time);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      GX2GL_Present();   // shows the TV image and mirrors it to the GamePad
   }

   GX2GL_DeleteContext(context);
   WHBProcShutdown();
   return 0;
}
