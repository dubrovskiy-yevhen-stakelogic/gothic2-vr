#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in uint color;
layout(push_constant) uniform Push {mat4 mvp;vec4 light;vec4 viewport;vec4 tint;} push;
layout(location=0) out vec2 texcoord;
void main() {
  gl_Position=push.mvp*vec4(position,1);
  texcoord=uv;
}
