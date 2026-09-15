#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace Vr {
// Called only for a private VR bow mesh. Keep positions and wedges unchanged so
// authored grip bounds and morph vertex IDs stay valid for the remaining body.
template<class Mesh>
size_t removeBowString(Mesh& mesh,float* stringHeight=nullptr) {
  if(stringHeight)*stringHeight=0;
  const auto count=mesh.positions.size();if(count<3)return 0;
  std::vector<size_t> parent(count);std::iota(parent.begin(),parent.end(),0);
  auto root=[&](size_t v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;};
  for(const auto& part:mesh.sub_meshes)for(const auto& tri:part.triangles) {
    size_t ids[3];
    for(size_t k=0;k<3;++k){if(tri.wedges[k]>=part.wedges.size())return 0;ids[k]=part.wedges[tri.wedges[k]].index;if(ids[k]>=count)return 0;}
    parent[root(ids[1])]=root(ids[0]);parent[root(ids[2])]=root(ids[0]);
  }
  struct Bounds {
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    void add(float x,float y,float z){const float v[]={x,y,z};for(size_t a=0;a<3;++a){low[a]=std::min(low[a],v[a]);high[a]=std::max(high[a],v[a]);}}
    float span(size_t a)const{return high[a]-low[a];}
  };
  Bounds all;std::vector<Bounds> components(count);
  for(size_t i=0;i<count;++i){const auto& p=mesh.positions[i];if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return 0;all.add(p.x,p.y,p.z);components[root(i)].add(p.x,p.y,p.z);}
  size_t axis=0;for(size_t a=1;a<3;++a)if(all.span(a)>all.span(axis))axis=a;
  const float height=all.span(axis);if(height<=0)return 0;
  std::vector<bool> string(count,false);size_t matches=0;
  for(size_t i=0;i<count;++i) {
    const auto& b=components[i];
    if(b.span(axis)<height*.75f || b.span((axis+1)%3)>height*.035f || b.span((axis+2)%3)>height*.035f)continue;
    if(b.low[axis]>all.low[axis]+height*.2f || b.high[axis]<all.high[axis]-height*.2f)continue;
    string[i]=true;++matches;
  }
  // An unknown mesh stays intact if its string cannot be identified uniquely.
  if(matches!=1)return 0;
  size_t removed=0,total=0;
  for(const auto& part:mesh.sub_meshes)for(const auto& tri:part.triangles){++total;if(string[root(part.wedges[tri.wedges[0]].index)])++removed;}
  if(removed==0 || removed>=total)return 0;
  if(stringHeight)for(size_t i=0;i<count;++i)if(string[i])*stringHeight=components[i].span(axis);
  for(auto& part:mesh.sub_meshes)std::erase_if(part.triangles,[&](const auto& tri){return string[root(part.wedges[tri.wedges[0]].index)];});
  return removed;
}
}
