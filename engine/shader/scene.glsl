#ifndef SCENE_GLSL
#define SCENE_GLSL

// std140, because uniform buffer
struct SceneDesc {
  mat4  viewProject;
  mat4  viewProjectInv;
  mat4  viewShadow[2];
  mat4  viewProjectLwcInv;
  mat4  viewShadowLwc[2];
  mat4  viewVirtualShadow;
  mat4  viewVirtualShadowLwc;
  mat4  viewProject2VirtualShadow;
  vec4  vsmDdx, vsmDdy;
  mat4  view;
  mat4  project;
  mat4  projectInv;
  vec3  sunDir;
  float waveAnim;
  vec3  ambient;
  float exposure;
  vec3  sunColor;
  float GSunIntensity;
  vec4  frustrum[6];
  vec3  clipInfo;
  uint  tickCount32;
  vec3  camPos;
  float isNight;
  vec2  screenResInv;
  vec2  closeupShadowSlice;
  vec3  pfxLeft;
  uint  underWater;
  vec3  pfxTop;
  float luminanceMed; // for debugging
  vec3  pfxDepth;
  float plPosY;
  ivec2 hiZTileSize;
  ivec2 screenRes;
  vec4  cloudsDir;
  float probeGridBias;
  float cameraFadeNear2;
  float cameraFadeFar2;
  float cameraFadePadding;
  vec4  cameraFadeTarget;
  vec4  vrShadowParams;  // x: VR cached shadow sampling on, y: static map edge fade start, z: baked sun visibility, w: 2002 static lighting
  vec4  vrStaticLight;   // x: 1/litRef^2.2 of the bake, y: reference n.d of the baked sun
  };

struct LightSource {
  vec3  pos;
  float range;
  vec3  color;
  float padd0;
  };

struct MorphDesc {
  uint  indexOffset;
  uint  sample0;
  uint  sample1;
  uint  alpha16_intensity16;
  };

struct Instance {
  mat4x3 mat;
  float  fatness;
  uint   animPtr;
  uint   padd0;
  uint   padd1;
  };

struct IndirectCmd {
  uint  vertexCount;
  uint  instanceCount;
  uint  firstVertex;
  uint  firstInstance;
  uint  writeOffset;
  };

struct Cluster {
  vec4  sphere;
  uint  bucketId_commandId;
  uint  firstMeshlet;
  int   meshletCount;
  uint  instanceId;
  };

const uint BK_SOLID = 0x1;
const uint BK_SKIN  = 0x2;
const uint BK_MORPH = 0x4;
const uint BK_WATER = 0x8;
const uint BK_CAMERA_FADE = 0x10;
const uint BK_FADE_MULTIPLY = 0x20;

// VR terrain level of detail (PackedMesh::LodTile / LodBoundary): landscape
// clusters carry their level in the top byte of Cluster.meshletCount.
const float LodTileSize  = 6000.0;
const uint  LOD_BOUNDARY = 3;

struct Bucket {
  vec4  bbox[2];
  ivec2 texAniMapDirPeriod;
  float bboxRadius;
  float waveMaxAmplitude;
  float alphaWeight;
  float envMapping;
  uint  flags;
  };

#endif
