#include "vr/vrswimmotion.h"
#include <cstdio>
#include <limits>

using namespace Vr::SwimMotion;
int checks{},failures{};
void Check(bool value,const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",label); }
}
float Pull(float travel,int rate,int handCount=2,bool reverse=false) {
    Strokes strokes;
    Vec hands[2]{};
    const bool valid[]={true,handCount==2};
    const float dt=1.0f/rate;
    strokes.Update(hands,valid,{0,0,-1},dt);
    float impulse=0;
    const int frames=rate/2;
    for (int f=1;f<=frames;++f) {
        hands[0].z=hands[1].z=(reverse?-1:1)*travel*f/frames;
        impulse+=strokes.Update(hands,valid,{0,0,-1},dt).impulse;
    }
    return impulse;
}
int main() {
    Vec velocity{}; float vertical=0;
    for (int i=0;i<180;++i) velocity=Advance(velocity,{},vertical,0,1.0f/60,10,false);
    Check(velocity.z<-0.21f&&velocity.z>-0.23f,"idle hands sink without a dive button");
    Check(velocity.x==0&&velocity.y==0,"idle has no forward motion");
    for (int i=0;i<180;++i) velocity=Advance(velocity,{},vertical,0.8f,1.0f/60,10,false);
    Check(velocity.z>0.27f,"sculling can regain the surface");
    velocity=Advance(velocity,{0,0,1},vertical,1.5f,1.0f/60,0,false);
    Check(velocity.z==0,"strokes cannot launch CJ above water");
    velocity=Advance({}, {},vertical,0,1.0f/60,10,true);
    Check(velocity.z>=0,"preserve native world floor");

    const float small=Pull(0.16f,60),large=Pull(0.60f,60);
    Check(small>0.1f&&small<0.5f,"short dog paddle produces gentle thrust");
    Check(large>small*4,"large stroke gives a stronger continuous impulse");
    Check(Pull(0.6f,60,2,true)==0,"returning arms forward gives no thrust");
    Check(Pull(0.16f,60,1)>0,"a single alternating hand can propel CJ");
    Check(std::abs(Pull(0.6f,30)-large)/large<0.06f,"30 Hz stroke agrees with 60 Hz");
    Check(std::abs(Pull(0.6f,90)-large)/large<0.06f,"90 Hz stroke agrees with 60 Hz");

    Strokes strokes;
    Vec hands[2]{}; bool valid[]={true,true};
    strokes.Update(hands,valid,{0,0,-1},1.0f/60);
    float sideThrust=0,support=0;
    for (int i=1;i<=120;++i) {
        hands[0].x=0.22f*std::sin(i*0.1f); hands[1].x=-hands[0].x;
        const auto effort=strokes.Update(hands,valid,{0,0,-1},1.0f/60);
        sideThrust+=effort.impulse; support+=effort.support;
    }
    Check(sideThrust==0,"lateral treading does not swim forward");
    Check(support/120>0.4f,"lateral sculling supplies enough lift to tread");
    hands[0].z=hands[1].z=10;
    Check(strokes.Update(hands,valid,{0,0,-1},1.0f/60).impulse==0,"recenter jump is not a stroke");
    Check(strokes.Update(hands,valid,{0,0,-1},1.0f).impulse==0,"stale sample is not a stroke");
    valid[0]=valid[1]=false;
    Check(strokes.Update(hands,valid,{0,0,-1},1.0f/60).support==0,"tracking loss clears lift");
    hands[0].z=std::numeric_limits<float>::quiet_NaN(); valid[0]=true;
    const auto invalid=strokes.Update(hands,valid,{0,0,-1},1.0f/60);
    Check(invalid.support==0&&invalid.impulse==0,"NaN pose rejected");
    vertical=0;
    velocity=Advance({}, {0,1,0.8f},vertical,0,1.0f/60,10,false);
    Check(velocity.y>0&&velocity.z>0.7f,"looking up gives upward stroke thrust");
    vertical=0;
    velocity=Advance({}, {0,1,-0.8f},vertical,0,1.0f/60,10,false);
    Check(velocity.z<-0.7f,"looking down gives dive thrust");
    // Repeated dive strokes, including the recovery phases, must not reverse
    // upwards between impulses even with maximum residual sculling effort.
    vertical=0; velocity={};
    bool rose=false;
    float depth=0;
    for (int i=0;i<600;++i) {
        const Vec impulse=i%60==0?Vec{0,0,-0.65f}:Vec{};
        velocity=Advance(velocity,impulse,vertical,SupportForGaze(1.5f,-0.7f),1.0f/60,10,false);
        rose=rose||velocity.z>0;
        depth+=velocity.z/60;
    }
    Check(!rose&&depth<-5,"dive and recovery cycles descend continuously toward the bottom");
    Check(SupportForGaze(1.0f,0)==1&&SupportForGaze(1.0f,-0.5f)==0,
          "sculling lifts while level but cannot fight a downward gaze");
    Strokes verticalStrokes;
    Vec verticalHands[2]{};
    verticalStrokes.Update(verticalHands,valid,{0,-1,0},1.0f/60);
    float diveThrust=0,diveLift=0;
    valid[0]=valid[1]=true;
    for (int i=0;i<60;++i) {
        verticalHands[0].y=verticalHands[1].y=i*0.01f;
        const auto e=verticalStrokes.Update(verticalHands,valid,{0,-1,0},1.0f/60);
        diveThrust+=e.impulse; diveLift+=e.support;
    }
    Check(diveThrust>1&&diveLift==0,"straight-down power stroke works with no second lift action");
    velocity=Advance({}, {40,40,40},vertical,1.5f,1.0f/60,10,false);
    Check(Length(velocity)<2.801f,"repeated strokes have a bounded speed");
    float heights[3]{};
    const int rates[]={30,60,90};
    for (int n=0;n<3;++n) {
        vertical=0; velocity={};
        for (int i=0;i<rates[n]*5;++i) {
            velocity=Advance(velocity,{},vertical,0,1.0f/rates[n],10,false);
            heights[n]+=velocity.z/rates[n];
        }
    }
    Check(std::abs(heights[0]-heights[1])<0.01f&&std::abs(heights[2]-heights[1])<0.01f,
          "sinking depth consistent at 30/60/90 Hz");

    std::printf("%s: %d swimming motion checks\n",failures?"FAIL":"PASS",checks);
    return failures?1:0;
}
