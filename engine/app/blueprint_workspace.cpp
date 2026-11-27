#include "app/blueprint_workspace.h"
#include "bindings/bindings.h"
#include "assets/assets.h"
#include "base/log.h"
#include "canvas/canvas.h"
#include "input/input.h"
#include "platform/platform.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>

namespace nx
{
    namespace
    {
        constexpr float kWidth = 1280, kHeight = 720, kLeft = 248, kRight = 1012, kTop = 76, kBottom = 674;
        constexpr float kNodeWidth = 196, kNodeHeight = 72;
        constexpr size_t kRows = 19;
        constexpr Color text = {0.88f, 0.91f, 0.96f, 1};
        constexpr Color muted = {0.57f, 0.65f, 0.76f, 1};
        constexpr Color accent = {0.27f, 0.65f, 0.98f, 1};
        bool in(float x, float y, float left, float top, float right, float bottom)
        { return x >= left && x < right && y >= top && y < bottom; }
        std::string title(const BlueprintDocument::Node& node)
        {
            if (node.kind.rfind("api:", 0) == 0)
            {
                const auto start = node.kind.find(':', 4);
                if (start != std::string::npos)
                {
                    // Native qualified names contain ::; retain the original signature in Details.
                    const auto end = node.kind.rfind(':');
                    if (end > start) return node.kind.substr(start + 1, end - start - 1);
                }
            }
            return node.kind;
        }
        std::string shorten(const std::string& s, size_t length)
        {
            if (s.size() <= length) return s;
            size_t cut = length;
            while (cut && (static_cast<unsigned char>(s[cut]) & 0xc0) == 0x80) --cut;
            return s.substr(0, cut) + "...";
        }
    }

    void BlueprintWorkspace::open(Services& s)
    {
        if (active()) return;
        m_font = s.assets->font("global/freesans.sfn", 18);
        if (!m_font || m_font->failed)
        {
            if (m_font) s.assets->release(m_font);
            m_font = nullptr;
            NX_ERROR("Blueprint Studio: cannot load the native workspace font.");
            return;
        }
        m_wantedPointer = s.input->wantsPointer();
        s.input->setWantsPointer(true);
        s.platform->lockMouse(false);
        s.input->consume();
        m_left = m_pan = m_drag = false;
        m_shortcuts = 0;
        if (m_document.graphs.empty())
        {
            FileData example;
            if (s.platform->readFile("blueprint/spinner.bp.json", example, MemTag::General) &&
                m_document.load(reinterpret_cast<const char*>(example.data), example.size, m_status))
            {
                selectGraph(0);
                m_status = "Example graph. Drag nodes or drop your own Blueprint JSON." ;
            }
            else
                m_status = "Drop a .bp.json or .bp.project.json file here.";
        }
    }

    void BlueprintWorkspace::close(Services& s)
    {
        if (!active()) return;
        cancelDrag();
        s.assets->release(m_font);
        m_font = nullptr;
        s.input->setWantsPointer(m_wantedPointer);
        s.input->consume();
    }

    void BlueprintWorkspace::selectGraph(size_t index)
    {
        if (index >= m_document.graphs.size()) return;
        cancelDrag();
        m_graph = index; m_selection = static_cast<size_t>(-1);
        fitGraph();
        if (index < m_first) m_first = index;
        if (index >= m_first + kRows) m_first = index - kRows + 1;
    }

    void BlueprintWorkspace::fitGraph()
    {
        m_zoom=1; m_x=m_y=0;
        if(m_graph>=m_document.graphs.size())return;
        const auto& nodes=m_document.graphs[m_graph].nodes;
        if(nodes.empty())return;
        float left=nodes.front().x,top=nodes.front().y,right=left+kNodeWidth,bottom=top+kNodeHeight;
        for(const auto& n:nodes)
        {
            left=std::min(left,n.x);top=std::min(top,n.y);
            right=std::max(right,n.x+kNodeWidth);bottom=std::max(bottom,n.y+kNodeHeight);
        }
        m_zoom=std::clamp(std::min((kRight-kLeft-48)/(right-left),(kBottom-kTop-48)/(bottom-top)),0.25f,1.0f);
        m_x=24-left*m_zoom;m_y=24-top*m_zoom;
    }

    bool BlueprintWorkspace::saveCopy(Services& s)
    {
        std::string output;
        if(!m_document.serialize(output,m_status))return false;
        static unsigned sequence=0;
        std::string name,path;
        // A new copy every time: never use the source name or replace an earlier snapshot.
        for(unsigned attempt=0;attempt<100;++attempt)
        {
            name="blueprint-layout-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence)+(m_document.isProject()?".bp.project.json":".bp.json");
            path="own/"+name;
            if(!s.platform->fileTime(path.c_str()))break;
            path.clear();
        }
        if(path.empty() || !s.platform->writeFile(path.c_str(),output.data(),output.size()))
        {m_status="Save copy failed. Changes remain in memory.";return false;}
        FileData saved;
        if(!s.platform->readFile(path.c_str(),saved,MemTag::General)||saved.size!=output.size()||
            std::memcmp(saved.data,output.data(),output.size())!=0)
        {m_status="Could not verify saved copy. Changes remain in memory.";return false;}
        m_document.markSaved();
        m_status="Saved copy: "+path;
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        return true;
    }

    void BlueprintWorkspace::recover(Services& s)
    {
        // Called on normal Redux shutdown, even if the workspace is no longer visible.
        if(m_document.dirty()&&!saveCopy(s))NX_ERROR("Blueprint Studio: unsaved layout could not be recovered.");
    }

    void BlueprintWorkspace::update(Services& s)
    {
        auto& input = *s.input;
        if (input.pressed(Action::Menu))
        {
            if(m_drag){cancelDrag();m_status="Drag cancelled.";return;}
            close(s);return;
        }
        std::string dropped;
        if (!m_drag && s.platform->takeDropped(dropped))
        {
            if (m_document.dirty()) m_status="Save your changes first (Ctrl+S), then drop the file again.";
            else if (dropped.size() < 8 || dropped.find_first_of("/\\:") != std::string::npos ||
                dropped.find("..") != std::string::npos || dropped.substr(dropped.size()-5) != ".json")
                m_status = "Drop a Blueprint JSON file.";
            else
            {
                FileData file;
                if (!s.platform->readFile(("own/" + dropped).c_str(), file, MemTag::General))
                    m_status = "Could not read the dropped file.";
                else if (m_document.load(reinterpret_cast<const char*>(file.data), file.size, m_status))
                {
                    m_first = 0; selectGraph(0);
                    m_status = "Opened " + dropped + " | Layout editing";
                }
            }
        }
        if (m_document.graphs.empty()) return;
        auto& canvas = *s.canvas;
        const auto& raw = input.raw();
        // Canvas design is the same fixed 1280x720 fit used by draw().
        const float scale = std::min(canvas.pixelWidth()/kWidth, canvas.pixelHeight()/kHeight);
        if (!(scale > 0)) return;
        const float x = (raw.mouseX - (canvas.pixelWidth()-kWidth*scale)*0.5f)/scale;
        const float y = (raw.mouseY - (canvas.pixelHeight()-kHeight*scale)*0.5f)/scale;
        const bool over = in(x,y,kLeft,kTop,kRight,kBottom);
        const bool left = raw.mouseButtons[0], pan = raw.mouseButtons[1] || raw.mouseButtons[2];
        const auto held=[&](Key key){return raw.keys[static_cast<size_t>(key)];};
        const bool control=held(Key::Control);
        const unsigned keys=(control&&held(Key::S)?1u:0u)|(control&&held(Key::Z)&&!held(Key::Shift)?2u:0u)|
            (control&&(held(Key::Y)||(held(Key::Z)&&held(Key::Shift)))?4u:0u);
        const unsigned pressed=keys&~m_shortcuts;m_shortcuts=keys;
        const bool click=left&&!m_left;
        if(!m_drag)
        {
            if((pressed&1u)||(click&&in(x,y,748,39,858,69)))saveCopy(s);
            if(((pressed&2u)||(click&&in(x,y,864,39,948,69)))&&m_document.undo())m_status="Undid layout change.";
            if(((pressed&4u)||(click&&in(x,y,954,39,1038,69)))&&m_document.redo())m_status="Redid layout change.";
            if(click&&in(x,y,1044,39,1128,69))fitGraph();
            if(click&&in(x,y,1134,39,1266,69)){close(s);return;}
        }
        if (left && !m_left && in(x,y,0,kTop,kLeft,kBottom))
        {
            const size_t row = static_cast<size_t>((y-kTop)/31);
            if (row < kRows) selectGraph(m_first + row);
        }
        if (left && !m_left && over)
        {
            const auto& nodes = m_document.graphs[m_graph].nodes;
            m_selection = static_cast<size_t>(-1);
            for (size_t i = nodes.size(); i > 0; --i)
            {
                const auto& n = nodes[i-1];
                const float nx = kLeft+m_x+n.x*m_zoom, ny = kTop+m_y+n.y*m_zoom;
                if (in(x,y,nx,ny,nx+kNodeWidth*m_zoom,ny+kNodeHeight*m_zoom))
                {
                    m_selection=i-1;
                    m_drag=true;m_dragX=n.x;m_dragY=n.y;
                    m_grabX=(x-nx)/m_zoom;m_grabY=(y-ny)/m_zoom;
                    break;
                }
            }
        }
        if(m_drag)
        {
            if(left)
            {
                m_dragX=std::clamp((x-kLeft-m_x)/m_zoom-m_grabX,-1000000.0f,1000000.0f);
                m_dragY=std::clamp((y-kTop-m_y)/m_zoom-m_grabY,-1000000.0f,1000000.0f);
            }
            else
            {
                if(m_document.move(m_graph,m_selection,m_dragX,m_dragY))m_status="Layout changed. Ctrl+S saves a new copy.";
                cancelDrag();
            }
            m_pan=false;m_left=left;m_lastX=x;m_lastY=y;
            return;
        }
        if (pan && m_pan) { m_x += x-m_lastX; m_y += y-m_lastY; }
        m_pan = pan && (over || m_pan);
        m_lastX=x; m_lastY=y; m_left=left;
        const float wheel = input.wheel();
        if (over && wheel != 0)
        {
            const float previous = m_zoom;
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, std::clamp(wheel,-8.0f,8.0f)),0.25f,2.0f);
            m_x = x-kLeft-(x-kLeft-m_x)*m_zoom/previous;
            m_y = y-kTop-(y-kTop-m_y)*m_zoom/previous;
        }
        else if (in(x,y,0,kTop,kLeft,kBottom) && wheel != 0)
        {
            if (wheel < 0 && m_first+kRows < m_document.graphs.size()) ++m_first;
            if (wheel > 0 && m_first) --m_first;
        }
        if (input.repeated(Action::Left)) m_x += 24;
        if (input.repeated(Action::Right)) m_x -= 24;
        if (input.repeated(Action::Up)) m_y += 24;
        if (input.repeated(Action::Down)) m_y -= 24;
        if (!control && input.pressed(Action::Alternate)) selectGraph((m_graph+1)%m_document.graphs.size());
    }

    void BlueprintWorkspace::draw(Canvas& c)
    {
        if (!active()) return;
        c.design(kWidth,kHeight);
        c.rect(0,0,kWidth,kHeight,{0.06f,0.08f,0.12f,1});
        c.rect(0,0,kWidth,kTop,{0.12f,0.16f,0.22f,1});
        c.text(*m_font,"NEXGEN  /  BLUEPRINT STUDIO",18,13,text);
        c.text(*m_font,"Native layout editor",1260,13,muted,Align::Right);
        const std::string titleText=(m_document.name.empty()?"No document open":m_document.name)+(m_document.dirty()?" *":"");
        c.clip(12,38,726,34);c.text(*m_font,shorten(titleText,65).c_str(),18,43,accent);c.noClip();
        const auto button=[&](float x,float width,const char* label,bool enabled){
            c.roundRect(x,39,width,30,5,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.14f,0.18f,0.24f,1});
            c.text(*m_font,label,x+width*0.5f,44,enabled?text:muted,Align::Center);
        };
        button(748,110,"Save copy",!m_document.graphs.empty());button(864,84,"Undo",m_document.canUndo());
        button(954,84,"Redo",m_document.canRedo());button(1044,84,"Fit",!m_document.graphs.empty());button(1134,132,"Return",true);
        c.rect(0,kTop,kLeft,kBottom-kTop,{0.10f,0.13f,0.18f,1});
        c.rect(kRight,kTop,kWidth-kRight,kBottom-kTop,{0.10f,0.13f,0.18f,1});
        c.clip(kLeft,kTop,kRight-kLeft,kBottom-kTop);
        const float step=32*m_zoom;
        for(float gx=kLeft+std::fmod(m_x,step);gx<kRight;gx+=step)
            c.line(gx,kTop,gx,kBottom,1,{0.12f,0.15f,0.20f,1});
        for(float gy=kTop+std::fmod(m_y,step);gy<kBottom;gy+=step)
            c.line(kLeft,gy,kRight,gy,1,{0.12f,0.15f,0.20f,1});
        if (m_document.graphs.empty())
        {
            c.text(*m_font,"Drop a Blueprint or project JSON file",630,330,text,Align::Center);
            c.text(*m_font,"The original file is never modified.",630,360,muted,Align::Center);
        }
        else
        {
            const auto& graph=m_document.graphs[m_graph];
            const auto nodeX=[&](size_t i){return m_drag&&i==m_selection?m_dragX:graph.nodes[i].x;};
            const auto nodeY=[&](size_t i){return m_drag&&i==m_selection?m_dragY:graph.nodes[i].y;};
            for(const auto& l:graph.links)
            {
                const float x1=kLeft+m_x+(nodeX(l.from)+kNodeWidth)*m_zoom,y1=kTop+m_y+(nodeY(l.from)+38)*m_zoom;
                const float x2=kLeft+m_x+nodeX(l.to)*m_zoom,y2=kTop+m_y+(nodeY(l.to)+38)*m_zoom;
                if(std::max(x1,x2)<kLeft||std::min(x1,x2)>kRight||std::max(y1,y2)<kTop||std::min(y1,y2)>kBottom)continue;
                const float mid=(x1+x2)*0.5f;
                const Color color=l.output=="then"||l.input=="in"?text:accent;
                c.line(x1,y1,mid,y1,2,color);c.line(mid,y1,mid,y2,2,color);c.line(mid,y2,x2,y2,2,color);
            }
            for(size_t i=0;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];
                const float x=kLeft+m_x+nodeX(i)*m_zoom,y=kTop+m_y+nodeY(i)*m_zoom,w=kNodeWidth*m_zoom,h=kNodeHeight*m_zoom;
                if(x+w<kLeft||x>kRight||y+h<kTop||y>kBottom) continue;
                c.roundRect(x,y,w,h,5,{0.15f,0.20f,0.27f,1});
                c.rect(x,y,w,24*m_zoom,n.kind=="entry"?Color{0.54f,0.20f,0.25f,1}:Color{0.19f,0.35f,0.53f,1});
                if(i==m_selection)c.roundLine(x,y,w,h,5,2,accent);
                c.circle(x,y+38*m_zoom,4*m_zoom,accent); c.circle(x+w,y+38*m_zoom,4*m_zoom,accent);
                if(m_zoom>=0.65f)
                {
                    c.clip(x+2,y+1,w-4,h-2);
                    c.text(*m_font,shorten(title(n),24).c_str(),x+8,y+3,text);
                    c.text(*m_font,shorten(n.ref.empty()?n.id:n.ref,24).c_str(),x+8,y+31,muted);
                    c.noClip();
                    c.clip(kLeft,kTop,kRight-kLeft,kBottom-kTop);
                }
            }
        }
        c.noClip();
        for(size_t row=0;row<kRows&&m_first+row<m_document.graphs.size();++row)
        {
            const size_t i=m_first+row;const auto& g=m_document.graphs[i];const float y=kTop+static_cast<float>(row)*31;
            if(i==m_graph)c.rect(4,y+2,kLeft-8,28,{0.19f,0.33f,0.50f,1});
            c.clip(5,y,kLeft-10,31);
            c.text(*m_font,shorten(g.owner+" / "+g.name,32).c_str(),12,y+6,text);c.noClip();
        }
        c.text(*m_font,"DETAILS",kRight+12,kTop+12,accent);
        if(!m_document.graphs.empty())
        {
            const auto& g=m_document.graphs[m_graph];
            c.paragraph(*m_font,g.name.c_str(),kRight+12,kTop+45,244,text);
            if(m_selection<g.nodes.size())
            {
                const auto& n=g.nodes[m_selection];
                c.paragraph(*m_font,shorten(n.kind,180).c_str(),kRight+12,kTop+95,244,muted);
                c.paragraph(*m_font,("ID: "+shorten(n.id,90)).c_str(),kRight+12,kTop+205,244,text);
                float y=kTop+260;
                for(const auto& l:g.links)
                {
                    if(l.from!=m_selection&&l.to!=m_selection) continue;
                    if(y>kBottom-35)break;
                    const std::string line=l.from==m_selection?"Out: "+l.output:"In: "+l.input;
                    c.clip(kRight+8,y,252,28);c.text(*m_font,shorten(line,30).c_str(),kRight+12,y,muted);c.noClip();
                    y+=28;
                }
            }
            else c.text(*m_font,"Select a node",kRight+12,kTop+95,muted);
        }
        c.rect(0,kBottom,kWidth,kHeight-kBottom,{0.12f,0.16f,0.22f,1});
        c.text(*m_font,shorten(m_status,110).c_str(),12,kBottom+6,text);
        c.text(*m_font,"Drag: move node   Wheel: zoom   Right/middle drag: pan   Ctrl+Z/Y: undo/redo   Ctrl+S: save copy",12,kBottom+26,muted);
    }
}
