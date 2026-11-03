// Focused, non-mutating-on-disk benchmark for Android input latency work.
// Usage: grab_bench model.sc1 [samples]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "core/brush.h"
#include "core/pick.h"
#include "io/sc1.h"
#include "test_util.h"

using namespace bh;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }

int main(int argc,char** argv) {
  if(argc<2) return 2;
  std::vector<uint8_t> bytes; Sc1File file; Mesh base; std::string error;
  if(!readBinaryFile(argv[1],bytes)||!readSc1(bytes,file,&error)||!sc1ToMesh(file,base,&error)) return 1;
  Vec3 center; const float size=length(bhtest::bboxDiagonal(base,&center));
  if(base.symmetric) center.x=0;
  const int samples=argc>2 ? std::max(1,std::atoi(argv[2])) : 60;
  for(float radiusFactor:{0.03f,0.10f,0.25f}) {
    Mesh m=base;
    auto input=[&](float x) {
      StrokeInput in; in.rayOrigin=center+Vec3{0,0,2*size};
      in.rayDir=normalize(center+Vec3{x,0,0}-in.rayOrigin);
      in.viewDir={0,0,-1}; in.camRight={1,0,0}; in.camUp={0,1,0};
      PickHit hit=pick(m,in.rayOrigin,in.rayDir);
      in.hit=hit.hit; in.point=hit.visiblePoint(); return in;
    };
    BrushSettings s; s.tool=Tool::Grab; s.radius=s.detailSize=radiusFactor*size; s.strength=1;
    auto start=Clock::now(); Mesh undo=m; double copy=elapsed(start);
    start=Clock::now();
    Stroke stroke(m,s,input(-0.05f*size));
    const double begin=elapsed(start);
    double picks=0,worstMove=0;
    start=Clock::now();
    for(int i=1;i<=samples;++i) {
      auto t=Clock::now();
      StrokeInput in=input((-0.05f+0.1f*i/samples)*size);
      picks+=elapsed(t); t=Clock::now();
      stroke.moveTo(in); worstMove=std::max(worstMove,elapsed(t));
    }
    double moves=elapsed(start);
    const DabTimes& d=stroke.times();
    std::printf("Grab r=%.2f samples=%d tris=%d->%d copy=%.1f begin=%.1f moves=%.1f pick=%.1f tess=%.1f tool=%.1f geometry=%.1f worst_move=%.1f ms\n",
        radiusFactor,samples,base.numTriangles(),m.numTriangles(),copy,begin,moves,picks,d.tessellate,d.tool,d.geometry,worstMove);
    std::fflush(stdout);
  }
}
