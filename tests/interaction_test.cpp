// Shared application interaction, independent of a window or graphics API.
#include "app/app.h"
#include "app/document.h"
#include "app/images.h"
#include "app/input.h"
#include "test_util.h"

// This test never imports image files; built-in matcaps need no decoder.
namespace bh::viewer {
bool loadImage(const std::string&, Image&, std::string*) { return false; }
}

using namespace bh;
using namespace bh::viewer;
using namespace bhtest;

int main() {
  for (int i=0;i<int(app.sliders.size());++i) app.sliders[i]=defaultSliders(Tool(i));
  newModel();
  const Mesh initial=app.mesh;
  app.undoBudget=app.mesh.storageBytes()*2;
  for (int i=0;i<8;++i) {
    check(pushUndo(), "snapshot fits budget");
    app.mesh.vertex(0).pos.x+=0.01f;
    size_t bytes=0;
    for (const auto& m:app.undo) bytes+=m.storageBytes();
    check(bytes<=app.undoBudget, "history stays within memory budget");
  }
  check(app.undo.size()<8, "old snapshots are evicted");
  const float expected=app.mesh.vertex(0).pos.x-0.01f;
  undoLast();
  check(std::fabs(app.mesh.vertex(0).pos.x-expected)<1e-6f,"undo restores the most recent state");
  const size_t history=app.undo.size();
  app.undoBudget=1;
  check(app.mesh.symmetric,"a new sphere starts with symmetry on, as in Sculptris");

  // The axis gizmo's six jumps (yaw, pitch as in widgets.cpp) must be named
  // for the side the camera really looks from, and the start view (tilted)
  // for none.
  {
    struct Jump { float yaw, pitch; Vec3 side; const char* name; };
    const Jump jumps[] = {
        {kPi/2,0,{1,0,0},"View from +X (right)"}, {-kPi/2,0,{-1,0,0},"View from -X (left)"},
        {0.7f,1.5f,{0,1,0},"View from +Y (top)"}, {0.7f,-1.5f,{0,-1,0},"View from -Y (bottom)"},
        {0,0,{0,0,1},"View from +Z (front)"},     {kPi,0,{0,0,-1},"View from -Z (back)"},
    };
    bool named=true;
    for (const Jump& j:jumps) {
      Camera c; c.yaw=j.yaw; c.pitch=j.pitch;
      const char* n=axisViewName(c);
      named=named && n && std::string(n)==j.name && dot(normalize(c.eye()-c.target),j.side)>0.99f;
    }
    check(named,"axis gizmo names the side the camera looks from");
    check(!axisViewName(Camera{}),"the tilted start view has no axis name");
    Camera turned; turned.yaw=kPi/2+0.3f; turned.pitch=0;
    check(!axisViewName(turned),"orbiting off an axis drops the name");
  }
  setSymmetry(false);
  check(app.mesh.symmetric && app.undo.size()==history,"over-budget operation leaves mesh and history unchanged");

  newModel();
  app.undoBudget=64*1024*1024;
  app.viewX=app.viewY=0; app.viewW=800; app.viewH=600;
  app.camera.pitch=0;
  app.brush.detailFactor=0; // Isolate pressure from topology changes.
  beginStroke(400,300,false,false,0);
  check(bool(app.stroke),"pointer starts a stroke on the sphere");
  moveStroke(420,300,0);
  bool stationary=app.mesh.vertexSlots()==initial.vertexSlots();
  for(int v=0;v<app.mesh.vertexSlots();++v)
    stationary=stationary && length(app.mesh.vertex(v).pos-initial.vertex(v).pos)<1e-7f;
  check(stationary,"zero pressure produces no displacement");
  moveStroke(440,300,1);
  moveStroke(460,300,1);
  bool displaced=false;
  for(int v=0;v<app.mesh.vertexSlots();++v)
    displaced=displaced || length(app.mesh.vertex(v).pos-initial.vertex(v).pos)>1e-6f;
  check(displaced,"pressure can increase during the same stroke");
  check(app.undo.size()==1,"pressure changes remain one undo step");
  endStroke();
  undoLast();
  bool restored=app.mesh.vertexSlots()==initial.vertexSlots();
  for(int v=0;v<app.mesh.vertexSlots();++v)
    restored=restored && length(app.mesh.vertex(v).pos-initial.vertex(v).pos)<1e-7f;
  check(restored,"undo restores the mesh before the entire pressure stroke");

  newModel(); app.camera.pitch=0; app.brush.tool=Tool::Grab;
  app.brush.detailFactor=0;
  beginStroke(400,300,false,false);
  const float grabRadius=app.stroke->radius();
  const Vec3 start=app.hover.visiblePoint();
  moveStroke(440,300);
  check(app.hover.hit && app.hover.visiblePoint().x>start.x,
        "Grab updates the cursor on the visible surface");
  displaced=false;
  for(int v=0;v<app.mesh.vertexSlots();++v)
    displaced=displaced || length(app.mesh.vertex(v).pos-initial.vertex(v).pos)>1e-6f;
  check(displaced,"Grab still moves the surface using the pointer ray");
  moveStroke(790,300);
  check(app.hover.hit && app.stroke->radius()==grabRadius,
        "Grab retains its initial radius and drag plane beyond the model silhouette");
  endStroke(); undoLast();
  restored=true;
  for(int v=0;v<app.mesh.vertexSlots();++v)
    restored=restored && length(app.mesh.vertex(v).pos-initial.vertex(v).pos)<1e-7f;
  check(restored,"ray-based Grab remains exactly undoable");

  newModel(); app.camera.pitch=0; app.brush.tool=Tool::Grab; app.brush.detailFactor=0;
  app.brush.anchoredGrab=true;
  beginStroke(400,300,false,false);
  const Mesh beforeFrame=app.mesh;
  bool accepted=true;
  for(int sample=1;sample<=600;++sample)
    accepted=deferGrabMove(400+sample/10,300,0.7f) && accepted;
  check(accepted,"Grab accepts every deferred input sample");
  bool unchanged=true;
  for(int v=0;v<app.mesh.vertexSlots();++v)
    unchanged=unchanged && length(app.mesh.vertex(v).pos-beforeFrame.vertex(v).pos)<1e-7f;
  check(unchanged,"raw Grab samples do not repeatedly recompute the mesh");
  check(flushPendingStroke(),"render frame applies the latest Grab target");
  const Mesh batched=app.mesh;
  check(!flushPendingStroke(),"a target is applied only once");
  endStroke();
  newModel(); app.camera.pitch=0;
  beginStroke(400,300,false,false); moveStroke(460,300,0.7f);
  bool sameTarget=app.mesh.vertexSlots()==batched.vertexSlots();
  for(int v=0;v<app.mesh.vertexSlots();++v)
    sameTarget=sameTarget && length(app.mesh.vertex(v).pos-batched.vertex(v).pos)<1e-7f;
  check(sameTarget,"frame Grab matches the latest position and pressure exactly");
  check(deferGrabMove(480,300,0.5f),"last move before release is accepted");
  endStroke();
  const Mesh released=app.mesh;
  check(!app.pendingGrab.valid && !app.stroke,"release drains the pending target and ends contact");
  newModel(); app.camera.pitch=0;
  beginStroke(400,300,false,false); moveStroke(460,300,0.7f); moveStroke(480,300,0.5f); endStroke();
  sameTarget=app.mesh.vertexSlots()==released.vertexSlots();
  for(int v=0;v<app.mesh.vertexSlots();++v)
    sameTarget=sameTarget && length(app.mesh.vertex(v).pos-released.vertex(v).pos)<1e-7f;
  check(sameTarget,"release retains the exact final target without another frame");
  // A light pen must hold the geometry as firmly as full pressure.
  newModel(); app.camera.pitch=0;
  beginStroke(400,300,false,false,0.2f); moveStroke(460,300,0.2f); moveStroke(480,300,0.1f); endStroke();
  sameTarget=app.mesh.vertexSlots()==released.vertexSlots();
  for(int v=0;v<app.mesh.vertexSlots();++v)
    sameTarget=sameTarget && length(app.mesh.vertex(v).pos-released.vertex(v).pos)<1e-7f;
  check(sameTarget,"Grab follows the pen independently of pressure");
  // Sculptris Grab regrips along the path: an L-shaped drag pulls a bent
  // tentacle through the corner, not a straight spike to the end point.
  newModel(); app.camera.pitch=0; app.brush.anchoredGrab=false; app.brush.detailFactor=1;
  beginStroke(400,300,false,false);
  Vec3 corner{};
  for(int step=1;step<=40;++step) {
    deferGrabMove(400+std::min(step,20)*10,300-std::max(step-20,0)*10,1);
    flushPendingStroke();
    if(step==20) corner=app.hover.point;
  }
  endStroke();
  float nearCorner=1e9f;
  for(int v=0;v<app.mesh.vertexSlots();++v)
    if(app.mesh.vertex(v).active) nearCorner=std::min(nearCorner,length(app.mesh.vertex(v).pos-corner));
  check(nearCorner<0.1f,"Grab bends along the pen path");
  app.brush.tool=Tool::Draw;
  beginStroke(400,300,false,false);
  check(!deferGrabMove(420,300,1),"surface brushes keep their movement history");
  endStroke();

  // Local Grab geometry updates must agree with a full recomputation,
  // including newly tessellated vertices and a moving brush region.
  for (int mode=0;mode<3;++mode) {
    Mesh local=initial, full=initial;
    BrushSettings settings; settings.tool=Tool::Grab; settings.global=mode==2;
    settings.anchoredGrab=mode==1;
    settings.radius=settings.detailSize=0.25f; settings.strength=0.7f;
    StrokeInput in; in.point={0,0,1}; in.rayOrigin={0,0,5}; in.rayDir={0,0,-1};
    in.viewDir={0,0,-1}; in.camRight={1,0,0}; in.camUp={0,1,0};
    Stroke a(local,settings,in),b(full,settings,in);
    bool equivalent=true;
    for(int step=1;step<=30;++step) {
      in.rayDir=normalize(Vec3{0.012f*step,0.03f*std::sin(float(step)),1}-in.rayOrigin);
      a.moveTo(in); b.moveTo(in); full.updateGeometry();
      equivalent=equivalent && local.vertexSlots()==full.vertexSlots() && local.numTriangles()==full.numTriangles();
      if(!equivalent) break;
      for(int v=0;v<local.vertexSlots();++v) {
        if(!local.vertex(v).active) continue;
        equivalent=equivalent && length(local.vertex(v).pos-full.vertex(v).pos)<1e-5f
                    && length(local.vertex(v).normal-full.vertex(v).normal)<1e-5f;
      }
    }
    check(equivalent,mode==2 ? "global Grab normals match full geometry updates" :
          mode==1 ? "anchored tessellated Grab normals match full geometry updates" :
          "local tessellated Grab normals match full geometry updates");
    std::string error;
    check(local.validate(&error),"Grab retains mesh invariants");
  }

  // Clear mask: every vertex editable again, one undo step brings the mask
  // back, and with nothing masked it neither changes nor records anything.
  {
    newModel();
    app.undo.clear();
    app.undoBudget=std::numeric_limits<size_t>::max();
    app.mesh.vertex(0).weight=0;
    app.mesh.vertex(1).weight=0.4f;
    check(clearMask(),"clear mask reports a cleared mask");
    bool unmasked=true;
    for(int v=0;v<app.mesh.vertexSlots();++v)
      unmasked=unmasked && (!app.mesh.vertex(v).active || app.mesh.vertex(v).weight==1);
    check(unmasked,"clear mask makes every vertex editable");
    check(app.undo.size()==1,"clear mask is one undo step");
    undoLast();
    check(app.mesh.vertex(0).weight==0 && app.mesh.vertex(1).weight==0.4f,"undo brings the mask back");
    check(clearMask() && !clearMask(),"a second clear finds nothing masked");
    check(app.undo.size()==1,"clearing an unmasked model records no undo step");
  }
  return failures ? 1 : 0;
}
