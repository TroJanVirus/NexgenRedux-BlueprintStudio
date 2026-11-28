#pragma once
#include "tools/blueprint/native_document.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace nx
{
    inline std::vector<BlueprintDocument::Point> blueprintGraphWire(
        const std::vector<BlueprintDocument::Point>& anchors,unsigned style,float zoom,
        float top,float bottom,std::vector<size_t>* segments,
        float left=std::numeric_limits<float>::infinity(),float right=-std::numeric_limits<float>::infinity())
    {
        using P=BlueprintDocument::Point;
        if(segments)segments->clear();
        if(anchors.empty())return {};
        std::vector<P> out{anchors.front()};
        const auto append=[&](P p,size_t segment)
        {
            if(out.back().x==p.x&&out.back().y==p.y)return;
            out.push_back(p);if(segments)segments->push_back(segment);
        };
        const auto bezier=[&](P a,P b,P c,P d,size_t segment)
        {
            const float length=std::hypot(b.x-a.x,b.y-a.y)+std::hypot(c.x-b.x,c.y-b.y)+std::hypot(d.x-c.x,d.y-c.y);
            const unsigned steps=static_cast<unsigned>(std::clamp(std::ceil(length/12.f),8.f,64.f));
            for(unsigned j=1;j<=steps;++j)
            {
                const float t=static_cast<float>(j)/steps,u=1-t;
                append(j==steps?d:P{u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,
                    u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y},segment);
            }
        };
        for(size_t i=1;i<anchors.size();++i)
        {
            const P a=anchors[i-1],b=anchors[i];const size_t seg=i-1;
            if(style==2){append(b,seg);continue;}
            if(style==1&&anchors.size()==2&&b.x<=a.x)
            {
                const float gap=std::max(22.f,36*zoom),radius=std::min(14.f*zoom,gap*0.4f);
                const float above=top-gap,below=bottom+gap;
                const float corridor=std::abs(a.y-above)+std::abs(b.y-above)<=std::abs(a.y-below)+std::abs(b.y-below)?above:below;
                const float outerRight=std::max(a.x,right)+gap,outerLeft=std::min(b.x,left)-gap;
                const std::vector<P> path{a,{outerRight,a.y},{outerRight,corridor},{outerLeft,corridor},{outerLeft,b.y},b};
                for(size_t j=1;j+1<path.size();++j)
                {
                    const P prev=path[j-1],corner=path[j],next=path[j+1];
                    if(style==1){append(corner,seg);continue;}
                    const float l=std::hypot(corner.x-prev.x,corner.y-prev.y),r=std::hypot(next.x-corner.x,next.y-corner.y);
                    if(l<=0||r<=0){append(corner,seg);continue;}
                    const float cut=std::min(radius,std::min(l,r)*0.4f);
                    const P p{corner.x+(prev.x-corner.x)*cut/l,corner.y+(prev.y-corner.y)*cut/l};
                    const P q{corner.x+(next.x-corner.x)*cut/r,corner.y+(next.y-corner.y)*cut/r};
                    append(p,seg);bezier(p,corner,corner,q,seg);
                }
                append(b,seg);continue;
            }
            if(style==1)
            {
                const float mid=(a.x+b.x)*0.5f;append({mid,a.y},seg);append({mid,b.y},seg);append(b,seg);continue;
            }
            float reach=std::clamp(std::max(std::abs(b.x-a.x)*0.5f,std::abs(b.y-a.y)*0.22f),12.f*zoom,180.f*zoom);
            if(anchors.size()==2&&b.x>a.x)reach=std::min(reach,(b.x-a.x)*0.75f);
            const float tangent=anchors.size()==2||b.x>=a.x?reach:-reach;
            bezier(a,{a.x+(i==1?reach:tangent),a.y},{b.x-(i+1==anchors.size()?reach:tangent),b.y},b,seg);
        }
        return out;
    }
}
