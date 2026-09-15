#version 450
layout(location=0) flat out vec4 color;
void main() {
  const vec2 corners[4] = vec2[4](vec2(-0.3,-0.6),vec2(0.3,-0.6),vec2(-0.3,0.6),vec2(0.3,0.6));
  int vertex = gl_VertexIndex-5;
  int instance = gl_InstanceIndex-7;
  if(vertex<0 || vertex>=4 || instance<0 || instance>2) {
    gl_Position=vec4(5,5,0,1);
    color=vec4(0,0,1,1);
    return;
  }
  if(instance==2) { gl_Position=vec4(corners[vertex]*0.25,0,1); color=vec4(0,0,1,1); return; }
  gl_Position=vec4(corners[vertex]+vec2(instance==0 ? -0.5 : 0.5,0),0,1);
  color=instance==0 ? vec4(1,0,0,1) : vec4(0,1,0,1);
}

