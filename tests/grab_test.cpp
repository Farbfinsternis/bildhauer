// Grab regressions for sparse frame updates and mirrored, oblique views.
#include "core/brush.h"
#include "test_util.h"

using namespace bh;
using namespace bhtest;

namespace {
Mesh grid(bool symmetric=false) {
  Mesh m;
  constexpr int n=20;
  for(int y=0;y<=n;++y) for(int x=0;x<=n;++x)
    m.addVertex({symmetric ? float(x)/n : -1+2.0f*x/n,-1+2.0f*y/n,0});
  for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
    int a=y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;
    m.addTriangle(a,b,d); m.addTriangle(a,d,c);
  }
  m.symmetric=symmetric;
  for(int v=0;v<m.vertexSlots();++v) m.vertex(v).onSymmetryPlane=symmetric && m.vertex(v).pos.x==0;
  m.updateGeometry(); m.storeReference(); return m;
}
StrokeInput ray(Vec3 eye,Vec3 view,Vec3 target) {
  StrokeInput in; in.point=target; in.rayOrigin=eye; in.rayDir=normalize(target-eye);
  in.viewDir=normalize(view); in.camRight=normalize(cross(in.viewDir,{0,1,0}));
  in.camUp=cross(in.camRight,in.viewDir); return in;
}
bool equalPositions(const Mesh& a,const Mesh& b) {
  if(a.vertexSlots()!=b.vertexSlots()) return false;
  for(int v=0;v<a.vertexSlots();++v)
    if(length(a.vertex(v).pos-b.vertex(v).pos)>1e-5f) return false;
  return true;
}
}

int main() {
  BrushSettings s; s.tool=Tool::Grab; s.anchoredGrab=true;
  s.radius=s.detailSize=0.2f; s.detailFactor=0; s.strength=1;
  const Mesh original=grid();
  const int center=10*21+10, remote=10*21+20;
  StrokeInput start=ray({0,0,3},{0,0,-1},{0,0,0});
  Mesh fast=original;
  Stroke one(fast,s,start);
  one.moveTo(ray({0,0,3},{0,0,-1},{0.8f,0,0}));
  check(length(fast.vertex(center).pos-Vec3{0.8f,0,0})<1e-5f,
        "a fast Grab retains the original contact beyond the brush radius");
  check(length(fast.vertex(remote).pos-original.vertex(remote).pos)<1e-7f,
        "Grab does not recapture unrelated geometry under the moving cursor");
  Mesh slow=original;
  Stroke many(slow,s,start);
  for(int i=1;i<=60;++i) many.moveTo(ray({0,0,3},{0,0,-1},{0.8f*i/60,0,0}));
  check(equalPositions(fast,slow),"fast and densely sampled constant-pressure Grabs agree");
  one.setStrength(0);
  one.moveTo(ray({0,0,3},{0,0,-1},{1.0f,0,0}));
  check(length(fast.vertex(center).pos-Vec3{0.8f,0,0})<1e-5f,
        "lifting pressure does not snap the held geometry back");

  Mesh mirror=grid(true), before=mirror;
  StrokeInput angled=ray({-2,1,3},{2,-1,-3},{-0.4f,0.2f,0});
  Stroke stationary(mirror,s,angled);
  stationary.moveTo(angled);
  check(equalPositions(mirror,before),"stationary pen on a mirrored oblique view never moves the mesh");
  const Vec3 displacement={0.1f,0.1f,0.033333333f}; // tangent to view plane
  const Vec3 expected=angled.point+displacement;
  const Vec3 actual=stationary.dragTarget(ray(angled.rayOrigin,angled.viewDir,expected));
  check(length(actual-expected)<1e-5f,"drag plane stays at the visible initial hit, including the mirrored half");
  check(planeIsExact(mirror),"mirrored Grab keeps the symmetry plane exact");
  return failures ? 1 : 0;
}
