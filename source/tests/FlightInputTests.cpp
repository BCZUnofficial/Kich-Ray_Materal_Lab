#include "FlightInput.h"
#include <iostream>
#include <stdexcept>

namespace {
using Input=lab::FlightInput;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool stopped(const Input::Movement& m){return m.right==0&&m.up==0&&m.forward==0;}

void saveDoesNotStartBackwardFlight(){
    Input input;Input::Keys physical{};
    physical[Input::Back]=true;
    require(stopped(input.Sample(physical,true,true)),"Ctrl+S must save without moving backwards");
    // The user releases Ctrl first, while S remains physically down. The old
    // per-frame Ctrl condition resumed S movement here after closing a dialog.
    for(int frame=0;frame<90;++frame)
        require(stopped(input.Sample(physical,true,false)),"releasing Ctrl before S must not start backwards flight");
    physical[Input::Forward]=true;
    require(input.Sample(physical,true,false).forward==1,"consumed S must not cancel a newly pressed W");
    physical[Input::Forward]=false;
    physical[Input::Back]=false;
    require(stopped(input.Sample(physical,true,false)),"physical S release leaves the camera stopped");
    physical[Input::Back]=true;
    require(input.Sample(physical,true,false).forward==-1,"a fresh S press after release must allow deliberate backwards flight");
    require(stopped(input.Sample(physical,true,true)),"a save chord must also consume a previously moving S");
    require(stopped(input.Sample(physical,true,false)),"the consumed movement must stay stopped after Ctrl release");
}

void focusAndDialogSuspension(){
    Input input;Input::Keys physical{};
    physical[Input::Back]=true;
    require(input.Sample(physical,true,false).forward==-1,"fixture starts with deliberate backwards movement");
    input.Suspend();
    for(int frame=0;frame<10;++frame)
        require(stopped(input.Sample(physical,false,false)),"focus loss or an open native dialog must suspend movement");
    // ImGui may clear its logical key state on focus loss. The production
    // sampler supplies the PHYSICAL state: S is still true in this scenario.
    require(stopped(input.Sample(physical,true,false)),"regaining focus while S is physically held must not resume movement");
    physical[Input::Back]=false;
    require(stopped(input.Sample(physical,true,false)),"physical key-up re-arms movement without causing a step");
    physical[Input::Back]=true;
    require(input.Sample(physical,true,false).forward==-1,"a new S press after focus restoration must work");

    input.Suspend();physical.fill(false);
    require(stopped(input.Sample(physical,false,false)),"released keys while a dialog is open may safely re-arm");
    physical[Input::Forward]=true;physical[Input::Up]=true;
    const auto resumed=input.Sample(physical,true,false);
    require(resumed.forward==1&&resumed.up==1,"keys pressed after a genuine release may move after the dialog closes");
}

void inspectorOwnsHeldKeys(){
    Input input;Input::Keys physical{};
    physical[Input::Left]=true;physical[Input::Back]=true;
    require(stopped(input.Sample(physical,false,false)),"typing or dragging an inspector control must own movement keys");
    require(stopped(input.Sample(physical,true,false)),"leaving the inspector while those keys are held must not move the scene");
    physical[Input::Right]=true;
    const auto right=input.Sample(physical,true,false);
    require(right.right==1&&right.forward==0,"an unrelated newly pressed key remains usable after UI ownership");
    physical.fill(false);require(stopped(input.Sample(physical,true,false)),"all released physical keys stop movement");
    physical[Input::Left]=true;physical[Input::Down]=true;
    const auto deliberate=input.Sample(physical,true,false);
    require(deliberate.right==-1&&deliberate.up==-1&&deliberate.forward==0,"fresh A/Q input is restored after releasing UI-owned keys");
}

void rawReleaseAndOpposingKeys(){
    Input input;Input::Keys physical{};
    physical[Input::Back]=true;
    require(input.Sample(physical,true,false).forward==-1,"S moves while physically held");
    physical[Input::Back]=false;
    for(int frame=0;frame<120;++frame)
        require(stopped(input.Sample(physical,true,false)),"raw physical S=false must stop motion even if a higher-level event was stale");
    physical.fill(true);
    require(stopped(input.Sample(physical,true,false)),"opposing WASD/QE pairs cancel without residual movement");
    physical.fill(false);input.Sample(physical,true,false);
    physical[Input::Forward]=true;
    require(input.Sample(physical,true,true).forward==1,"save-chord handling must not consume an unrelated forward key");
}
}

int main(){
    try{
        saveDoesNotStartBackwardFlight();focusAndDialogSuspension();inspectorOwnsHeldKeys();rawReleaseAndOpposingKeys();
        std::cout<<"PASS: physical flight input, Ctrl+S release order, focus/dialog suspension and inspector keyboard ownership\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
