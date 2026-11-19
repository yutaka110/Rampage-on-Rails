#include "course/RailMothershipPresentation.h"
#include <cstdio>
#include <cstdlib>
using namespace RailMothership;
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static float Error(Vector3 a,Vector3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
static Frame Base(){Frame f;f.visible=true;f.right={1,0,0};f.up={0,1,0};f.forward={0,0,1};return f;}
int main(){
 auto f=Base();const auto bay=f.Bay(0);ApplyOpeningCombatClearance(f,850,6);
 Require(Error(f.center,{-46,68,82})<.0001f,"escort clears central combat corridor");
 Require(Error(f.Bay(0),Add(bay,f.center))<.0001f,"physical launch bay moves with visible hull");
 for(float d:{1410.0f,1800.0f,3350.0f}){auto a=Base();ApplyOpeningCombatClearance(a,d,10);
  Require(Error(a.center,{})<.0001f,"arch firing and boss sockets retain authored pose");}
 for(float t:{0.0f,1.8f}){auto a=Base();ApplyOpeningCombatClearance(a,850,t);
  Require(Error(a.center,{})<.0001f,"opening flyby begins without a teleport");}
 for(float d:{1249.99f,1250.0f,1409.99f,1410.0f}){
  auto a=Base(),b=Base();ApplyOpeningCombatClearance(a,d,10);ApplyOpeningCombatClearance(b,d+.01f,10);
  Require(Error(a.center,b.center)<.02f,"smooth standoff release has no frame discontinuity");}
 for(float t=0;t<5;t+=.01f){auto a=Base(),b=Base();ApplyOpeningCombatClearance(a,900,t);ApplyOpeningCombatClearance(b,900,t+.01f);
  Require(Error(a.center,b.center)<.7f,"continuous hull and bay velocity during clearance");}
 std::puts("PASS: escort corridor, shared bay translation, smooth handoff, arch/boss pose isolation");
}
