#pragma once
#include "platform/platform.h"
namespace nx
{
    inline constexpr const char* studioPadLabels[]={"D Up","D Down","D Left","D Right","A","B","X","Y","White","Black","LT","RT","L Up","L Down","L Left","L Right","R Up","R Down","R Left","R Right"};
    inline constexpr unsigned studioPadCount=sizeof(studioPadLabels)/sizeof(studioPadLabels[0]);
    // An isolated virtual controller. A lost connection can never leave a
    // button held. Physical controllers always take precedence.
    struct StudioPadPulse
    {
        Gamepad pad{};
        double until=0,lease=0;
        bool active(double now)const{return lease>now;}
        void clear(){pad={};until=lease=0;}
        bool start(unsigned control,double now)
        {
            if(control>=studioPadCount||until>now)return false;
            pad={};pad.connected=true;
            constexpr Button buttons[]={Button::Up,Button::Down,Button::Left,Button::Right,Button::A,Button::B,Button::X,Button::Y,Button::LeftShoulder,Button::RightShoulder};
            if(control<10)pad.buttons=1u<<static_cast<unsigned>(buttons[control]);
            else if(control==10)pad.leftTrigger=1;
            else if(control==11)pad.rightTrigger=1;
            else if(control==12)pad.leftY=1;
            else if(control==13)pad.leftY=-1;
            else if(control==14)pad.leftX=-1;
            else if(control==15)pad.leftX=1;
            else if(control==16)pad.rightY=1;
            else if(control==17)pad.rightY=-1;
            else if(control==18)pad.rightX=-1;
            else if(control==19)pad.rightX=1;
            until=now+0.15;lease=now+2;return true;
        }
        bool apply(InputState& state,double now)const
        {
            if(!active(now))return false;
            for(auto& physical:state.pads)if(!physical.connected)
            {
                physical=now<until?pad:Gamepad{};
                physical.connected=true;return true;
            }
            return false;
        }
    };
}
