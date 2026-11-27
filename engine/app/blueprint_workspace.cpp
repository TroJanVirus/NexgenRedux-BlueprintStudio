#include "app/blueprint_workspace.h"
#include "tools/blueprint/native_compile.h"
#include "script/script_host.h"
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
#include <unordered_map>
#include <cctype>

namespace nx
{
    namespace
    {
        constexpr float kWidth = 1280, kHeight = 720, kLeft = 248, kRight = 1012, kTop = 76, kBottom = 674;
        constexpr float kNodeWidth = 196, kNodeHeight = 72;
        // Each native pin is provided by the generated catalogue.
        constexpr float kPinStart=38, kPinStep=22;
        constexpr size_t kRows = 19;
        constexpr Color text = {0.88f, 0.91f, 0.96f, 1};
        constexpr Color muted = {0.57f, 0.65f, 0.76f, 1};
        constexpr Color accent = {0.27f, 0.65f, 0.98f, 1};
        bool in(float x, float y, float left, float top, float right, float bottom)
        { return x >= left && x < right && y >= top && y < bottom; }
        std::string title(const BlueprintDocument::Node& node)
        {
            if(const auto* spec=blueprintSpec(node.kind))return spec->title;
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
        float pinY(size_t row){return kPinStart+static_cast<float>(row)*kPinStep;}
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
        m_previousTyping=s.input->typing();m_focus=0;s.input->setTyping(false);
        if(m_document.graphs.empty())
        {
            m_document.create();selectGraph(0);
            m_status="New Blueprint. Search the node library to start.";
        }
        filterCatalogue();
    }

    void BlueprintWorkspace::close(Services& s)
    {
        if (!active()) return;
        cancelDrag();clearWire();m_focus=0;s.input->setTyping(m_previousTyping);
        s.assets->release(m_font);
        m_font = nullptr;
        s.input->setWantsPointer(m_wantedPointer);
        s.input->consume();
    }

    void BlueprintWorkspace::selectGraph(size_t index)
    {
        if (index >= m_document.graphs.size()) return;
        cancelDrag();clearWire();
        m_graph = index; m_selection = static_cast<size_t>(-1);
        rebuildPins();fitGraph();
        if (index < m_first) m_first = index;
        if (index >= m_first + kRows) m_first = index - kRows + 1;
    }

    void BlueprintWorkspace::rebuildPins()
    {
        m_rows.clear();m_wirePins.clear();
        const auto& graph=m_document.graphs[m_graph];
        m_rows.resize(graph.nodes.size());
        m_wirePins.reserve(graph.links.size());
        struct Lookup { std::unordered_map<std::string,size_t> inputs,outputs; };
        std::vector<Lookup> lookup(graph.nodes.size());
        const auto add=[](std::vector<std::string>& rows,std::unordered_map<std::string,size_t>& index,const std::string& pin)
        {
            auto result=index.emplace(pin,rows.size());
            if(result.second)rows.push_back(pin);
            return result.first->second;
        };
        for(size_t i=0;i<graph.nodes.size();++i)
        {
            if(const auto* spec=blueprintSpec(graph.nodes[i].kind))
            {
                for(const auto& pin:spec->inputs)add(m_rows[i].inputs,lookup[i].inputs,pin.name);
                for(const auto& pin:spec->outputs)add(m_rows[i].outputs,lookup[i].outputs,pin.name);
            }
        }
        for(const auto& link:graph.links)
            m_wirePins.push_back({add(m_rows[link.from].outputs,lookup[link.from].outputs,link.output),
                                  add(m_rows[link.to].inputs,lookup[link.to].inputs,link.input)});
        for(auto& row:m_rows)
            row.height=std::max(kNodeHeight,kPinStart+kPinStep*static_cast<float>(std::max(row.inputs.size(),row.outputs.size()))+8);
    }

    void BlueprintWorkspace::filterCatalogue()
    {
        m_palette.clear();m_paletteFirst=0;
        auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
        const auto query=lower(m_query);
        for(const auto& node:blueprintCatalogue())
            if(node.id!="entry"&&lower(node.category+" "+node.title+" "+node.id).find(query)!=std::string::npos)m_palette.push_back(&node);
    }

    void BlueprintWorkspace::fitGraph()
    {
        m_zoom=1; m_x=m_y=0;
        if(m_graph>=m_document.graphs.size())return;
        const auto& graph=m_document.graphs[m_graph];const auto& nodes=graph.nodes;
        if(nodes.empty())return;
        const auto& rows=m_rows;
        float left=nodes.front().x,top=nodes.front().y,right=left+kNodeWidth,bottom=top+kNodeHeight;
        for(size_t i=0;i<nodes.size();++i)
        {
            const auto& n=nodes[i];left=std::min(left,n.x);top=std::min(top,n.y);
            right=std::max(right,n.x+kNodeWidth);bottom=std::max(bottom,n.y+rows[i].height);
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
            name="blueprint-native-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence)+(m_document.isProject()?".bp.project.json":".bp.json");
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

    bool BlueprintWorkspace::compileAndExport(Services& s)
    {
        std::string source;
        if(!compileBlueprint(m_document,source,m_status))return false;
        const char* module="bp_native_validation";
        const bool valid=s.host->compile(module,"blueprint.as",source.data(),source.size())&&s.host->findClass(module,"Screen");
        if(!valid)m_status=std::string("AngelScript validation failed: ")+s.host->lastError();
        s.host->discard(module);
        if(!valid)return false;
        static unsigned sequence=0;
        std::string path;
        for(unsigned attempt=0;attempt<100;++attempt)
        {
            path="own/blueprint-script-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence)+".as";
            if(!s.platform->fileTime(path.c_str()))break;
            path.clear();
        }
        if(path.empty()||!s.platform->writeFile(path.c_str(),source.data(),source.size())){m_status="Script export failed.";return false;}
        m_status="Compiled and exported: "+path;NX_INFO("Blueprint Studio: %s",m_status.c_str());return true;
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
            if(m_focus){m_focus=0;input.setTyping(false);m_status="Input cancelled.";return;}
            if(m_pending!=static_cast<size_t>(-1)){clearWire();m_status="Connection cancelled.";return;}
            if(m_drag){cancelDrag();m_left=input.raw().mouseButtons[0];m_status="Drag cancelled.";return;}
            close(s);return;
        }
        std::string dropped;
        if (!m_drag && !m_focus && s.platform->takeDropped(dropped))
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
                    m_status = "Opened " + dropped;
                }
            }
        }
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
        if(m_focus)
        {
            auto& value=m_focus==1?m_query:m_edit;
            bool changed=false;
            for(u32 i=0;i<input.typedCount();++i)
            {
                const u32 ch=input.typed(i);
                if(ch==8&&!value.empty()){value.pop_back();changed=true;}
                else if(ch>=32&&ch<127&&value.size()< (m_focus==1?80u:4096u)){value+=static_cast<char>(ch);changed=true;}
                else if(ch==13)
                {
                    if(m_focus==2&&!m_document.setLiteral(m_graph,m_selection,m_edit,m_status))break;
                    m_focus=0;input.setTyping(false);m_status="Ready.";break;
                }
            }
            if(changed&&m_focus!=2)filterCatalogue();
            if(click&&m_focus==1&&!in(x,y,kRight+8,kTop+38,kWidth-8,kTop+68)){m_focus=0;input.setTyping(false);}
            else {m_left=left;return;}
        }
        if(!m_drag)
        {
            if(click&&in(x,y,1134,39,1266,69)){close(s);return;}
            if(m_document.graphs.empty()){m_left=left;return;}
            if(click&&in(x,y,610,39,742,69))compileAndExport(s);
            if((pressed&1u)||(click&&in(x,y,748,39,858,69)))saveCopy(s);
            if(((pressed&2u)||(click&&in(x,y,864,39,948,69)))&&m_document.undo()){clearWire();m_selection=static_cast<size_t>(-1);rebuildPins();m_status="Undid graph change.";}
            if(((pressed&4u)||(click&&in(x,y,954,39,1038,69)))&&m_document.redo()){clearWire();m_selection=static_cast<size_t>(-1);rebuildPins();m_status="Redid graph change.";}
            if(click&&in(x,y,1044,39,1128,69))fitGraph();
        }
        if(click&&in(x,y,8,kBottom-40,kLeft-8,kBottom-8))
        {
            if(m_document.dirty())m_status="Save changes before creating a new Blueprint.";
            else{m_document.create();selectGraph(0);m_status="New Blueprint.";}
            m_left=left;return;
        }
        if(!m_drag&&in(x,y,kRight,kTop,kWidth,kBottom))
        {
            if(input.wheel()<0&&m_paletteFirst+10<m_palette.size())++m_paletteFirst;
            if(input.wheel()>0&&m_paletteFirst)--m_paletteFirst;
            if(click&&in(x,y,kRight+8,kTop+38,kWidth-8,kTop+68)){m_focus=1;input.setTyping(true);m_left=left;return;}
            if(click&&in(x,y,kRight+8,kTop+80,kWidth-8,kTop+340))
            {
                const size_t row=m_paletteFirst+static_cast<size_t>((y-kTop-80)/26);
                if(row<m_palette.size()&&m_document.addNode(m_graph,m_palette[row]->id,(kRight-kLeft)*0.5f/m_zoom-m_x/m_zoom,(kBottom-kTop)*0.4f/m_zoom-m_y/m_zoom,m_status))
                {clearWire();rebuildPins();m_selection=m_document.graphs[m_graph].nodes.size()-1;m_status="Added "+m_palette[row]->title;}
                m_left=left;return;
            }
            if(click&&m_selection<m_document.graphs[m_graph].nodes.size())
            {
                const auto& n=m_document.graphs[m_graph].nodes[m_selection];
                if(in(x,y,kRight+8,kTop+456,kWidth-8,kTop+486)&&n.kind.rfind("literal:",0)==0){m_edit=n.ref;m_focus=2;input.setTyping(true);}
                else if(in(x,y,kRight+8,kBottom-40,kWidth-8,kBottom-8)&&m_document.deleteNode(m_graph,m_selection,m_status)){clearWire();m_selection=static_cast<size_t>(-1);rebuildPins();m_status="Deleted node.";}
            }
            m_left=left;return;
        }
        if (left && !m_left && in(x,y,0,kTop,kLeft,kBottom))
        {
            const size_t row = static_cast<size_t>((y-kTop)/31);
            if (row < kRows) selectGraph(m_first + row);
        }
        if (left && !m_left && over)
        {
            const auto& graph=m_document.graphs[m_graph];const auto& nodes=graph.nodes;
            const auto& rows=m_rows;
            // Pin clicks take precedence over node dragging. Ctrl+click removes a connection.
            for(size_t i=nodes.size();i>0;--i)
            {
                const auto& n=nodes[i-1];const auto& row=rows[i-1];
                const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;
                for(unsigned side=0;side<2;++side)
                {
                    const auto& pins=side?row.outputs:row.inputs;
                    const float px=nx+(side?kNodeWidth*m_zoom:0);
                    for(size_t p=0;p<pins.size();++p)
                    {
                        if(std::abs(x-px)>9||std::abs(y-(ny+pinY(p)*m_zoom))>9)continue;
                        m_selection=i-1;
                        if(control){if(m_document.disconnect(m_graph,i-1,pins[p],side!=0)){rebuildPins();m_status="Disconnected pin.";}clearWire();}
                        else if(side){m_pending=i-1;m_pendingPin=pins[p];m_status="Choose an input pin (Esc cancels): "+pins[p];}
                        else if(m_pending!=static_cast<size_t>(-1))
                        {
                            if(m_document.connect(m_graph,m_pending,m_pendingPin,i-1,pins[p],m_status)){rebuildPins();clearWire();m_status="Connected pins.";}
                        }
                        else m_status="Choose an output pin first.";
                        m_left=left;return;
                    }
                }
            }
            clearWire();m_selection = static_cast<size_t>(-1);
            for (size_t i = nodes.size(); i > 0; --i)
            {
                const auto& n = nodes[i-1];
                const float nx = kLeft+m_x+n.x*m_zoom, ny = kTop+m_y+n.y*m_zoom;
                if (in(x,y,nx,ny,nx+kNodeWidth*m_zoom,ny+rows[i-1].height*m_zoom))
                {
                    m_selection=i-1;
                    m_drag=true;m_dragMoved=false;m_dragX=n.x;m_dragY=n.y;
                    m_pressX=x;m_pressY=y;m_grabX=n.x;m_grabY=n.y;
                    break;
                }
            }
        }
        if(m_drag)
        {
            if(left)
            {
                // Selecting a node must never round or move it. Begin dragging only after a small threshold.
                if(std::abs(x-m_pressX)>3||std::abs(y-m_pressY)>3)m_dragMoved=true;
                if(m_dragMoved)
                {
                    m_dragX=std::clamp(m_grabX+(x-m_pressX)/m_zoom,-1000000.0f,1000000.0f);
                    m_dragY=std::clamp(m_grabY+(y-m_pressY)/m_zoom,-1000000.0f,1000000.0f);
                }
            }
            else
            {
                if(m_dragMoved&&m_document.move(m_graph,m_selection,m_dragX,m_dragY))m_status="Layout changed. Ctrl+S saves a new copy.";
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
        c.text(*m_font,"Native Blueprint editor",1260,13,muted,Align::Right);
        const std::string titleText=(m_document.name.empty()?"No document open":m_document.name)+(m_document.dirty()?" *":"");
        c.clip(12,38,590,34);c.text(*m_font,shorten(titleText,65).c_str(),18,43,accent);c.noClip();
        const auto button=[&](float x,float width,const char* label,bool enabled){
            c.roundRect(x,39,width,30,5,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.14f,0.18f,0.24f,1});
            c.text(*m_font,label,x+width*0.5f,44,enabled?text:muted,Align::Center);
        };
        button(610,132,"Compile / export",!m_document.graphs.empty());button(748,110,"Save copy",!m_document.graphs.empty());button(864,84,"Undo",m_document.canUndo());
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
            const auto& rows=m_rows;
            const auto nodeX=[&](size_t i){return m_drag&&i==m_selection?m_dragX:graph.nodes[i].x;};
            const auto nodeY=[&](size_t i){return m_drag&&i==m_selection?m_dragY:graph.nodes[i].y;};
            for(size_t wire=0;wire<graph.links.size();++wire)
            {
                const auto& l=graph.links[wire];const auto& pins=m_wirePins[wire];
                const float x1=kLeft+m_x+(nodeX(l.from)+kNodeWidth)*m_zoom,y1=kTop+m_y+(nodeY(l.from)+pinY(pins.output))*m_zoom;
                const float x2=kLeft+m_x+nodeX(l.to)*m_zoom,y2=kTop+m_y+(nodeY(l.to)+pinY(pins.input))*m_zoom;
                if(std::max(x1,x2)<kLeft||std::min(x1,x2)>kRight||std::max(y1,y2)<kTop||std::min(y1,y2)>kBottom)continue;
                const float mid=(x1+x2)*0.5f;
                const Color color=l.output=="then"||l.input=="in"?text:accent;
                c.line(x1,y1,mid,y1,2,color);c.line(mid,y1,mid,y2,2,color);c.line(mid,y2,x2,y2,2,color);
            }
            if(m_pending<graph.nodes.size())
            {
                const auto& n=graph.nodes[m_pending];const auto& pins=rows[m_pending].outputs;
                const auto pin=std::find(pins.begin(),pins.end(),m_pendingPin);
                if(pin!=pins.end())c.ring(kLeft+m_x+(n.x+kNodeWidth)*m_zoom,kTop+m_y+(n.y+pinY(static_cast<size_t>(pin-pins.begin())))*m_zoom,9,2,accent);
            }
            for(size_t i=0;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];
                const float x=kLeft+m_x+nodeX(i)*m_zoom,y=kTop+m_y+nodeY(i)*m_zoom,w=kNodeWidth*m_zoom,h=rows[i].height*m_zoom;
                if(x+w<kLeft||x>kRight||y+h<kTop||y>kBottom) continue;
                c.roundRect(x,y,w,h,5,{0.15f,0.20f,0.27f,1});
                c.rect(x,y,w,24*m_zoom,n.kind=="entry"?Color{0.54f,0.20f,0.25f,1}:Color{0.19f,0.35f,0.53f,1});
                if(i==m_selection)c.roundLine(x,y,w,h,5,2,accent);
                // Draw only the visible rows of tall nodes.
                const size_t first=static_cast<size_t>(std::max(0.0f,std::floor(((kTop-y)/m_zoom-kPinStart)/kPinStep)-1));
                const size_t last=static_cast<size_t>(std::max(0.0f,std::ceil(((kBottom-y)/m_zoom-kPinStart)/kPinStep)+2));
                for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)
                    c.circle(x,y+pinY(p)*m_zoom,4*m_zoom,rows[i].inputs[p]=="in"?text:accent);
                for(size_t p=first;p<std::min(last,rows[i].outputs.size());++p)
                    c.circle(x+w,y+pinY(p)*m_zoom,4*m_zoom,rows[i].outputs[p]=="then"?text:accent);
                if(m_zoom>=0.65f)
                {
                    c.push();c.clip(x+2,y+1,w-4,24*m_zoom);
                    c.text(*m_font,shorten(title(n),24).c_str(),x+8,y+3,text);c.pop();
                    // Clip each column separately to prevent long pin names overlapping.
                    c.push();c.clip(x+6,y+24*m_zoom,w*0.5f-8,h-24*m_zoom);
                    for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)c.text(*m_font,shorten(rows[i].inputs[p],12).c_str(),x+8,y+pinY(p)*m_zoom-8,muted);
                    c.pop();
                    c.push();c.clip(x+w*0.5f,y+24*m_zoom,w*0.5f-6,h-24*m_zoom);
                    for(size_t p=first;p<std::min(last,rows[i].outputs.size());++p)c.text(*m_font,shorten(rows[i].outputs[p],12).c_str(),x+w-8,y+pinY(p)*m_zoom-8,muted,Align::Right);
                    c.pop();
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
        c.roundRect(8,kBottom-40,kLeft-16,32,5,{0.19f,0.33f,0.50f,1});
        c.text(*m_font,"New Blueprint",kLeft*0.5f,kBottom-34,text,Align::Center);
        c.text(*m_font,"NODE LIBRARY",kRight+12,kTop+10,accent);
        c.roundRect(kRight+8,kTop+38,kWidth-kRight-16,30,4,m_focus==1?Color{0.24f,0.35f,0.48f,1}:Color{0.16f,0.21f,0.28f,1});
        c.clip(kRight+12,kTop+38,kWidth-kRight-24,30);
        c.text(*m_font,m_query.empty()?"Search nodes...":m_query.c_str(),kRight+14,kTop+43,text);c.noClip();
        for(size_t row=0;row<10&&m_paletteFirst+row<m_palette.size();++row)
        {
            const auto* n=m_palette[m_paletteFirst+row];const float y=kTop+80+26*static_cast<float>(row);
            c.rect(kRight+8,y,kWidth-kRight-16,24,{0.15f,0.20f,0.27f,1});
            c.clip(kRight+12,y,kWidth-kRight-24,24);
            c.text(*m_font,shorten(n->category+" / "+n->title,34).c_str(),kRight+14,y+2,text);c.noClip();
        }
        c.text(*m_font,(std::to_string(m_palette.size())+" results | wheel to scroll").c_str(),kRight+12,kTop+346,muted);
        c.text(*m_font,"DETAILS",kRight+12,kTop+378,accent);
        if(!m_document.graphs.empty()&&m_selection<m_document.graphs[m_graph].nodes.size())
        {
            const auto& n=m_document.graphs[m_graph].nodes[m_selection];
            const auto* spec=blueprintSpec(n.kind);
            c.clip(kRight+8,kTop+404,kWidth-kRight-16,44);
            c.paragraph(*m_font,spec?spec->title.c_str():n.kind.c_str(),kRight+12,kTop+408,244,text);c.noClip();
            if(n.kind.rfind("literal:",0)==0)
            {
                c.roundRect(kRight+8,kTop+456,kWidth-kRight-16,30,4,m_focus==2?Color{0.24f,0.35f,0.48f,1}:Color{0.16f,0.21f,0.28f,1});
                c.clip(kRight+12,kTop+456,kWidth-kRight-24,30);
                c.text(*m_font,(m_focus==2?m_edit:n.ref).c_str(),kRight+14,kTop+461,text);c.noClip();
                c.text(*m_font,"Click value, type, Enter",kRight+12,kTop+490,muted);
            }
            c.roundRect(kRight+8,kBottom-40,kWidth-kRight-16,32,5,{0.38f,0.19f,0.23f,1});
            c.text(*m_font,"Delete selected", (kWidth+kRight)*0.5f,kBottom-34,text,Align::Center);
        }
        c.rect(0,kBottom,kWidth,kHeight-kBottom,{0.12f,0.16f,0.22f,1});
        c.text(*m_font,shorten(m_status,110).c_str(),12,kBottom+6,text);
        c.text(*m_font,"Click output then input: connect | Ctrl+click pin: disconnect | Drag: move | Ctrl+S: save copy",12,kBottom+26,muted);
    }
}
