#include <cstdlib>
#include <angelscript.h>
#include "app/blueprint_workspace.h"
#include "tools/blueprint/native_compile.h"
#include "tools/blueprint/native_assets.h"
#include "tools/blueprint/graph_geometry.h"
#include "tools/blueprint/native_import.h"
#include "script/script_host.h"
#include "script/debugger.h"
#include "script/source_fingerprint.h"
#include "screens/screens.h"
#include "bindings/bindings.h"
#include "assets/assets.h"
#include "assets/model.h"
#include "base/log.h"
#include "base/memory.h"
#include "audio/audio.h"
#include "base/json_tree.h"
#include "base/curve.h"
#include "base/json_value.h"
#include "scene/world.h"
#include "ui/ui.h"
#include "canvas/canvas.h"
#include "input/input.h"
#include "platform/platform.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <cctype>

namespace nx
{
    namespace
    {
        constexpr float kTop = 76;
        constexpr float kBaseWidth=1280,kBaseHeight=720;
        constexpr float kNodeWidth = 196, kNodeHeight = 72;
        // Each native pin is provided by the generated catalogue.
        constexpr float kPinStart=38, kPinStep=22;
        constexpr const char* variableTypes[]={"bool","int","uint","int64","uint64","float","double","string","Playlist@","WebSocket@","Widget@","Thread@","Channel@","Samples@","AudioStream@","Sound@","Voice","Curve@","Batch@","Packed@","Zip@","Font@","Texture@","array<int>","array<uint>","array<float>","array<bool>","array<string>","array<Vec2>","array<Color>","BP_MapInt","BP_MapString","BP_SetInt","BP_SetString"};
        constexpr Color text = {0.88f, 0.91f, 0.96f, 1};
        constexpr Color muted = {0.57f, 0.65f, 0.76f, 1};
        constexpr Color accent = {0.27f, 0.65f, 0.98f, 1};
        bool in(float x, float y, float left, float top, float right, float bottom)
        { return x >= left && x < right && y >= top && y < bottom; }
        constexpr Color nodeColours[]={{0.20f,0.34f,0.48f,1},{0.59f,0.23f,0.24f,1},{0.25f,0.47f,0.31f,1},{0.63f,0.40f,0.17f,1},{0.35f,0.30f,0.57f,1},{0.18f,0.48f,0.51f,1},{0.54f,0.29f,0.46f,1},{0.35f,0.37f,0.40f,1}};
        Color rgbColour(unsigned colour,Color fallback)
        {
            if(!colour)return fallback;
            if(colour<8)return nodeColours[colour];
            return {static_cast<float>((colour>>16)&255u)/255.f,static_cast<float>((colour>>8)&255u)/255.f,static_cast<float>(colour&255u)/255.f,1};
        }
        std::string title(const BlueprintDocument::Node& node)
        {
            if(!node.displayName.empty())return node.displayName;
            if(node.kind=="camera:orbit-timed")return "Orbit camera (timed)";
            if(node.kind=="spawn:plane")return "Spawn floor plane";
            if(node.kind=="render:space")return "Render 3D scene";
            if(node.kind.rfind("capture:",0)==0)return "Capture "+node.ref;
            if(node.kind.rfind("typed:",0)==0)return std::string(node.kind.ends_with(":get")?"Get ":"Set ")+node.ref;
            if(node.kind.rfind("global:",0)==0)return (node.kind.rfind("global:get:",0)==0?"Get global ":"Set global ")+node.ref;
            if(node.kind.rfind("var:",0)==0)return (node.kind.rfind("var:get:",0)==0?"Get ":"Set ")+node.ref;
            if(blueprintNamedCall(node.kind)||node.kind.rfind("data:",0)==0||node.kind=="actor:is-class"||node.kind=="actor:cast-class")
                if(const auto* spec=blueprintSpec(node.kind))return spec->title+": "+node.ref;
            if(node.kind=="attach:behaviour")return "Attach "+node.ref;
            if(node.kind=="attach:direct")return "Attach directly: "+node.ref;
            if(node.kind=="level:attach")return "Attach named: "+node.ref;
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
        Color pinColor(const std::string& type)
        {
            if(type.rfind("local:",0)==0)return {0.95f,0.68f,0.35f,1};
            if(type=="exec")return text;
            if(type=="bool")return {0.95f,0.39f,0.45f,1};
            if(type=="string")return {0.84f,0.47f,0.90f,1};
            if(type=="float"||type=="double")return {0.46f,0.83f,0.48f,1};
            if(type=="int"||type=="uint"||type=="int64"||type=="uint64")return {0.34f,0.82f,0.81f,1};
            if(type=="Vec2"||type=="Vec3"||type=="Color")return {0.96f,0.71f,0.32f,1};
            return accent;
        }
        bool appendCharacter(std::string& value,u32 ch,size_t limit)
        {
            if(ch<32||ch==127||ch>0x10ffff||(ch>=0xd800&&ch<=0xdfff))return false;
            char bytes[4];size_t count=0;
            if(ch<0x80)bytes[count++]=static_cast<char>(ch);
            else if(ch<0x800){bytes[count++]=static_cast<char>(0xc0|(ch>>6));bytes[count++]=static_cast<char>(0x80|(ch&63));}
            else if(ch<0x10000){bytes[count++]=static_cast<char>(0xe0|(ch>>12));bytes[count++]=static_cast<char>(0x80|((ch>>6)&63));bytes[count++]=static_cast<char>(0x80|(ch&63));}
            else{bytes[count++]=static_cast<char>(0xf0|(ch>>18));bytes[count++]=static_cast<char>(0x80|((ch>>12)&63));bytes[count++]=static_cast<char>(0x80|((ch>>6)&63));bytes[count++]=static_cast<char>(0x80|(ch&63));}
            if(value.size()+count>limit)return false;
            value.append(bytes,count);return true;
        }
        std::string shorten(const std::string& s, size_t length)
        {
            if (s.size() <= length) return s;
            size_t cut = length;
            while (cut && (static_cast<unsigned char>(s[cut]) & 0xc0) == 0x80) --cut;
            return s.substr(0, cut) + "...";
        }
    }

    void BlueprintWorkspace::open(Services& s,bool projects)
    {
        if (active()) return;
        if(!s.platform->acquireBlueprintLease())
        {
            m_status="Cannot open Blueprint Studio: save directory is busy or unavailable.";
            NX_ERROR("Blueprint Studio: %s",m_status.c_str());return;
        }
        m_font = s.assets->font("global/freesans.sfn", 16);
        if (!m_font || m_font->failed)
        {
            if (m_font) s.assets->release(m_font);
            m_font = nullptr;
            s.platform->releaseBlueprintLease();
            NX_ERROR("Blueprint Studio: cannot load the native workspace font.");
            return;
        }
        m_measureCanvas=s.canvas;
        s.platform->setEditorChrome(true);
        m_wantedPointer = s.input->wantsPointer();
        s.input->setWantsPointer(true);
        s.platform->lockMouse(false);
        s.input->consume();
        m_left = m_pan = m_drag = false;
        m_shortcuts = 0;
        m_previousTyping=s.input->typing();m_focus=0;m_browsing=false;m_assetBrowser=false;m_actorEditor=false;m_actorTabId.clear();m_actorTabScreen.clear();m_contentDrawer=false;m_contentFirst=0;m_contentDrag.clear();m_actorDetailsScroll=0;m_splitter=0;s.input->setTyping(false);
        loadLayout(s);resizeWorkspace(*s.canvas);
        if(m_document.graphs.empty())
        {
            FileData recovery;
            const bool exists=s.platform->fileTime("own/blueprint-recovery.bp.json")!=0;
            const bool readable=exists&&s.platform->readFile("own/blueprint-recovery.bp.json",recovery,MemTag::General);
            const bool found=exists&&(!readable||recovery.size>0);
            m_protectDamagedRecovery=false;
            m_checkpointBytes.clear();
            if(readable&&recovery.size)
                m_checkpointBytes.assign(reinterpret_cast<const char*>(recovery.data),recovery.size);
            if(readable&&recovery.size>0&&m_document.load(reinterpret_cast<const char*>(recovery.data),recovery.size,m_status))
            {
                m_document.markRecovered();m_checkpointPresent=true;m_checkpointRevision=m_document.revision();
                selectGraph(0);m_status="Recovered unsaved Blueprint. Save as to keep it.";
            }
            else
            {
                m_document.create();selectGraph(0);
                m_protectDamagedRecovery=found;
                m_status=found?"Recovery damaged or unreadable. Original will be archived before autosave.":"New Blueprint. Search the node library to start.";
            }
        }
        if(!m_document.graphs.empty()&&m_graphTabs.empty())selectGraph(std::min(m_graph,m_document.graphs.size()-1));
        filterCatalogue();
        if(projects)browseFiles(s);
        NX_INFO("Blueprint Studio: workspace ready (%s)",m_status.c_str());
    }

    void BlueprintWorkspace::close(Services& s)
    {
        if (!active()) return;
        stopPreview(s);
        m_device.disconnect();m_deviceAddressEditing=false;m_deployPassword.clear();m_deployField=-1;
        stopTone(s);m_audioEditor=false;m_toneWav.clear();m_clipSource.clear();m_audioClip=BlueprintAudioClip{};
        s.platform->setEditorChrome(false);
        saveActorTab();cancelActorDrag();m_actorOrbit=false;m_splitter=0;m_actorEditor=false;m_contentDrag.clear();m_contentPanelDrag=false;releaseContentPreviews();
        releaseWidgetPreview();releasePlacementPreview();m_widgetDesigner=m_visualPlacement=false;
        checkpoint(s,true);saveLayout(s);
        cancelDrag();clearWire();m_quickDrag=m_quickMenu=m_pinMenu=m_rightDown=m_pinContext=false;m_layoutMenu=m_layoutDragging=false;m_graphBack.clear();m_focus=0;m_browsing=false;m_assetBrowser=false;s.input->setTyping(m_previousTyping);
        s.assets->release(m_font);
        m_font = nullptr;m_measureCanvas=nullptr;
        s.platform->releaseBlueprintLease();
        s.input->setWantsPointer(m_wantedPointer);
        s.input->consume();
    }

    std::string BlueprintWorkspace::layoutBytes() const
    {
        return "{\"version\":1,\"left\":"+std::to_string(m_leftPanel)+",\"graphRight\":"+std::to_string(m_graphRightPanel)+
            ",\"actorRight\":"+std::to_string(m_actorRightPanel)+",\"scale\":"+std::to_string(m_uiScale)+
            ",\"collapsed\":"+std::to_string(m_actorCollapsed)+",\"content\":"+(m_contentDrawer?"true":"false")+
            ",\"contentFloating\":"+(m_contentFloating?"true":"false")+",\"contentX\":"+std::to_string(m_contentPanelX)+
            ",\"contentY\":"+std::to_string(m_contentPanelY)+",\"contentWidth\":"+std::to_string(m_contentPanelWidth)+
            ",\"contentTop\":"+(m_contentDockTop?"true":"false")+",\"contentHeight\":"+std::to_string(m_contentPanelHeight)+
            ",\"graphCategories\":"+(m_graphTree?"true":"false")+",\"contentSide\":"+std::to_string(m_contentDockSide)+
            ",\"contentSideWidth\":"+std::to_string(m_contentSideWidth)+",\"minimap\":"+(m_minimap?"true":"false")+
            ",\"hideGraphLeft\":"+(m_graphHideLeft?"true":"false")+",\"hideGraphRight\":"+(m_graphHideRight?"true":"false")+",\"wireStyle\":"+std::to_string(m_wireStyle)+",\"confirmDelete\":"+(m_confirmDelete?"true":"false")+"}\n";
    }
    void BlueprintWorkspace::loadLayout(Services& s)
    {
        m_leftPanel=248;m_graphRightPanel=268;m_actorRightPanel=310;m_uiScale=0;m_actorCollapsed=0;m_contentDrawer=false;m_contentFloating=false;m_contentPanelX=0.25f;m_contentPanelY=0.65f;m_contentPanelWidth=620;m_contentDockTop=false;m_contentPanelHeight=174;m_contentDockSide=0;m_contentSideWidth=300;
        m_graphTree=true;m_minimap=m_minimapDragging=false;m_graphHideLeft=m_graphHideRight=false;m_navigationFirst=0;m_navigationCollapsed.clear();
        m_wireStyle=0;m_confirmDelete=true;m_skipDeleteConfirmation=false;m_deleteSelection.clear();
        FileData file;JsonTree tree;
        if(s.platform->readFile("own/blueprint-editor-layout.json",file,MemTag::General)&&file.size&&file.size<=4096&&
            tree.parse(reinterpret_cast<const char*>(file.data),file.size))
        {
            const auto root=tree.root();
            if(root["version"].isNumber()&&root["version"].number()==1)
            {
                const auto bounded=[&](const char* field,float fallback,float lo,float hi)
                {
                    const auto v=root[field];const float n=v.number(fallback);
                    return v.isNumber()&&std::isfinite(n)&&n>=lo&&n<=hi?n:fallback;
                };
                m_leftPanel=bounded("left",248,220,400);m_graphRightPanel=bounded("graphRight",268,250,420);
                m_actorRightPanel=bounded("actorRight",310,310,440);m_uiScale=static_cast<unsigned>(bounded("scale",0,0,3));
                m_actorCollapsed=static_cast<unsigned>(bounded("collapsed",0,0,31));
                m_contentDrawer=root["content"].flag();m_contentFloating=root["contentFloating"].flag();
                m_contentPanelX=bounded("contentX",0.25f,0,1);m_contentPanelY=bounded("contentY",0.65f,0,1);
                m_contentPanelWidth=bounded("contentWidth",620,460,1000);
                m_contentDockTop=root["contentTop"].flag();m_contentPanelHeight=bounded("contentHeight",174,174,510);
                m_graphTree=root["graphCategories"].type()!=JsonTree::Type::Bool||root["graphCategories"].flag();m_minimap=root["minimap"].flag();
                m_graphHideLeft=root["hideGraphLeft"].flag();m_graphHideRight=root["hideGraphRight"].flag();
                m_wireStyle=static_cast<unsigned>(bounded("wireStyle",0,0,2));
                m_confirmDelete=root["confirmDelete"].type()!=JsonTree::Type::Bool||root["confirmDelete"].flag();
                m_contentDockSide=static_cast<unsigned>(bounded("contentSide",0,0,2));
                m_contentSideWidth=bounded("contentSideWidth",300,260,480);
                NX_INFO("Blueprint Studio: restored editor layout");
            }
        }
        m_layoutBaseline=layoutBytes();
    }
    void BlueprintWorkspace::saveLayout(Services& s)
    {
        if(m_splitter||m_contentPanelDrag||m_contentResize)return; // persist the final divider position, not every intermediate frame.
        const auto bytes=layoutBytes();if(bytes==m_layoutBaseline)return;
        const auto previous=m_status;
        if(writeVerified(s,"own/blueprint-editor-layout.json",bytes))m_layoutBaseline=bytes;
        else NX_ERROR("Blueprint Studio: editor layout could not be saved");
        m_status=previous;
    }
    void BlueprintWorkspace::resizeWorkspace(const Canvas& c)
    {
        const float w=c.pixelWidth(),h=c.pixelHeight();
        const float automatic=std::max(1.f,std::min(w/1920.f,h/1200.f));
        const float requested=m_uiScale==0?automatic:m_uiScale==1?1.f:m_uiScale==2?1.25f:1.5f;
        const float density=std::min(requested,std::min(w/kBaseWidth,h/kBaseHeight));
        const float width=density>0?w/density:kBaseWidth;
        const float height=density>0?h/density:kBaseHeight;
        // Keep legacy letterboxing for small clients to preserve every control.
        const bool small=w<kBaseWidth||h<kBaseHeight;
        const float logicalWidth=small?kBaseWidth:width,logicalHeight=small?kBaseHeight:height;
        if(std::abs(logicalWidth-kWidth)>0.5f||std::abs(logicalHeight-kHeight)>0.5f)
        {
            cancelDrag();clearWire();m_layoutDragging=m_quickDrag=m_quickMenu=m_pan=false;
            cancelVisualPlacement();m_sceneOrbit=m_scenePan=false;cancelActorDrag();m_contentDrag.clear();m_contentPanelDrag=m_contentResize=false;m_splitter=0;m_actorOrbit=false;m_widgetPreviewDrag=m_widgetDragging=false;m_placementReady=false;
        }
        kWidth=logicalWidth;kHeight=logicalHeight;kLeft=m_leftPanel;kRight=kWidth-m_graphRightPanel;kBottom=kHeight-46;
        const float extra=std::max(0.f,kHeight-kBaseHeight);
        kNavTop=kTop+154.f;
        kRows=4+static_cast<size_t>(extra/62.f);
        kVariableTop=kNavTop+31.f*static_cast<float>(kRows)+32.f;
        kVariableRows=static_cast<size_t>(std::max(1.f,std::floor((kBottom-72.f-kVariableTop)/31.f)));
        kVariableBottom=kVariableTop+31.f*static_cast<float>(kVariableRows);
        kPaletteRows=10+static_cast<size_t>(extra/26.f);kPaletteExtra=26.f*static_cast<float>(kPaletteRows-10);
        if(m_graph<m_document.graphs.size()&&m_graph>=m_first+kRows)m_first=m_graph-kRows+1;
    }
    bool BlueprintWorkspace::resizePanels(float x,float y,bool left,bool click,bool actor)
    {
        const float right=actor?kWidth-m_actorRightPanel:kRight;
        if(click&&y>=kTop&&y<kBottom)
        {
            if((actor||!m_graphHideLeft)&&std::abs(x-kLeft)<=4)m_splitter=1;
            else if((actor||!m_graphHideRight)&&std::abs(x-right)<=4)m_splitter=2;
        }
        if(!m_splitter)return false;
        if(left)
        {
            if(m_splitter==1)m_leftPanel=std::clamp(x,220.f,400.f);
            else if(actor)m_actorRightPanel=std::clamp(kWidth-x,310.f,440.f);
            else m_graphRightPanel=std::clamp(kWidth-x,250.f,420.f);
            kLeft=m_leftPanel;kRight=kWidth-m_graphRightPanel;m_placementReady=false;
        }
        else m_splitter=0;
        return true;
    }
    bool BlueprintWorkspace::modalOpen() const
    {return m_assetBrowser||m_browsing||m_focus==5||m_focus==6||m_focus==9||m_focus==10||m_focus==11||m_focus==12||m_focus==13||m_focus==14||m_focus==15||m_focus==17||m_focus==18||m_focus==20||m_focus==21||m_focus==22||m_focus==23||m_focus==24||m_focus==25||m_focus==26||m_focus==27||m_focus==28;}
    void BlueprintWorkspace::resetGraphTabs()
    {
        releaseContentPreviews();m_graphTabs.clear();m_actorTabs.clear();m_visualTabs.clear();m_activeVisualTab={};m_activeTab={};m_tabFirst=0;
    }
    size_t BlueprintWorkspace::graphTabIndex(const GraphTab& tab) const
    {
        const auto found=std::find_if(m_document.graphs.begin(),m_document.graphs.end(),[&](const BlueprintDocument::Graph& g){return g.owner==tab.owner&&g.name==tab.name;});
        return static_cast<size_t>(found-m_document.graphs.begin());
    }
    void BlueprintWorkspace::pruneGraphTabs()
    {
        m_graphTabs.erase(std::remove_if(m_graphTabs.begin(),m_graphTabs.end(),[&](const GraphTab& tab){return graphTabIndex(tab)>=m_document.graphs.size();}),m_graphTabs.end());
        m_actorTabs.erase(std::remove_if(m_actorTabs.begin(),m_actorTabs.end(),[&](const ActorTab& tab){const auto* asset=m_document.visualAsset(tab.id);return !asset||asset->kind!="actor";}),m_actorTabs.end());
        if(!m_actorTabId.empty()&&!m_document.visualAsset(m_actorTabId))m_actorTabId.clear();
        const auto screens=m_document.screens();
        m_visualTabs.erase(std::remove_if(m_visualTabs.begin(),m_visualTabs.end(),[&](const VisualTab& tab)
        {
            if(std::find(screens.begin(),screens.end(),tab.screen)==screens.end())return true;
            if(!tab.widget)return false;
            const auto* asset=m_document.visualAsset(tab.id);return !asset||asset->kind!="widget";
        }),m_visualTabs.end());
        if(m_tabFirst>=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size())m_tabFirst=0;
    }
    std::vector<BlueprintWorkspace::ComponentNavRow> BlueprintWorkspace::componentNavigation() const
    {
        std::vector<ComponentNavRow> rows;
        const auto& owner=m_document.graphs[m_graph].owner;
        for(const auto& asset:m_document.visualAssets())
        {
            if(asset.kind!="actor")continue;
            const auto behaviour=m_document.actorBehaviour(asset.id);
            if(owner.rfind("behaviour:",0)==0&&behaviour!=owner&&"behaviour:"+behaviour!=owner)continue;
            if(owner.rfind("behaviour:",0)!=0&&asset.id!=m_actorTabId&&m_document.moduleForOwner(behaviour)!=owner)continue;
            rows.push_back({asset.id,asset.title,static_cast<size_t>(-1),0});
            std::string resolved,error;JsonTree tree;if(!m_document.visualActorSource(asset.id,resolved,error)||!tree.parse(resolved.data(),resolved.size()))continue;
            const auto entities=tree.root()["entities"],parents=tree.root()["parents"];
            for(size_t i=0;i<entities.count();++i){const auto item=entities[static_cast<u32>(i)];unsigned depth=1;int parent=static_cast<int>(parents[static_cast<u32>(i)].number(-1));for(unsigned j=0;parent>=0&&static_cast<size_t>(parent)<i&&j<8;++j){++depth;parent=static_cast<int>(parents[static_cast<u32>(parent)].number(-1));}rows.push_back({asset.id,std::string(item["name"].text("Component"))+" / "+item["shape"].text(item["camera"].exists()?"camera":"model"),i,depth});}
        }
        return rows;
    }
    std::vector<BlueprintWorkspace::NavigationRow> BlueprintWorkspace::navigationRows() const
    {
        std::vector<NavigationRow> result;
        const auto& graphs=m_document.graphs;
        if(!m_graphTree)
        {
            for(size_t i=0;i<graphs.size();++i)result.push_back({"",graphs[i].owner+" / "+graphs[i].name,i,0});
            return result;
        }
        const auto collapsed=[&](const std::string& key){return std::find(m_navigationCollapsed.begin(),m_navigationCollapsed.end(),key)!=m_navigationCollapsed.end();};
        const auto category=[](const BlueprintDocument::Graph& g)->unsigned
        {
            if(g.dataRole=="enum"||g.dataRole=="struct")return 5;
            if(g.sharedLibrary)return 4;
            if(g.inlineMacro)return 3;
            if(g.kind=="event"||g.dataRole=="event"||g.dataRole=="customEvent")return 0;
            if(g.name=="Construction")return 1;
            return 2;
        };
        const char* labels[]={"Events","Construction","Functions","Macros","Shared library","Types"};
        for(unsigned c=0;c<6;++c)
        {
            if(std::none_of(graphs.begin(),graphs.end(),[&](const BlueprintDocument::Graph& g){return category(g)==c;}))continue;
            const auto group="category:"+std::to_string(c);
            result.push_back({group,labels[c],static_cast<size_t>(-1),0});
            if(collapsed(group))continue;
            for(size_t i=0;i<graphs.size();++i)if(category(graphs[i])==c)
            {
                const auto& g=graphs[i];
                const auto owner=g.owner.rfind("behaviour:",0)==0?g.owner.substr(10):g.owner;
                result.push_back({"",owner+" / "+g.name,i,1});
            }
        }
        return result;
    }
    void BlueprintWorkspace::revealNavigationGraph(size_t index)
    {
        if(!m_graphTree||index>=m_document.graphs.size())return;
        auto rows=navigationRows();
        auto found=std::find_if(rows.begin(),rows.end(),[&](const NavigationRow& row){return row.graph==index;});
        if(found==rows.end())
        {
            m_navigationCollapsed.clear();
            rows=navigationRows();found=std::find_if(rows.begin(),rows.end(),[&](const NavigationRow& row){return row.graph==index;});
        }
        if(found==rows.end())return;
        const size_t row=static_cast<size_t>(found-rows.begin());
        if(row<m_navigationFirst)m_navigationFirst=row;
        if(row>=m_navigationFirst+kRows)m_navigationFirst=row-kRows+1;
    }
    void BlueprintWorkspace::selectGraph(size_t index)
    {
        if (index >= m_document.graphs.size()) return;
        pruneGraphTabs();
        for(auto& tab:m_graphTabs)if(tab.owner==m_activeTab.first&&tab.name==m_activeTab.second)
        {tab.x=m_x;tab.y=m_y;tab.zoom=m_zoom;tab.viewed=true;}
        const auto& target=m_document.graphs[index];
        m_activeTab={target.owner,target.name};
        auto found=std::find_if(m_graphTabs.begin(),m_graphTabs.end(),[&](const GraphTab& tab){return tab.owner==target.owner&&tab.name==target.name;});
        if(found==m_graphTabs.end())
        {
            if(m_graphTabs.size()>=32)m_graphTabs.erase(m_graphTabs.begin());
            m_graphTabs.push_back({target.owner,target.name});found=m_graphTabs.end()-1;
        }
        revealTab(static_cast<size_t>(found-m_graphTabs.begin()));
        cancelDrag();clearWire();m_highlightWire=m_moveWire=m_lastClickedWire=static_cast<size_t>(-1);m_nodeClickTime=m_wireClickTime=-1;m_diagnostic={};
        m_quickDrag=m_quickMenu=m_pinMenu=m_rightDown=false;m_quickVariableFirst=0;
        m_componentNavFirst=0;m_nodeContext=false;m_rgbOpen=false;m_commentResize=0;m_layoutMenu=m_layoutDragging=false;m_layoutComment=m_layoutLink=static_cast<size_t>(-1);m_selectedNodes.clear();
        m_graph = index; m_selection = static_cast<size_t>(-1);
        rebuildPins();fitGraph();filterCatalogue();
        if(found->viewed){m_x=found->x;m_y=found->y;m_zoom=found->zoom;}
        if (index < m_first) m_first = index;
        if (index >= m_first + kRows) m_first = index - kRows + 1;
        revealNavigationGraph(index);
    }

    bool BlueprintWorkspace::visualTabOpen() const
    {return !m_actorEditor&&m_assetBrowser&&m_assetLibrary&&(m_widgetDesigner||m_visualPlacement);}
    void BlueprintWorkspace::rememberVisualTab()
    {
        if(!visualTabOpen()){m_activeVisualTab={};return;}
        const auto screen=assetScreen();
        if(screen.empty())return;
        const bool widget=m_widgetDesigner;
        const auto id=widget?m_visualSelection:screen;
        const auto key=std::make_pair(widget,id);
        pruneGraphTabs();
        auto found=std::find_if(m_visualTabs.begin(),m_visualTabs.end(),[&](const VisualTab& tab){return tab.widget==widget&&tab.id==id&&tab.screen==screen;});
        if(found==m_visualTabs.end())
        {
            if(m_visualTabs.size()>=16)m_visualTabs.erase(m_visualTabs.begin());
            VisualTab tab;tab.widget=widget;tab.id=id;tab.screen=screen;
            m_visualTabs.push_back(std::move(tab));found=m_visualTabs.end()-1;
        }
        found->graph=m_activeTab;found->asset=m_visualSelection;
        found->selection=m_widgetSelection;found->instance=m_placementSelectedId;
        found->first=widget?m_widgetFirst:m_placementFirst;
        found->fourThree=m_widgetFourThree;found->threeD=m_placement3D;found->outliner=m_placementOutliner;
        found->yaw=m_placementYaw;found->distance=m_placementDistance;found->height=m_placementHeight;found->mode=m_gizmoMode;
        found->focusX=m_sceneFocusX;found->focusY=m_sceneFocusY;found->focusZ=m_sceneFocusZ;found->pitch=m_scenePitch;
        if(m_activeVisualTab!=key)
        {
            m_activeVisualTab=key;
            revealTab(m_graphTabs.size()+m_actorTabs.size()+static_cast<size_t>(found-m_visualTabs.begin()));
        }
    }
    void BlueprintWorkspace::leaveVisualTab()
    {
        rememberVisualTab();
        m_widgetDesigner=m_visualPlacement=m_assetBrowser=false;
        m_widgetDragging=m_widgetPreviewDrag=m_widgetTimelinePlay=false;
        m_widgetDragId.clear();cancelVisualPlacement();releaseWidgetPreview();releasePlacementPreview();
        m_activeVisualTab={};
    }
    void BlueprintWorkspace::openVisualTab(const VisualTab& original)
    {
        const VisualTab tab=original;
        if(m_actorEditor)leaveActorViewport(false);
        if(visualTabOpen())leaveVisualTab();
        auto found=std::find_if(m_document.graphs.begin(),m_document.graphs.end(),[&](const BlueprintDocument::Graph& graph)
        {return graph.owner==tab.graph.first&&graph.name==tab.graph.second;});
        if(found==m_document.graphs.end()||found->owner!=tab.screen)
            found=std::find_if(m_document.graphs.begin(),m_document.graphs.end(),[&](const BlueprintDocument::Graph& graph){return graph.owner==tab.screen;});
        if(found==m_document.graphs.end())return;
        selectGraph(static_cast<size_t>(found-m_document.graphs.begin()));
        m_assetBrowser=m_assetLibrary=true;m_visualInput=0;m_visualSelection=tab.asset;
        m_widgetDesigner=tab.widget;m_visualPlacement=!tab.widget;
        m_widgetPalette=m_widgetEvents=m_widgetUnbind=m_widgetAnchors=m_widgetTimeline=m_widgetTimelinePlay=false;
        m_widgetTimelineTime=0;m_widgetSelection=tab.selection;m_widgetFirst=tab.widget?tab.first:0;
        m_widgetFourThree=tab.fourThree;
        m_placementSelectedOwner=tab.screen;m_placementSelectedId=tab.instance;
        m_placementFirst=tab.widget?0:tab.first;m_placement3D=tab.threeD;m_placementOutliner=tab.outliner;
        m_placementYaw=tab.yaw;m_placementDistance=tab.distance;m_placementHeight=tab.height;m_gizmoMode=tab.mode;
        m_sceneFocusX=tab.focusX;m_sceneFocusY=tab.focusY;m_sceneFocusZ=tab.focusZ;m_scenePitch=tab.pitch;m_sceneOrbit=m_scenePan=false;
        rememberVisualTab();
        m_status=tab.widget?"Widget designer tab restored.":"Scene placement tab restored.";
    }

    bool BlueprintWorkspace::contentSideActive() const
    {return !m_actorEditor&&!visualTabOpen()&&m_contentDrawer&&!m_contentFloating&&m_contentDockSide!=0;}
    float BlueprintWorkspace::graphBaseLeft() const
    {return !m_actorEditor&&!visualTabOpen()&&m_graphHideLeft?0.f:kLeft;}
    float BlueprintWorkspace::graphBaseRight() const
    {return !m_actorEditor&&!visualTabOpen()&&m_graphHideRight?kWidth:kRight;}
    float BlueprintWorkspace::graphAreaLeft() const
    {return contentSideActive()&&m_contentDockSide==1?contentBounds().right:graphBaseLeft();}
    float BlueprintWorkspace::graphAreaRight() const
    {return contentSideActive()&&m_contentDockSide==2?contentBounds().left:graphBaseRight();}
    float BlueprintWorkspace::tabLeft() const
    {return graphAreaLeft();}
    float BlueprintWorkspace::tabRight() const
    {return m_actorEditor?kWidth-m_actorRightPanel:graphAreaRight();}
    float BlueprintWorkspace::tabTop() const
    {return kBottom-(!m_actorEditor&&!visualTabOpen()&&m_contentDrawer&&!m_contentFloating&&!m_contentDockSide&&!m_contentDockTop?contentBounds().bottom-contentBounds().top:0.f)-30.f;}
    float BlueprintWorkspace::graphAreaTop() const
    {return !m_actorEditor&&!visualTabOpen()&&m_contentDrawer&&!m_contentFloating&&!m_contentDockSide&&m_contentDockTop?contentBounds().bottom:kTop;}
    void BlueprintWorkspace::revealTab(size_t index)
    {
        const size_t visible=static_cast<size_t>(std::max(1.f,(tabRight()-tabLeft()-8)/146.f));
        if(index<m_tabFirst)m_tabFirst=index;
        if(index>=m_tabFirst+visible)m_tabFirst=index-visible+1;
    }
    bool BlueprintWorkspace::updateGraphTabs(Services& s,float x,float y,bool click)
    {
        const float top=tabTop(),right=tabRight();
        if(!in(x,y,tabLeft(),top,right,top+30))return false;
        pruneGraphTabs();
        const auto visible=static_cast<size_t>(std::max(1.f,(right-tabLeft()-8)/146.f)),count=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size();
        if(s.input->wheel()<0&&m_tabFirst+visible<count)++m_tabFirst;
        if(s.input->wheel()>0&&m_tabFirst)--m_tabFirst;
        if(!click||x<tabLeft()+4)return true;
        const auto slot=static_cast<size_t>((x-tabLeft()-4)/146.f),index=m_tabFirst+slot;
        if(slot>=visible||index>=count)return true;
        const bool close=x>=tabLeft()+4+146.f*static_cast<float>(slot)+119&&count>1;
        if(index>=m_graphTabs.size()+m_actorTabs.size())
        {
            const auto tab=m_visualTabs[index-m_graphTabs.size()-m_actorTabs.size()];
            if(close)
            {
                if(visualTabOpen()&&m_activeVisualTab==std::make_pair(tab.widget,tab.id)&&assetScreen()==tab.screen)leaveVisualTab();
                m_visualTabs.erase(std::remove_if(m_visualTabs.begin(),m_visualTabs.end(),[&](const VisualTab& item)
                {return item.widget==tab.widget&&item.id==tab.id&&item.screen==tab.screen;}),m_visualTabs.end());
                if(m_tabFirst>=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size())m_tabFirst=0;
            }
            else openVisualTab(tab);
            return true;
        }
        if(index>=m_graphTabs.size())
        {
            const auto actorIndex=index-m_graphTabs.size();const auto id=m_actorTabs[actorIndex].id;
            if(close)
            {
                if(m_actorEditor&&id==m_actorTabId)leaveActorViewport(false);
                m_actorTabs.erase(std::remove_if(m_actorTabs.begin(),m_actorTabs.end(),[&](const ActorTab& tab){return tab.id==id;}),m_actorTabs.end());
                if(m_actorTabId==id)m_actorTabId=m_actorTabs.empty()?std::string():m_actorTabs.back().id;
                if(m_tabFirst>=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size())m_tabFirst=0;
            }
            else if(!m_actorEditor||m_actorTabId!=id){if(visualTabOpen())leaveVisualTab();openActorViewport(id);}
            return true;
        }
        const auto tab=m_graphTabs[index];const auto graph=graphTabIndex(tab);
        if(graph>=m_document.graphs.size())return true;
        if(close&&m_graphTabs.size()>1)
        {
            const bool active=!visualTabOpen()&&tab.owner==m_activeTab.first&&tab.name==m_activeTab.second;
            m_graphTabs.erase(m_graphTabs.begin()+static_cast<std::ptrdiff_t>(index));
            if(m_tabFirst>=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size())m_tabFirst=0;
            if(active)
            {
                if(m_actorEditor)leaveActorViewport(false);
                m_activeTab={};selectGraph(graphTabIndex(m_graphTabs[std::min(index,m_graphTabs.size()-1)]));
            }
        }
        else if(!close)
        {
            if(m_actorEditor)leaveActorViewport(false);
            if(visualTabOpen())leaveVisualTab();
            selectGraph(graph);
        }
        return true;
    }
    void BlueprintWorkspace::drawGraphTabs(Canvas& c)
    {
        const float top=tabTop(),right=tabRight();
        c.rect(tabLeft(),top,right-tabLeft(),30,{0.15f,0.15f,0.16f,1});
        const auto visible=static_cast<size_t>(std::max(1.f,(right-tabLeft()-8)/146.f)),count=m_graphTabs.size()+m_actorTabs.size()+m_visualTabs.size();
        for(size_t i=0;i<visible&&m_tabFirst+i<count;++i)
        {
            const auto index=m_tabFirst+i;const float x=tabLeft()+4+146.f*static_cast<float>(i);std::string label;bool selected=false;
            if(index<m_graphTabs.size())
            {
                const auto& tab=m_graphTabs[index];selected=!m_actorEditor&&!visualTabOpen()&&tab.owner==m_activeTab.first&&tab.name==m_activeTab.second;
                label=(tab.owner.rfind("behaviour:",0)==0?tab.owner.substr(10):tab.owner)+"/"+tab.name;
            }
            else if(index<m_graphTabs.size()+m_actorTabs.size())
            {
                const auto& tab=m_actorTabs[index-m_graphTabs.size()];const auto* asset=m_document.visualAsset(tab.id);
                selected=m_actorEditor&&m_actorTabId==tab.id;label=asset?"3D / "+asset->title:"Closed actor";
            }
            else
            {
                const auto& tab=m_visualTabs[index-m_graphTabs.size()-m_actorTabs.size()];
                const auto* asset=tab.widget?m_document.visualAsset(tab.id):nullptr;
                selected=visualTabOpen()&&m_activeVisualTab==std::make_pair(tab.widget,tab.id)&&assetScreen()==tab.screen;
                label=tab.widget?(asset?"UI / "+asset->title:"Closed widget"):"Scene / "+tab.screen;
            }
            c.rect(x,top+3,142,27,selected?Color{0.33f,0.34f,0.37f,1}:Color{0.22f,0.22f,0.24f,1});
            if(selected)c.rect(x,top+3,142,2,accent);
            c.clip(x+5,top+5,112,23);c.text(*m_font,label.c_str(),x+6,top+7,text);c.noClip();
            if(count>1&&(index>=m_graphTabs.size()||m_graphTabs.size()>1)){c.line(x+125,top+11,x+133,top+19,1,muted);c.line(x+133,top+11,x+125,top+19,1,muted);}
        }
    }
    void BlueprintWorkspace::openSelectedFunction()
    {
        const auto& g=m_document.graphs[m_graph];
        if(m_selection>=g.nodes.size()){m_status="Select a function call first.";return;}
        const auto& n=g.nodes[m_selection];
        if(n.kind!="call:function"&&n.kind!="call:library"&&n.kind!="call:parent"&&n.kind!="call:event"&&n.kind!="async:start"){m_status="Select a function call first.";return;}
        const auto* target=n.kind=="call:library"?m_document.libraryFunction(n.ref):n.kind=="call:parent"?m_document.function(m_document.parentBehaviour(g.owner),g.name):m_document.function(g.owner,n.ref);
        if(!target){m_status="Function target is missing.";return;}
        if(m_graphBack.size()>=32)m_graphBack.erase(m_graphBack.begin());
        m_graphBack.emplace_back(g.owner,g.name);selectGraph(static_cast<size_t>(target-m_document.graphs.data()));m_status="Opened function. Back returns to caller.";
    }
    void BlueprintWorkspace::returnToCaller()
    {
        while(!m_graphBack.empty())
        {
            const auto key=m_graphBack.back();m_graphBack.pop_back();
            const auto found=std::find_if(m_document.graphs.begin(),m_document.graphs.end(),[&](const BlueprintDocument::Graph& g){return g.owner==key.first&&g.name==key.second;});
            if(found!=m_document.graphs.end()){selectGraph(static_cast<size_t>(found-m_document.graphs.begin()));m_status="Returned to caller.";return;}
        }
        m_status="No previous caller.";
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
            BlueprintNodeSpec resolved;
            if(m_document.nodeSpec(graph,graph.nodes[i],resolved))
            {
                const auto* spec=&resolved;
                for(const auto& pin:spec->inputs){add(m_rows[i].inputs,lookup[i].inputs,pin.name);m_rows[i].inputTypes.push_back(pin.type);}
                for(const auto& pin:spec->outputs){add(m_rows[i].outputs,lookup[i].outputs,pin.name);m_rows[i].outputTypes.push_back(pin.type);}
            }
        }
        for(const auto& link:graph.links)
            m_wirePins.push_back({add(m_rows[link.from].outputs,lookup[link.from].outputs,link.output),
                                  add(m_rows[link.to].inputs,lookup[link.to].inputs,link.input)});
        std::vector<std::vector<bool>> connected(m_rows.size());
        for(size_t i=0;i<m_rows.size();++i)connected[i].resize(m_rows[i].inputs.size(),false);
        for(size_t i=0;i<graph.links.size();++i)connected[graph.links[i].to][m_wirePins[i].input]=true;
        const auto measure=[&](const std::string& value)
        {return m_measureCanvas&&m_font?m_measureCanvas->measure(*m_font,value.c_str()).x:8.f*static_cast<float>(value.size());};
        for(size_t i=0;i<m_rows.size();++i)
        {
            auto& row=m_rows[i];const auto& n=graph.nodes[i];
            row.height=std::max(kNodeHeight,kPinStart+kPinStep*static_cast<float>(std::max(row.inputs.size(),row.outputs.size()))+8);
            const auto& alias=n.displayName;
            row.title=graph.collapsed&&n.kind=="entry"?"Input tunnel":graph.collapsed&&n.kind=="return:function"?"Output tunnel":n.kind=="call:function"&&m_document.function(graph.owner,n.ref)&&m_document.function(graph.owner,n.ref)->collapsed?"Collapsed "+n.ref:!alias.empty()?alias:n.kind=="call:function"&&m_document.function(graph.owner,n.ref)&&m_document.function(graph.owner,n.ref)->inlineMacro?"Macro "+n.ref:title(n);
            row.width=std::max(kNodeWidth,measure(row.title)*1.45f+54);
            // A stable field column, shared by rendering and pointer hit testing.
            const auto details=inputDetails(i);
            float column=88.f;
            for(const auto& detail:details)if(detail.editable)column=std::max(column,measure(detail.name)+29.f);
            for(size_t p=0;p<row.inputs.size();++p)
            {
                const auto& pin=row.inputs[p];std::string value;
                const auto detail=std::find_if(details.begin(),details.end(),[&](const InputDetail& item){return item.name==pin;});
                const bool editable=!connected[i][p]&&detail!=details.end()&&detail->editable;
                if(editable)value=detail->value;
                for(char& ch:value)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
                row.fieldValue.push_back(value);
                row.fieldX.push_back(editable?column:0.f);
                const float outputSpace=p<row.outputs.size()?measure(row.outputs[p])+25.f:0.f;
                const auto components=editable?blueprintInputComponentLabels(row.inputTypes[p]).size():0;
                const float left=editable?column+std::max(components?74.f*static_cast<float>(components):65.f,measure(value)+12.f):measure(pin)+20.f;
                row.width=std::max(row.width,left+outputSpace+20.f);
            }
            for(const auto& output:row.outputs)row.width=std::max(row.width,measure(output)+40.f);
            if(n.kind.rfind("literal:",0)==0&&row.inputs.empty()&&!row.outputs.empty())row.width=std::max(row.width,measure(row.outputs.front())+std::max(std::max(75.f,74.f*static_cast<float>(blueprintInputComponentLabels(row.outputTypes.front()).size())),measure(n.ref)+15.f)+55.f);
            if(!n.comment.empty()){row.height+=24.f;row.width=std::max(row.width,std::min(400.f,measure(n.comment)+24.f));}
            row.width=std::clamp(row.width,kNodeWidth,640.f);
            for(size_t p=0;p<row.inputs.size();++p){const float outputSpace=p<row.outputs.size()?measure(row.outputs[p])+25.f:0.f;row.fieldW.push_back(row.fieldX[p]>0?std::max(0.f,row.width-13.f-row.fieldX[p]-outputSpace):0.f);}
            if(n.kind.rfind("literal:",0)==0&&row.inputs.empty()&&!row.outputs.empty())row.literalFieldW=std::max(0.f,row.width-26.f-measure(row.outputs.front())-25.f);
        }
    }

    namespace
    {
        constexpr const char* paletteGroups[]={"Events & flow","Variables & data","Math & logic","Actors & gameplay","Physics & collision","Camera & rendering","UI & input","Audio & music","Assets & media","Networking","System & platform","Other"};
        unsigned paletteGroup(const BlueprintNodeSpec& node)
        {
            const auto& c=node.category;const auto& id=node.id;
            const auto is=[&](std::initializer_list<const char*> names){return std::any_of(names.begin(),names.end(),[&](const char* n){return c==n;});};
            if(is({"Events","Events / Dispatchers","Flow","Time / Async","Time / Animation","Tween","Functions","Functions / Libraries","Thread","Channel"}))return 0;
            if(is({"Values","Variables","Module variables","Custom types","Arrays","Collections","Json","Data","Packed","Zip","Save"}))return 1;
            if(is({"Math","Maths","Math / Vector","Math / Camera","Logic","String","Conversions","Vec2","Random","Curve","Curve / Visual","Path","Paths and noise","Color"}))return 2;
            if(is({"Level","Behaviours","Actors / Character","Space / Blueprint Classes","Space / Primitives","Entity","Scene","Space"}))
            {
                for(const char* term:{"::hinge","::join","::weld","::rope","::spring","::slider","::motor","::wheel","::drag","::unjoin","::gravity","::raycast","::sphereCast","::spherePick","::pick","::body","::collider","::force","::impulse","::velocity","::mass","::friction","::bounce"})
                    if(id.find(term)!=std::string::npos)return 4;
                for(const char* term:{"::camera","::draw","::light","::sun","::fog","::shadow","::sky","::ambient","::fieldLook"})
                    if(id.find(term)!=std::string::npos)return 5;
                return 3;
            }
            if(is({"Space / Camera","Space / Rendering","Canvas","Batch","Look","Effect"}))return 5;
            if(is({"Widget","Ui","Ui / Designer","Input"}))return 6;
            if(is({"Audio","Audio / Synthesis","Sound","Voice","Samples","AudioStream","Playlist"}))return 7;
            if(is({"Assets","Texture","Model","Mesh","Font","Video","Pixels"}))return 8;
            if(is({"Message","Room","Request","WebSocket","Connection","Datagrams","Net"}))return 9;
            if(is({"System","Window","Windows","Mac","Linux","Xbox","Xbox360","XboxOne","Screens","App"}))return 10;
            return 11;
        }
    }
    void BlueprintWorkspace::rebuildPaletteRows()
    {
        m_paletteRows.clear();m_paletteFirst=0;
        if(!m_query.empty()||!m_paletteGroup.empty())
        {
            std::vector<const BlueprintNodeSpec*> sorted;
            for(const auto* n:m_palette)if(!m_query.empty()||m_paletteGroup==paletteGroups[paletteGroup(*n)])sorted.push_back(n);
            std::stable_sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b)
            {if(a->category!=b->category)return a->category<b->category;return a->title<b->title;});
            // Exact ID/name searches stay first, including automation and expert workflows.
            if(!m_query.empty())std::stable_partition(sorted.begin(),sorted.end(),[&](const auto* n){return n->id==m_query||n->title==m_query;});
            for(const auto* n:sorted)m_paletteRows.push_back({n,n->category+" / "+n->title,""});
        }
        else for(unsigned i=0;i<sizeof(paletteGroups)/sizeof(paletteGroups[0]);++i)
        {
            const auto count=std::count_if(m_palette.begin(),m_palette.end(),[&](const auto* n){return paletteGroup(*n)==i;});
            if(count)m_paletteRows.push_back({nullptr,std::string(paletteGroups[i])+" ("+std::to_string(count)+")",paletteGroups[i]});
        }
    }
    void BlueprintWorkspace::filterCatalogue()
    {
        m_palette.clear();m_paletteRows.clear();m_contextResults.clear();m_paletteFirst=0;
        if(m_graph>=m_document.graphs.size())return;
        auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
        const auto query=lower(m_query);
        const auto contextQuery=lower(m_pinQuery);
        const auto& graph=m_document.graphs[m_graph];const bool behaviour=graph.owner.rfind("behaviour:",0)==0;
        const auto behaviours=m_document.behaviours();
        const bool hasBehaviours=std::any_of(behaviours.begin(),behaviours.end(),[&](const std::string& name){return m_document.canAttach(graph.owner,name);});
        const auto& globals=m_document.moduleGlobals(graph.owner);
        for(const auto& node:blueprintCatalogue())
            if((node.id.rfind("global:",0)!=0||std::any_of(globals.begin(),globals.end(),[&](const BlueprintDocument::Field& f){return f.type==node.id.substr(11);}))&&node.id!="entry"&&(node.id!="return:function"||(graph.kind=="function"&&!graph.inlineMacro))&&(node.id!="call:parent"||(graph.kind=="function"&&m_document.function(m_document.parentBehaviour(graph.owner),graph.name)))&&(node.id!="event:entity"||behaviour)&&
                (node.id!="event:other"||(behaviour&&(graph.name=="onHit"||graph.name=="onLeave")))&&
                (node.id!="event:levelName"||(!behaviour&&(graph.name=="onLevelTouch"||graph.name=="onLevelAgent")))&&
                (node.id!="event:levelWho"||(!behaviour&&graph.name=="onLevelTouch"))&&
                (node.id!="event:levelWhat"||(!behaviour&&graph.name=="onLevelAgent"))&&
                ((node.id!="attach:behaviour"&&node.id!="level:attach")||(!behaviour&&hasBehaviours))&&(node.id!="attach:direct"||hasBehaviours)&&(!behaviour||node.id.rfind("level:",0)!=0)&&(node.id!="event:delta"||m_document.graphs[m_graph].name=="update")&&
                (node.id!="event:canvas"||m_document.graphs[m_graph].name=="draw")&&
                ((node.id!="async:wait"&&node.id!="async:next")||graph.dataRole=="coroutine")&&
                (node.id!="camera:sequence"||(!behaviour&&graph.name=="update"))&&((node.id!="time:timeline"&&node.id!="camera:orbit-timed")||graph.name=="update")&&(node.id!="render:space"||(!behaviour&&graph.name=="draw"))&&
                (m_pinMenu||lower(node.category+" "+node.title+" "+node.id).find(query)!=std::string::npos))m_palette.push_back(&node);
        if(!m_pinMenu){rebuildPaletteRows();return;}
        const auto namedVariables=variableRows();
        auto consider=[&](const BlueprintNodeSpec& spec,const std::string& reference,const std::string& label)
        {
            if(lower(spec.category+" "+label+" "+spec.id).find(contextQuery)==std::string::npos)return;
            const auto& pins=m_pendingOutput?spec.inputs:spec.outputs;
            if(m_pinType.empty()){m_contextResults.push_back({spec.id,reference,label,"",false});return;}
            // Show all matching pins, so a user can deliberately choose an argument.
            for(const auto& p:pins)
            {
                const bool exact=p.type==m_pinType;
                if(exact||(m_pendingOutput?(blueprintLosslessConversion(m_pinType,p.type)||m_document.classAssignable(m_pinType,p.type)):(blueprintLosslessConversion(p.type,m_pinType)||m_document.classAssignable(p.type,m_pinType))))
                    m_contextResults.push_back({spec.id,reference,label+" / "+p.name+(exact?"":" (convert)"),p.name,!exact});
            }
        };
        for(const auto* base:m_palette)
        {
            if(blueprintNamedCall(base->id))
            {
                for(const auto& fn:m_document.graphs)if(fn.kind=="function"&&!fn.collapsed&&(&fn!=&graph)&&
                    (base->id=="call:library"?m_document.libraryFunction(fn.name)==&fn:(m_document.function(graph.owner,fn.name)==&fn&&m_document.canCallFunction(m_graph,fn.name))))
                {
                    BlueprintNodeSpec resolved;
                    if(m_document.nodeSpec(graph,{"",base->id,fn.name},resolved))consider(resolved,fn.name,(fn.inlineMacro?"Macro":base->title)+" "+fn.name);
                }
            }
            else if(base->id.rfind("data:",0)==0)
            {
                for(const auto& declaration:m_document.graphs)if(m_document.dataType(declaration.name)==&declaration)
                {BlueprintNodeSpec resolved;if(m_document.nodeSpec(graph,{"",base->id,declaration.name},resolved))consider(resolved,declaration.name,base->title+" "+declaration.name);}
            }
            else if(base->id.rfind("typed:",0)==0)
            {
                for(const auto& row:namedVariables)if(row.global==(base->id.rfind("typed:global:",0)==0))
                {BlueprintNodeSpec resolved;if(m_document.nodeSpec(graph,{"",base->id,row.field.name},resolved))consider(resolved,row.field.name,base->title+" "+row.field.name);}
            }
            else if(base->id=="actor:is-class"||base->id=="actor:cast-class"||base->id=="actor:class-entity")
            {
                for(const auto& name:behaviours)if(m_document.canAttach(graph.owner,name))
                {BlueprintNodeSpec resolved;if(m_document.nodeSpec(graph,{"",base->id,name},resolved))consider(resolved,name,base->title+" "+name);}
            }
            else if(base->id=="return:function"||base->id=="call:parent")
            {
                BlueprintNodeSpec resolved;if(m_document.nodeSpec(graph,{"",base->id,""},resolved))consider(resolved,"",base->title);
            }
            else if(base->id.rfind("var:",0)==0||base->id.rfind("global:",0)==0)
            {
                for(const auto& row:namedVariables)
                {
                    const bool isGlobal=base->id.rfind("global:",0)==0;
                    if(row.global==isGlobal&&row.field.type==base->id.substr(isGlobal?11:8))
                        consider(*base,row.field.name,std::string(base->id.find(":set:")!=std::string::npos?"Set ":"Get ")+(isGlobal?"global ":"")+row.field.name);
                }
                if(base->id.rfind("var:",0)==0)consider(*base,"",base->title);
            }
            else consider(*base,"",base->title);
        }
        std::stable_sort(m_contextResults.begin(),m_contextResults.end(),[](const ContextResult& a,const ContextResult& b){return a.conversion<b.conversion;});
    }

    void BlueprintWorkspace::openPinMenu(float x,float y)
    {
        m_pinType.clear();
        if(m_pending<m_rows.size())
        {
            const auto& row=m_rows[m_pending];const auto& pins=m_pendingOutput?row.outputs:row.inputs;
            const auto pin=std::find(pins.begin(),pins.end(),m_pendingPin);if(pin==pins.end())return;
            m_pinType=(m_pendingOutput?row.outputTypes:row.inputTypes)[static_cast<size_t>(pin-pins.begin())];
        }
        m_pinQuery.clear();m_pinX=x;m_pinY=y;m_pinMenuX=std::clamp(x,kLeft+4,kRight-304);m_pinMenuY=std::clamp(y,kTop+4,kBottom-324);
        m_pinMenu=true;m_pinDrag=false;m_pan=false;filterCatalogue();m_status=m_pinType.empty()?"Choose a node to place.":"Choose a compatible node for "+m_pendingPin+".";
    }
    void BlueprintWorkspace::closePinMenu(Services& s)
    {
        m_pinMenu=false;m_rightDown=false;clearWire();s.input->setTyping(false);filterCatalogue();
    }
    bool BlueprintWorkspace::graphMap(GraphMap& map) const
    {
        if(!m_minimap||m_actorEditor||visualTabOpen()||m_graph>=m_document.graphs.size())return false;
        const auto& graph=m_document.graphs[m_graph];if(graph.nodes.empty()||m_rows.size()!=graph.nodes.size())return false;
        const float availableWidth=graphAreaRight()-graphAreaLeft(),availableHeight=tabTop()-graphAreaTop();
        if(availableWidth<340||availableHeight<240)return false;
        map.w=std::min(220.f,availableWidth*0.35f);map.h=120;
        map.x=graphAreaRight()-map.w-12;map.y=tabTop()-map.h-12;
        float loX=graph.nodes.front().x,loY=graph.nodes.front().y,hiX=loX+kNodeWidth,hiY=loY+kNodeHeight;
        for(size_t i=0;i<graph.nodes.size();++i)
        {
            const auto& node=graph.nodes[i];
            loX=std::min(loX,node.x);loY=std::min(loY,node.y);
            hiX=std::max(hiX,node.x+m_rows[i].width);hiY=std::max(hiY,node.y+m_rows[i].height);
        }
        for(const auto& link:graph.links)for(const auto& point:link.route)
        {loX=std::min(loX,point.x);loY=std::min(loY,point.y);hiX=std::max(hiX,point.x);hiY=std::max(hiY,point.y);}
        map.left=loX-24;map.top=loY-24;hiX+=24;hiY+=24;
        const float width=hiX-map.left,height=hiY-map.top;
        map.scale=std::min((map.w-12)/width,(map.h-32)/height);
        map.ox=map.x+(map.w-width*map.scale)*0.5f;
        map.oy=map.y+20+(map.h-20-height*map.scale)*0.5f;
        return std::isfinite(map.scale)&&map.scale>0;
    }
    bool BlueprintWorkspace::updateGraphMap(float x,float y,bool left,bool click)
    {
        GraphMap map;
        if(!graphMap(map)){m_minimapDragging=false;return false;}
        if(!m_minimapDragging&&(!click||!in(x,y,map.x,map.y+20,map.x+map.w,map.y+map.h)))return false;
        if(!left){m_minimapDragging=false;NX_INFO("Blueprint Studio: minimap navigation completed");return true;}
        m_minimapDragging=true;clearWire();m_pan=false;
        const float gx=map.left+(std::clamp(x,map.x,map.x+map.w)-map.ox)/map.scale;
        const float gy=map.top+(std::clamp(y,map.y+20,map.y+map.h)-map.oy)/map.scale;
        m_x=(graphAreaLeft()+graphAreaRight())*0.5f-kLeft-gx*m_zoom;
        m_y=(graphAreaTop()+tabTop())*0.5f-kTop-gy*m_zoom;
        m_status="Graph minimap navigation.";
        return true;
    }
    void BlueprintWorkspace::drawGraphMap(Canvas& c)
    {
        GraphMap map;if(!graphMap(map))return;
        const auto& graph=m_document.graphs[m_graph];
        c.roundRect(map.x,map.y,map.w,map.h,4,{0.11f,0.12f,0.14f,0.97f});
        c.roundLine(map.x,map.y,map.w,map.h,4,1,{0.42f,0.44f,0.47f,1});
        c.rect(map.x+1,map.y+1,map.w-2,19,{0.24f,0.25f,0.27f,1});
        c.clip(map.x+6,map.y+1,map.w-12,19);
        c.text(*m_font,"GRAPH OVERVIEW",map.x+6,map.y+1,muted);c.noClip();
        c.clip(map.x+2,map.y+20,map.w-4,map.h-22);
        const auto px=[&](float v){return map.ox+(v-map.left)*map.scale;};
        const auto py=[&](float v){return map.oy+(v-map.top)*map.scale;};
        for(const auto& link:graph.links)
        {
            if(link.from>=graph.nodes.size()||link.to>=graph.nodes.size())continue;
            const auto& from=graph.nodes[link.from];const auto& to=graph.nodes[link.to];
            float x=px(from.x+m_rows[link.from].width),y=py(from.y+24);
            for(const auto& point:link.route){const float xx=px(point.x),yy=py(point.y);c.line(x,y,xx,yy,1,{0.46f,0.48f,0.52f,1});x=xx;y=yy;}
            c.line(x,y,px(to.x),py(to.y+24),1,{0.46f,0.48f,0.52f,1});
        }
        for(size_t i=0;i<graph.nodes.size();++i)
        {
            const auto& node=graph.nodes[i];const bool selected=i==m_selection;
            const auto* spec=blueprintSpec(node.kind);
            const Color color=selected?accent:node.kind=="entry"||node.kind.rfind("event:",0)==0?Color{0.66f,0.29f,0.31f,1}:spec&&spec->pure?Color{0.32f,0.57f,0.39f,1}:Color{0.32f,0.47f,0.64f,1};
            c.rect(px(node.x),py(node.y),std::max(2.f,m_rows[i].width*map.scale),std::max(2.f,m_rows[i].height*map.scale),color);
        }
        const float left=px((graphAreaLeft()-kLeft-m_x)/m_zoom),top=py((graphAreaTop()-kTop-m_y)/m_zoom);
        const float right=px((graphAreaRight()-kLeft-m_x)/m_zoom),bottom=py((tabTop()-kTop-m_y)/m_zoom);
        c.roundLine(left,top,right-left,bottom-top,1,1.5f,{0.92f,0.85f,0.52f,1});
        c.noClip();
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
            right=std::max(right,n.x+rows[i].width);bottom=std::max(bottom,n.y+rows[i].height);
        }
        for(const auto& l:graph.links)for(const auto& p:l.route){left=std::min(left,p.x-6);top=std::min(top,p.y-6);right=std::max(right,p.x+6);bottom=std::max(bottom,p.y+6);}
        if(!graph.comments.empty()){left-=18;top-=44;right+=18;bottom+=18;}
        m_zoom=std::clamp(std::min((graphAreaRight()-graphAreaLeft()-48)/(right-left),(tabTop()-graphAreaTop()-48)/(bottom-top)),0.35f,1.0f);
        m_x=graphAreaLeft()-kLeft+24-left*m_zoom;m_y=graphAreaTop()-kTop+24-top*m_zoom;
    }

    bool BlueprintWorkspace::writeVerified(Services& s,const std::string& path,const std::string& bytes)
    {
        if(!s.platform->writeFile(path.c_str(),bytes.data(),bytes.size()))
        {m_status="Save failed. Changes remain in memory.";return false;}
        FileData check;
        if(!s.platform->readFile(path.c_str(),check,MemTag::General)||check.size!=bytes.size()||
            std::memcmp(check.data,bytes.data(),bytes.size())!=0)
        {m_status="Could not verify written file. Changes remain in memory.";return false;}
        return true;
    }
    namespace
    {
        bool recoveryArchiveName(const std::string& file)
        {
            const std::string damaged="blueprint-recovery-damaged-",conflict="blueprint-recovery-conflict-";
            const size_t prefix=file.rfind(damaged,0)==0?damaged.size():file.rfind(conflict,0)==0?conflict.size():0;
            if(!prefix||file.size()>96||file.size()<=prefix+4||!file.ends_with(".bak"))return false;
            const auto stamp=file.substr(prefix,file.size()-prefix-4);
            const auto separator=stamp.find('-');
            return separator!=std::string::npos&&separator>0&&separator+1<stamp.size()&&
                stamp.substr(0,separator).find_first_not_of("0123456789")==std::string::npos&&
                stamp.substr(separator+1).find_first_not_of("0123456789")==std::string::npos;
        }
        bool projectFolderName(const std::string& folder)
        {return blueprintImportProjectFolderName(folder);}
        bool standaloneSourceName(const std::string& file)
        {
            return file.size()>3&&file.size()<=128&&file.front()!='.'&&file.ends_with(".as")&&
                file.find_first_of("/\\:")==std::string::npos&&file.find("..")==std::string::npos&&
                std::none_of(file.begin(),file.end(),[](unsigned char c){return c<32||c==127;});
        }
        bool expandImportSource(Platform& platform,const std::string& source,const std::string& label,const std::string& localAssets,const std::string& sharedAssets,std::string& expanded,std::string& error)
        {
            return resolveBlueprintIncludes(source,label,[&](const std::string& name,std::string& bytes)
            {
                std::string path;
                if(name.rfind("asset:",0)==0)path=localAssets+"/"+name.substr(6);
                else
                {
                    const auto relative=name.substr(7);
                    path=sharedAssets.empty()?"global/"+relative:sharedAssets+"/"+relative;
                    if(!sharedAssets.empty()&&!platform.fileTime(path.c_str()))path="global/"+relative;
                }
                FileData file;
                if(!platform.readFile(path.c_str(),file,MemTag::General)||!file.size||file.size>512*1024)return false;
                bytes.assign(reinterpret_cast<const char*>(file.data),file.size);return true;
            },expanded,error);
        }
    }

    bool BlueprintWorkspace::importSource(Services& s,const std::string& file)
    {
        if(!preserveBeforeSwitch(s))return false;
        if(!standaloneSourceName(file))
        {m_status="Drop a standalone AngelScript .as file.";return false;}
        FileData data;
        if(!s.platform->readFile(("own/"+file).c_str(),data,MemTag::General)||data.size>512*1024)
        {m_status="Cannot read source or file exceeds import limit.";return false;}
        const std::string source(reinterpret_cast<const char*>(data.data),data.size);
        std::string expanded;
        const auto stem=file.substr(0,file.size()-3);
        if(!expandImportSource(*s.platform,source,file,"own/"+stem+"/asset","",expanded,m_status))return false;
        BlueprintDocument candidate;
        if(!importBlueprintScreen(expanded,candidate,m_status))return false;
        // Validate the expanded original as well as the conversion using the installed engine.
        if(!s.screens->validateBlueprintSource(expanded,m_status)){m_status="Original source: "+m_status;return false;}
        std::vector<BlueprintScreenOutput> output;
        if(!compileBlueprintProject(candidate,output,m_status))return false;
        for(const auto& screen:output)
            if(!s.screens->validateBlueprintSource(screen.source,m_status)){m_status="Converted source: "+m_status;return false;}
        if(!blueprintImportSourceAssets(*s.platform,file,candidate.assetRoot,m_status))
        {m_status="Companion assets: "+m_status;return false;}
        resetGraphTabs();m_actorTabId.clear();m_actorTabScreen.clear();m_document=std::move(candidate);m_filePath.clear();m_fileBaseline.clear();
        m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;
        m_status="Imported "+file+(m_document.assetRoot.empty()?" as editable graphs.":" with companion assets.")+" Save as a new project.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        checkpoint(s,true);return true;
    }

    bool BlueprintWorkspace::importProjectFolder(Services& s,const std::string& folder)
    {
        if(!preserveBeforeSwitch(s))return false;
        if(!projectFolderName(folder)){m_status="Invalid project folder.";return false;}
        const std::string base="own/"+folder;
        std::vector<std::string> owners;
        bool overflow=false;
        struct Listing {std::vector<std::string>& names;bool& overflow;} listing{owners,overflow};
        s.platform->listFolders(base.c_str(),[](const char* name,void* user)
        {
            auto& state=*static_cast<Listing*>(user);
            if(std::strcmp(name,"_global")==0)return;
            if(state.names.size()>=10)state.overflow=true;
            else state.names.emplace_back(name);
        },&listing);
        if(overflow||owners.empty()){m_status="Import requires 1-10 native screen folders.";return false;}
        std::sort(owners.begin(),owners.end());
        if(std::adjacent_find(owners.begin(),owners.end())!=owners.end())
        {m_status="Duplicate screen folder.";return false;}
        // Redux projects conventionally boot main. Keep it first, independent of listing order.
        const auto home=std::find(owners.begin(),owners.end(),"main");
        if(home!=owners.end())std::rotate(owners.begin(),home,home+1);
        BlueprintDocument candidate;candidate.name=folder.rfind("project-",0)==0?folder.substr(8):folder;
        for(const auto& owner:owners)
        {
            // Validate against an empty document: the canonical initial Screen ID
            // is legal, but create() would pre-populate it and reject it as a duplicate.
            BlueprintDocument probe;std::string error;
            if(!probe.addScreen(owner,error)){m_status=owner+": "+error;return false;}
            FileData data;
            if(!s.platform->readFile((base+"/"+owner+"/main.as").c_str(),data,MemTag::General)||
                data.size>512*1024)
            {m_status=owner+": missing or oversized main.as.";return false;}
            const std::string source(reinterpret_cast<const char*>(data.data),data.size);
            std::string expanded;
            BlueprintDocument screen;
            if(!expandImportSource(*s.platform,source,owner+"/main.as",base+"/"+owner+"/asset",base+"/_global",expanded,error)||
                !importBlueprintScreen(expanded,screen,error)||!s.screens->validateBlueprintSource(expanded,error))
            {m_status=owner+": "+error;return false;}
            if(!mergeImportedBlueprintScreen(candidate,std::move(screen),owner,m_status))
            {m_status=owner+": "+m_status;return false;}
            const auto manifestPath=base+"/"+owner+"/screen.json";
            if(s.platform->fileTime(manifestPath.c_str()))
            {
                FileData manifest;
                if(!s.platform->readFile(manifestPath.c_str(),manifest,MemTag::General)||!manifest.size||manifest.size>64*1024)
                {m_status=owner+": cannot read screen.json or exceeds 64 KiB.";return false;}
                if(!candidate.importScreenManifest(owner,std::string(reinterpret_cast<const char*>(manifest.data),manifest.size),m_status))return false;
            }
        }
        std::vector<BlueprintScreenOutput> outputs;
        if(!compileBlueprintProject(candidate,outputs,m_status))return false;
        for(const auto& output:outputs)
            if(!s.screens->validateBlueprintSource(output.source,m_status))
            {m_status=output.id+": "+m_status;return false;}
        if(!blueprintImportFolderAssets(*s.platform,folder,owners,candidate.assetRoot,m_status))return false;
        std::string bytes;
        if(!candidate.serialize(bytes,m_status))return false;
        BlueprintDocument verified;
        if(!verified.load(bytes.data(),bytes.size(),m_status))return false;
        verified.markRecovered();resetGraphTabs();m_actorTabId.clear();m_actorTabScreen.clear();m_document=std::move(verified);m_filePath.clear();m_fileBaseline.clear();
        m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;
        m_status="Imported "+std::to_string(owners.size())+" screens from "+folder+". Save as a new project.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        checkpoint(s,true);return true;
    }

    bool BlueprintWorkspace::openFile(Services& s,const std::string& file,bool managed)
    {
        if(blueprintFileName(file)&&file.ends_with(".bplib.bp.json"))
        {
            FileData data;
            if(!s.platform->readFile(("own/"+file).c_str(),data,MemTag::General)){m_status="Could not read library package.";return false;}
            if(!m_document.importLibrary(reinterpret_cast<const char*>(data.data),data.size,m_document.graphs[m_graph].owner,m_status))return false;
            m_browsing=false;selectGraph(m_graph);checkpoint(s,true);m_status="Imported library "+file;return true;
        }
        if(!preserveBeforeSwitch(s))return false;
        if(blueprintPackageName(file))
        {
            BlueprintDocument candidate;
            if(!blueprintImportPackage(*s.platform,file,candidate,m_status))return false;
            resetGraphTabs();m_actorTabId.clear();m_actorTabScreen.clear();m_contentFirst=0;m_contentDrag.clear();
            m_document=std::move(candidate);m_filePath.clear();m_fileBaseline.clear();m_checkpointRevision=static_cast<size_t>(-1);
            m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;checkpoint(s,true);
            m_status="Imported "+file+". Save as a new project.";return true;
        }
        m_actorTabId.clear();m_actorTabScreen.clear();m_contentFirst=0;m_contentDrag.clear();
        const bool archive=recoveryArchiveName(file);
        if(!blueprintFileName(file)&&!archive){m_status="Choose a Blueprint or recovery archive.";return false;}
        const std::string path="own/"+file;
        FileData data;
        if(!s.platform->readFile(path.c_str(),data,MemTag::General))
        {m_status="Could not read Blueprint.";return false;}
        if(!m_document.load(reinterpret_cast<const char*>(data.data),data.size,m_status))return false;
        resetGraphTabs();m_filePath.clear();m_fileBaseline.clear();
        std::string expected;
        if(!archive&&managed&&blueprintManagedName(m_document.name,expected)&&file==expected)
        {
            m_filePath=path;
            m_fileBaseline.assign(reinterpret_cast<const char*>(data.data),data.size);
        }
        m_checkpointRevision=static_cast<size_t>(-1);
        m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;
        if(archive)
        {
            m_document.markRecovered();
            m_status="Opened recovery archive "+file+". Save as a new project.";
        }
        else m_status="Opened "+file;
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        if(archive)checkpoint(s,true);
        return true;
    }
    bool BlueprintWorkspace::createTemplate(Services& s,const std::string& id,const std::string& name)
    {
        if(!preserveBeforeSwitch(s))return false;
        std::string file;
        if(!blueprintManagedName(name,file)){m_status="Enter a valid project name (1-64 letters, digits, spaces, _ or -).";return false;}
        const auto path="own/"+file;
        if(s.platform->fileTime(path.c_str())){m_status="A project with that name already exists.";return false;}
        BlueprintDocument candidate;
        if(!blueprintProjectTemplate(id,candidate,m_status))return false;
        candidate.name=name;
        std::vector<BlueprintScreenOutput> outputs;
        if(!compileBlueprintProject(candidate,outputs,m_status))return false;
        for(const auto& output:outputs)
            if(!s.screens->validateBlueprintSource(output.source,m_status))return false;
        if(!blueprintCreateAssetRoot(*s.platform,candidate.assetRoot,m_status)||
            !blueprintStageTemplateMedia(*s.platform,candidate.assetRoot,id,m_status))return false;
        std::string bytes;
        if(!candidate.serialize(bytes,m_status)||!writeVerified(s,path,bytes))return false;
        candidate.markSaved();
        resetGraphTabs();m_actorTabId.clear();m_actorTabScreen.clear();m_contentFirst=0;m_contentDrag.clear();
        m_document=std::move(candidate);m_filePath=path;m_fileBaseline=std::move(bytes);
        m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;
        m_browsing=false;m_projectCreate=0;m_projectName.clear();s.input->setTyping(false);
        selectGraph(0);checkpoint(s,true);m_status="Created "+m_document.name+" from "+id+" (native source compiled).";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());return true;
    }
    void BlueprintWorkspace::browseFiles(Services& s)
    {
        m_projectCreate=0;m_projectName.clear();s.input->setTyping(false);
        m_files.clear();m_fileFirst=0;
        s.platform->list("own",false,[](const char* name,void* user)
        {
            auto& files=*static_cast<std::vector<std::string>*>(user);
            if(files.size()<4096&&(blueprintFileName(name)||blueprintPackageName(name)||recoveryArchiveName(name)||standaloneSourceName(name)))files.emplace_back(name);
        },&m_files);
        s.platform->listFolders("own",[](const char* name,void* user)
        {
            auto& files=*static_cast<std::vector<std::string>*>(user);
            if(files.size()<4096&&projectFolderName(name))files.emplace_back(name);
        },&m_files);
        std::sort(m_files.begin(),m_files.end(),[](const std::string& a,const std::string& b)
        {
            const auto rank=[](const std::string& file){return blueprintFileName(file)?0:recoveryArchiveName(file)?1:standaloneSourceName(file)?2:3;};
            return rank(a)!=rank(b)?rank(a)<rank(b):a<b;
        });
        m_files.erase(std::unique(m_files.begin(),m_files.end()),m_files.end());
        m_browsing=true;clearWire();cancelDrag();
        m_status="Choose a template or project. Unsaved changes are saved or archived before switching.";
        NX_INFO("Blueprint Studio: project library opened");
    }
    std::string BlueprintWorkspace::assetScreen() const
    {
        if(m_graph>=m_document.graphs.size())return "";
        const auto& owner=m_document.graphs[m_graph].owner;
        return owner.rfind("behaviour:",0)==0?"":owner;
    }
    void BlueprintWorkspace::browseAssets(Services& s)
    {
        const auto screen=assetScreen();
        if(screen.empty()){m_status="Select a screen graph to manage its assets.";return;}
        if(!ensureDraftAssets(s))return;
        if(!blueprintEnsureAssetFolder(*s.platform,m_document.assetRoot,screen,m_status)||
            !blueprintListAssets(*s.platform,m_document.assetRoot,screen,m_assetFiles,m_status))return;
        stopTone(s);m_audioEditor=false;m_sharedMedia=false;
        m_assetBrowser=true;m_assetLibrary=false;m_widgetDesigner=false;releaseWidgetPreview();m_visualPlacement=false;cancelVisualPlacement();m_assetFirst=0;m_visualInput=0;clearWire();cancelDrag();
        m_status="Drop a native asset or scene JSON into this window.";
    }
    bool BlueprintWorkspace::publishVisualWidget(Services& s)
    {
        const auto* page=m_document.visualAsset(m_visualSelection);
        const auto screen=assetScreen();
        if(!page||page->kind!="widget"||screen.empty()||m_document.assetRoot.empty())
        {m_status="Select a widget page and a named screen.";return false;}
        if(!blueprintEnsureAssetFolder(*s.platform,m_document.assetRoot,screen,m_status))return false;
        std::string resolved;if(!m_document.visualWidgetPage(page->id,resolved,m_status))return false;
        const auto filename="ui_"+page->id+".json";
        const auto path="own/"+m_document.assetRoot+"/"+screen+"/asset/"+filename;
        if(s.platform->fileTime(path.c_str()))
        {
            FileData old;
            if(!s.platform->readFile(path.c_str(),old,MemTag::General)||old.size!=resolved.size()||
                std::memcmp(old.data,resolved.data(),old.size)!=0)
            {m_status="Page already exists with different contents. Existing file preserved.";return false;}
        }
        else if(!writeVerified(s,path,resolved))return false;
        if(!blueprintListAssets(*s.platform,m_document.assetRoot,screen,m_assetFiles,m_status))return false;
        m_status="Copied asset:"+filename+". Use Ui::load and Ui::draw in your graph.";
        return true;
    }
    namespace
    {
        // This is a plan view of authored linked placements, not a second
        // scene renderer. Runtime-generated and wired coordinates are omitted.
        constexpr float planScale=14;
        struct ActorMarker {size_t graph,node;std::string asset,id,label;float x,y,z;};
        std::vector<ActorMarker> actorMarkers(const BlueprintDocument& doc,const std::string& owner)
        {
            std::vector<ActorMarker> result;
            for(size_t g=0;g<doc.graphs.size();++g)
            {
                const auto& graph=doc.graphs[g];if(graph.owner!=owner||graph.name!="onEnter")continue;
                for(size_t n=0;n<graph.nodes.size();++n)
                {
                    const auto& node=graph.nodes[n];if(node.kind!="actor:spawn-asset")continue;
                    if(std::any_of(graph.links.begin(),graph.links.end(),[&](const BlueprintDocument::Link& link)
                        {return link.to==n&&(link.input=="place"||link.input=="id");}))continue;
                    std::string asset,position="0,0,0",label;
                    for(const auto& value:node.values){if(value.pin=="id")asset=value.value;else if(value.pin=="place")position=value.value;else if(value.pin=="instanceName")label=value.value;}
                    std::vector<std::string> xyz;
                    if(!doc.visualAsset(asset)||!blueprintInputComponents("Vec3",position,xyz))continue;
                    if(label.empty())label=doc.visualAsset(asset)->title;
                    result.push_back({g,n,asset,node.id,label,std::strtof(xyz[0].c_str(),nullptr),std::strtof(xyz[1].c_str(),nullptr),std::strtof(xyz[2].c_str(),nullptr)});
                }
            }
            return result;
        }
    }
    // Keep filtering separate from the full placement set so a search never changes
    // scene geometry, transform edits or viewport picking.
    static bool outlinerMatch(const std::string& label,const std::string& id,const std::string& asset,const std::string& query)
    {
        const auto lower=[](std::string value){for(char& ch:value)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));return value;};
        return lower(label+" "+id+" "+asset).find(lower(query))!=std::string::npos;
    }
    void BlueprintWorkspace::releasePlacementPreview()
    {
        m_placementRoots.clear();m_sceneNativeEntities.clear();m_sceneNativeShown=m_sceneNativeOmitted=0;m_sceneNativeError.clear();m_sceneOrbit=m_scenePan=false;
        if(m_placementWorld){destroy(m_placementWorld);m_placementWorld=nullptr;}
        m_placementReady=false;m_placementRevision=static_cast<size_t>(-1);m_placementScreen.clear();m_actorPreviewId.clear();m_actorPreviewRoot=0;
    }
    void BlueprintWorkspace::runConstructionPreview(Services& s,const std::string& owner,const std::string& id,const std::string& actorJson,const std::string& folder)
    {
        m_actorConstructionApplied=false;m_actorConstructionStatus.clear();
        const auto classOwner=m_document.actorBehaviour(id);
        std::string source,why;
        if(!m_document.validateEditorConstruction(classOwner,why))
        {m_actorConstructionStatus=why;return;}
        if(!compileBlueprintOwner(m_document,owner,source,why))
        {m_actorConstructionStatus="Construction preview compile: "+why;return;}
        JsonTree tree;if(!tree.parse(actorJson.data(),actorJson.size()))return;
        World* candidate=create<World>(MemTag::Scene,*s.assets,*s.host,"blueprint-construct",4096,0);
        if(!candidate){m_actorConstructionStatus="Construction preview allocation failed.";return;}
        candidate->space().init(s.solids);
        const u32 root=candidate->space().makeActor(tree.root(),folder.c_str(),{0,0,0});
        if(!root){destroy(candidate);m_actorConstructionStatus="Construction actor creation failed.";return;}
        // This object is deliberately never installed as a world Behaviour:
        // no start/update/onGone callbacks execute, including on destruction.
        // Its entity property points only to the isolated candidate world.
        constexpr const char* module="blueprint-construct";
        auto* oldWorld=s.world;s.world=candidate;
        const bool compiled=s.host->compile(module,"construction.as",source.data(),source.size());
        bool ok=false;
        if(compiled)
        {
            const std::string name="BP_Behaviour_"+classOwner.substr(10);
            auto* type=s.host->findType(module,name.c_str());
            auto* object=type?s.host->create(type):nullptr;
            if(object)
            {
                for(asUINT i=0;i<type->GetPropertyCount();++i)
                {
                    const char* property=nullptr;type->GetProperty(i,&property);
                    if(!property||std::strcmp(property,"entity")!=0)continue;
                    auto** target=static_cast<EntityRef**>(object->GetAddressOfProperty(i));
                    if(*target)(*target)->release();
                    *target=candidate->ref(root);
                    auto* method=type->GetMethodByDecl("void bp_fn_Construction()");
                    if(*target&&method)ok=s.host->callMethod(object,method);
                    break;
                }
                s.host->release(object);
            }
        }
        s.host->discard(module);s.world=oldWorld;
        if(!ok)
        {
            destroy(candidate);m_actorConstructionStatus="Construction preview failed; showing authored defaults.";
            NX_ERROR("Blueprint Studio: isolated Construction preview failed: %s",id.c_str());return;
        }
        candidate->eachEntity([&](u32 entity,const World::Entity&){candidate->space().transform(entity&0xffffu);});
        destroy(m_placementWorld);m_placementWorld=candidate;m_actorPreviewRoot=root;
        m_actorConstructionApplied=true;m_actorConstructionStatus="Construction preview applied (not saved).";
        NX_INFO("Blueprint Studio: isolated Construction preview ready: %s",id.c_str());
    }

    void BlueprintWorkspace::refreshPlacementPreview(Services& s)
    {
        const bool classPreview=m_actorEditor&&m_assetBrowser&&m_assetLibrary;
        if(!classPreview&&(!m_assetBrowser||!m_assetLibrary||!m_visualPlacement||!m_placement3D))
        {if(m_placementWorld)releasePlacementPreview();return;}
        const auto owner=classPreview&&!m_actorTabScreen.empty()?m_actorTabScreen:assetScreen();const auto previewId=classPreview?m_visualSelection:std::string();
        if(m_placementWorld&&m_placementRevision==m_document.revision()&&m_placementScreen==owner&&m_actorPreviewId==previewId)return;
        releasePlacementPreview();
        if(!s.assets||!s.host||!s.solids||owner.empty())return;
        m_placementWorld=create<World>(MemTag::Scene,*s.assets,*s.host,"blueprint-placement",4096,0);
        if(!m_placementWorld){m_status="Could not allocate native viewport.";return;}
        m_placementRevision=m_document.revision();m_placementScreen=owner;m_actorPreviewId=previewId;
        auto& space=m_placementWorld->space();space.init(s.solids);
        const auto folder="own/"+m_document.assetRoot+"/"+owner;
        unsigned shown=0,failed=0;
        if(classPreview)
        {
            std::string json,why;JsonValue root;
            if(m_document.visualActorSource(previewId,json,why)&&root.parse(json.data(),json.size(),why))
            {
                if(auto* entities=root.find("entities"))for(u32 i=0;i<entities->count();++i)
                    for(const char* field:{"behaviour","sound","camera","group","hits","onLand","mass","friction","bounce","gravity","drag","walker","animate"})entities->at(i)->remove(field);
                json=root.write(true);JsonTree tree;
                if(tree.parse(json.data(),json.size()))m_actorPreviewRoot=space.makeActor(tree.root(),folder.c_str(),{0,0,0});
                m_actorConstructionApplied=false;m_actorConstructionStatus.clear();
                if(m_actorPreviewRoot&&m_actorConstructionPreview)runConstructionPreview(s,owner,previewId,json,folder);
            }
            m_placementOmitted=m_actorPreviewRoot?0u:1u;
            if(m_actorPreviewRoot)NX_INFO("Blueprint Studio: Actor class preview ready: %s",previewId.c_str());
            return;
        }
        if(s.platform)
        {
            std::string nativeJson;
            if(blueprintScenePreview(*s.platform,m_document.assetRoot,owner,nativeJson,m_sceneNativeOmitted,m_sceneNativeError))
            {
                JsonTree nativeTree;
                if(nativeTree.parse(nativeJson.data(),nativeJson.size())&&nativeTree.root().isArray())
                    for(auto entity=nativeTree.root().first();entity.exists();entity=entity.next())
                    {
                        const auto id=space.make(entity,folder.c_str(),{0,0,0});
                        if(id){m_sceneNativeEntities.push_back(id);++m_sceneNativeShown;}else ++m_sceneNativeOmitted;
                    }
                if(m_sceneNativeShown)NX_INFO("Blueprint Studio: native scene context ready: %u entities",m_sceneNativeShown);
            }
        }
        for(const auto& marker:actorMarkers(m_document,owner))
        {
            if(shown>=120){++failed;continue;}
            std::string json,error;
            if(!m_document.visualActorInstance(marker.asset,owner,m_document.graphs[marker.graph].nodes[marker.node].values,json,error)){++failed;continue;}
            JsonValue root;if(!root.parse(json.data(),json.size(),error)){++failed;continue;}
            auto* entities=root.find("entities");if(!entities){++failed;continue;}
            for(u32 i=0;i<entities->count();++i)
            {
                auto* entity=entities->at(i);
                // Native render-only copy: never run a project's callbacks,
                // activate its camera or play sounds in the editor.
                for(const char* field:{"behaviour","sound","camera","group","hits","onLand","collider","mass","friction","bounce","gravity","drag"})
                    entity->remove(field);
            }
            json=root.write(true);JsonTree tree;
            if(!tree.parse(json.data(),json.size())){++failed;continue;}
            const auto rootId=space.makeActor(tree.root(),folder.c_str(),{marker.x,marker.y,marker.z});
            if(!rootId){++failed;continue;}
            m_placementRoots.emplace_back(rootId,marker.id);++shown;
        }
        m_placementOmitted=failed;
    }
    BlueprintWorkspace::SceneLayout BlueprintWorkspace::sceneLayout() const
    {
        SceneLayout layout;
        if(kWidth<1500||kHeight<720)return layout;
        layout.wide=true;
        const float ox=modalX(),oy=modalY();
        layout.sideDx=12-ox-280;layout.sideDy=96-oy-156;
        layout.left=354-ox;layout.right=kWidth-12-ox;
        layout.top=144-oy;layout.bottom=kBottom-102-oy;
        layout.toolsDx=layout.left-630;layout.toolsDy=96-oy-208;
        layout.bottomDy=layout.bottom+4-551;
        return layout;
    }
    void BlueprintWorkspace::frameScenePlacement()
    {
        const auto markers=actorMarkers(m_document,assetScreen());
        bool found=false;float loX=0,hiX=0,loY=0,hiY=0,loZ=0,hiZ=0;
        const auto include=[&](float x,float y,float z,float r)
        {
            if(!found){loX=x-r;hiX=x+r;loY=y-r;hiY=y+r;loZ=z-r;hiZ=z+r;found=true;return;}
            loX=std::min(loX,x-r);hiX=std::max(hiX,x+r);
            loY=std::min(loY,y-r);hiY=std::max(hiY,y+r);
            loZ=std::min(loZ,z-r);hiZ=std::max(hiZ,z+r);
        };
        for(const auto& marker:markers)if(m_placementSelectedId.empty()||marker.id==m_placementSelectedId)
            include(marker.x,marker.y,marker.z,1);
        if(m_placementWorld)
        {
            auto& world=*m_placementWorld;auto& space=world.space();
            world.eachEntity([&](u32 id,const World::Entity& e)
            {
                if(!e.shown||e.dying)return;
                u32 root=id;
                for(unsigned depth=0;depth<64;++depth)
                {const auto parent=world.parentOf(root);if(!parent)break;root=parent;}
                const auto item=std::find_if(m_placementRoots.begin(),m_placementRoots.end(),[&](const auto& entry){return entry.first==root;});
                const bool nativeContext=m_placementSelectedId.empty()&&std::find(m_sceneNativeEntities.begin(),m_sceneNativeEntities.end(),root)!=m_sceneNativeEntities.end();
                if(!nativeContext&&(item==m_placementRoots.end()||(!m_placementSelectedId.empty()&&item->second!=m_placementSelectedId)))return;
                if(const auto* spatial=space.getSpatial(id))
                {
                    const auto pos=space.worldPlace(id);const auto& w=spatial->world;
                    const float radius=std::max({1.f,Vec3(w.m[0],w.m[1],w.m[2]).length(),Vec3(w.m[4],w.m[5],w.m[6]).length(),Vec3(w.m[8],w.m[9],w.m[10]).length()});
                    include(pos.x,pos.y,pos.z,radius);
                }
            });
        }
        m_sceneFocusX=found?(loX+hiX)*0.5f:0;
        m_sceneFocusZ=found?(loZ+hiZ)*0.5f:0;
        m_sceneFocusY=found?(loY+hiY)*0.5f:0;
        m_placementDistance=found?std::clamp(std::max({hiX-loX,hiY-loY,hiZ-loZ})*2.2f,4.f,10000.f):25;
        m_placementReady=false;
        m_status=found?"Framed scene actors.":"Reset empty scene view.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
    }
    bool BlueprintWorkspace::placementPoint(float x,float y,float height,float& wx,float& wz) const
    {
        const auto layout=sceneLayout();
        const float planCenterX=(layout.left+layout.right)*0.5f,planCenterY=(layout.top+layout.bottom)*0.5f;
        if(!m_placement3D){wx=std::round((x-planCenterX)/planScale);wz=std::round((y-planCenterY)/planScale);return true;}
        if(!m_placementWorld||!m_placementReady)return false;
        const auto& space=m_placementWorld->space();
        const auto ray=space.rayThrough({x+modalX(),y+modalY()}),eye=space.cameraPlace();
        if(std::abs(ray.y)<0.00001f)return false;
        const float t=(height-eye.y)/ray.y;
        if(!(t>0&&t<10000))return false;
        wx=std::round(eye.x+ray.x*t);wz=std::round(eye.z+ray.z*t);
        return std::isfinite(wx)&&std::isfinite(wz)&&std::abs(wx)<=100000&&std::abs(wz)<=100000;
    }
    bool BlueprintWorkspace::placementProject(float wx,float wy,float wz,float& x,float& y) const
    {
        const auto layout=sceneLayout();
        const float planCenterX=(layout.left+layout.right)*0.5f,planCenterY=(layout.top+layout.bottom)*0.5f;
        if(!m_placement3D){x=planCenterX+wx*planScale;y=planCenterY+wz*planScale;return true;}
        if(!m_placementReady||!m_placementWorld)return false;
        Vec2 p;if(!m_placementWorld->space().toScreen({wx,wy,wz},p))return false;
        x=p.x-modalX();y=p.y-modalY();return std::isfinite(x)&&std::isfinite(y);
    }
    bool BlueprintWorkspace::gizmoAxis(float wx,float wy,float wz,unsigned axis,float& x,float& y,float& dx,float& dy)const
    {
        float endX=0,endY=0;
        if(!placementProject(wx,wy,wz,x,y)||!placementProject(wx+(axis==0?1.f:0.f),wy+(axis==1?1.f:0.f),wz+(axis==2?1.f:0.f),endX,endY))return false;
        dx=endX-x;dy=endY-y;
        return dx*dx+dy*dy>0.25f;
    }
    void BlueprintWorkspace::updateVisualPlacement(Services& s,float x,float y,bool left,bool click)
    {
        const auto layout=sceneLayout();
        const float planLeft=layout.left,planTop=layout.top,planRight=layout.right,planBottom=layout.bottom;
        m_placementX=x;m_placementY=y;
        if(m_placementDragging)
        {
            if(left)return;
            if(m_document.revision()!=m_placementDragRevision){cancelVisualPlacement();m_status="Placement cancelled: document changed.";return;}
            if(m_placementGraph!=static_cast<size_t>(-1)&&std::abs(x-m_placementStartX)<3&&std::abs(y-m_placementStartY)<3){cancelVisualPlacement();return;}
            if(in(x,y,planLeft,planTop,planRight,planBottom))
            {
                bool ok=false;
                if(m_placementGraph!=static_cast<size_t>(-1))
                {
                    const auto markers=actorMarkers(m_document,assetScreen());
                    for(const auto& marker:markers)if(marker.graph==m_placementGraph&&marker.node==m_placementNode)
                    {
                        if(m_gizmoAxis>=0)
                        {
                            const float delta=((x-m_placementStartX)*m_gizmoDx+(y-m_placementStartY)*m_gizmoDy)/(m_gizmoDx*m_gizmoDx+m_gizmoDy*m_gizmoDy);
                            auto values=std::vector<float>{m_gizmoInitial[0],m_gizmoInitial[1],m_gizmoInitial[2]};
                            auto& v=values[static_cast<size_t>(m_gizmoAxis)];
                            if(m_gizmoMode==0)v+=std::round(delta*10.f)/10.f;
                            else if(m_gizmoMode==1)v+=std::round(delta*15.f);
                            else v=std::max(0.01f,v+delta*0.25f);
                            const char* property=m_gizmoMode==0?"place":m_gizmoMode==1?"rotationOverride":"sizeOverride";
                            ok=m_document.editVisualActorPlacement(assetScreen(),marker.id,property,std::to_string(values[0])+","+std::to_string(values[1])+","+std::to_string(values[2]),m_status);
                        }
                        else
                        {
                            const float height=marker.y;
                            float wx=0,wz=0;
                            if(placementPoint(x,y,height,wx,wz))ok=m_document.editVisualActorPlacement(assetScreen(),marker.id,"place",std::to_string(wx+m_placementGrabWorldX)+","+std::to_string(height)+","+std::to_string(wz+m_placementGrabWorldZ),m_status);
                            else m_status="No forward intersection with placement plane.";
                        }
                        break;
                    }
                }
                else
                {
                    const float height=m_placement3D?m_placementHeight:0;float wx=0,wz=0;
                    if(placementPoint(x,y,height,wx,wz))ok=m_document.placeVisualActor(assetScreen(),m_placementAsset,std::to_string(wx)+","+std::to_string(height)+","+std::to_string(wz),m_status);
                    else m_status="No forward intersection with placement plane.";
                }
                if(ok){rebuildPins();filterCatalogue();m_diagnostic={};m_status="Linked placement updated. Ctrl+Z undoes it.";}
            }
            else m_status="Placement cancelled.";
            cancelVisualPlacement();return;
        }
        const float pointerX=x,pointerY=y;
        const bool inside=in(x,y,planLeft,planTop,planRight,planBottom);
        const auto& raw=s.input->raw();
        const bool orbit=m_placement3D&&raw.mouseButtons[1],pan=m_placement3D&&raw.mouseButtons[2];
        if(!orbit&&m_sceneOrbit){m_sceneOrbit=false;NX_INFO("Blueprint Studio: scene orbit completed");}
        if(!pan&&m_scenePan){m_scenePan=false;NX_INFO("Blueprint Studio: scene pan completed");}
        if(m_placement3D&&(m_sceneOrbit||m_scenePan||inside))
        {
            if(orbit||pan)
            {
                if(m_sceneOrbit||m_scenePan)
                {
                    const float dx=x-m_scenePointerX,dy=y-m_scenePointerY;
                    if(orbit){m_placementYaw+=dx*0.008f;m_scenePitch=std::clamp(m_scenePitch+dy*0.008f,0.10f,1.45f);}
                    else
                    {
                        const float units=m_placementDistance/std::max(1.f,planBottom-planTop);
                        m_sceneFocusX=std::clamp(m_sceneFocusX+(-dx*std::cos(m_placementYaw)-dy*std::sin(m_placementYaw))*units,-100000.f,100000.f);
                        m_sceneFocusZ=std::clamp(m_sceneFocusZ+(dx*std::sin(m_placementYaw)-dy*std::cos(m_placementYaw))*units,-100000.f,100000.f);
                    }
                }
                m_sceneOrbit=orbit;m_scenePan=pan;m_scenePointerX=x;m_scenePointerY=y;m_placementReady=false;return;
            }
            const float wheel=s.input->wheel();
            if(inside&&wheel!=0){m_placementDistance=std::clamp(m_placementDistance*std::pow(0.88f,std::clamp(wheel,-8.f,8.f)),2.f,10000.f);m_placementReady=false;NX_INFO("Blueprint Studio: scene wheel zoom");return;}
        }
        if(layout.wide)
        {
            if(in(x,y,280+layout.sideDx,156+layout.sideDy,610+layout.sideDx,590+layout.sideDy))
            {x-=layout.sideDx;y-=layout.sideDy;}
            else if(in(x,y,630+layout.toolsDx,208+layout.toolsDy,1000+layout.toolsDx,241+layout.toolsDy))
            {x-=layout.toolsDx;y-=layout.toolsDy;}
            else if(in(x,y,630+layout.toolsDx,551+layout.bottomDy,1000+layout.toolsDx,598+layout.bottomDy))
            {x-=layout.toolsDx;y-=layout.bottomDy;}
            else {x=-10000;y=-10000;}
        }
        const auto owner=assetScreen();
        if(m_placementSelectedOwner!=owner){m_placementSelectedId.clear();m_placementSelectedOwner=owner;m_placementFirst=0;m_outlinerQuery.clear();}
        const auto listed=actorMarkers(m_document,owner);
        auto filtered=listed;std::erase_if(filtered,[&](const ActorMarker& item){return !outlinerMatch(item.label,item.id,item.asset,m_outlinerQuery);});
        if(m_placementFirst>=filtered.size())m_placementFirst=0;
        if(m_placementOutliner&&in(x,y,280,234,610,486))
        {
            if(s.input->wheel()<0&&m_placementFirst+7<filtered.size())++m_placementFirst;
            if(s.input->wheel()>0&&m_placementFirst)--m_placementFirst;
        }
        if(!m_placementOutliner&&in(x,y,280,198,610,522))
        {
            const auto count=m_document.visualAssets().size();
            if(s.input->wheel()<0&&m_visualFirst+9<count)++m_visualFirst;
            if(s.input->wheel()>0&&m_visualFirst)--m_visualFirst;
        }
        if(!click)return;
        if(in(x,y,280,156,610,189)){m_placementOutliner=x>=449;return;}
        if(m_placementOutliner&&in(x,y,280,198,610,231))
        {
            if(x>=550){m_outlinerQuery.clear();m_placementFirst=0;return;}
            m_visualText=m_outlinerQuery;m_visualProperty="FILTER WORLD OUTLINER";m_visualInput=11;s.input->setTyping(true);return;
        }
        if(m_placementOutliner&&in(x,y,280,234,610,486))
        {
            const size_t index=m_placementFirst+static_cast<size_t>((y-234)/36);
            if(index<filtered.size()){const auto& marker=filtered[index];m_placementSelectedId=marker.id;m_visualSelection=marker.asset;m_status="Instance selected.";}
            return;
        }
        if(m_placementOutliner&&in(x,y,280,490,610,560))
        {
            const auto it=std::find_if(listed.begin(),listed.end(),[&](const ActorMarker& marker){return marker.id==m_placementSelectedId;});
            if(it==listed.end()){m_status="Select an instance in the outliner.";return;}
            const char* pins[]={"place","rotationOverride","sizeOverride","instanceName"};
            const unsigned index=(y>=526?2u:0u)+(x>=449?1u:0u);
            m_visualProperty=pins[index];m_visualText.clear();
            const auto& node=m_document.graphs[it->graph].nodes[it->node];
            for(const auto& value:node.values)if(value.pin==m_visualProperty)m_visualText=value.value;
            if(index==0&&m_visualText.empty())m_visualText="0,0,0";
            m_visualInput=10;s.input->setTyping(true);m_status=index==0?"Enter world X,Y,Z.":"Enter instance override; blank inherits class default.";return;
        }
        if(in(x,y,825,208,1000,241)){m_placement3D=!m_placement3D;releasePlacementPreview();if(m_placement3D)frameScenePlacement();return;}
        if(in(x,y,630,208,825,241)){m_visualPlacement=false;releasePlacementPreview();return;}
        if(m_placement3D&&in(x,y,630,551,1000,573))
        {
            const unsigned button=static_cast<unsigned>((x-630)/74);
            if(button==0)m_placementYaw-=0.35f;else if(button==1)m_placementYaw+=0.35f;
            else if(button==2)m_placementDistance=std::max(2.f,m_placementDistance*0.8f);
            else if(button==3)m_placementDistance=std::min(10000.f,m_placementDistance*1.25f);
            else frameScenePlacement();
            m_placementReady=false;return;
        }
        if(m_placement3D&&in(x,y,630,575,1000,598))
        {
            if(x<740)m_placementHeight=std::max(-100.f,m_placementHeight-1);
            else if(x>890)m_placementHeight=std::min(100.f,m_placementHeight+1);
            else m_gizmoMode=(m_gizmoMode+1)%3;
            return;
        }
        const auto& assets=m_document.visualAssets();
        if(!m_placementOutliner&&in(x,y,280,198,610,522))
        {
            const size_t row=m_visualFirst+static_cast<size_t>((y-198)/36);
            if(row<assets.size()&&assets[row].kind=="actor")
            {
                m_visualSelection=assets[row].id;m_placementAsset=assets[row].id;
                m_placementDragging=true;m_placementDragRevision=m_document.revision();m_status="Drag actor to the viewport; release to place.";
            }
            return;
        }
        x=pointerX;y=pointerY;
        if(!in(x,y,planLeft,planTop,planRight,planBottom))return;
        if(m_placement3D&&!m_placementSelectedId.empty())for(const auto& marker:listed)if(marker.id==m_placementSelectedId)
        {
            for(unsigned axis=0;axis<3;++axis)
            {
                float ox=0,oy=0,dx=0,dy=0;
                if(!gizmoAxis(marker.x,marker.y,marker.z,axis,ox,oy,dx,dy))continue;
                const float len=std::sqrt(dx*dx+dy*dy),ex=ox+40*dx/len,ey=oy+40*dy/len;
                if(std::abs(x-ex)>9||std::abs(y-ey)>9)continue;
                m_gizmoInitial[0]=marker.x;m_gizmoInitial[1]=marker.y;m_gizmoInitial[2]=marker.z;
                if(m_gizmoMode)
                {
                    std::string json;JsonTree tree;
                    if(!m_document.visualActorInstance(marker.asset,owner,m_document.graphs[marker.graph].nodes[marker.node].values,json,m_status)||!tree.parse(json.data(),json.size()))return;
                    const auto value=tree.root()["entities"][0u][m_gizmoMode==1?"angles":"size"];
                    const float fallback=m_gizmoMode==2?1.f:0.f;
                    for(u32 i=0;i<3;++i)m_gizmoInitial[i]=value.isArray()?value[i].number(fallback):value.isNumber()?(m_gizmoMode==1?(i==1?value.number():0.f):value.number()):fallback;
                }
                m_gizmoAxis=static_cast<int>(axis);m_gizmoDx=dx;m_gizmoDy=dy;
                m_placementGraph=marker.graph;m_placementNode=marker.node;m_placementAsset=marker.asset;
                m_placementStartX=x;m_placementStartY=y;m_placementDragRevision=m_document.revision();m_placementDragging=true;
                m_status="Drag axis; release to apply one undoable transform.";return;
            }
            break;
        }
        m_placementSelectedId.clear();
        const auto& markers=listed;
        std::string surface;
        if(m_placement3D&&m_placementWorld&&m_placementReady)
        {
            auto& world=*m_placementWorld;auto& space=world.space();
            const auto eye=space.cameraPlace(),ray=space.rayThrough({x+modalX(),y+modalY()});
            float distance=100000.f;
            world.eachEntity([&](u32 id,const World::Entity& e)
            {
                if(!e.shown||e.dying)return;
                u32 root=id;
                for(unsigned depth=0;depth<64;++depth)
                {
                    const auto parent=world.parentOf(root);if(!parent)break;root=parent;
                }
                const auto entry=std::find_if(m_placementRoots.begin(),m_placementRoots.end(),[&](const auto& item){return item.first==root;});
                if(entry!=m_placementRoots.end()&&space.pickSurface(id,eye,ray,distance))surface=entry->second;
            });
        }
        for(auto it=markers.rbegin();it!=markers.rend();++it)
        {
            float mx=0,my=0;
            const bool markerHit=placementProject(it->x,it->y,it->z,mx,my)&&std::abs(x-mx)<=10&&std::abs(y-my)<=10;
            if(!surface.empty()?surface==it->id:markerHit)
            {
                m_placementGraph=it->graph;m_placementNode=it->node;m_placementAsset=it->asset;
                m_placementSelectedId=it->id;m_visualSelection=it->asset;m_placementStartX=x;m_placementStartY=y;m_placementDragRevision=m_document.revision();
                m_placementGrabWorldX=m_placementGrabWorldZ=0;m_placementMoveHeight=it->y;
                if(m_placement3D)
                {
                    float wx=0,wz=0;
                    if(placementPoint(x,y,it->y,wx,wz))
                    {m_placementGrabWorldX=it->x-wx;m_placementGrabWorldZ=it->z-wz;}
                }
                m_placementDragging=true;m_status="Move instance; release to save position.";return;
            }
        }
    }
    void BlueprintWorkspace::drawVisualPlacement(Canvas& c)
    {
        const auto layout=sceneLayout();
        const float planLeft=layout.left,planTop=layout.top,planRight=layout.right,planBottom=layout.bottom;
        const float planCenterX=(planLeft+planRight)*0.5f,planCenterY=(planTop+planBottom)*0.5f;
        c.push();c.translate(layout.toolsDx,layout.toolsDy);
        c.roundRect(630,208,190,33,4,{0.19f,0.33f,0.50f,1});
        c.text(*m_font,"Back",725,215,text,Align::Center);
        c.roundRect(825,208,175,33,4,{0.19f,0.33f,0.50f,1});
        c.text(*m_font,m_placement3D?"Plan view":"3D viewport",912,215,text,Align::Center);
        c.pop();
        c.rect(planLeft,planTop,planRight-planLeft,planBottom-planTop,{0.07f,0.10f,0.15f,1});
        if(m_placement3D&&m_placementWorld)
        {
            auto& space=m_placementWorld->space();
            const float d=m_placementDistance;
            const Vec3 focus(m_sceneFocusX,m_sceneFocusY,m_sceneFocusZ);
            const float horizontal=d*std::cos(m_scenePitch);
            space.setCamera(focus+Vec3(std::sin(m_placementYaw)*horizontal,d*std::sin(m_scenePitch),std::cos(m_placementYaw)*horizontal),focus,55);
            space.setFar(std::max(400.f,d*4.f));
            space.m_editorGrid=true;space.m_editorGridHeight=m_placementHeight;
            space.m_editorGridX=std::round(m_sceneFocusX);space.m_editorGridZ=std::round(m_sceneFocusZ);
            space.draw(c,planLeft+modalX(),planTop+modalY(),planRight-planLeft,planBottom-planTop);
            m_placementReady=true;
        }
        c.clip(planLeft,planTop,planRight-planLeft,planBottom-planTop);
        if(!m_placement3D)for(int i=-static_cast<int>(std::ceil(std::max(planRight-planLeft,planBottom-planTop)/(2*planScale)));i<=static_cast<int>(std::ceil(std::max(planRight-planLeft,planBottom-planTop)/(2*planScale)));++i)
        {
            const float offset=static_cast<float>(i)*planScale;
            c.rect(planCenterX+offset,planTop,i?1.f:2.f,planBottom-planTop,i?Color{0.16f,0.21f,0.28f,1}:Color{0.37f,0.49f,0.60f,1});
            c.rect(planLeft,planCenterY+offset,planRight-planLeft,i?1.f:2.f,i?Color{0.16f,0.21f,0.28f,1}:Color{0.37f,0.49f,0.60f,1});
        }
        const auto authoredMarkers=actorMarkers(m_document,assetScreen());
        const auto& target=m_document.target();
        const auto sceneBudget=std::to_string(authoredMarkers.size())+" authored placements | "+(target.xbox()?std::to_string(target.ram)+" MiB unified RAM":"Target selected")+" | actual runtime fit unknown";
        c.rect(planLeft,planTop,planRight-planLeft,20,{0.11f,0.15f,0.20f,0.92f});c.text(*m_font,sceneBudget.c_str(),planLeft+6,planTop+1,muted);
        for(const auto& marker:authoredMarkers)
        {
            if(m_placementDragging&&marker.graph==m_placementGraph&&marker.node==m_placementNode)continue;
            float x=0,y=0;if(!placementProject(marker.x,marker.y,marker.z,x,y)||!in(x,y,planLeft,planTop,planRight,planBottom))continue;
            const bool selected=marker.id==m_placementSelectedId&&m_placementSelectedOwner==assetScreen();
            c.roundRect(x-8,y-8,16,16,3,selected?Color{0.95f,0.72f,0.24f,1}:Color{0.26f,0.56f,0.85f,1});
            c.text(*m_font,marker.label.c_str(),x+11,y-8,text);
            if(selected&&m_placement3D)
            {
                const Color colors[]={{0.95f,0.2f,0.2f,1},{0.2f,0.9f,0.35f,1},{0.25f,0.5f,1,1}};
                for(unsigned axis=0;axis<3;++axis)
                {
                    float ox=0,oy=0,dx=0,dy=0;if(!gizmoAxis(marker.x,marker.y,marker.z,axis,ox,oy,dx,dy))continue;
                    const float len=std::sqrt(dx*dx+dy*dy),ex=ox+40*dx/len,ey=oy+40*dy/len;
                    c.line(ox,oy,ex,ey,2,colors[axis]);c.roundRect(ex-5,ey-5,10,10,2,colors[axis]);
                    c.text(*m_font,axis==0?"X":axis==1?"Y":"Z",ex+6,ey-6,text);
                }
            }
        }
        if(m_placementDragging&&in(m_placementX,m_placementY,planLeft,planTop,planRight,planBottom))
        {
            const float height=m_placementGraph!=static_cast<size_t>(-1)?m_placementMoveHeight:m_placement3D?m_placementHeight:0;
            float wx=0,wz=0,x=0,y=0;
            if(placementPoint(m_placementX,m_placementY,height,wx,wz)&&placementProject(wx+m_placementGrabWorldX,height,wz+m_placementGrabWorldZ,x,y))
                c.roundRect(x-9,y-9,18,18,3,{0.35f,0.80f,0.56f,1});
        }
        c.noClip();
        c.push();c.translate(layout.toolsDx,layout.bottomDy);
        if(m_placement3D)
        {
            const char* labels[]={"Orbit L","Orbit R","Zoom +","Zoom -","Frame"};
            for(unsigned i=0;i<5;++i){const float x=630+74*static_cast<float>(i);c.rect(x,551,71,22,{0.19f,0.33f,0.50f,1});c.text(*m_font,labels[i],x+35,552,text,Align::Center);}
            c.text(*m_font,"Height -",635,575,text);c.text(*m_font,"Height +",925,575,text);
            if(layout.wide){c.text(*m_font,("Plane Y: "+std::to_string(m_placementHeight)).c_str(),1010,551,muted);c.text(*m_font,"Right drag: orbit | Middle drag: pan | Wheel: zoom",1010,575,muted);
                const auto context=m_sceneNativeError.empty()?"Native scene context: "+std::to_string(m_sceneNativeShown)+" visible / "+std::to_string(m_sceneNativeOmitted)+" omitted (read-only)":m_sceneNativeError;
                c.clip(1010,526,std::max(1.f,layout.right-layout.toolsDx-1010),22);c.text(*m_font,context.c_str(),1010,528,muted);c.noClip();}
            c.text(*m_font,m_gizmoMode==0?"Move XYZ":m_gizmoMode==1?"Rotate XYZ":"Scale XYZ",815,575,accent,Align::Center);
        }
        else
        {
            c.text(*m_font,"X/Z plan | 1 m snap | Drag actor from library",630,555,muted);
            c.text(*m_font,"Only constant linked placements are shown.",630,575,muted);
        }
        c.pop();
    }
    namespace
    {
        constexpr const char* themeStates[]={"normal","hovered","checked","focused","pressed","disabled"};
        constexpr const char* themeProps[]={"back","text","line","track","radius","lineWidth","scale","shadow","size","padding","gap","seconds","minWidth","minHeight","slice","align"};
        constexpr size_t themeLookCount=8;
        constexpr const char* themeMediaProps[]={"font","image","soundMove","soundAccept","soundBack","soundChange"};
        bool themeColorProperty(const std::string& key){return key=="back"||key=="text"||key=="line"||key=="track";}
        bool themeMediaProperty(const std::string& key){for(const auto* p:themeMediaProps)if(key==p)return true;return false;}
        bool themeMediaMatch(const std::string& key,const std::string& path)
        {
            const auto dot=path.rfind('.');if(dot==std::string::npos)return false;
            auto ext=path.substr(dot);for(char& ch:ext)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if(key=="font")return ext==".sfn"||ext==".fnt"||ext==".ttf"||ext==".otf";
            if(key=="image")return ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".dds"||ext==".bmp"||ext==".tga";
            return ext==".wav"||ext==".ogg"||ext==".adpcm"||ext==".nxa";
        }
        Color themePickerColor(const std::string& value)
        {
            const auto start=!value.empty()&&value.front()=='#'?1u:0u;
            if((value.size()!=start+6&&value.size()!=start+8)||value.find_first_not_of("0123456789abcdefABCDEF",start)!=std::string::npos)return {1,1,1,1};
            const auto byte=[&](size_t at){return static_cast<float>(std::strtoul(value.substr(at,2).c_str(),nullptr,16))/255.f;};
            return {byte(start),byte(start+2),byte(start+4),value.size()==start+8?byte(start+6):1.f};
        }
        std::string themePickerHex(Color color)
        {
            char value[10];const auto byte=[](float n){return static_cast<unsigned>(std::round(std::clamp(n,0.f,1.f)*255.f));};
            snprintf(value,sizeof(value),"%02X%02X%02X%02X",byte(color.r),byte(color.g),byte(color.b),byte(color.a));return value;
        }
        constexpr const char* timelineTracks[]={"x","y","scale","opacity","rotation"};
        JsonTree::Value designItem(JsonTree::Value node,const std::string& id)
        {
            if(node.isObject()&&node["id"].isString()&&id==node["id"].text())return node;
            if(node.isObject()||node.isArray())for(auto child=node.first();child.exists();child=child.next())
            {auto found=designItem(child,id);if(found.exists())return found;}
            return {};
        }
        struct DesignRow {std::string id,kind,label;unsigned depth;};
        constexpr const char* designPalette[]={"column","row","grid","stack","scroll","label","button","toggle","slider","progress","field","space","component"};
        constexpr const char* designEvents[]={"click","change","focus","done","draw","makeRow"};
        constexpr const char* designProps[]={"text","width","height","gap","padding","grow","align","justify","self","style","value","visible"};
        constexpr const char* advancedDesignProps[]={"minWidth","maxWidth","columns","columnWidth","rowHeight","low","high","step","enabled","checked","wrap","count","minHeight","maxHeight","gridSlot"};
        void designRows(JsonTree::Value value,std::vector<DesignRow>& rows,unsigned depth)
        {
            if(depth>12||rows.size()>=256)return;
            if(value.isArray())
            {
                for(auto child=value.first();child.exists();child=child.next())designRows(child,rows,depth);
                return;
            }
            if(!value.isObject())return;
            const char* kind=nullptr;
            for(const char* candidate:{"component","column","row","grid","stack","scroll","label","paragraph","image","button","toggle","slider","progress","choice","field","list","tabs","space","line","custom"})
                if(value[candidate].exists()){kind=candidate;break;}
            if(!kind)return;
            const auto main=value[kind];
            rows.push_back({value["id"].text(),kind,value["text"].text(main.text()),depth});
            if(main.isArray())
            {
                if(std::string(kind)=="tabs")
                    for(auto tab=main.first();tab.exists();tab=tab.next())designRows(tab["page"],rows,depth+1);
                else if(std::string(kind)!="choice")designRows(main,rows,depth+1);
            }
        }
    }
    void BlueprintWorkspace::releaseWidgetPreview()
    {
        if(m_designUi){destroy(m_designUi);m_designUi=nullptr;}
        m_designPreviewAsset.clear();m_designPreviewError.clear();m_designRevision=static_cast<size_t>(-1);
    }
    void BlueprintWorkspace::refreshWidgetPreview(Services& s)
    {
        if(!m_assetBrowser||!m_assetLibrary||!m_widgetDesigner){m_widgetPreviewDrag=false;if(m_designUi)releaseWidgetPreview();return;}
        if(m_visualInput)return;
        if(m_designPreviewAsset==m_visualSelection&&m_designRevision==m_document.revision())return;
        releaseWidgetPreview();
        m_designPreviewAsset=m_visualSelection;m_designRevision=m_document.revision();
        const auto* page=m_document.visualAsset(m_visualSelection);
        if(!page||page->kind!="widget")return;
        const auto folder="own/"+m_document.assetRoot+"/"+assetScreen();
        m_designUi=create<Ui>(MemTag::UI,*s.canvas,*s.assets,*s.audio,*s.input,*s.host,*s.platform,folder.c_str());
        if(!m_designUi){m_designPreviewError="Cannot allocate widget preview.";return;}
        std::string theme;
        if(!m_document.visualWidgetTheme(m_visualSelection,theme,m_designPreviewError))return;
        if(!theme.empty()&&!m_designUi->loadThemeSource(theme.data(),theme.size()))
        {m_designPreviewError=m_designUi->error();return;}
        std::string resolved;if(!m_document.visualWidgetPage(m_visualSelection,resolved,m_designPreviewError))return;
        if(!m_designUi->loadPage(resolved.data(),resolved.size(),m_designUi->root(),nullptr,"designer",true))
            m_designPreviewError=m_designUi->error();
    }
    BlueprintWorkspace::WidgetLayout BlueprintWorkspace::widgetLayout() const
    {
        WidgetLayout layout;
        const auto& target=m_document.target();
        const float designWidth=target.width()*target.pixelAspect(),designHeight=static_cast<float>(target.height());
        layout.previewScale=std::min(370.f/designWidth,208.f/designHeight);
        layout.previewScaleX=layout.previewScale*target.pixelAspect();
        layout.previewWidth=designWidth*layout.previewScale;layout.previewHeight=designHeight*layout.previewScale;
        layout.previewX=815-layout.previewWidth*0.5f;layout.previewY=207+(208-layout.previewHeight)*0.5f;
        if(kWidth<1500||kHeight<720)return layout;
        layout.wide=true;
        const float ox=modalX(),oy=modalY();
        layout.leftDx=12-ox-280;layout.leftDy=96-oy-156;
        layout.rightDx=kWidth-382-ox-630;layout.rightDy=96-oy-156;
        layout.propsDy=layout.rightDy-218;
        layout.headerDx=kWidth-12-ox-1000;layout.headerDy=12-oy-116;
        const float l=354-ox,r=kWidth-394-ox,t=112-oy,b=kBottom-48-oy;
        layout.previewScale=std::max(0.01f,std::min((r-l-20)/designWidth,(b-t-20)/designHeight));
        layout.previewScaleX=layout.previewScale*target.pixelAspect();
        layout.previewWidth=designWidth*layout.previewScale;layout.previewHeight=designHeight*layout.previewScale;
        layout.previewX=(l+r-layout.previewWidth)*0.5f;layout.previewY=(t+b-layout.previewHeight)*0.5f;
        return layout;
    }
    void BlueprintWorkspace::updateWidgetDesigner(Services& s,float x,float y,bool left,bool click)
    {
        const auto* page=m_document.visualAsset(m_visualSelection);
        if(!page||page->kind!="widget"){m_widgetDesigner=false;return;}
        JsonTree tree;if(!tree.parse(page->source.data(),page->source.size()))return;
        const auto layout=widgetLayout();
        const float pointerX=x,pointerY=y;
        if(m_targetResolutionMenu&&click)
        {
            m_targetResolutionMenu=false;
            const float menuX=630+layout.rightDx,menuY=194+layout.rightDy;
            const unsigned choices=BlueprintTarget::resolutionCount;
            const unsigned guideCount=m_document.target().resolution==3?3u:0u;
            if(in(x,y,menuX,menuY,menuX+370,menuY+(choices+guideCount)*36))
            {
                const auto index=static_cast<unsigned>((y-menuY)/36);
                if(index<choices)
                {
                    auto target=m_document.target();target.resolution=index;
                    if(m_document.setTarget(target))m_status=target.warning();
                }
                else if(index<choices+guideCount)
                {
                    m_widgetGuideMask^=1u<<(index-choices);
                    m_targetResolutionMenu=true;
                    m_status="Centered display-size guides only; project target unchanged.";
                }
            }
            return;
        }
        if(layout.wide)
        {
            if(in(x,y,280+layout.leftDx,156+layout.leftDy,610+layout.leftDx,582+layout.leftDy))
            {x-=layout.leftDx;y-=layout.leftDy;}
            else if(in(x,y,630+layout.rightDx,156+layout.rightDy,1000+layout.rightDx,189+layout.rightDy))
            {x-=layout.rightDx;y-=layout.rightDy;}
            else if(in(x,y,630+layout.rightDx,420+layout.propsDy,1000+layout.rightDx,584+layout.propsDy))
            {x-=layout.rightDx;y-=layout.propsDy;}
            else {x=-10000;y=-10000;}
        }
        std::vector<DesignRow> rows;designRows(tree.root(),rows,0);
        const size_t count=m_widgetPalette?sizeof(designPalette)/sizeof(designPalette[0]):rows.size()+1;
        if(m_widgetFirst>=count)m_widgetFirst=0;
        if(in(x,y,280,202,610,522))
        {
            if(s.input->wheel()<0&&m_widgetFirst+8<count)++m_widgetFirst;
            if(s.input->wheel()>0&&m_widgetFirst)--m_widgetFirst;
        }
        const auto animation=designItem(tree.root(),m_widgetSelection)["animation"];
        const float duration=animation["duration"].exists()?std::clamp(animation["duration"].number(),0.01f,60.f):2.f;
        const double now=s.platform->seconds();
        if(m_widgetTimeline&&m_widgetTimelinePlay)
        {
            m_widgetTimelineTime+=static_cast<float>(std::clamp(now-m_widgetTimelineLast,0.0,0.1));
            if(m_widgetTimelineTime>=duration)
            {if(animation["loop"].flag())m_widgetTimelineTime=std::fmod(m_widgetTimelineTime,duration);else{m_widgetTimelineTime=duration;m_widgetTimelinePlay=false;}}
        }
        m_widgetTimelineLast=now;
        m_widgetTimelineTime=std::clamp(m_widgetTimelineTime,0.f,duration);
        const float previewScale=layout.previewScale,previewWidth=layout.previewWidth,previewHeight=layout.previewHeight;
        const float previewX=layout.previewX,previewY=layout.previewY;
        if(m_widgetPreviewDrag)
        {
            m_widgetPreviewDx=(pointerX-m_widgetDragX)/layout.previewScaleX;
            m_widgetPreviewDy=(pointerY-m_widgetDragY)/previewScale;
            if(left)return;
            m_widgetPreviewDrag=false;
            if(m_widgetDragRevision==m_document.revision()&&in(pointerX,pointerY,previewX,previewY,previewX+previewWidth,previewY+previewHeight)&&
                (std::abs(pointerX-m_widgetDragX)>2||std::abs(pointerY-m_widgetDragY)>2))
            {
                const char* const names[]={"top-left","top-center","top-right","middle-left","center","middle-right","bottom-left","bottom-center","bottom-right"};
                if(m_document.positionVisualWidget(m_visualSelection,m_widgetSelection,names[m_widgetDragAnchor],
                    std::round(m_widgetBaseX+m_widgetPreviewDx),std::round(m_widgetBaseY+m_widgetPreviewDy),m_status))
                    m_status="Widget position updated. Ctrl+Z undoes the entire drag.";
            }
            else m_status="Widget drag cancelled or unchanged.";
            return;
        }
        if(m_widgetDragging)
        {
            if(left)return;
            m_widgetDragging=false;
            if((std::abs(x-m_widgetDragX)>6||std::abs(y-m_widgetDragY)>6)&&in(x,y,280,202,610,522))
            {
                const size_t target=m_widgetFirst+static_cast<size_t>((y-202)/40);
                if(target<=rows.size()&&(!target||!rows[target-1].id.empty()))
                    if(m_document.reparentVisualWidget(m_visualSelection,m_widgetDragId,target?rows[target-1].id:"",m_status))
                        m_status="Moved widget into container.";
            }
            m_widgetDragId.clear();return;
        }
        if(!click)return;
        if(!m_widgetPalette&&m_designUi&&m_designPreviewError.empty()&&
            in(pointerX,pointerY,previewX,previewY,previewX+previewWidth,previewY+previewHeight))
        {
            const float localX=(pointerX-previewX)/layout.previewScaleX,localY=(pointerY-previewY)/previewScale;
            // Walk in reverse painting order, taking the deepest visible widget
            // with a stable ID. Bounds use native layout, not a second renderer.
            const auto hit=[&](auto&& self,Widget& item)->Widget*
            {
                if(!item.visible)return nullptr;
                float sx=0,sy=0;m_designUi->place(item,sx,sy);
                if(!in(localX,localY,sx,sy,sx+item.w,sy+item.h))return nullptr;
                for(u32 j=item.children.count();j>0;--j)
                    if(auto* found=self(self,*item.children[j-1]))return found;
                return !item.id.empty()?&item:nullptr;
            };
            if(auto* item=hit(hit,m_designUi->root()))
            {
                const auto slash=item->id.find('/');
                if(slash!=std::string::npos)if(auto* outer=m_designUi->find(item->id.substr(0,slash).c_str()))item=outer;
                m_widgetSelection=item->id;
                if(m_widgetTimeline){m_widgetTimelinePlay=false;m_widgetTimelineTime=0;return;}
                if(item->parent&&item->parent->kind==WidgetKind::Stack&&item->anchor>=9)
                {m_status="Stretch anchor selected. Edit inset / offset in Anchors.";}
                else if(item->parent&&item->parent->kind==WidgetKind::Stack)
                {
                    // Preserve the existing anchor, or convert the current
                    // visual position to a top-left anchor with no jump.
                    m_widgetDragAnchor=item->anchor>=0&&item->anchor<9?item->anchor:0;
                    const auto& parent=*item->parent;
                    m_widgetBaseX=item->anchor>=0?item->offsetX:item->x-parent.pad(0);
                    m_widgetBaseY=item->anchor>=0?item->offsetY:item->y-parent.pad(1);
                    m_widgetDragX=pointerX;m_widgetDragY=pointerY;m_widgetPreviewDx=m_widgetPreviewDy=0;
                    m_widgetDragRevision=m_document.revision();m_widgetPreviewDrag=true;
                    m_status="Drag to position; release to apply. Escape = Cancel.";
                }
                else m_status="Selected widget. Use a Stack container for free positioning.";
            }
            else m_widgetSelection.clear();
            return;
        }
        if(in(x,y,510,156,610,189)){m_widgetPreviewState=m_widgetPreviewState>=5?-1:m_widgetPreviewState+1;return;}
        if(in(x,y,280,156,500,189)){m_widgetPalette=!m_widgetPalette;m_widgetFirst=0;return;}
        if(in(x,y,630,156,810,189)){m_widgetDesigner=false;releaseWidgetPreview();return;}
        if(in(x,y,820,156,1000,189)){m_targetResolutionMenu=true;return;}
        if(in(x,y,280,202,610,522))
        {
            const auto row=m_widgetFirst+static_cast<size_t>((y-202)/40);
            if(row>=count)return;
            if(m_widgetPalette)
            {
                if(std::string(designPalette[row])=="component")
                {m_visualProperty="Reusable widget ID";m_visualText.clear();m_visualInput=8;s.input->setTyping(true);m_status="Enter the reusable widget asset ID from the library.";return;}
                if(m_document.appendVisualWidget(m_visualSelection,designPalette[row],"",m_status,m_widgetSelection))
                {m_status="Added widget to selected container.";m_widgetPalette=false;m_widgetFirst=0;}
            }
            else {m_widgetSelection=row?rows[row-1].id:"";if(!m_widgetSelection.empty()){m_widgetDragging=true;m_widgetDragId=m_widgetSelection;m_widgetDragX=x;m_widgetDragY=y;}}
            return;
        }
        if(m_widgetAnchors&&in(x,y,280,548,610,581))
        {
            const size_t at=static_cast<size_t>((x-280)/110);const char* modes[]={"stretch-x","stretch-y","stretch-both"};
            if(at<3&&!m_widgetSelection.empty()&&m_document.setVisualWidgetProperty(m_visualSelection,m_widgetSelection,"anchor",modes[at],m_status))m_status="Stretch anchor updated.";
            return;
        }
        if(!m_widgetSelection.empty()&&in(x,y,280,548,440,581))
        {if(m_document.moveVisualWidget(m_visualSelection,m_widgetSelection,-1,m_status))m_status="Moved widget up.";return;}
        if(!m_widgetSelection.empty()&&in(x,y,450,548,610,581))
        {if(m_document.moveVisualWidget(m_visualSelection,m_widgetSelection,1,m_status))m_status="Moved widget down.";return;}
        if(in(x,y,500,524,610,545)){m_widgetTimeline=!m_widgetTimeline;m_widgetTimelinePlay=false;m_widgetTimelineTime=0;m_widgetAnchors=m_widgetEvents=false;return;}
        if(in(x,y,280,524,500,545)){m_widgetAnchors=!m_widgetAnchors;m_widgetEvents=m_widgetTimeline=m_widgetTimelinePlay=false;return;}
        if(in(x,y,630,420,1000,438)){if(m_widgetTimeline||m_widgetAnchors){m_widgetTimeline=m_widgetTimelinePlay=m_widgetAnchors=false;return;}m_widgetEvents=x>=877;m_widgetAdvanced=x>=753&&x<877;m_widgetUnbind=false;return;}
        if(m_widgetTimeline)
        {
            if(m_widgetSelection.empty())return;
            if(in(x,y,630,439,810,472)){m_widgetTimelineTrack=(m_widgetTimelineTrack+1)%5;return;}
            if(in(x,y,820,439,1000,472))
            {m_visualProperty="duration";m_visualText=std::to_string(duration);m_visualInput=9;m_widgetTimelinePlay=false;s.input->setTyping(true);return;}
            if(in(x,y,630,475,1000,510))
            {m_widgetTimelineTime=std::min(duration,std::round(duration*std::clamp((x-640)/350.f,0.f,1.f)*100)/100);m_widgetTimelinePlay=false;return;}
            if(in(x,y,630,512,745,545))
            {m_document.setWidgetKey(m_visualSelection,m_widgetSelection,"loop",animation["loop"].flag()?"false":"true",0,m_status);return;}
            if(in(x,y,752,512,870,545))
            {if(m_widgetTimelineTime>=duration)m_widgetTimelineTime=0;m_widgetTimelinePlay=!m_widgetTimelinePlay;return;}
            if(in(x,y,877,512,1000,545))
            {
                m_visualProperty=timelineTracks[m_widgetTimelineTrack];m_visualText=m_widgetTimelineTrack==2||m_widgetTimelineTrack==3?"1":"0";
                for(auto key=animation[m_visualProperty.c_str()].first();key.exists();key=key.next())
                    if(std::abs(key[0u].number()-m_widgetTimelineTime)<0.00001f)m_visualText=std::to_string(key[1u].number());
                m_visualInput=9;m_widgetTimelinePlay=false;s.input->setTyping(true);m_status="Enter value at the selected time.";return;
            }
            if(in(x,y,630,548,810,581))
            {m_widgetTimelinePlay=false;m_document.setWidgetKey(m_visualSelection,m_widgetSelection,timelineTracks[m_widgetTimelineTrack],"",m_widgetTimelineTime,m_status);return;}
            if(in(x,y,820,548,1000,581)){m_widgetTimelineTime=0;m_widgetTimelinePlay=false;return;}
            return;
        }
        if(m_widgetAnchors)
        {
            if(m_widgetSelection.empty())return;
            const char* const anchors[]={"top-left","top-center","top-right","middle-left","center","middle-right","bottom-left","bottom-center","bottom-right"};
            if(in(x,y,630,439,1000,547))
            {
                const auto index=static_cast<size_t>((y-439)/36)*3+static_cast<size_t>((x-630)/123.34f);
                if(index<9&&m_document.setVisualWidgetProperty(m_visualSelection,m_widgetSelection,"anchor",anchors[index],m_status))m_status="Anchor updated.";
                return;
            }
            if(in(x,y,630,548,810,581))
            {
                if(m_document.setVisualWidgetProperty(m_visualSelection,m_widgetSelection,"anchor","",m_status))m_status="Anchor cleared.";
                return;
            }
            if(in(x,y,820,548,1000,581))
            {
                m_visualProperty="offset";m_visualText="";m_visualInput=4;s.input->setTyping(true);
                m_status="Enter X,Y offset; stretch axes use symmetric insets.";return;
            }
            return;
        }
        if(m_widgetEvents)
        {
            if(in(x,y,630,520,1000,553)){m_widgetUnbind=!m_widgetUnbind;return;}
            if(m_widgetSelection.empty()||!in(x,y,630,439,1000,511))return;
            const size_t index=static_cast<size_t>((y-439)/36)*3+static_cast<size_t>((x-630)/123.34f);
            if(index>=sizeof(designEvents)/sizeof(designEvents[0]))return;
            if(m_widgetUnbind)
            {
                if(m_document.clearVisualWidgetEvent(m_visualSelection,m_widgetSelection,designEvents[index],m_status))m_status="Event unbound; function graph preserved.";
            }
            else
            {
                size_t target=0;
                if(m_document.openVisualWidgetEvent(assetScreen(),m_visualSelection,m_widgetSelection,designEvents[index],target,m_status))
                {
                    m_widgetDesigner=false;m_assetBrowser=false;releaseWidgetPreview();selectGraph(target);
                    m_status="Widget callback graph opened. Connect actions to its entry.";
                }
            }
            return;
        }
        if(!m_widgetSelection.empty()&&in(x,y,630,439,1000,m_widgetAdvanced?619.f:583.f))
        {
            const size_t column=static_cast<size_t>((x-630)/123.34f),row=static_cast<size_t>((y-439)/36);
            const size_t prop=row*3+column;if(prop>=(m_widgetAdvanced?sizeof(advancedDesignProps)/sizeof(advancedDesignProps[0]):sizeof(designProps)/sizeof(designProps[0])))return;
            if(!m_widgetAdvanced&&prop==0&&std::any_of(rows.begin(),rows.end(),[&](const DesignRow& row){return row.id==m_widgetSelection&&row.kind=="component";}))
            {if(m_document.removeVisualWidgetComponent(m_visualSelection,m_widgetSelection,m_status)){m_widgetSelection.clear();m_status="Unlinked component; original definition preserved.";}return;}
            m_visualProperty=m_widgetAdvanced?advancedDesignProps[prop]:designProps[prop];
            JsonTree::Value selected;
            const auto find=[&](auto&& self,JsonTree::Value node)->void
            {
                if(node.isObject()&&node["id"].isString()&&m_widgetSelection==node["id"].text())selected=node;
                if(node.isArray()||node.isObject())for(auto child=node.first();child.exists();child=child.next())self(self,child);
            };
            find(find,tree.root());if(!selected.exists())return;
            const auto field=selected[m_visualProperty.c_str()];
            m_visualText=field.isString()?field.text():field.isNumber()?std::to_string(field.exact()):
                field.type()==JsonTree::Type::Bool?(field.flag()?"true":"false"):"";
            if((m_visualProperty=="padding"||m_visualProperty=="gridSlot")&&field.isArray())
                for(auto part=field.first();part.exists();part=part.next())
                {if(!m_visualText.empty())m_visualText+=",";m_visualText+=std::to_string(part.exact());}
            if(m_visualProperty=="text"&&!field.exists())
                for(const char* key:{"label","paragraph","button","toggle","field"})if(selected[key].isString()){m_visualText=selected[key].text();break;}
            m_visualInput=4;s.input->setTyping(true);m_status=m_visualProperty=="padding"?"Padding: one number, X,Y, or left,top,right,bottom.":m_visualProperty=="grow"?"Slot size: 0 = Auto; positive = relative Fill weight.":m_visualProperty=="gridSlot"?"Grid: row,column,rowSpan,columnSpan (zero-based; blank = Auto).":"Edit property; blank uses default (except text).";
        }
    }
    void BlueprintWorkspace::drawWidgetDesigner(Canvas& c)
    {
        const auto button=[&](float x,float y,float width,const char* label)
        {c.roundRect(x,y,width,33,4,{0.19f,0.33f,0.50f,1});c.clip(x,y,width,33);c.text(*m_font,label,x+width*0.5f,y+7,text,Align::Center);c.noClip();};
        const auto layout=widgetLayout();
        if(layout.wide)
        {
            c.rect(280+layout.leftDx-6,156+layout.leftDy-6,342,kBottom-112,{0.17f,0.17f,0.18f,1});
            c.rect(630+layout.rightDx-6,156+layout.rightDy-6,382,kBottom-112,{0.17f,0.17f,0.18f,1});
        }
        c.push();c.translate(layout.leftDx,layout.leftDy);
        button(280,156,220,m_widgetPalette?"Back to hierarchy":"Add widget");
        button(510,156,100,m_widgetPreviewState<0?"Live style":themeStates[m_widgetPreviewState]);
        const auto* page=m_document.visualAsset(m_visualSelection);
        JsonTree tree;std::vector<DesignRow> rows;
        if(page&&tree.parse(page->source.data(),page->source.size()))designRows(tree.root(),rows,0);
        const size_t count=m_widgetPalette?sizeof(designPalette)/sizeof(designPalette[0]):rows.size()+1;
        for(size_t i=0;i<8&&i+m_widgetFirst<count;++i)
        {
            const size_t at=i+m_widgetFirst;const float y=202+40*static_cast<float>(i);
            const auto id=m_widgetPalette||!at?"":rows[at-1].id;
            const auto label=m_widgetPalette?std::string(designPalette[at]):!at?"PAGE ROOT":std::string(rows[at-1].depth*2,' ')+rows[at-1].kind+" / "+id+" "+rows[at-1].label;
            c.rect(280,y,330,36,!m_widgetPalette&&id==m_widgetSelection?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});
            c.clip(286,y,318,36);c.text(*m_font,label.c_str(),290,y+9,text);c.noClip();
        }
        if(m_widgetAnchors){button(280,548,106,"Fill X");button(390,548,106,"Fill Y");button(500,548,110,"Fill both");}
        else {button(280,548,160,"Move up");button(450,548,160,"Move down");}
        c.rect(280,524,220,21,{0.19f,0.33f,0.50f,1});c.text(*m_font,m_widgetAnchors?"Back to properties":"Anchors / offset",390,525,text,Align::Center);
        c.rect(502,524,108,21,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Timeline",556,525,text,Align::Center);
        c.pop();
        c.push();c.translate(layout.rightDx,layout.rightDy);
        button(630,156,180,"Back to content");button(820,156,180,(std::to_string(m_document.target().width())+" x "+std::to_string(m_document.target().height())+" v").c_str());
        c.pop();
        const auto& target=m_document.target();
        const float width=static_cast<float>(target.width()),height=static_cast<float>(target.height()),scale=layout.previewScale,screenWidth=layout.previewWidth;
        const auto resolutionMenu=[&](){
            if(!m_targetResolutionMenu)return;
            c.push();c.translate(layout.rightDx,layout.rightDy);
            for(unsigned i=0;i<BlueprintTarget::resolutionCount;++i)
            {
                auto option=target;option.resolution=i;
                button(630,194+36.f*i,370,(std::string(option.resolutionName())+(option.highResolutionWarning()?" / poor performance risk":"")).c_str());
            }
            if(target.resolution==3)
                for(unsigned i=0;i<3;++i)
                {
                    auto option=target;option.resolution=i;
                    const std::string label=std::string(m_widgetGuideMask&(1u<<i)?"[x] Guide: ":"[ ] Guide: ")+option.resolutionName();
                    button(630,194+36.f*(BlueprintTarget::resolutionCount+i),370,label.c_str());
                }
            c.pop();
        };
        const float px=layout.previewX,py=layout.previewY;
        if(layout.wide)
        {
            c.rect(354-modalX(),112-modalY(),kWidth-748,kBottom-160,{0.065f,0.065f,0.07f,1});
            c.text(*m_font,(std::string("DESIGN PREVIEW / ")+target.resolutionName()).c_str(),366-modalX(),92-modalY(),muted);
            c.rect(px-1,py-1,screenWidth+2,layout.previewHeight+2,{0.34f,0.36f,0.39f,1});
        }
        c.rect(layout.wide?px:630,layout.wide?py:202,layout.wide?screenWidth:370,layout.wide?layout.previewHeight:219,{0.055f,0.075f,0.105f,1});
        if(m_designUi&&m_designPreviewError.empty())
        {
            c.push();c.clip(px,py,screenWidth,layout.previewHeight);c.translate(px,py);c.scale(layout.previewScaleX,scale);
            m_designUi->drawDesign(c,0,0,width,height,m_widgetSelection.c_str(),m_widgetPreviewState,m_widgetTimeline?m_widgetTimelineTime:0);
            if(!m_widgetSelection.empty())if(auto* selected=m_designUi->find(m_widgetSelection.c_str()))
            {
                float sx=0,sy=0;m_designUi->place(*selected,sx,sy);
                c.roundLine(sx,sy,selected->w,selected->h,0,3,{0.25f,0.85f,0.55f,1});
                if(m_widgetPreviewDrag)
                {
                    const float gx=sx+m_widgetPreviewDx,gy=sy+m_widgetPreviewDy;
                    c.rect(gx,gy,selected->w,selected->h,{0.25f,0.85f,0.55f,0.12f});
                    c.roundLine(gx,gy,selected->w,selected->h,0,4,{0.35f,0.95f,0.65f,1});
                }
            }
            c.pop();
        }
        else
        {
            c.clip(px+6,py,screenWidth-12,layout.previewHeight);c.text(*m_font,m_designPreviewError.empty()?"Native preview":m_designPreviewError.c_str(),px+10,py+18,muted);c.noClip();
        }
        if(target.resolution==3)
        {
            const Color colors[]={{0.3f,0.9f,0.5f,1},{1.f,0.72f,0.2f,1},{0.35f,0.75f,1.f,1}};
            const char* labels[]={"640x480","720x480 wide","1280x720"};
            c.push();c.clip(px,py,screenWidth,layout.previewHeight);
            for(unsigned i=0;i<3;++i)if(m_widgetGuideMask&(1u<<i))
            {
                const auto g=uiDesignGuide(px+screenWidth*0.5f,py+layout.previewHeight*0.5f,scale,i);
                c.rect(g.x,g.y,g.w,1,colors[i]);c.rect(g.x,g.y+g.h-1,g.w,1,colors[i]);
                c.rect(g.x,g.y,1,g.h,colors[i]);c.rect(g.x+g.w-1,g.y,1,g.h,colors[i]);
                c.text(*m_font,labels[i],g.x+3,i==1?g.y+g.h-18:g.y+2,colors[i]);
            }
            c.pop();
        }
        c.push();c.translate(layout.rightDx,layout.propsDy);
        if(m_widgetTimeline||m_widgetAnchors)
        {
            c.rect(630,420,370,18,{0.19f,0.33f,0.50f,1});
            c.text(*m_font,m_widgetTimeline?"TIMELINE / click to exit":"ANCHORS / Stack children / click to exit",630,420,text);
        }
        else
        {
            const char* names[]={"Properties","Advanced","Events"};
            for(unsigned i=0;i<3;++i)
            {
                const bool selected=i==0?!m_widgetAdvanced&&!m_widgetEvents:i==1?m_widgetAdvanced&&!m_widgetEvents:m_widgetEvents;
                const float x=630+123.34f*i;
                c.rect(x,420,121,18,selected?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});
                c.text(*m_font,names[i],x+60,420,text,Align::Center);
            }
        }
        if(m_widgetTimeline)
        {
            const auto a=designItem(tree.root(),m_widgetSelection)["animation"];
            const float duration=a["duration"].exists()?std::clamp(a["duration"].number(),0.01f,60.f):2.f;
            char label[64];snprintf(label,sizeof(label),"Duration %.2fs",duration);
            button(630,439,180,timelineTracks[m_widgetTimelineTrack]);button(820,439,180,label);
            c.rect(640,486,350,4,{0.35f,0.43f,0.55f,1});
            for(auto k=a[timelineTracks[m_widgetTimelineTrack]].first();k.exists();k=k.next())
                c.rect(638+350*std::clamp(k[0u].number()/duration,0.f,1.f),480,5,15,{0.95f,0.7f,0.2f,1});
            const float cursor=640+350*std::clamp(m_widgetTimelineTime/duration,0.f,1.f);
            c.rect(cursor-1,476,2,24,{0.4f,0.9f,0.7f,1});
            snprintf(label,sizeof(label),"%.2fs / %.2fs",m_widgetTimelineTime,duration);c.text(*m_font,label,815,493,text,Align::Center);
            button(630,512,115,a["loop"].flag()?"Loop on":"Loop off");button(752,512,118,m_widgetTimelinePlay?"Pause":"Play");button(877,512,123,"Set key");
            button(630,548,180,"Delete key");button(820,548,180,"Rewind");
            c.pop();resolutionMenu();return;
        }
        if(m_widgetAnchors)
        {
            const char* const labels[]={"Top left","Top center","Top right","Middle left","Center","Middle right","Bottom left","Bottom center","Bottom right"};
            for(size_t i=0;i<9;++i)button(630+123.34f*static_cast<float>(i%3),439+36*static_cast<float>(i/3),119,labels[i]);
            button(630,548,180,"Clear anchor");button(820,548,180,"Set XY offset");
        }
        else if(m_widgetEvents)
        {
            for(size_t i=0;i<sizeof(designEvents)/sizeof(designEvents[0]);++i)
                button(630+123.34f*static_cast<float>(i%3),439+36*static_cast<float>(i/3),119,designEvents[i]);
            button(630,520,370,m_widgetUnbind?"Mode: unbind event":"Mode: open event graph");
            c.text(*m_font,"Link page to screen before opening events.",630,558,muted);
        }
        else for(size_t i=0;i<(m_widgetAdvanced?sizeof(advancedDesignProps)/sizeof(advancedDesignProps[0]):sizeof(designProps)/sizeof(designProps[0]));++i)
            button(630+123.34f*static_cast<float>(i%3),439+36*static_cast<float>(i/3),119,!m_widgetAdvanced&&i==0&&std::any_of(rows.begin(),rows.end(),[&](const DesignRow& row){return row.id==m_widgetSelection&&row.kind=="component";})?"Unlink":m_widgetAdvanced?advancedDesignProps[i]:designProps[i]);
        c.pop();resolutionMenu();
    }
    void BlueprintWorkspace::updateVisualLibrary(Services& s,float x,float y,bool left,bool click,unsigned pressed)
    {
        auto& input=*s.input;
        if(m_visualInput)
        {
            const bool colorPicker=m_visualInput==7&&themeColorProperty(m_visualProperty),mediaPicker=m_visualInput==7&&themeMediaProperty(m_visualProperty);
            if(colorPicker){unsigned colour=0;if(!m_rgbOpen){const auto old=themePickerColor(m_visualText);startRGB(themePickerHex(old).substr(0,6),true,old.a);}updateRGB(input,x,y,left,click,colour);if(!m_rgbOpen){if(m_rgbApplied){const auto value=colour?"#"+themePickerHex(rgbColour(colour,{1,1,1,1})).substr(0,6)+themePickerHex({0,0,0,m_rgbAlpha}).substr(6):std::string{};if(!m_document.setVisualThemeProperty(m_visualSelection,m_themeStyle,themeStates[m_themeState],m_visualProperty,value,m_status)){m_rgbOpen=true;return;}}m_visualInput=0;input.setTyping(false);}return;}
            const float dialogTop=colorPicker?250.f:mediaPicker?265.f:286.f,footer=colorPicker?445.f:mediaPicker?505.f:379.f;
            const bool applyClick=click&&in(x,y,650,footer,745,footer+32);
            if(click&&(!in(x,y,420,dialogTop,860,footer+37)||in(x,y,750,footer,840,footer+32)))
            {m_visualInput=0;m_visualText.clear();input.setTyping(false);return;}
            if(m_visualInput==7&&themeColorProperty(m_visualProperty)&&click&&in(x,y,500,320,820,432))
            {
                Color color=themePickerColor(m_visualText);const int channel=static_cast<int>((y-320)/28);
                const float value=std::clamp((x-500)/320.f,0.f,1.f);
                if(channel==0)color.r=value;else if(channel==1)color.g=value;else if(channel==2)color.b=value;else if(channel==3)color.a=value;
                m_visualText=themePickerHex(color);return;
            }
            if(m_visualInput==7&&themeMediaProperty(m_visualProperty))
            {
                if(input.wheel()<0&&m_themeMediaFirst+5<m_themeMediaFiles.size())++m_themeMediaFirst;
                if(input.wheel()>0&&m_themeMediaFirst)--m_themeMediaFirst;
                if(click&&in(x,y,440,345,840,495))
                {
                    const size_t at=m_themeMediaFirst+static_cast<size_t>((y-345)/30);
                    if(at<m_themeMediaFiles.size())m_visualText="asset:"+m_themeMediaFiles[at];
                    return;
                }
            }
            for(u32 i=0;i<input.typedCount()+(applyClick?1u:0u);++i)
            {
                const auto ch=i<input.typedCount()?input.typed(i):13u;
                if(ch==8&&!m_visualText.empty())
                {
                    size_t end=m_visualText.size()-1;
                    while(end&&(static_cast<unsigned char>(m_visualText[end])&0xc0)==0x80)--end;
                    m_visualText.resize(end);
                }
                else if(ch==13)
                {
                    if(m_visualInput==1&&!m_document.renameVisualAsset(m_visualSelection,m_visualText,m_status))break;
                    if(m_visualInput==3&&!m_document.setVisualActorProperty(m_visualSelection,m_visualComponent,m_visualProperty,m_visualText,m_status))break;
                    if(m_visualInput==4&&!m_document.setVisualWidgetProperty(m_visualSelection,m_widgetSelection,m_visualProperty,m_visualText,m_status))break;
                    if(m_visualInput==5&&!m_document.setVisualTheme(m_visualSelection,m_visualText,m_status))break;
                    if(m_visualInput==6)
                    {
                        if(m_visualText.empty()||m_visualText.size()>31||m_visualText.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos||
                            (m_visualText.front()>='0'&&m_visualText.front()<='9')){m_status="Enter a valid style identifier.";break;}
                        m_themeStyle=m_visualText;
                    }
                    if(m_visualInput==7){const auto nativeValue=themeColorProperty(m_visualProperty)&&!m_visualText.empty()&&m_visualText.front()!='#'?"#"+m_visualText:m_visualText;if(!m_document.setVisualThemeProperty(m_visualSelection,m_themeStyle,themeStates[m_themeState],m_visualProperty,nativeValue,m_status))break;}
                    if(m_visualInput==10&&!m_document.editVisualActorPlacement(m_placementSelectedOwner,m_placementSelectedId,m_visualProperty,m_visualText,m_status))break;
                    if(m_visualInput==11){m_outlinerQuery=m_visualText;m_placementFirst=0;}
                    if(m_visualInput==9&&!m_document.setWidgetKey(m_visualSelection,m_widgetSelection,m_visualProperty,m_visualText,m_widgetTimelineTime,m_status))break;
                    if(m_visualInput==8)
                    {
                        if(!m_document.appendVisualWidget(m_visualSelection,"component",m_visualText,m_status,m_widgetSelection))break;
                        m_widgetPalette=false;m_widgetFirst=0;
                    }
                    m_visualInput=0;input.setTyping(false);m_status="Text applied.";break;
                }
                else if(ch>=32)appendCharacter(m_visualText,ch,128);
            }
            return;
        }
        if(m_placementDragging){updateVisualPlacement(s,x,y,left,click);return;}
        if(m_widgetPreviewDrag){updateWidgetDesigner(s,x,y,left,click);return;}
        if(pressed&1u){saveCurrent(s);return;}
        if(pressed&2u){if(m_document.undo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Library edit undone.";}return;}
        if(pressed&4u){if(m_document.redo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Library edit redone.";}return;}
        if(m_widgetDesigner)
        {
            const auto layout=widgetLayout();
            const float hx=x-layout.headerDx,hy=y-layout.headerDy;
            if(m_widgetDragging||!(click&&(in(hx,hy,860,116,1000,150)||in(hx,hy,704,116,850,150))))
            {updateWidgetDesigner(s,x,y,left,click);return;}
            x=hx;y=hy;
        }
        if(m_visualPlacement)
        {
            const auto scene=sceneLayout();const auto layout=widgetLayout();
            const float hx=x-(scene.wide?layout.headerDx:0),hy=y-(scene.wide?layout.headerDy:0);
            if(click&&(scene.wide?in(x,y,420-modalX(),12-modalY(),630-modalX(),45-modalY()):in(x,y,630,156,1000,189)))
            {launchSceneEditor(s);return;}
            if(!(click&&(in(hx,hy,860,116,1000,150)||in(hx,hy,704,116,850,150))))
            {updateVisualPlacement(s,x,y,left,click);return;}
            x=hx;y=hy;
        }
        const auto& assets=m_document.visualAssets();
        if(m_visualFirst>=assets.size())m_visualFirst=assets.empty()?0:assets.size()-1;
        if(!m_visualPlacement&&in(x,y,280,198,610,535))
        {
            if(input.wheel()<0&&m_visualFirst+9<assets.size())++m_visualFirst;
            if(input.wheel()>0&&m_visualFirst)--m_visualFirst;
        }
        if(m_visualPlacement&&!(click&&(in(x,y,860,116,1000,150)||in(x,y,704,116,850,150)))){updateVisualPlacement(s,x,y,left,click);return;}
        if(const auto* selected=m_document.visualAsset(m_visualSelection);selected&&selected->kind=="actor")
        {
            JsonTree tree;std::string resolved,why;
            if(m_document.visualActorSource(selected->id,resolved,why)&&tree.parse(resolved.data(),resolved.size()))
            {
                const auto count=tree.root()["entities"].count();
                if(m_visualComponent>=count)m_visualComponent=0;
                if(m_visualComponentFirst>=count)m_visualComponentFirst=0;
                if(in(x,y,630,366,1000,498))
                {
                    if(input.wheel()<0&&m_visualComponentFirst+4<count)++m_visualComponentFirst;
                    if(input.wheel()>0&&m_visualComponentFirst)--m_visualComponentFirst;
                }
            }
        }
        if(!click)return;
        if(in(x,y,860,116,1000,150)){m_assetBrowser=false;m_widgetDesigner=false;releaseWidgetPreview();m_visualPlacement=false;cancelVisualPlacement();return;}
        if(in(x,y,704,116,850,150)){m_assetLibrary=false;m_widgetDesigner=false;releaseWidgetPreview();m_visualPlacement=false;cancelVisualPlacement();return;}
        if(in(x,y,280,156,439,189)||in(x,y,449,156,610,189)||in(x,y,566,116,695,150))
        {
            std::string id;
            if(m_document.createVisualAsset(y<150?"theme":x<440?"actor":"widget",id,m_status))
            {
                m_visualSelection=id;m_visualComponent=m_visualComponentFirst=0;m_visualInspector=false;m_visualPhysics=false;m_visualText.clear();m_visualFirst=m_document.visualAssets().size()>9?m_document.visualAssets().size()-9:0;
                m_status="Created reusable definition. Ctrl+S saves the project.";
            }
            return;
        }
        if(in(x,y,280,198,610,522))
        {
            const size_t row=m_visualFirst+static_cast<size_t>((y-198)/36);
            if(row<assets.size()){m_visualSelection=assets[row].id;m_visualComponent=m_visualComponentFirst=0;m_visualInspector=false;m_visualPhysics=false;m_visualText.clear();m_status="Selected "+assets[row].title;}
            return;
        }
        const auto* selected=m_document.visualAsset(m_visualSelection);
        if(!selected)return;
        if(in(x,y,630,156,805,189))
        {m_visualText=selected->title;m_visualInput=1;input.setTyping(true);m_status="Edit name, then Enter.";return;}
        if(in(x,y,815,156,1000,189))
        {
            if(m_document.removeVisualAsset(m_visualSelection,m_status))
            {m_visualSelection.clear();m_visualText.clear();m_status="Removed definition. Ctrl+Z restores it.";}
            return;
        }
        if(selected->kind=="sequence"){if(in(x,y,630,232,1000,266)){const auto id=selected->id;m_assetBrowser=false;beginSequenceAsset(id,input);}return;}
        if(selected->kind=="theme")
        {
            if(in(x,y,630,232,810,266))
            {m_visualInput=6;m_visualText=m_themeStyle;input.setTyping(true);m_status="Enter style name (e.g. button or custom).";return;}
            if(in(x,y,820,232,1000,266)){m_themeState=(m_themeState+1)%6;m_themeMedia=false;return;}
            if(in(x,y,630,548,1000,581)){m_themeMedia=!m_themeMedia;m_themeState=0;return;}
            if(in(x,y,630,277,1000,493))
            {
                const size_t index=static_cast<size_t>((y-277)/36)*3+static_cast<size_t>((x-630)/123.34f);
                const size_t count=m_themeMedia?sizeof(themeMediaProps)/sizeof(themeMediaProps[0]):m_themeState?themeLookCount:sizeof(themeProps)/sizeof(themeProps[0]);
                if(index>=count)return;
                m_visualProperty=m_themeMedia?themeMediaProps[index]:themeProps[index];
                JsonTree tree;if(!tree.parse(selected->source.data(),selected->source.size()))return;
                const auto style=tree.root()["styles"][m_themeStyle.c_str()];
                const auto state=m_themeState?style[themeStates[m_themeState]]:style;
                const auto property=m_visualProperty.rfind("sound",0)==0?
                    tree.root()["sounds"][m_visualProperty=="soundMove"?"move":m_visualProperty=="soundAccept"?"accept":m_visualProperty=="soundBack"?"back":"change"]:state[m_visualProperty.c_str()];
                m_visualText=property.isNumber()?std::to_string(property.exact()):property.text();
                if(themeColorProperty(m_visualProperty)&&!m_visualText.empty()&&m_visualText.front()=='#')m_visualText.erase(0,1);
                if(themeMediaProperty(m_visualProperty))
                {
                    m_themeMediaFiles.clear();m_themeMediaFirst=0;std::vector<std::string> available;std::string error;
                    if(blueprintListAssets(*s.platform,m_document.assetRoot,assetScreen(),available,error))
                        for(const auto& file:available)if(themeMediaMatch(m_visualProperty,file))m_themeMediaFiles.push_back(file);
                }
                m_visualInput=7;input.setTyping(true);m_status=m_visualProperty=="align"?"Enter left, center or right; blank restores default.":"Edit style value; blank restores default.";return;
            }
            return;
        }
        if(selected->kind=="actor")
        {
            if(in(x,y,815,193,1000,226))
            {
                std::string child;
                if(m_document.createVisualActorChild(m_visualSelection,child,m_status))
                {m_visualSelection=child;m_visualComponent=m_visualComponentFirst=0;m_visualInspector=false;m_visualPhysics=false;m_visualFirst=m_document.visualAssets().size()>9?m_document.visualAssets().size()-9:0;m_status="Created linked child class.";}
                return;
            }
            if(!selected->dependencies.empty()&&m_visualInspector&&!m_visualPhysics&&in(x,y,630,400,1000,430))
            {if(m_document.resetVisualActorOverrides(m_visualSelection,m_status,m_visualComponent))m_status="Selected component overrides cleared.";return;}
            if(in(x,y,280,584,610,617)){openActorViewport(m_visualSelection);return;}
            if(in(x,y,280,548,610,581)){m_visualPlacement=true;m_placementOutliner=false;m_placementSelectedId.clear();m_placementSelectedOwner=assetScreen();m_placementFirst=0;cancelVisualPlacement();m_status="Drag actors into placement or open the World outliner.";return;}
            if(in(x,y,630,232,1000,266)){m_visualInspector=!m_visualInspector;m_visualPhysics=false;return;}
            if(!m_visualInspector&&in(x,y,630,548,810,581))
            {
                std::string owner;
                if(m_document.ensureActorBehaviour(m_visualSelection,owner,m_status))
                {
                    for(size_t i=0;i<m_document.graphs.size();++i)if(m_document.graphs[i].owner==owner&&m_document.graphs[i].name=="start")
                    {m_assetBrowser=false;selectGraph(i);m_status="Class graphs: declare variables, add Construction or override inherited functions by name.";break;}
                }
                return;
            }
            if(m_visualInspector)
            {
                if(in(x,y,630,m_visualPhysics?548.f:558.f,1000,m_visualPhysics?581.f:591.f)){m_visualPhysics=!m_visualPhysics;return;}
                const char* prop=nullptr;
                if(m_visualPhysics)
                {
                    if(in(x,y,630,277,810,310))prop="collision";
                    else if(in(x,y,820,277,1000,310))prop="mass";
                    else if(in(x,y,630,318,810,351))prop="friction";
                    else if(in(x,y,820,318,1000,351))prop="bounce";
                    else if(in(x,y,630,359,810,392))prop="gravity";
                    else if(in(x,y,820,359,1000,392))prop="animate";
                }
                else if(in(x,y,630,277,810,310))prop="name";
                else if(in(x,y,820,277,1000,310))prop="place";
                else if(in(x,y,630,318,810,351))prop="angles";
                else if(in(x,y,820,318,1000,351))prop="size";
                else if(in(x,y,630,359,810,392))prop="model";
                else if(in(x,y,820,359,1000,392))prop="behaviour";
                else if(in(x,y,630,520,1000,553))prop="camera";
                if(!prop)return;
                JsonTree tree;std::string resolved;if(!m_document.visualActorSource(selected->id,resolved,m_status)||!tree.parse(resolved.data(),resolved.size()))return;
                const auto item=tree.root()["entities"][static_cast<u32>(m_visualComponent)];
                if(!item.exists()){m_status="Select a component.";return;}
                const auto v=item[prop];m_visualProperty=prop;
                if(m_visualProperty=="place"||m_visualProperty=="angles"||m_visualProperty=="size")
                {
                    const float fallback=m_visualProperty=="size"?1.f:0.f;
                    m_visualText="";
                    for(u32 axis=0;axis<3;++axis)
                    {
                        const float n=v.isArray()?v[axis].number(fallback):v.isNumber()?(m_visualProperty=="angles"?(axis==1?v.number():0.f):v.number()):fallback;
                        if(axis)m_visualText+=",";
                        m_visualText+=std::to_string(n);
                    }
                }
                else
                {
                    if(m_visualProperty=="collision")
                    {
                        m_visualText="none";
                        for(const char* key:{"cube","sphere","capsule"})
                        {
                            const auto cv=item[key];if(!cv.exists())continue;
                            m_visualText=std::string(std::string(key)=="cube"?"box":key)+":";
                            const u32 count=std::string(key)=="cube"?3:std::string(key)=="sphere"?1:2;
                            for(u32 i=0;i<count;++i){if(i)m_visualText+=",";m_visualText+=std::to_string(cv.isArray()?cv[i].number(1):cv.number(1));}
                            break;
                        }
                    }
                    else m_visualText=v.isNumber()?std::to_string(v.exact()):v.text();
                    if(m_visualProperty=="behaviour"&&m_visualText.rfind("BP_Behaviour_",0)==0)m_visualText.erase(0,13);
                }
                m_visualInput=3;input.setTyping(true);m_status="Edit property; Enter = Apply, Escape = Cancel.";return;
            }
            const char* shape=nullptr;
            if(in(x,y,630,277,810,310))shape="box";
            else if(in(x,y,820,277,1000,310))shape="sphere";
            else if(in(x,y,630,318,810,351))shape="capsule";
            else if(in(x,y,820,318,1000,351))shape="plane";
            if(shape)
            {
                if(m_document.setVisualActorShape(m_visualSelection,shape,m_status,m_visualComponent))m_status="Actor shape updated; all linked spawn nodes use the definition.";
            }
            else if(in(x,y,630,366,1000,498))
            {
                JsonTree tree;std::string resolved;
                if(m_document.visualActorSource(selected->id,resolved,m_status)&&tree.parse(resolved.data(),resolved.size()))
                {
                    const auto index=m_visualComponentFirst+static_cast<size_t>((y-366)/33);
                    if(index<tree.root()["entities"].count())m_visualComponent=index;
                }
            }
            else if(in(x,y,630,505,1000,538))
            {
                const bool camera=x>=820;
                if(m_document.appendVisualActorComponent(m_visualSelection,m_visualComponent,camera?"camera":"box",camera?"0,2,-5":"0,1.5,0",m_status))
                    m_status=camera?"Added camera child; it follows its parent when spawned.":"Added box child under selected component.";
            }
            else if(in(x,y,630,548,1000,581))
            {
                if(m_document.placeVisualActor(assetScreen(),m_visualSelection,"0,0,0",m_status))
                {
                    rebuildPins();filterCatalogue();m_diagnostic={};
                    m_status="Linked actor added to On Enter. Set its position there; ensure Draw renders Space.";
                }
            }
            return;
        }
        if(selected->kind!="widget")return;
        if(in(x,y,630,510,1000,543)){m_visualInput=5;m_visualText.clear();for(const auto& dependency:selected->dependencies)if(const auto* a=m_document.visualAsset(dependency);a&&a->kind=="theme")m_visualText=dependency;input.setTyping(true);m_status="Enter theme asset ID or blank for native defaults.";return;}
        if(in(x,y,280,548,610,581)){m_widgetDesigner=true;m_widgetPalette=false;m_widgetEvents=m_widgetUnbind=m_widgetDragging=m_widgetAnchors=m_widgetPreviewDrag=m_widgetTimeline=m_widgetTimelinePlay=false;m_widgetTimelineTime=0;m_widgetSelection.clear();m_widgetFirst=0;return;}
        if(in(x,y,630,232,1000,266))
        {m_visualInput=2;input.setTyping(true);m_status="Enter the next widget label, then press Enter.";return;}
        const char* kind=nullptr;
        if(in(x,y,630,277,810,310))kind="label";
        else if(in(x,y,820,277,1000,310))kind="button";
        else if(in(x,y,630,318,810,351))kind="progress";
        else if(in(x,y,820,318,1000,351))kind="toggle";
        if(kind)
        {
            if(m_document.appendVisualWidget(m_visualSelection,kind,m_visualText,m_status))
            {m_visualText.clear();m_status="Added native widget. Ctrl+Z restores the previous page.";}
        }
        else if(in(x,y,630,548,810,581))publishVisualWidget(s);
        else if(in(x,y,820,548,1000,581))
        {
            if(m_document.connectVisualWidget(assetScreen(),m_visualSelection,m_status))
            {
                rebuildPins();filterCatalogue();m_diagnostic={};
                m_status="Linked page to On Enter and Draw. Close Content and Play test.";
            }
        }
    }
    namespace
    {
        constexpr const char* actorProperties[]={"name","place","angles","size","model","behaviour","camera","collision","mass","friction","bounce","gravity","animate"};
        constexpr const char* actorPropertyLabels[]={"Name","Location","Rotation","Scale","Model","Behaviour","Camera FOV","Collision","Mass","Friction","Bounce","Gravity","Animation"};
        std::string actorVector(JsonTree::Value v,const std::string& property)
        {
            std::string result;const float fallback=property=="size"?1.f:0.f;
            for(u32 i=0;i<3;++i)
            {
                if(i)result+=",";
                result+=std::to_string(v.isArray()?v[i].number(fallback):v.isNumber()?(property=="angles"?(i==1?v.number():0.f):v.number()):fallback);
            }
            return result;
        }
    }
    bool BlueprintWorkspace::editActorProperty(Services& s,const char* property)
    {
        std::string resolved;JsonTree tree;
        if(!m_document.visualActorSource(m_visualSelection,resolved,m_status)||!tree.parse(resolved.data(),resolved.size()))return false;
        const auto item=tree.root()["entities"][static_cast<u32>(m_visualComponent)];
        if(!item.exists()){m_status="Select a component.";return false;}
        m_visualProperty=property;const auto v=item[property];m_visualText.clear();m_actorAxis=-1;m_actorAxisValues.clear();
        if(m_visualProperty=="place"||m_visualProperty=="angles"||m_visualProperty=="size")m_visualText=actorVector(v,m_visualProperty);
        else if(m_visualProperty=="collision")
        {
            m_visualText="none";
            for(const char* key:{"cube","sphere","capsule"})
            {
                const auto shape=item[key];if(!shape.exists())continue;
                m_visualText=std::string(std::string(key)=="cube"?"box":key)+":";
                const u32 count=std::string(key)=="cube"?3:std::string(key)=="sphere"?1:2;
                for(u32 i=0;i<count;++i){if(i)m_visualText+=",";m_visualText+=std::to_string(shape.isArray()?shape[i].number(1):shape.number(1));}
                break;
            }
        }
        else
        {
            m_visualText=v.isNumber()?std::to_string(v.exact()):v.text();
            if(m_visualProperty=="behaviour"&&m_visualText.rfind("BP_Behaviour_",0)==0)m_visualText.erase(0,13);
        }
        m_actorReplace=true;m_visualInput=3;s.input->setTyping(true);m_status="Edit property; Enter = Apply, Escape = Cancel.";return true;
    }
    unsigned BlueprintWorkspace::actorPreviewEntity() const
    {
        if(!m_placementWorld||!m_actorPreviewRoot)return 0;
        unsigned found=0;size_t index=0;
        m_placementWorld->eachEntity([&](u32 id,const World::Entity&){if(index++==m_visualComponent)found=id;});
        return found;
    }
    bool BlueprintWorkspace::actorHandle(unsigned axis,float& x,float& y,float& dx,float& dy) const
    {
        const auto id=actorPreviewEntity();if(!id||!m_placementReady||axis>=3)return false;
        auto& space=m_placementWorld->space();
        const auto p=space.worldPlace(id);Vec3 unit(axis==0?1.f:0.f,axis==1?1.f:0.f,axis==2?1.f:0.f);
        // Translation is authored in the parent's coordinate frame, including
        // its scale. Project the actual change from one local unit.
        if(const auto parent=m_placementWorld->parentOf(id);parent)
            if(const auto* spatial=space.getSpatial(parent))unit=spatial->world.transformDirection(unit);
        Vec2 a,b;if(!space.toScreen(p,a)||!space.toScreen(p+unit,b))return false;
        x=a.x;y=a.y;dx=b.x-a.x;dy=b.y-a.y;
        return std::isfinite(dx)&&std::isfinite(dy)&&dx*dx+dy*dy>0.25f;
    }
    void BlueprintWorkspace::cancelActorDrag()
    {
        if(m_actorDragAxis>=0){m_actorDragAxis=-1;releasePlacementPreview();}
    }
    void BlueprintWorkspace::saveActorTab()
    {
        if(!m_actorEditor)return;
        for(auto& tab:m_actorTabs)if(tab.id==m_actorTabId)
        {
            tab.component=m_visualComponent;tab.first=m_visualComponentFirst;tab.yaw=m_placementYaw;
            tab.pitch=m_actorPitch;tab.distance=m_placementDistance;tab.detailsScroll=m_actorDetailsScroll;tab.mode=m_actorMode;
            for(unsigned i=0;i<3;++i)tab.focus[i]=m_actorFocus[i];
            break;
        }
    }
    void BlueprintWorkspace::leaveActorViewport(bool library)
    {
        saveActorTab();cancelActorDrag();m_actorOrbit=false;m_actorEditor=false;m_assetBrowser=library;releasePlacementPreview();
        for(const auto& tab:m_actorTabs)if(tab.id==m_actorTabId)
        {
            const auto found=std::find_if(m_document.graphs.begin(),m_document.graphs.end(),[&](const BlueprintDocument::Graph& g){return g.owner==tab.returnGraph.first&&g.name==tab.returnGraph.second;});
            if(found!=m_document.graphs.end())selectGraph(static_cast<size_t>(found-m_document.graphs.begin()));
            return;
        }
    }
    void BlueprintWorkspace::openActorViewport(const std::string& id)
    {
        const auto* asset=m_document.visualAsset(id);
        if(!asset||asset->kind!="actor")return;
        const auto selected=id; // id can alias m_visualSelection.
        saveActorTab();cancelActorDrag();m_actorOrbit=false;pruneGraphTabs();
        auto found=std::find_if(m_actorTabs.begin(),m_actorTabs.end(),[&](const ActorTab& tab){return tab.id==selected;});
        if(found==m_actorTabs.end())
        {
            if(m_actorTabs.size()>=16)m_actorTabs.erase(m_actorTabs.begin());
            ActorTab tab;tab.id=selected;tab.returnGraph=m_activeTab;tab.screen=assetScreen();
            if(tab.screen.empty())tab.screen=m_document.moduleForOwner(m_document.actorBehaviour(selected));
            const auto screens=m_document.screens();
            if(std::find(screens.begin(),screens.end(),tab.screen)==screens.end())
                tab.screen=std::find(screens.begin(),screens.end(),m_actorTabScreen)!=screens.end()?m_actorTabScreen:(screens.empty()?std::string():screens.front());
            m_actorTabs.push_back(std::move(tab));found=m_actorTabs.end()-1;
        }
        m_actorEditor=true;m_assetBrowser=true;m_assetLibrary=true;m_visualSelection=selected;
        m_actorTabId=selected;m_actorTabScreen=found->screen;
        m_visualInspector=m_visualPhysics=m_visualPlacement=m_widgetDesigner=false;
        m_visualComponent=found->component;m_visualComponentFirst=found->first;m_visualInput=0;m_actorDetailsScroll=found->detailsScroll;
        m_placementYaw=found->yaw;m_actorPitch=found->pitch;m_placementDistance=found->distance;m_actorMode=found->mode;
        for(unsigned i=0;i<3;++i)m_actorFocus[i]=found->focus[i];
        clearWire();cancelDrag();releasePlacementPreview();revealTab(m_graphTabs.size()+static_cast<size_t>(found-m_actorTabs.begin()));
        m_status="Actor class viewport: select a component to edit its properties.";
    }
    std::vector<BlueprintWorkspace::DetailRow> BlueprintWorkspace::actorDetailRows() const
    {
        // Negative property IDs denote category headings.
        static constexpr int members[][6]={{0,-1,-1,-1,-1,-1},{1,2,3,-1,-1,-1},{4,12,-1,-1,-1,-1},{5,6,-1,-1,-1,-1},{7,8,9,10,11,-1}};
        std::vector<DetailRow> result;float y=112-m_actorDetailsScroll;
        for(unsigned group=0;group<5;++group)
        {
            result.push_back({-1-static_cast<int>(group),y});y+=24;
            if(m_actorCollapsed&(1u<<group))continue;
            for(const int property:members[group])if(property>=0){result.push_back({property,y});y+=36;}
        }
        return result;
    }
    bool BlueprintWorkspace::actorRing(unsigned axis,float angle,float& x,float& y,float& dx,float& dy) const
    {
        float ox=0,oy=0,ax=0,ay=0,bx=0,by=0,unusedX=0,unusedY=0;
        if(!actorHandle((axis+1)%3,ox,oy,ax,ay)||!actorHandle((axis+2)%3,unusedX,unusedY,bx,by))return false;
        const float extent=std::max(std::hypot(ax,ay),std::hypot(bx,by));
        if(extent<0.5f)return false;
        const float scale=64.f/extent,c=std::cos(angle),s=std::sin(angle);
        x=ox+scale*(ax*c+bx*s);y=oy+scale*(ay*c+by*s);
        // The tangent is in pixels per degree, so dragging the ring edits
        // the authored Euler angle directly instead of using a translation rate.
        dx=scale*(-ax*s+bx*c)*0.01745329252f;dy=scale*(-ay*s+by*c)*0.01745329252f;
        return std::isfinite(x)&&std::isfinite(y);
    }
    void BlueprintWorkspace::openActorGraph(bool construction)
    {
        std::string owner;
        if(!m_document.ensureActorBehaviour(m_visualSelection,owner,m_status))return;
        const std::string graphName=construction?"Construction":"start";
        if(construction&&!m_document.function(owner,graphName)&&!m_document.addFunction(graphName,m_status,owner))return;
        for(size_t i=0;i<m_document.graphs.size();++i)if(m_document.graphs[i].owner==owner&&m_document.graphs[i].name==graphName)
        {saveActorTab();m_actorEditor=false;m_assetBrowser=false;releasePlacementPreview();selectGraph(i);m_status=construction?"Construction runs at actor spawn, before Start.":"Actor event graph. Return to viewport with the Actor viewport tab.";return;}
        m_status="Class graph not found.";
    }
    void BlueprintWorkspace::updateActorEditor(Services& s,float x,float y,bool left,bool click,unsigned pressed)
    {
        auto& input=*s.input;
        if(m_actorDragAxis>=0)
        {
            if(m_document.revision()!=m_actorDragRevision){cancelActorDrag();m_status="Component drag cancelled: document changed.";return;}
            const auto axis=static_cast<unsigned>(m_actorDragAxis);
            const float delta=((x-m_actorDragX)*m_actorDragDx+(y-m_actorDragY)*m_actorDragDy)/(m_actorDragDx*m_actorDragDx+m_actorDragDy*m_actorDragDy);
            const float initial=m_actorDragStart[axis];
            const float v=m_actorMode==0?initial+std::round(delta*10.f)/10.f:m_actorMode==1?initial+std::round(delta):std::max(0.01f,initial+delta*0.25f);
            m_actorDragValues[axis]=std::clamp(v,m_actorMode==2?0.01f:-1000000.f,1000000.f);
            const Vec3 value(m_actorDragValues[0],m_actorDragValues[1],m_actorDragValues[2]);
            if(left)
            {
                // Mutate only the private preview. The document receives one
                // undoable edit on release, not one edit per mouse move.
                if(const auto id=actorPreviewEntity();id)
                {
                    auto& space=m_placementWorld->space();
                    if(auto* p=space.getSpatial(id))
                    {
                        if(m_actorMode==0)p->place=value;
                        else if(m_actorMode==1)p->turn=Quat::fromAngles(value);
                        else p->size=value;
                        m_placementWorld->eachEntity([&](u32 entity,const World::Entity&){space.transform(entity&0xffffu);});
                    }
                }
                return;
            }
            const char* prop=m_actorMode==0?"place":m_actorMode==1?"angles":"size";
            const bool moved=std::abs(m_actorDragValues[axis]-initial)>0.0001f;
            if(moved&&m_document.setVisualActorProperty(m_visualSelection,m_visualComponent,prop,std::to_string(value.x)+","+std::to_string(value.y)+","+std::to_string(value.z),m_status))
            {m_status="Component transform applied. Ctrl+Z undoes it.";NX_INFO("Blueprint Studio: component gizmo committed");}
            cancelActorDrag();return;
        }
        if(m_visualInput)
        {
            const float dialogX=(kWidth-560)*0.5f,dialogY=(kHeight-160)*0.5f;
            const bool applyClick=click&&in(x,y,dialogX+324,dialogY+104,dialogX+424,dialogY+140);
            if(click&&(!in(x,y,dialogX,dialogY,dialogX+560,dialogY+160)||in(x,y,dialogX+434,dialogY+104,dialogX+544,dialogY+140)))
            {m_visualInput=0;m_actorAxis=-1;input.setTyping(false);return;}
            for(u32 i=0;i<input.typedCount()+(applyClick?1u:0u);++i)
            {
                const auto ch=i<input.typedCount()?input.typed(i):13u;
                if(ch==13)
                {
                    std::string value=m_visualText;
                    if(m_actorAxis>=0&&m_actorAxisValues.size()==3)
                    {
                        auto parts=m_actorAxisValues;parts[static_cast<size_t>(m_actorAxis)]=value;
                        value=parts[0]+","+parts[1]+","+parts[2];
                    }
                    if(m_document.setVisualActorProperty(m_visualSelection,m_visualComponent,m_visualProperty,value,m_status))
                    {m_visualInput=0;m_actorAxis=-1;input.setTyping(false);m_status="Component property updated.";break;}
                }
                else if(ch==8)
                {
                    m_actorReplace=false;
                    if(!m_visualText.empty()){size_t end=m_visualText.size()-1;while(end&&(static_cast<unsigned char>(m_visualText[end])&0xc0)==0x80)--end;m_visualText.resize(end);}
                }
                else if(ch>=32)
                {
                    if(m_actorReplace){m_visualText.clear();m_actorReplace=false;}
                    appendCharacter(m_visualText,ch,128);
                }
            }
            return;
        }
        const auto* selected=m_document.visualAsset(m_visualSelection);
        if(!selected||selected->kind!="actor"){m_actorEditor=false;releasePlacementPreview();return;}
        if(pressed&1u){saveCurrent(s);return;}
        if(pressed&2u){if(m_document.undo())m_status="Undid actor edit.";return;}
        if(pressed&4u){if(m_document.redo())m_status="Redid actor edit.";return;}
        std::string resolved,why;JsonTree tree;
        if(!m_document.visualActorSource(m_visualSelection,resolved,why)||!tree.parse(resolved.data(),resolved.size()))return;
        const auto entries=tree.root()["entities"];const size_t count=entries.count();
        if(m_visualComponent>=count)m_visualComponent=0;
        const auto rows=static_cast<size_t>(std::max(1.f,(kBottom-188)/30.f));
        if(m_visualComponentFirst>=count)m_visualComponentFirst=0;
        if(resizePanels(x,y,left,click,true)||updateGraphTabs(s,x,y,click))return;
        if(in(x,y,0,120,kLeft,kBottom-64))
        {
            if(input.wheel()<0&&m_visualComponentFirst+rows<count)++m_visualComponentFirst;
            if(input.wheel()>0&&m_visualComponentFirst)--m_visualComponentFirst;
            if(click){const auto i=m_visualComponentFirst+static_cast<size_t>((y-120)/30);if(i<count)m_visualComponent=i;}
            return;
        }
        const float right=kWidth-m_actorRightPanel;
        const float fieldWidth=(m_actorRightPanel-16.f)/3.f;
        const bool orbit=input.raw().mouseButtons[1]||input.raw().mouseButtons[2];
        if(m_actorOrbit&&orbit)
        {
            m_placementYaw+=(x-m_actorOrbitX)*0.008f;
            m_actorPitch=std::clamp(m_actorPitch+(y-m_actorOrbitY)*0.008f,-1.4f,1.4f);
            m_actorOrbitX=x;m_actorOrbitY=y;return;
        }
        m_actorOrbit=false;
        if(orbit&&in(x,y,kLeft,kTop+44,right,kBottom)){m_actorOrbit=true;m_actorOrbitX=x;m_actorOrbitY=y;return;}
        if(in(x,y,kLeft,kTop,right,kBottom))
        {
            const float wheel=input.wheel();
            if(wheel!=0)m_placementDistance=std::clamp(m_placementDistance*std::pow(0.88f,std::clamp(wheel,-8.f,8.f)),2.f,100.f);
        }
        if(in(x,y,right+5,112,kWidth-5,kBottom-88)&&input.wheel()!=0)
        {
            const auto rowsHere=actorDetailRows();
            const float full=rowsHere.empty()?112.f:rowsHere.back().y+m_actorDetailsScroll+36;
            m_actorDetailsScroll=std::clamp(m_actorDetailsScroll-input.wheel()*40.f,0.f,std::max(0.f,full-(kBottom-88)));return;
        }
        if(!click)return;
        if(in(x,y,kWidth-244,5,kWidth-8,34))
        {
            m_actorConstructionPreview=!m_actorConstructionPreview;
            releasePlacementPreview();
            m_status=m_actorConstructionPreview?"Rebuilding isolated Construction preview.":"Showing authored actor defaults.";
            return;
        }
        if(in(x,y,kLeft,kTop+44,right,kBottom))
        {
            int picked=-1;float best=10.f,pickedDx=0,pickedDy=0;
            for(unsigned axis=0;axis<3;++axis)
            {
                if(m_actorMode==1)
                {
                    for(unsigned step=0;step<96;++step)
                    {
                        float px=0,py=0,dx=0,dy=0;
                        if(!actorRing(axis,6.2831853f*static_cast<float>(step)/96.f,px,py,dx,dy)||dx*dx+dy*dy<0.01f)continue;
                        const float distance=std::hypot(x-px,y-py);
                        if(distance<best){best=distance;picked=static_cast<int>(axis);pickedDx=dx;pickedDy=dy;}
                    }
                }
                else
                {
                    float ox=0,oy=0,dx=0,dy=0;if(!actorHandle(axis,ox,oy,dx,dy))continue;
                    const float len=std::hypot(dx,dy),ex=ox+52*dx/len,ey=oy+52*dy/len;
                    const float distance=std::hypot(x-ex,y-ey);
                    if(distance<best){best=distance;picked=static_cast<int>(axis);pickedDx=dx;pickedDy=dy;}
                }
            }
            if(picked>=0)
            {
                if(m_actorConstructionApplied){m_status="Turn off Construction preview before dragging authored transforms.";return;}
                const auto item=entries[static_cast<u32>(m_visualComponent)];
                const char* prop=m_actorMode==0?"place":m_actorMode==1?"angles":"size";
                std::vector<std::string> values;
                if(!blueprintInputComponents("Vec3",actorVector(item[prop],prop),values))return;
                for(size_t i=0;i<3;++i)m_actorDragStart[i]=m_actorDragValues[i]=std::strtof(values[i].c_str(),nullptr);
                m_actorDragAxis=picked;m_actorDragRevision=m_document.revision();m_actorDragX=x;m_actorDragY=y;m_actorDragDx=pickedDx;m_actorDragDy=pickedDy;
                m_status="Drag axis; release to apply one undoable change.";return;
            }
            if(m_placementWorld&&m_placementReady)
            {
                size_t component=0,nearest=count,marker=count;
                float distance=100000.f,markerDistance=18.f;auto& space=m_placementWorld->space();
                const auto from=space.cameraPlace(),ray=space.rayThrough({x,y});
                m_placementWorld->eachEntity([&](u32 id,const World::Entity& entity)
                {
                    if(entity.shown&&!entity.dying)
                    {
                        if(space.pickSurface(id,from,ray,distance))nearest=component;
                        const auto* appearance=space.getAppearance(id);
                        // Only non-mesh helpers use origin proximity; never select a missed mesh.
                        if(!appearance||(!appearance->model&&!appearance->custom&&appearance->solid==Solid::None))
                        {
                            Vec2 projected;
                            if(space.toScreen(space.worldPlace(id),projected))
                            {
                                const float delta=std::hypot(projected.x-x,projected.y-y);
                                if(delta<markerDistance){markerDistance=delta;marker=component;}
                            }
                        }
                    }
                    ++component;
                });
                if(nearest==count)nearest=marker;
                if(nearest<count)
                {
                    m_visualComponent=nearest;
                    if(nearest<m_visualComponentFirst)m_visualComponentFirst=nearest;
                    else if(nearest>=m_visualComponentFirst+rows)m_visualComponentFirst=nearest-rows+1;
                    m_status="Selected component in viewport.";return;
                }
            }
        }
        if(in(x,y,12,39,150,69)){leaveActorViewport(true);return;}
        if(in(x,y,158,39,308,69)){openActorGraph(false);return;}
        if(in(x,y,900,39,1058,69)){openActorGraph(true);return;}
        if(in(x,y,1066,39,1208,69)){m_uiScale=(m_uiScale+1)%4;return;}
        if(in(x,y,316,39,466,69))
        {if(m_document.placeVisualActor(m_actorTabScreen.empty()?assetScreen():m_actorTabScreen,m_visualSelection,"0,0,0",m_status)){rebuildPins();filterCatalogue();m_status="Linked actor added to screen.";}return;}
        if(in(x,y,474,39,610,69)){saveCurrent(s);return;}
        if(in(x,y,kLeft+12,kTop+8,kLeft+102,kTop+36)){m_placementYaw-=0.35f;return;}
        if(in(x,y,kLeft+108,kTop+8,kLeft+198,kTop+36)){m_placementYaw+=0.35f;return;}
        if(in(x,y,kLeft+204,kTop+8,kLeft+294,kTop+36)){m_placementYaw=0.65f;m_actorPitch=0.576375f;m_placementDistance=8;m_actorFocus[0]=m_actorFocus[1]=m_actorFocus[2]=0;return;}
        if(in(x,y,kLeft+300,kTop+8,kLeft+390,kTop+36))
        {
            if(const auto id=actorPreviewEntity();id){const auto p=m_placementWorld->space().worldPlace(id);m_actorFocus[0]=p.x;m_actorFocus[1]=p.y;m_actorFocus[2]=p.z;m_placementDistance=4;m_status="Focused selected component.";}
            return;
        }
        if(in(x,y,618,39,738,69)){m_actorMode=std::min(2u,static_cast<unsigned>((x-618)/40));m_status=m_actorMode==0?"Translate component":m_actorMode==1?"Rotate component":"Scale component";return;}
        if(in(x,y,746,39,892,69)){m_actorCollision=!m_actorCollision;return;}
        if(in(x,y,8,kBottom-60,kLeft-8,kBottom-32)||in(x,y,8,kBottom-30,kLeft-8,kBottom-2))
        {
            const bool camera=y>=kBottom-30;
            if(m_document.appendVisualActorComponent(m_visualSelection,m_visualComponent,camera?"camera":"box",camera?"0,2,-5":"0,1.5,0",m_status))
            {m_visualComponent=count;m_visualComponentFirst=count>=rows?count-rows+1:0;m_status="Added child component.";}
            return;
        }
        if(in(x,y,right+8,112,kWidth-8,kBottom-88))
        {
            for(const auto& item:actorDetailRows())
            {
                if(item.y<112||item.y+(item.property<0?24.f:36.f)>kBottom-88||y<item.y||y>=item.y+(item.property<0?24.f:36.f))continue;
                if(item.property<0)
                {
                    m_actorCollapsed^=1u<<static_cast<unsigned>(-1-item.property);
                    const auto rowsHere=actorDetailRows();
                    m_actorDetailsScroll=std::min(m_actorDetailsScroll,std::max(0.f,rowsHere.back().y+m_actorDetailsScroll+36-(kBottom-88)));
                    return;
                }
                const auto row=static_cast<size_t>(item.property);
                if(row==7&&x>=kWidth-114&&y<item.y+17)
                {
                    const auto id=actorPreviewEntity();const auto* look=id&&m_placementWorld?m_placementWorld->space().getAppearance(id):nullptr;
                    if(!look){m_status="Select a visible mesh component first.";return;}
                    Vec3 low(-0.5f,-0.5f,-0.5f),high(0.5f,0.5f,0.5f);
                    if(look->model){if(!look->model->model){m_status="Model is still loading; generate collision once it is ready.";return;}low=look->model->model->low;high=look->model->model->high;}
                    else if(look->solid==Solid::None||look->custom){m_status="Simple collision requires a loaded model or native primitive.";return;}
                    const auto extent=high-low,center=(high+low)*0.5f;
                    const auto str=[](const Vec3& v){return std::to_string(v.x)+","+std::to_string(v.y)+","+std::to_string(v.z);};
                    if(m_document.setVisualActorProperty(m_visualSelection,m_visualComponent,"simpleCollision",str(Vec3(std::max(0.02f,extent.x),std::max(0.02f,extent.y),std::max(0.02f,extent.z)))+";"+str(center),m_status)){m_actorCollision=true;m_status="Generated solid box from local mesh bounds. Undo restores prior collision.";}
                    return;
                }
                if(editActorProperty(s,actorProperties[row])&&row>=1&&row<=3&&y>=item.y+15&&blueprintInputComponents("Vec3",m_visualText,m_actorAxisValues))
                {m_actorAxis=std::clamp(static_cast<int>((x-right-8)/fieldWidth),0,2);m_visualText=m_actorAxisValues[static_cast<size_t>(m_actorAxis)];}
                return;
            }
            return;
        }
        if(in(x,y,right+8,kBottom-82,kWidth-8,kBottom-50))
        {
            const auto item=entries[static_cast<u32>(m_visualComponent)];
            const std::string shape=item["shape"].text();
            const char* next=shape=="box"?"sphere":shape=="sphere"?"capsule":shape=="capsule"?"plane":"box";
            m_document.setVisualActorShape(m_visualSelection,next,m_status,m_visualComponent);return;
        }
        if(in(x,y,right+8,kBottom-40,kWidth-8,kBottom-8)&&!selected->dependencies.empty())
            m_document.resetVisualActorOverrides(m_visualSelection,m_status,m_visualComponent);
    }
    void BlueprintWorkspace::drawActorEditor(Canvas& c)
    {
        c.rect(0,0,kWidth,kHeight,{0.10f,0.10f,0.11f,1});
        c.rect(0,0,kWidth,kTop,{0.16f,0.16f,0.17f,1});
        const auto* selected=m_document.visualAsset(m_visualSelection);
        if(!selected)return;
        const auto button=[&](float x,float y,float w,const char* label)
        {c.rect(x,y,w,30,{0.24f,0.24f,0.26f,1});c.text(*m_font,label,x+w*0.5f,y+6,text,Align::Center);};
        c.clip(12,0,kWidth-270,38);
        c.text(*m_font,("BLUEPRINT CLASS / "+selected->title+(m_document.dirty()?" *":"")+" / "+m_document.target().label()).c_str(),12,10,text);c.noClip();
        button(kWidth-244,5,236,m_actorConstructionPreview?"Construction preview: ON":"Construction preview: OFF");
        if(m_actorConstructionPreview&&!m_actorConstructionStatus.empty())
        {
            c.clip(12,24,kWidth-270,15);c.text(*m_font,m_actorConstructionStatus.c_str(),12,24,muted);c.noClip();
        }
        button(12,39,138,"Content");button(158,39,150,"Event graph");button(316,39,150,"Add to screen");button(474,39,136,"Save");
        const float right=kWidth-m_actorRightPanel;
        const float fieldWidth=(m_actorRightPanel-16.f)/3.f;
        // Compact independent transform tools, with selected state and glyphs.
        for(unsigned mode=0;mode<3;++mode)
        {
            const float xx=618+40.f*static_cast<float>(mode),cx=xx+19,cy=54;
            c.rect(xx,39,38,30,m_actorMode==mode?Color{0.52f,0.42f,0.19f,1}:Color{0.24f,0.24f,0.26f,1});
            if(mode==0)
            {
                c.line(cx-10,cy,cx+10,cy,2,text);c.line(cx,cy-10,cx,cy+10,2,text);
                c.line(cx+10,cy,cx+6,cy-4,2,text);c.line(cx+10,cy,cx+6,cy+4,2,text);
                c.line(cx,cy-10,cx-4,cy-6,2,text);c.line(cx,cy-10,cx+4,cy-6,2,text);
            }
            else if(mode==1)
            {
                for(unsigned i=0;i<24;++i){const float p=5.2f*static_cast<float>(i)/24.f,q=5.2f*static_cast<float>(i+1)/24.f;c.line(cx+9*std::cos(p),cy+9*std::sin(p),cx+9*std::cos(q),cy+9*std::sin(q),2,text);}
                c.line(cx+4,cy-8,cx+4,cy-2,2,text);c.line(cx+4,cy-8,cx-2,cy-9,2,text);
            }
            else
            {
                c.line(cx-8,cy+8,cx+8,cy-8,2,text);c.rect(cx-11,cy+5,6,6,text);c.rect(cx+4,cy-12,8,8,text);
            }
        }
        button(746,39,146,m_actorCollision?"Collision on":"Collision off");button(900,39,158,"Construction");
        button(1066,39,142,m_uiScale==0?"UI: Auto":m_uiScale==1?"UI: 100%":m_uiScale==2?"UI: 125%":"UI: 150%");
        c.rect(0,kTop,kLeft,kBottom-kTop,{0.16f,0.16f,0.17f,1});
        c.rect(right,kTop,kWidth-right,kBottom-kTop,{0.16f,0.16f,0.17f,1});
        c.rect(kLeft,kTop,right-kLeft,kBottom-kTop,{0.10f,0.10f,0.11f,1});
        c.text(*m_font,"COMPONENTS",12,kTop+12,text);
        c.text(*m_font,"DETAILS / COMPONENT",right+12,kTop+12,text);
        unsigned selectedEntity=0;std::string resolved,why;JsonTree tree;
        if(m_document.visualActorSource(m_visualSelection,resolved,why)&&tree.parse(resolved.data(),resolved.size()))
        {
            const auto entries=tree.root()["entities"],parents=tree.root()["parents"];
            const auto rows=static_cast<size_t>(std::max(1.f,(kBottom-188)/30.f));
            for(size_t i=0;i<rows&&m_visualComponentFirst+i<entries.count();++i)
            {
                const auto index=static_cast<u32>(m_visualComponentFirst+i);const auto item=entries[index];
                unsigned depth=0;int parent=static_cast<int>(parents[index].number(-1));
                for(unsigned n=0;parent>=0&&static_cast<u32>(parent)<index&&n<12;++n){++depth;parent=static_cast<int>(parents[static_cast<u32>(parent)].number(-1));}
                const float y=120+30.f*static_cast<float>(i);
                c.rect(5,y,kLeft-10,28,index==m_visualComponent?Color{0.39f,0.34f,0.18f,1}:Color{0.20f,0.20f,0.21f,1});
                c.clip(8,y,kLeft-16,28);c.text(*m_font,(std::string(item["name"].text("Component"))+" / "+item["shape"].text(item["camera"].exists()?"camera":"model")).c_str(),12+10.f*static_cast<float>(depth),y+5,text);c.noClip();
            }
            const auto item=entries[static_cast<u32>(m_visualComponent)];
            selectedEntity=actorPreviewEntity();
            const char* categories[]={"Identity","Transform","Rendering / animation","Class / camera","Collision / physics"};
            for(const auto& row:actorDetailRows())
            {
                const float y=row.y;
                if(y<112||y+(row.property<0?24.f:36.f)>kBottom-88)continue;
                if(row.property<0)
                {
                    const unsigned group=static_cast<unsigned>(-1-row.property);
                    c.rect(right+8,y,m_actorRightPanel-16,22,{0.27f,0.27f,0.29f,1});
                    c.text(*m_font,(std::string(m_actorCollapsed&(1u<<group)?"+  ":"-  ")+categories[group]).c_str(),right+14,y+2,text);
                    continue;
                }
                const auto i=static_cast<size_t>(row.property);const auto v=item[actorProperties[i]];
                c.rect(right+8,y,m_actorRightPanel-16,34,{0.20f,0.20f,0.21f,1});
                c.text(*m_font,actorPropertyLabels[i],right+12,y+1,muted);
                if(i==7){c.roundRect(kWidth-114,y,104,16,3,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Generate box",kWidth-62,y,text,Align::Center);}
                if(i>=1&&i<=3)
                {
                    const float fallback=i==3?1.f:0.f;const Color colors[]={{0.75f,0.22f,0.22f,1},{0.20f,0.56f,0.28f,1},{0.23f,0.43f,0.78f,1}};
                    for(u32 axis=0;axis<3;++axis)
                    {
                        const float xx=right+8+fieldWidth*static_cast<float>(axis);
                        char number[32];snprintf(number,sizeof(number),"%.2f",static_cast<double>(v.isArray()?v[axis].number(fallback):v.isNumber()?(i==2?(axis==1?v.number():0.f):v.number()):fallback));
                        c.rect(xx,y+16,fieldWidth-2,18,{0.30f,0.30f,0.31f,1});c.rect(xx,y+16,15,18,colors[axis]);
                        c.text(*m_font,axis==0?"X":axis==1?"Y":"Z",xx+2,y+16,text);c.clip(xx+17,y+16,fieldWidth-20,18);c.text(*m_font,number,xx+19,y+16,text);c.noClip();
                    }
                }
                else
                {
                    std::string value=v.isNumber()?std::to_string(v.exact()):v.text();
                    if(i==7)value=item["cube"].exists()?"Box":item["sphere"].exists()?"Sphere":item["capsule"].exists()?"Capsule":"None";
                    c.clip(right+12,y+16,m_actorRightPanel-24,18);c.text(*m_font,value.empty()?"Default":value.c_str(),right+12,y+16,text);c.noClip();
                }
            }
            button(right+8,kBottom-82,m_actorRightPanel-16,"Cycle component shape");
            if(!selected->dependencies.empty())button(right+8,kBottom-40,m_actorRightPanel-16,"Reset inherited overrides");
        }
        button(8,kBottom-60,kLeft-16,"+ Add box child");button(8,kBottom-30,kLeft-16,"+ Add camera child");
        const float top=kTop+44;
        if(m_placementWorld)
        {
            auto& space=m_placementWorld->space();const float d=m_placementDistance;
            const float horizontal=d*std::cos(m_actorPitch);
            const Vec3 focus(m_actorFocus[0],m_actorFocus[1],m_actorFocus[2]);
            space.setCamera(focus+Vec3(std::sin(m_placementYaw)*horizontal,d*std::sin(m_actorPitch),std::cos(m_placementYaw)*horizontal),focus,55);
            space.m_showShapes=m_actorCollision;space.m_editorGrid=true;space.m_editorGridHeight=0;
            space.draw(c,kLeft,top,right-kLeft,kBottom-top);m_placementReady=true;
            c.clip(kLeft,top,right-kLeft,kBottom-top);
            if(selectedEntity)
            {
                const auto place=space.worldPlace(selectedEntity);Vec2 origin;
                if(space.toScreen(place,origin))
                {
                    c.circle(origin.x,origin.y,5,{0.95f,0.74f,0.22f,1});
                    const Color axes[]={{0.95f,0.2f,0.2f,1},{0.2f,0.9f,0.35f,1},{0.25f,0.5f,1,1}};
                    for(unsigned i=0;i<3;++i)
                    {
                        const Color ink=m_actorDragAxis==static_cast<int>(i)?Color{1.f,0.8f,0.2f,1}:axes[i];
                        if(m_actorMode==1)
                        {
                            float lastX=0,lastY=0;bool have=false;
                            for(unsigned step=0;step<=96;++step)
                            {
                                float px=0,py=0,dx=0,dy=0;
                                if(!actorRing(i,6.2831853f*static_cast<float>(step)/96.f,px,py,dx,dy)){have=false;continue;}
                                if(have)c.line(lastX,lastY,px,py,2,ink);
                                lastX=px;lastY=py;have=true;
                            }
                            continue;
                        }
                        float ox=0,oy=0,dx=0,dy=0;if(!actorHandle(i,ox,oy,dx,dy))continue;
                        const float len=std::hypot(dx,dy),xx=ox+52*dx/len,yy=oy+52*dy/len;
                        c.line(ox,oy,xx,yy,2,ink);if(m_actorMode==2)c.rect(xx-5,yy-5,10,10,ink);else c.circle(xx,yy,5,ink);c.text(*m_font,i==0?"X":i==1?"Y":"Z",xx+6,yy-6,text);
                    }
                }
            }
            c.noClip();
        }
        else c.text(*m_font,"Preparing isolated native preview",kLeft+16,top+14,muted);
        button(kLeft+12,kTop+8,90,"Orbit L");button(kLeft+108,kTop+8,90,"Orbit R");button(kLeft+204,kTop+8,90,"Reset");button(kLeft+300,kTop+8,90,"Focus");
        if(right-kLeft>540)c.text(*m_font,"Perspective",right-12,kTop+14,muted,Align::Right);
        c.rect(kLeft-2,kTop,4,kBottom-kTop,{0.32f,0.32f,0.34f,1});c.rect(right-2,kTop,4,kBottom-kTop,{0.32f,0.32f,0.34f,1});
        c.rect(0,kBottom,kWidth,kHeight-kBottom,{0.16f,0.16f,0.17f,1});
        c.clip(10,kBottom+4,kWidth-20,34);c.text(*m_font,(m_placementOmitted?"Preview unavailable for this definition; "+m_status:m_status).c_str(),12,kBottom+10,text);c.noClip();
        drawGraphTabs(c);
        if(m_visualInput)
        {
            c.rect(0,0,kWidth,kHeight,{0,0,0,0.65f});const float x=(kWidth-560)*0.5f,y=(kHeight-160)*0.5f;
            const auto label=m_visualProperty+(m_actorAxis>=0?std::string(" / ")+"XYZ"[m_actorAxis]:"");
            c.roundRect(x,y,560,160,5,{0.18f,0.18f,0.19f,1});c.text(*m_font,label.c_str(),x+16,y+16,accent);
            c.rect(x+16,y+50,528,38,{0.28f,0.28f,0.30f,1});c.clip(x+20,y+50,520,38);c.text(*m_font,m_visualText.c_str(),x+24,y+60,text);c.noClip();
            c.text(*m_font,"Enter = Apply | Escape = Cancel",x+16,y+112,muted);
            c.roundRect(x+324,y+104,100,36,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",x+374,y+111,text,Align::Center);
            c.roundRect(x+434,y+104,110,36,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",x+489,y+111,text,Align::Center);
        }
    }
    BlueprintWorkspace::ContentBounds BlueprintWorkspace::contentBounds() const
    {
        const float height=std::min(m_contentPanelHeight,std::max(174.f,kBottom-kTop-160));
        if(!m_contentFloating)
        {
            if(m_contentDockSide)
            {
                const float left=graphBaseLeft(),right=graphBaseRight();
                const float width=std::min(m_contentSideWidth,std::max(156.f,right-left-200));
                return m_contentDockSide==1?ContentBounds{left,kTop,left+width,kBottom}:ContentBounds{right-width,kTop,right,kBottom};
            }
            return m_contentDockTop?ContentBounds{graphBaseLeft(),kTop,graphBaseRight(),kTop+height}:ContentBounds{graphBaseLeft(),kBottom-height,graphBaseRight(),kBottom};
        }
        const float width=std::min(m_contentPanelWidth,kWidth-24);
        const float x=12+m_contentPanelX*std::max(0.f,kWidth-width-24);
        const float y=kTop+m_contentPanelY*std::max(0.f,kBottom-kTop-height);
        return {x,y,x+width,y+height};
    }
    size_t BlueprintWorkspace::contentColumns(const ContentBounds& bounds) const
    {return static_cast<size_t>(std::max(1.f,std::floor((bounds.right-bounds.left-16)/148.f)));}
    size_t BlueprintWorkspace::contentRows(const ContentBounds& bounds) const
    {return static_cast<size_t>(std::max(1.f,std::floor((bounds.bottom-bounds.top-58)/112.f)));}
    void BlueprintWorkspace::releaseContentPreviews()
    {
        for(auto& preview:m_contentPreviews){if(preview.world)destroy(preview.world);if(preview.ui)destroy(preview.ui);}
        m_contentPreviews.clear();m_contentPreviewRevision=static_cast<size_t>(-1);m_contentPreviewScreen.clear();
    }
    void BlueprintWorkspace::refreshContentPreviews(Services& s)
    {
        if(!m_contentDrawer||m_actorEditor||modalOpen()||!s.assets||!s.host||!s.solids)
        {if(!m_contentPreviews.empty())releaseContentPreviews();return;}
        auto screen=assetScreen();
        if(screen.empty())screen=m_document.moduleForOwner(m_document.graphs[m_graph].owner);
        if(screen.empty())
        {
            const auto screens=m_document.screens();if(!screens.empty())screen=screens.front();
        }
        const auto bounds=contentBounds();const auto& assets=m_document.visualAssets();
        const size_t capacity=contentColumns(bounds)*contentRows(bounds);
        std::vector<std::string> visible;
        for(size_t i=0;i<capacity&&m_contentFirst+i<assets.size()&&visible.size()<4;++i)
            if(assets[m_contentFirst+i].kind=="actor"||assets[m_contentFirst+i].kind=="widget")visible.push_back(assets[m_contentFirst+i].id);
        if(m_contentPreviewRevision==m_document.revision()&&m_contentPreviewScreen==screen&&visible.size()==m_contentPreviews.size()&&
            std::equal(visible.begin(),visible.end(),m_contentPreviews.begin(),[](const std::string& id,const ContentPreview& p){return id==p.id;}))return;
        releaseContentPreviews();m_contentPreviewRevision=m_document.revision();m_contentPreviewScreen=screen;
        const auto folder="own/"+m_document.assetRoot+"/"+screen;
        for(const auto& id:visible)
        {
            ContentPreview item;item.id=id;std::string json,why;JsonValue root;
            const auto* asset=m_document.visualAsset(id);
            if(asset&&asset->kind=="widget")
            {
                std::string theme;
                if(s.canvas&&s.audio&&s.input&&s.platform&&m_document.visualWidgetTheme(id,theme,why)&&m_document.visualWidgetPage(id,json,why))
                {
                    item.ui=create<Ui>(MemTag::UI,*s.canvas,*s.assets,*s.audio,*s.input,*s.host,*s.platform,folder.c_str());
                    if(item.ui)
                    {
                        if((!theme.empty()&&!item.ui->loadThemeSource(theme.data(),theme.size()))||
                           !item.ui->loadPage(json.data(),json.size(),item.ui->root(),nullptr,"thumbnail",true))
                        {destroy(item.ui);item.ui=nullptr;}
                        else NX_INFO("Blueprint Studio: native widget thumbnail ready: %s",id.c_str());
                    }
                }
                m_contentPreviews.push_back(std::move(item));continue;
            }
            if(m_document.visualActorSource(id,json,why)&&root.parse(json.data(),json.size(),why))
            {
                auto* entities=root.find("entities");
                if(entities&&entities->count()<=128)
                {
                    for(u32 i=0;i<entities->count();++i)
                        for(const char* field:{"behaviour","sound","camera","group","hits","onLand","cube","sphere","capsule","collider","mass","friction","bounce","gravity","drag","walker","animate"})
                            entities->at(i)->remove(field);
                    json=root.write(true);JsonTree tree;
                    if(tree.parse(json.data(),json.size()))
                    {
                        item.world=create<World>(MemTag::Scene,*s.assets,*s.host,"blueprint-thumbnail",128,0);
                        if(item.world)
                        {
                            auto& space=item.world->space();space.init(s.solids);
                            if(space.makeActor(tree.root(),folder.c_str(),{0,0,0}))
                            {
                                Vec3 lo(0,0,0),hi(0,0,0);
                                item.world->eachEntity([&](u32 entity,const World::Entity&)
                                {
                                    const auto p=space.worldPlace(entity);float radius=1;
                                    if(const auto* spatial=space.getSpatial(entity))
                                    {
                                        const auto& w=spatial->world;
                                        radius=std::max({0.25f,Vec3(w.m[0],w.m[1],w.m[2]).length(),Vec3(w.m[4],w.m[5],w.m[6]).length(),Vec3(w.m[8],w.m[9],w.m[10]).length()});
                                    }
                                    lo=Vec3(std::min(lo.x,p.x-radius),std::min(lo.y,p.y-radius),std::min(lo.z,p.z-radius));
                                    hi=Vec3(std::max(hi.x,p.x+radius),std::max(hi.y,p.y+radius),std::max(hi.z,p.z+radius));
                                });
                                const Vec3 center=(lo+hi)*0.5f;
                                const float d=std::clamp((hi-lo).length()*1.35f,2.f,10000.f);
                                space.setCamera(center+Vec3(d*0.65f,d*0.45f,d*0.65f),center,45);
                                NX_INFO("Blueprint Studio: native content preview ready: %s",id.c_str());
                            }
                            else {destroy(item.world);item.world=nullptr;}
                        }
                    }
                }
            }
            m_contentPreviews.push_back(std::move(item));
        }
    }
    bool BlueprintWorkspace::updateContentDrawer(Services& s,float x,float y,bool left,bool click)
    {
        auto bounds=contentBounds();
        if(m_contentResize)
        {
            if(left)
            {
                if(!m_contentFloating&&m_contentDockSide)
                    m_contentSideWidth=std::clamp(m_contentStartWidth+(x-m_contentResizeX)*(m_contentDockSide==1?1.f:-1.f),260.f,480.f);
                else
                {
                    if(m_contentFloating)m_contentPanelWidth=std::clamp(m_contentStartWidth+x-m_contentResizeX,460.f,1000.f);
                    const float delta=y-m_contentResizeY;
                    m_contentPanelHeight=std::clamp(m_contentStartHeight+(!m_contentFloating&&!m_contentDockTop?-delta:delta),174.f,510.f);
                }
            }
            else {m_contentResize=false;if(contentSideActive())fitGraph();}
            return true;
        }
        if(m_contentPanelDrag)
        {
            if(left)
            {
                m_contentFloating=true;
                const float width=std::min(m_contentPanelWidth,kWidth-24);
                m_contentPanelX=std::clamp((x-m_contentGrabX-12)/std::max(1.f,kWidth-width-24),0.f,1.f);
                m_contentPanelY=std::clamp((y-m_contentGrabY-kTop)/std::max(1.f,kBottom-kTop-(bounds.bottom-bounds.top)),0.f,1.f);
            }
            else
            {
                m_contentPanelDrag=false;
                if(x>=graphBaseLeft()&&x<=graphBaseRight())
                {
                    if(y>kBottom-38){m_contentFloating=false;m_contentDockTop=false;m_contentDockSide=0;}
                    else if(y<kTop+38){m_contentFloating=false;m_contentDockTop=true;m_contentDockSide=0;}
                    else if(x<graphBaseLeft()+44){m_contentFloating=false;m_contentDockSide=1;}
                    else if(x>graphBaseRight()-44){m_contentFloating=false;m_contentDockSide=2;}
                }
                if(contentSideActive())fitGraph();
                m_status=m_contentFloating?"Content panel floating. Drag its title to a graph edge to dock.":m_contentDockSide==1?"Content panel docked at left.":m_contentDockSide==2?"Content panel docked at right.":m_contentDockTop?"Content panel docked at top.":"Content panel docked at bottom.";
                NX_INFO("Blueprint Studio: %s",m_status.c_str());
            }
            return true;
        }
        if(m_contentDrag.size())
        {
            if(left)return true;
            const auto id=m_contentDrag;m_contentDrag.clear();
            if(m_contentDragRevision!=m_document.revision()||!m_document.visualAsset(id))return true;
            if(!in(x,y,bounds.left,bounds.top,bounds.right,bounds.bottom)&&in(x,y,graphAreaLeft(),graphAreaTop(),graphAreaRight(),tabTop())&&std::hypot(x-m_contentDragX,y-m_contentDragY)>8)
            {
                const auto screen=assetScreen();
                if(screen.empty())m_status="Open a screen graph before placing an actor instance.";
                else if(m_document.placeVisualActor(screen,id,"0,0,0",m_status)){rebuildPins();filterCatalogue();m_status="Linked actor added to screen. Use the placement viewport to position it.";}
            }
            else if(in(x,y,bounds.left,bounds.top,bounds.right,bounds.bottom)&&std::hypot(x-m_contentDragX,y-m_contentDragY)<8)openActorViewport(id);
            return true;
        }
        if(click&&in(x,y,kWidth-150,kBottom+2,kWidth-8,kBottom+24))
        {m_contentDrawer=!m_contentDrawer;return true;}
        if(!m_contentDrawer||!in(x,y,bounds.left,bounds.top,bounds.right,bounds.bottom))return false;
        const auto& assets=m_document.visualAssets();const size_t columns=contentColumns(bounds),rows=contentRows(bounds),capacity=columns*rows;
        if(m_contentFirst>=assets.size())m_contentFirst=0;
        if(s.input->wheel()<0&&m_contentFirst+capacity<assets.size())m_contentFirst+=columns;
        if(s.input->wheel()>0)m_contentFirst=m_contentFirst>=columns?m_contentFirst-columns:0;
        if(click)
        {
            const bool resize=m_contentFloating?in(x,y,bounds.right-14,bounds.bottom-14,bounds.right,bounds.bottom):
                m_contentDockSide==1?x>=bounds.right-5:m_contentDockSide==2?x<bounds.left+5:
                m_contentDockTop?y>=bounds.bottom-5:y<bounds.top+4;
            if(resize)
            {
                m_contentResize=true;m_contentResizeX=x;m_contentResizeY=y;
                m_contentStartWidth=bounds.right-bounds.left;m_contentStartHeight=bounds.bottom-bounds.top;return true;
            }
            const bool narrow=bounds.right-bounds.left<360;
            if(in(x,y,bounds.right-(narrow?72:110),bounds.top+3,bounds.right-8,bounds.top+28)){browseAssets(s);return true;}
            if(in(x,y,bounds.right-(narrow?142:208),bounds.top+3,bounds.right-(narrow?78:116),bounds.top+28))
            {
                m_contentFloating=!m_contentFloating;return true;
            }
            if(y<bounds.top+30)
            {
                m_contentPanelDrag=true;m_contentGrabX=std::clamp(x-bounds.left,0.f,300.f);m_contentGrabY=y-bounds.top;
                return true;
            }
            if(y>=bounds.top+37&&y<bounds.top+37+112.f*rows)
            {
                if(x<bounds.left+8)return true;
                const auto column=static_cast<size_t>((x-bounds.left-8)/148.f),row=static_cast<size_t>((y-bounds.top-37)/112.f);
                if(column>=columns||row>=rows)return true;
                const auto index=m_contentFirst+row*columns+column;
                if(index<assets.size()&&index<m_contentFirst+capacity)
                {
                    const auto& asset=assets[index];
                    if(asset.kind=="actor")
                    {m_contentDrag=asset.id;m_contentDragX=x;m_contentDragY=y;m_contentDragRevision=m_document.revision();}
                    else
                    {
                        const auto id=asset.id;const bool widget=asset.kind=="widget";
                        browseAssets(s);
                        if(m_assetBrowser)
                        {
                            m_assetLibrary=true;m_visualSelection=id;
                            if(widget)
                            {
                                m_widgetDesigner=true;m_widgetPalette=false;
                                m_widgetEvents=m_widgetUnbind=m_widgetDragging=m_widgetAnchors=m_widgetPreviewDrag=m_widgetTimeline=m_widgetTimelinePlay=false;
                                m_widgetTimelineTime=0;m_widgetSelection.clear();m_widgetFirst=0;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
    void BlueprintWorkspace::drawContentDrawer(Canvas& c)
    {
        c.rect(kWidth-150,kBottom+2,142,22,{0.26f,0.26f,0.28f,1});
        c.text(*m_font,m_contentDrawer?"Hide content":"Content drawer",kWidth-79,kBottom+4,text,Align::Center);
        if(!m_contentDrawer)return;
        const auto bounds=contentBounds();const float top=bounds.top,left=bounds.left,right=bounds.right;
        const float height=bounds.bottom-top;
        if(m_contentFloating)c.rect(left+5,top+5,right-left,height,{0,0,0,0.6f});
        c.rect(left,top,right-left,height,{0.16f,0.16f,0.17f,1});
        c.rect(left,top,right-left,30,{0.24f,0.24f,0.26f,1});
        const bool narrow=right-left<360;
        c.clip(left+8,top,std::max(1.f,right-left-(narrow?150:216)),30);c.text(*m_font,narrow?"CONTENT":"CONTENT / PROJECT ASSETS",left+10,top+6,text);c.noClip();
        c.rect(right-(narrow?72:110),top+3,narrow?64.f:102.f,25,{0.30f,0.30f,0.32f,1});c.text(*m_font,narrow?"Edit":"Manage",right-(narrow?40:59),top+5,text,Align::Center);
        c.rect(right-(narrow?142:208),top+3,narrow?64.f:92.f,25,{0.30f,0.30f,0.32f,1});c.text(*m_font,m_contentFloating?"Dock":"Float",right-(narrow?110:162),top+5,text,Align::Center);
        const auto& assets=m_document.visualAssets();const size_t columns=contentColumns(bounds),capacity=columns*contentRows(bounds);
        for(size_t i=0;i<capacity&&m_contentFirst+i<assets.size();++i)
        {
            const auto& asset=assets[m_contentFirst+i];const float x=left+8+148.f*static_cast<float>(i%columns),y=top+37+112.f*static_cast<float>(i/columns);
            c.rect(x,y,140,108,asset.id==m_contentDrag?Color{0.34f,0.31f,0.20f,1}:Color{0.22f,0.22f,0.24f,1});
            c.rect(x+5,y+5,130,62,{0.12f,0.12f,0.14f,1});
            const float cx=x+70,cy=y+35;const Color ink={0.66f,0.70f,0.76f,1};
            const auto preview=std::find_if(m_contentPreviews.begin(),m_contentPreviews.end(),[&](const ContentPreview& p){return p.id==asset.id&&(p.world||p.ui);});
            if(preview!=m_contentPreviews.end()&&preview->world)preview->world->space().draw(c,x+5,y+5,130,62);
            else if(preview!=m_contentPreviews.end()&&preview->ui)
            {
                const auto& target=m_document.target();
                const float w=static_cast<float>(target.width()),h=static_cast<float>(target.height()),aspect=target.pixelAspect();
                const float scale=std::min(130.f/(w*aspect),62.f/h);
                const float thumbWidth=w*aspect*scale,thumbHeight=h*scale,px=x+70-thumbWidth*0.5f,py=y+5+(62-thumbHeight)*0.5f;
                c.push();c.clip(px,py,thumbWidth,thumbHeight);c.translate(px,py);c.scale(scale*aspect,scale);
                preview->ui->drawDesign(c,0,0,w,h);
                c.pop();
            }
            else if(asset.kind=="actor")
            {
                c.line(cx,cy-22,cx+21,cy-11,2,ink);c.line(cx+21,cy-11,cx+21,cy+13,2,ink);c.line(cx+21,cy+13,cx,cy+24,2,ink);
                c.line(cx,cy+24,cx-21,cy+13,2,ink);c.line(cx-21,cy+13,cx-21,cy-11,2,ink);c.line(cx-21,cy-11,cx,cy-22,2,ink);
                c.line(cx-21,cy-11,cx,cy,2,ink);c.line(cx,cy,cx+21,cy-11,2,ink);c.line(cx,cy,cx,cy+24,2,ink);
            }
            else {c.rect(cx-23,cy-17,46,34,ink);c.rect(cx-20,cy-14,40,28,{0.12f,0.12f,0.14f,1});c.line(cx-15,cy-5,cx+15,cy-5,2,ink);c.line(cx-15,cy+5,cx+8,cy+5,2,ink);}
            c.clip(x+5,y+68,130,36);c.text(*m_font,asset.title.c_str(),x+8,y+69,text);c.text(*m_font,asset.kind.c_str(),x+8,y+86,muted);c.noClip();
        }
        c.clip(left+8,bounds.bottom-26,right-left-24,26);c.text(*m_font,assets.empty()?"Create a reusable asset using Manage.":"Click to edit; drag actor to graph. Scroll for more.",left+10,bounds.bottom-23,muted);c.noClip();
        if(m_contentFloating)
        {
            c.line(right-12,bounds.bottom-3,right-3,bounds.bottom-12,1,muted);
            c.line(right-7,bounds.bottom-3,right-3,bounds.bottom-7,1,muted);
            if(m_contentPanelDrag)
            {
                c.roundLine(graphBaseLeft(),kTop,graphBaseRight()-graphBaseLeft(),32,0,2,accent);
                c.roundLine(graphBaseLeft(),kBottom-32,graphBaseRight()-graphBaseLeft(),32,0,2,accent);
                c.roundLine(graphBaseLeft(),kTop+38,38,kBottom-kTop-76,0,2,accent);
                c.roundLine(graphBaseRight()-38,kTop+38,38,kBottom-kTop-76,0,2,accent);
            }
        }
        else if(m_contentDockSide)c.rect(m_contentDockSide==1?right-3:left,top,3,height,{0.36f,0.36f,0.39f,1});
        else c.rect(left,m_contentDockTop?bounds.bottom-3:top,right-left,3,{0.36f,0.36f,0.39f,1});
    }
    void BlueprintWorkspace::drawVisualLibrary(Canvas& c)
    {
        const auto* current=m_document.visualAsset(m_visualSelection);
        const bool actorClass=current&&current->kind=="actor"&&!m_visualPlacement&&!m_widgetDesigner;
        const auto layout=widgetLayout();const auto scene=sceneLayout();
        const bool wide=(m_widgetDesigner&&layout.wide)||(m_visualPlacement&&scene.wide);
        if(wide)
        {
            c.rect(-modalX(),-modalY(),kWidth,kHeight,{0.10f,0.10f,0.11f,1});
            c.rect(-modalX(),-modalY(),kWidth,kTop,{0.16f,0.16f,0.17f,1});
        }
        else c.roundRect(260,100,760,actorClass?565.f:525.f,9,{0.16f,0.16f,0.17f,1});
        const auto button=[&](float x,float y,float w,const char* label)
        {c.roundRect(x,y,w,33,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,label,x+w*0.5f,y+7,text,Align::Center);};
        if(wide)c.text(*m_font,(m_visualPlacement?"SCENE PLACEMENT / "+assetScreen():"WIDGET DESIGNER / "+(current?current->title:std::string())).c_str(),12-modalX(),18-modalY(),text);
        else c.text(*m_font,"CONTENT / PROJECT LIBRARY",280,122,accent);
        if(m_widgetDesigner||m_visualPlacement){const auto info=m_document.target().label();c.clip(wide?12-modalX():280,wide?36-modalY():142,wide?kWidth-30:720,16);c.text(*m_font,info.c_str(),wide?12-modalX():280,wide?36-modalY():142,muted);c.noClip();}
        if(!m_widgetDesigner&&!m_visualPlacement)button(566,116,129,"New theme");
        c.push();if(wide)c.translate(layout.headerDx,layout.headerDy);
        button(704,116,146,"Media");button(860,116,140,"Close");c.pop();
        if(m_visualPlacement)
        {
            if(wide)button(420-modalX(),12-modalY(),210,"Native scene editor");
            else button(630,156,370,"Native scene editor");
        }
        if(m_widgetDesigner)drawWidgetDesigner(c);
        else
        {
        c.push();
        if(m_visualPlacement&&scene.wide)
        {
            c.translate(scene.sideDx,scene.sideDy);
            c.rect(274,150,342,kBottom-112,{0.17f,0.17f,0.18f,1});
        }
        button(280,156,159,m_visualPlacement?"Content library":"New actor");button(449,156,161,m_visualPlacement?"World outliner":"New widget");
        if(m_visualPlacement&&m_placementOutliner)
        {
            auto markers=actorMarkers(m_document,assetScreen());const auto total=markers.size();
            std::erase_if(markers,[&](const ActorMarker& item){return !outlinerMatch(item.label,item.id,item.asset,m_outlinerQuery);});
            c.roundRect(280,198,265,33,4,{0.12f,0.17f,0.22f,1});c.clip(287,198,251,33);
            c.text(*m_font,m_outlinerQuery.empty()?"Search placed actors...":m_outlinerQuery.c_str(),290,205,text);c.noClip();button(550,198,60,"Clear");
            for(size_t i=0;i<7&&m_placementFirst+i<markers.size();++i)
            {
                const auto& marker=markers[m_placementFirst+i];const float y=234+36*static_cast<float>(i);
                c.rect(280,y,330,33,marker.id==m_placementSelectedId?Color{0.28f,0.40f,0.52f,1}:Color{0.22f,0.22f,0.24f,1});
                c.clip(287,y,316,33);c.text(*m_font,(marker.label+" / "+marker.id).c_str(),290,y+8,text);c.noClip();
            }
            if(markers.empty())c.text(*m_font,total?"No matching actors.":"No constant linked actors.",290,246,muted);
            button(280,490,159,"Position XYZ");button(449,490,161,"Rotation XYZ");
            button(280,526,159,"Scale XYZ");button(449,526,161,"Instance name");
            c.text(*m_font,(std::to_string(markers.size())+" / "+std::to_string(total)+" placed actors").c_str(),290,568,muted);
        }
        else
        {
        const auto& assets=m_document.visualAssets();
        for(size_t i=0;i<9&&m_visualFirst+i<assets.size();++i)
        {
            const auto& a=assets[m_visualFirst+i];const float y=198+36*static_cast<float>(i);
            c.rect(280,y,330,33,a.id==m_visualSelection?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});
            c.clip(287,y,316,33);c.text(*m_font,((a.kind=="actor"?"ACTOR  ":a.kind=="theme"?"THEME  ":a.kind=="sequence"?"SEQUENCE  ":"UI  ")+a.title).c_str(),290,y+8,text);c.noClip();
        }
        if(assets.empty())c.text(*m_font,"Create a reusable asset.",290,210,muted);
        }
        const auto* selected=m_document.visualAsset(m_visualSelection);
        if(selected&&selected->kind=="actor"&&!m_visualPlacement){button(280,548,330,"Open placement plan");button(280,584,330,"Open actor editor / viewport");}
        if(selected&&selected->kind=="widget"&&!m_visualPlacement)button(280,548,330,"Open UI Designer");
        c.pop();
        if(m_visualPlacement)drawVisualPlacement(c);
        else if(selected)
        {
            button(630,156,175,"Rename");button(815,156,185,"Remove");
            c.clip(630,195,selected->kind=="actor"?180.f:370.f,32);c.text(*m_font,(selected->id+" / "+selected->title).c_str(),630,201,accent);c.noClip();
            if(selected->kind=="actor")button(815,193,185,"Create child class");
            if(selected->kind=="sequence"){button(630,232,370,"Open Sequencer");c.text(*m_font,"Place this asset from Scenes.",630,280,muted);}
            else if(selected->kind=="theme")
            {
                button(630,232,180,m_themeStyle.c_str());button(820,232,180,themeStates[m_themeState]);
                const size_t count=m_themeMedia?sizeof(themeMediaProps)/sizeof(themeMediaProps[0]):m_themeState?themeLookCount:sizeof(themeProps)/sizeof(themeProps[0]);
                for(size_t i=0;i<count;++i)
                    button(630+123.34f*static_cast<float>(i%3),277+36*static_cast<float>(i/3),119,m_themeMedia?themeMediaProps[i]:themeProps[i]);
                c.text(*m_font,"Colours: #RRGGBB or #RRGGBBAA",630,505,muted);
                c.text(*m_font,m_themeState?"State overrides appearance only.":"Normal also controls size and layout.",630,530,muted);
                button(630,548,370,m_themeMedia?"Back to style properties":"Fonts, images and sounds");
            }
            else if(selected->kind=="widget")
            {
                c.rect(630,232,370,34,{0.07f,0.10f,0.15f,1});
                c.clip(636,232,358,34);c.text(*m_font,m_visualText.empty()?"Next widget text (optional)":m_visualText.c_str(),638,239,muted);c.noClip();
                button(630,277,180,"Add text");button(820,277,180,"Add button");
                button(630,318,180,"Add progress");button(820,318,180,"Add toggle");
                c.text(*m_font,"Page structure (not live preview)",630,363,muted);
                JsonTree tree;
                if(tree.parse(selected->source.data(),selected->source.size()))
                {
                    size_t index=0;
                    for(auto item=tree.root().first();item.exists()&&index<5;item=item.next(),++index)
                    {
                        const float row=387+28*static_cast<float>(index);
                        const char* kind=item["label"].exists()?"label":item["button"].exists()?"button":item["progress"].exists()?"progress":item["toggle"].exists()?"toggle":"widget";
                        const std::string label=std::string(kind)+"  "+item[kind].text()+"  "+item["id"].text();
                        c.rect(630,row,370,25,{0.22f,0.22f,0.24f,1});
                        c.clip(635,row,360,25);c.text(*m_font,label.c_str(),638,row+4,text);c.noClip();
                    }
                    if(tree.root().count()>5)c.text(*m_font,("+"+std::to_string(tree.root().count()-5)+" more widgets").c_str(),630,528,muted);
                }
                button(630,510,370,selected->dependencies.empty()?"Assign theme (default)":selected->dependencies.front().c_str());
                button(630,548,180,"Copy JSON");button(820,548,180,"Link to screen");
            }
            else
            {
                button(630,232,370,m_visualInspector?"Back to component hierarchy":"Edit selected component properties");
                if(m_visualInspector)
                {
                    if(m_visualPhysics)
                    {
                        button(630,277,180,"Collider");button(820,277,180,"Mass");
                        button(630,318,180,"Friction");button(820,318,180,"Bounce");
                        button(630,359,180,"Gravity");button(820,359,180,"Animation");
                        c.text(*m_font,"box:X,Y,Z or sphere:radius",630,407,muted);
                        c.text(*m_font,"capsule:radius,height or none",630,432,muted);
                        c.text(*m_font,"Blank physics values use native defaults.",630,467,muted);
                        c.text(*m_font,"Animation: model clip name (looped).",630,492,muted);
                        button(630,548,370,"Transform and model properties");
                    }
                    else
                    {
                    button(630,277,180,"Name");button(820,277,180,"Position XYZ");
                    button(630,318,180,"Rotation XYZ");button(820,318,180,"Size XYZ");
                    button(630,359,180,"Model");button(820,359,180,"Behaviour");
                    JsonTree tree;std::string resolved,why;
                    if(m_document.visualActorSource(selected->id,resolved,why)&&tree.parse(resolved.data(),resolved.size()))
                    {
                        const auto item=tree.root()["entities"][static_cast<u32>(m_visualComponent)];
                        if(!selected->dependencies.empty())button(630,400,370,"Reset selected component overrides");
                        else {c.clip(630,401,370,28);c.text(*m_font,(std::string("Component: ")+item["name"].text()).c_str(),630,404,accent);c.noClip();}
                        const auto vec=[&](const char* prop,float fallback)
                        {
                            const auto v=item[prop];std::string value;
                            for(u32 i=0;i<3;++i){if(i)value+=", ";value+=std::to_string(v.isArray()?v[i].number(fallback):v.isNumber()?(std::string(prop)=="angles"?(i==1?v.number():0.f):v.number()):fallback);}
                            return std::string(prop)+": "+value;
                        };
                        for(u32 i=0;i<3;++i)
                        {
                            const auto value=vec(i==0?"place":i==1?"angles":"size",i==2?1.f:0.f);
                            c.clip(630,435+25*static_cast<float>(i),370,25);c.text(*m_font,value.c_str(),630,437+25*static_cast<float>(i),muted);c.noClip();
                        }
                        const auto model=std::string("Model: ")+item["model"].text("(none)");
                        const auto behaviour=std::string("Behaviour: ")+item["behaviour"].text("(none)");
                        c.clip(630,490,370,20);c.text(*m_font,model.c_str(),630,490,muted);c.noClip();
                        c.clip(630,510,370,20);c.text(*m_font,behaviour.c_str(),630,510,muted);c.noClip();
                        button(630,525,370,item["camera"].exists()?(std::string("Camera FOV: ")+std::to_string(item["camera"].number())).c_str():"Camera FOV (blank = off)");
                    }
                    button(630,558,370,"Physics and animation");
                    }
                }
                else
                {
                button(630,277,180,"Box");button(820,277,180,"Sphere");
                button(630,318,180,"Capsule");button(820,318,180,"Plane");
                JsonTree tree;std::string resolved,why;
                if(m_document.visualActorSource(selected->id,resolved,why)&&tree.parse(resolved.data(),resolved.size()))
                {
                    const auto entries=tree.root()["entities"],parents=tree.root()["parents"];
                    for(size_t i=0;i<4&&i+m_visualComponentFirst<entries.count();++i)
                    {
                        const auto index=static_cast<u32>(i+m_visualComponentFirst);
                        const auto item=entries[index];const float yy=366+33*static_cast<float>(i);
                        const auto label=std::to_string(index)+"  "+item["name"].text("Component")+" / "+item["shape"].text(item["camera"].exists()?"camera":"custom")+
                            (index?"  (parent "+std::to_string(static_cast<int>(parents[index].number()))+")":"  (root)");
                        c.rect(630,yy,370,31,index==m_visualComponent?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});
                        c.clip(635,yy,360,31);c.text(*m_font,label.c_str(),638,yy+6,text);c.noClip();
                    }
                }
                button(630,505,185,"Add child");button(820,505,180,"Add camera");
                button(630,548,180,"Class graphs");button(820,548,180,"Add to screen");
                }
            }
        }
        else c.text(*m_font,"Select a definition to edit.",630,210,muted);
        }
        const float statusY=wide?kBottom+5-modalY():actorClass?630.f:591.f;
        const float statusX=wide?12-modalX():280;
        c.clip(statusX,statusY,wide?kWidth-24:720,26);c.text(*m_font,m_status.c_str(),statusX,statusY+4,text);c.noClip();
        if(m_visualInput==7&&themeColorProperty(m_visualProperty))
        {
            if(!m_rgbOpen){const auto old=themePickerColor(m_visualText);startRGB(themePickerHex(old).substr(0,6),true,old.a);}
            drawRGB(c);
        }
        else if(m_visualInput==7&&themeMediaProperty(m_visualProperty))
        {
            c.roundRect(420,265,440,277,6,{0.08f,0.12f,0.18f,1});
            c.text(*m_font,m_visualProperty.c_str(),440,275,accent);
            c.clip(440,305,400,30);c.text(*m_font,m_visualText.empty()?"Default / enter asset: or global: path":m_visualText.c_str(),440,305,text);c.noClip();
            for(size_t i=0;i<5&&m_themeMediaFirst+i<m_themeMediaFiles.size();++i)
            {
                const float y=345+30.f*static_cast<float>(i);c.rect(440,y,400,28,{0.22f,0.22f,0.24f,1});
                c.clip(446,y,388,28);c.text(*m_font,m_themeMediaFiles[m_themeMediaFirst+i].c_str(),446,y+4,text);c.noClip();
            }
            if(m_themeMediaFiles.empty())c.text(*m_font,"No matching imported media on this screen.",440,365,muted);
            c.text(*m_font,"Enter = Apply",440,503,muted);c.text(*m_font,"Escape = Cancel",440,519,muted);c.roundRect(650,505,95,32,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",697.5f,510,text,Align::Center);c.roundRect(750,505,90,32,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",795,510,text,Align::Center);
        }
        else if(m_visualInput)
        {
            c.roundRect(420,286,440,130,6,{0.08f,0.12f,0.18f,1});
            c.text(*m_font,m_visualInput==1?"RENAME ASSET":m_visualInput==5?"THEME ASSET ID":m_visualInput==6?"STYLE NAME":m_visualInput>=3?m_visualProperty.c_str():"NEXT WIDGET TEXT",440,300,accent);
            c.clip(440,330,400,34);c.text(*m_font,m_visualText.c_str(),440,336,text);c.noClip();
            c.text(*m_font,"Enter = Apply",440,377,muted);c.text(*m_font,"Escape = Cancel",440,393,muted);c.roundRect(650,379,95,32,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",697.5f,384,text,Align::Center);c.roundRect(750,379,90,32,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",795,384,text,Align::Center);
        }
    }
    void BlueprintWorkspace::launchSceneEditor(Services& s)
    {
        const auto screen=assetScreen();
        if(screen.empty()||!ensureDraftAssets(s))
        {m_status="Save a named project and select its screen first.";return;}
        checkpoint(s,true);
        if(!blueprintPrepareSceneEditor(*s.platform,m_document.assetRoot,screen,m_status))return;
        s.platform->setEditorChrome(false);
        if(!s.screens->startSceneEditor(m_document.assetRoot,screen,m_status))
        {
            s.platform->setEditorChrome(true);
            s.input->setWantsPointer(true);s.input->setTyping(false);s.platform->lockMouse(false);
            return;
        }
        m_sceneReturnToPlacement=m_visualPlacement;
        m_sceneEditor=true;m_sceneExit=false;m_playing=true;
        m_left=s.input->raw().mouseButtons[0];
        m_status="Scene editor: Save your scene, then Escape to return.";
        NX_INFO("Blueprint Studio: native scene editor opened: %s",screen.c_str());
    }
    void BlueprintWorkspace::stopTone(Services& s)
    {
        if(s.audio&&m_toneVoice)s.audio->stop(m_toneVoice,0.02f);
        m_toneVoice={};
    }
    void BlueprintWorkspace::refreshTone()
    {
        const bool ok=m_clipSource.empty()?blueprintToneWave(m_tone,m_toneWav,m_status):
            blueprintEditAudioClip(m_audioClip,m_clipEdit,m_tone,m_toneWav,m_status);
        if(!ok)m_toneWav.clear();
    }
    void BlueprintWorkspace::updateTone(Services& s,float x,float y,bool click)
    {
        m_toneVoices=s.audio?s.audio->voicesInUse():0;
        if(!click)return;
        if(in(x,y,860,116,1000,150)){stopTone(s);m_audioEditor=false;m_status="Audio editor closed.";return;}
        if(in(x,y,550,116,650,150)){m_toneEffects=false;return;}
        if(in(x,y,650,116,750,150)){m_toneEffects=true;return;}
        if(in(x,y,750,116,850,150))
        {
            std::string relative;
            const bool saved=m_clipSource.empty()?blueprintSaveTonePreset(*s.platform,m_document.assetRoot,assetScreen(),m_tone,relative,m_status):
                blueprintSaveClipPreset(*s.platform,m_document.assetRoot,assetScreen(),m_clipSource,m_audioClip,m_clipEdit,m_tone,relative,m_status);
            if(saved&&
                blueprintListAssets(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles,m_status))
                m_status="Saved editable preset:"+relative;
            return;
        }
        bool changed=false;
        if(m_clipSource.empty()&&!m_toneEffects&&in(x,y,635,388,1000,410))
        {
            BlueprintDocument graph;const bool stream=x>=815;
            if(!blueprintAudioGraph(m_tone,stream,graph,m_status))return;
            std::string bytes;if(!graph.serialize(bytes,m_status))return;
            for(unsigned i=1;i<=10000;++i)
            {
                const auto file=std::string(stream?"audio-stream-":"audio-synthesis-")+std::to_string(i)+".bp.json";
                if(s.platform->fileTime(("own/"+file).c_str()))continue;
                if(writeVerified(s,"own/"+file,bytes))m_status="Saved "+file+"; open it via Open. Effects are separate graph nodes.";
                return;
            }
            m_status="No unused audio graph filename.";return;
        }
        if(m_clipSource.empty()&&!m_toneEffects&&in(x,y,280,388,445,410))
        {m_tone.independentStereo=!m_tone.independentStereo;m_tone.channels=2;m_toneRight=false;changed=true;}
        if(m_clipSource.empty()&&!m_toneEffects&&m_tone.independentStereo&&in(x,y,455,388,625,410))
        {m_toneRight=!m_toneRight;return;}
        if(in(x,y,830,497,1000,525)){m_tone.channels=m_tone.channels==1?2:1;changed=true;}
        if(m_toneEffects)
        {
            unsigned i=0;
            for(const auto& f:blueprintEffectControls)
            {
                const float xx=280.f+360.f*(i%2),yy=157.f+25.f*(i/2);
                if(in(x,y,xx+150,yy,xx+344,yy+24))
                {
                    m_tone.effects.*f.field=f.lo+(f.hi-f.lo)*std::clamp((x-xx-150)/194.f,0.f,1.f);
                    changed=true;
                }
                ++i;
            }
            if(in(x,y,430,382,624,407)){m_tone.tail=2*std::clamp((x-430)/194.f,0.f,1.f);changed=true;}
            if(in(x,y,640,382,1000,407)){m_tone.effects=BusEffectSettings{};changed=true;}
        }
        else
        {
        if(!m_clipSource.empty())
        {
            if(in(x,y,280,158,455,190)){m_clipEdit.reverse=!m_clipEdit.reverse;changed=true;}
            if(in(x,y,460,158,635,190)){m_clipEdit.normalize=!m_clipEdit.normalize;changed=true;}
            if(in(x,y,640,158,1000,190)){m_clipEdit=BlueprintClipEdit{};changed=true;}
            struct ClipControl{float* value;float lo,hi;};
            const ClipControl controls[]={{&m_clipEdit.start,0,0.99f},{&m_clipEdit.end,0.01f,1},
                {&m_clipEdit.gain,0,2},{&m_clipEdit.fadeIn,0,1},{&m_clipEdit.fadeOut,0,1}};
            for(unsigned i=0;i<5;++i)
            {
                const float xx=280.f+360.f*(i%2),yy=240.f+36.f*(i/2);
                if(in(x,y,xx+150,yy,xx+344,yy+30))
                {
                    *controls[i].value=controls[i].lo+(controls[i].hi-controls[i].lo)*std::clamp((x-xx-150)/194.f,0.f,1.f);
                    if(i==0)m_clipEdit.start=std::min(m_clipEdit.start,m_clipEdit.end-0.01f);
                    if(i==1)m_clipEdit.end=std::max(m_clipEdit.end,m_clipEdit.start+0.01f);
                    changed=true;
                }
            }
        }
        else if(in(x,y,280,158,1000,190))
        {
            const auto index=static_cast<unsigned>((x-280)/144);
            if(index<5){(m_toneRight&&m_tone.independentStereo?m_tone.rightWave:m_tone.wave)=index;changed=true;}
        }
        if(in(x,y,280,197,1000,229))
        {
            constexpr unsigned rates[]={11025,22050,44100,48000};
            const auto index=static_cast<unsigned>((x-280)/180);
            if(index<4){m_tone.rate=rates[index];changed=true;}
        }
        struct Control {float* value;float lo,hi;};
        const bool right=m_toneRight&&m_tone.independentStereo;
        const Control controls[]={{right?&m_tone.rightFrequency:&m_tone.frequency,20,4000},{right?&m_tone.rightEndFrequency:&m_tone.endFrequency,20,4000},
            {&m_tone.seconds,0.05f,4},{right?&m_tone.rightAmplitude:&m_tone.amplitude,0,1},{right?&m_tone.rightAttack:&m_tone.attack,0,0.33f},
            {right?&m_tone.rightDecay:&m_tone.decay,0,0.33f},{right?&m_tone.rightSustain:&m_tone.sustain,0,1},{right?&m_tone.rightRelease:&m_tone.release,0,0.33f}};
        if(m_clipSource.empty())for(unsigned i=0;i<8;++i)
        {
            const float xx=280.f+360.f*(i%2),yy=240.f+36.f*(i/2);
            if(in(x,y,xx+150,yy,xx+344,yy+30))
            {
                const float fraction=std::clamp((x-xx-150)/194.f,0.f,1.f);
                *controls[i].value=controls[i].lo+(controls[i].hi-controls[i].lo)*fraction;
                changed=true;
            }
        }
        }
        if(changed){stopTone(s);refreshTone();if(!m_toneWav.empty())m_status="Tone updated. Preview or save to the project.";return;}
        if(in(x,y,280,530,445,565))
        {
            stopTone(s);
            if(!s.audio||m_toneWav.empty()){m_status="Audio preview unavailable.";return;}
            if(s.audio->voicesInUse()>=Audio::kVoices){m_status="All Redux voices are in use.";return;}
            auto* sound=loadSound(reinterpret_cast<const u8*>(m_toneWav.data()),m_toneWav.size());
            if(!sound){m_status="Redux could not decode the generated WAV.";return;}
            PlayOptions options;options.volume=0.6f;options.priority=0;
            m_toneVoice=s.audio->playOwn(*sound,sound,[](void* owner){freeSound(static_cast<SoundData*>(owner));},options);
            if(!m_toneVoice){freeSound(sound);m_status="No preview voice available.";}
            else m_status="Previewing through Redux audio.";
        }
        else if(in(x,y,455,530,600,565)){stopTone(s);m_status="Preview stopped.";}
        else if(in(x,y,610,530,825,565))
        {
            std::string relative;
            const bool saved=m_clipSource.empty()?blueprintSaveTone(*s.platform,m_document.assetRoot,assetScreen(),m_tone,relative,m_status):
                blueprintSaveEditedAudio(*s.platform,m_document.assetRoot,assetScreen(),m_audioClip,m_clipEdit,m_tone,relative,m_status);
            if(saved&&blueprintListAssets(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles,m_status))
                m_status="Saved asset:"+relative;
        }
        else if(in(x,y,835,530,1000,565))
        {stopTone(s);m_tone=BlueprintTone{};m_clipEdit=BlueprintClipEdit{};if(!m_clipSource.empty())m_tone.channels=m_audioClip.channels;refreshTone();m_status="Audio settings reset.";}
    }
    void BlueprintWorkspace::drawTone(Canvas& c)
    {
        const Color selected={0.19f,0.33f,0.50f,1},normal={0.22f,0.22f,0.24f,1};
        const auto button=[&](float x,float y,float width,float height,const char* name,bool active)
        {
            c.roundRect(x,y,width,height,4,active?selected:normal);
            c.text(*m_font,name,x+width/2,y+6,text,Align::Center);
        };
        c.roundRect(260,100,760,525,9,{0.16f,0.16f,0.17f,1});
        c.text(*m_font,m_clipSource.empty()?"AUDIO SYNTHESIS":"AUDIO CLIP EDITOR",280,122,accent);
        button(550,116,96,34,m_clipSource.empty()?"Synth":"Clip",!m_toneEffects);button(650,116,96,34,"Effects",m_toneEffects);
        button(750,116,100,34,"Preset +",false);
        button(860,116,140,34,"Back",false);
        if(m_toneEffects)
        {
            unsigned i=0;
            for(const auto& f:blueprintEffectControls)
            {
                const float value=m_tone.effects.*f.field,x=280.f+360.f*(i%2),y=157.f+25.f*(i/2);
                char label[64];std::snprintf(label,sizeof(label),"%s %.2f",f.name,static_cast<double>(value));
                c.clip(x,y,148,24);c.text(*m_font,label,x,y+2,text);c.noClip();
                c.rect(x+150,y+8,194,7,normal);
                c.rect(x+150,y+8,194*(value-f.lo)/(f.hi-f.lo),7,accent);++i;
            }
            char tail[64];std::snprintf(tail,sizeof(tail),"Tail %.2f s",static_cast<double>(m_tone.tail));
            c.text(*m_font,tail,280,385,text);c.rect(430,390,194,7,normal);
            c.rect(430,390,97*m_tone.tail,7,accent);button(640,382,360,25,"Clear effects",false);
        }
        else
        {
        if(m_clipSource.empty())
        {
            const char* waves[]={"Sine","Triangle","Saw","Square","Noise"};
            for(unsigned i=0;i<5;++i)button(280.f+i*144,158,138,32,waves[i],(m_toneRight&&m_tone.independentStereo?m_tone.rightWave:m_tone.wave)==i);
        }
        else
        {
            button(280,158,175,32,"Reverse",m_clipEdit.reverse);
            button(460,158,175,32,"Normalize",m_clipEdit.normalize);
            button(640,158,360,32,"Reset clip edits",false);
        }
        const unsigned rates[]={11025,22050,44100,48000};
        for(unsigned i=0;i<4;++i)
            button(280.f+i*180,197,174,32,(std::to_string(rates[i])+" Hz").c_str(),m_tone.rate==rates[i]);
        struct Item{const char* name;float value,lo,hi;};
        const bool right=m_toneRight&&m_tone.independentStereo;
        const Item items[]={{"Start Hz",right?m_tone.rightFrequency:m_tone.frequency,20,4000},{"End Hz",right?m_tone.rightEndFrequency:m_tone.endFrequency,20,4000},
            {"Seconds",m_tone.seconds,0.05f,4},{"Gain",right?m_tone.rightAmplitude:m_tone.amplitude,0,1},
            {"Attack",right?m_tone.rightAttack:m_tone.attack,0,0.33f},{"Decay",right?m_tone.rightDecay:m_tone.decay,0,0.33f},
            {"Sustain",right?m_tone.rightSustain:m_tone.sustain,0,1},{"Release",right?m_tone.rightRelease:m_tone.release,0,0.33f}};
        if(m_clipSource.empty())for(unsigned i=0;i<8;++i)
        {
            const auto& item=items[i];const float x=280.f+360.f*(i%2),y=240.f+36.f*(i/2);
            char label[64];std::snprintf(label,sizeof(label),"%s %.2f",item.name,static_cast<double>(item.value));
            c.text(*m_font,label,x,y+6,text);
            c.rect(x+150,y+10,194,8,normal);
            c.rect(x+150,y+10,194*(item.value-item.lo)/(item.hi-item.lo),8,accent);
        }
        if(!m_clipSource.empty())
        {
            const Item edits[]={{"Start",m_clipEdit.start,0,0.99f},{"End",m_clipEdit.end,0.01f,1},
                {"Gain",m_clipEdit.gain,0,2},{"Fade in",m_clipEdit.fadeIn,0,1},{"Fade out",m_clipEdit.fadeOut,0,1}};
            for(unsigned i=0;i<5;++i)
            {
                const auto& item=edits[i];const float x=280.f+360.f*(i%2),y=240.f+36.f*(i/2);
                char label[64];std::snprintf(label,sizeof(label),"%s %.2f",item.name,static_cast<double>(item.value));
                c.text(*m_font,label,x,y+6,text);c.rect(x+150,y+10,194,8,normal);
                c.rect(x+150,y+10,194*(item.value-item.lo)/(item.hi-item.lo),8,accent);
            }
            c.clip(280,350,720,32);c.text(*m_font,("Source: "+m_clipSource).c_str(),280,356,muted);c.noClip();
        }
        }
        if(m_clipSource.empty()&&!m_toneEffects)
        {
            button(280,388,165,22,m_tone.independentStereo?"Dual oscillator":"Linked stereo",m_tone.independentStereo);
            if(m_tone.independentStereo)button(455,388,170,22,m_toneRight?"Editing right":"Editing left",true);
            button(635,388,175,22,"Sample graph",false);button(815,388,185,22,"Stream graph",false);
        }
        const float waveTop=m_toneEffects||m_clipSource.empty()?414.f:388.f;
        const float middle=(waveTop+492)*0.5f,half=(492-waveTop)*0.5f-4;
        c.rect(280,waveTop,720,492-waveTop,{0.09f,0.12f,0.16f,1});
        c.line(280,middle,1000,middle,1,muted);
        const size_t frames=m_toneWav.size()>44?(m_toneWav.size()-44)/(2*m_tone.channels):0;
        const auto sample=[&](size_t i)
        {
            const unsigned v=static_cast<unsigned char>(m_toneWav[44+2*i])|
                (static_cast<unsigned>(static_cast<unsigned char>(m_toneWav[45+2*i]))<<8);
            return static_cast<float>(v>=32768?static_cast<int>(v)-65536:static_cast<int>(v))/32768.f;
        };
        if(frames)for(size_t i=0;i<360;++i)
        {
            const size_t begin=i*frames/360,end=std::min(frames,std::max(begin+1,(i+1)*frames/360));
            float lo=1,hi=-1;
            for(size_t j=begin;j<end;++j)for(unsigned ch=0;ch<m_tone.channels;++ch)
            {const float v=sample(j*m_tone.channels+ch);lo=std::min(lo,v);hi=std::max(hi,v);}
            const float x=281.f+2.f*static_cast<float>(i);
            c.line(x,middle-hi*half,x,middle-lo*half,1,accent);
        }
        const auto budget="Xbox: "+std::to_string((frames*2*m_tone.channels+1023)/1024)+" KiB / 6144 KiB cache | "+
            std::to_string(m_toneVoices)+"/32 desktop voices";
        c.clip(280,497,545,28);c.text(*m_font,budget.c_str(),280,501,muted);c.noClip();
        button(830,497,170,28,m_tone.channels==1?"Mono / toggle":"Stereo / toggle",true);
        button(280,530,165,35,"Preview",true);button(455,530,145,35,"Stop",false);
        button(610,530,215,35,"Save WAV asset",true);button(835,530,165,35,"Reset",false);
        c.clip(280,575,720,42);c.text(*m_font,m_status.c_str(),280,580,text);c.noClip();
    }
    void BlueprintWorkspace::importAsset(Services& s,const std::string& file)
    {
        const auto screen=assetScreen();
        if(screen.empty()||!ensureDraftAssets(s))
        {m_status="Save as a named project and select a screen before importing assets.";return;}
        if(!blueprintImportAsset(*s.platform,m_document.assetRoot,screen,file,m_status,m_sharedMedia))return;
        const bool listed=m_sharedMedia?blueprintListSharedAssets(*s.platform,m_document.assetRoot,m_assetFiles,m_status):
            blueprintListAssets(*s.platform,m_document.assetRoot,screen,m_assetFiles,m_status);
        if(!listed)return;
        m_status=std::string("Imported ")+(m_sharedMedia?"global:":"asset:")+blueprintAssetImportPath(file);
    }
    bool BlueprintWorkspace::ensureDraftAssets(Services& s)
    {
        if(!m_document.assetRoot.empty())return true;
        std::string root;if(!blueprintCreateAssetRoot(*s.platform,root,m_status))return false;
        m_document.assetRoot=root;m_document.markRecovered();checkpoint(s,true);return true;
    }
    bool BlueprintWorkspace::preserveBeforeSwitch(Services& s)
    {
        if(!m_document.dirty())return true;
        if(!m_filePath.empty()&&saveCurrent(s))return true;
        // Never discard an unnamed or externally changed project when switching.
        std::string bytes;if(!m_document.serialize(bytes,m_status))return false;
        static unsigned sequence=0;for(unsigned i=0;i<100;++i)
        {
            const auto file="own/blueprint-recovery-conflict-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence)+".bak";
            if(!s.platform->fileTime(file.c_str()))
            {
                if(!writeVerified(s,file,bytes)){m_notice=m_status;return false;}
                m_notice="Previous unsaved work was preserved. Reopen the recovery archive from Projects: "+file.substr(4);return true;
            }
        }
        m_status="Could not preserve current draft. Project remains open.";m_notice=m_status;return false;
    }
    bool BlueprintWorkspace::saveNamed(Services& s,const std::string& name)
    {
        std::string file;
        if(!blueprintManagedName(name,file))
        {m_status="Use 1-64 letters, numbers, spaces, underscores or hyphens.";return false;}
        const std::string path="own/"+file;
        if(path==m_filePath)return saveCurrent(s);
        if(s.platform->fileTime(path.c_str()))
        {m_status="Name already exists. Choose a different name.";return false;}
        // Title is file metadata, not a graph edit. Restore it if writing fails.
        const std::string old=m_document.name,oldRoot=m_document.assetRoot;
        std::string newRoot;
        if(!blueprintCreateAssetRoot(*s.platform,newRoot,m_status))return false;
        for(const auto& screen:m_document.screens())
            if(!blueprintCopyAssets(*s.platform,oldRoot,newRoot,screen,m_status))return false;
        if(!blueprintCopySharedAssets(*s.platform,oldRoot,newRoot,m_status))return false;
        m_document.name=name;m_document.assetRoot=newRoot;
        std::string bytes;
        if(!m_document.serialize(bytes,m_status)||!writeVerified(s,path,bytes))
        {m_document.name=old;m_document.assetRoot=oldRoot;return false;}
        m_filePath=path;m_fileBaseline=std::move(bytes);
        m_document.markSaved();checkpoint(s,true);
        m_status="Saved "+file;return true;
    }
    bool BlueprintWorkspace::saveCurrent(Services& s)
    {
        if(m_filePath.empty())
        {
            m_edit=m_document.name=="Untitled"?"":m_document.name;
            m_focus=5;s.input->setTyping(true);
            m_status="Type a new Blueprint name and press Enter.";return false;
        }
        FileData current;
        if(!s.platform->readFile(m_filePath.c_str(),current,MemTag::General)||
            current.size!=m_fileBaseline.size()||std::memcmp(current.data,m_fileBaseline.data(),current.size)!=0)
        {
            m_status="File changed outside Studio. Use Projects > Save copy to preserve both versions.";m_notice=m_status;
            NX_WARN("Blueprint Studio: %s",m_status.c_str());return false;
        }
        if(!m_document.dirty()){m_status="Already saved.";return true;}
        std::string bytes;
        if(!m_document.serialize(bytes,m_status)||!writeVerified(s,m_filePath,bytes)){m_notice=m_status;return false;}
        m_fileBaseline=std::move(bytes);m_document.markSaved();checkpoint(s,true);
        m_status="Saved "+m_document.name;return true;
    }

    void BlueprintWorkspace::focusDiagnostic(BlueprintLocation location)
    {
        m_diagnostic={};
        if(location.graph>=m_document.graphs.size()||location.node>=m_document.graphs[location.graph].nodes.size())return;
        selectGraph(location.graph);m_selection=location.node;m_diagnostic=location;
        const auto& n=m_document.graphs[m_graph].nodes[m_selection];
        m_zoom=1;m_x=(kRight-kLeft-m_rows[m_selection].width)*0.5f-n.x;
        m_y=(kBottom-kTop-m_rows[m_selection].height)*0.5f-n.y;
    }

    void BlueprintWorkspace::focusPreviewError(const std::string& error)
    {
        for(const auto& screen:m_previewOutputs)
        {
            const size_t line=blueprintErrorLine(error,screen.id+"/main.as");
            if(line){focusDiagnostic(blueprintSourceLocation(screen.map,line));return;}
        }
    }

    bool BlueprintWorkspace::validateProject(Services& s,const std::vector<BlueprintScreenOutput>& screens)
    {
        for(const auto& screen:screens)
        {
            std::string error;
            const bool valid=s.screens->validateBlueprintSource(screen.source,error);
            if(!valid)
            {
                m_status=screen.id+": "+error;
                focusDiagnostic(blueprintSourceLocation(screen.map,blueprintErrorLine(m_status,"blueprint.as")));
            }
            if(!valid)return false;
        }
        return true;
    }

    // Explicit integrity pass: source media readability and direct known local
    // asset references, not a residency estimate or a promise of Xbox fit.
    bool BlueprintWorkspace::auditTargetAssets(Services& s,std::string& summary)
    {
        auto& platform=*s.platform;
        u64 bytes=0;size_t count=0,missing=0,dynamic=0;
        std::string firstMissing,error;
        const auto& root=m_document.assetRoot;
        const auto readAssets=[&](const std::string& base,const std::vector<std::string>& files)->bool
        {
            for(const auto& file:files)
            {
                FileData data;
                if(!platform.readFile((base+file).c_str(),data,MemTag::General))
                {summary="Unreadable project asset: "+file;return false;}
                bytes+=data.size;++count;
            }
            return true;
        };
        for(const auto& screen:m_document.screens())
        {
            std::vector<std::string> files;
            if(!blueprintListAssets(platform,root,screen,files,error)){summary=error;return false;}
            const auto base="own/"+root+"/"+screen+"/asset/";
            if(!readAssets(base,files))return false;
        }
        if(!root.empty())
        {
            std::vector<std::string> shared;
            if(!blueprintListSharedAssets(platform,root,shared,error)){summary=error;return false;}
            if(!readAssets("own/"+root+"/_global/",shared))return false;
        }
        for(const auto& graph:m_document.graphs)
        {
            // Behaviour graphs can run in multiple screens. Their scope and
            // dynamically computed paths need runtime dependency analysis.
            if(graph.owner.rfind("behaviour:",0)==0)continue;
            for(size_t n=0;n<graph.nodes.size();++n)
            {
                const auto& node=graph.nodes[n];
                const auto& kind=node.kind;
                const bool known=kind=="api:Assets:Assets::texture:string"||kind=="api:Assets:Assets::model:string"||
                    kind=="api:Assets:Assets::sound:string"||kind=="api:Assets:Assets::video:string"||
                    kind=="api:Assets:Assets::font:string,uint"||kind=="api:Assets:Assets::imageFont:string,string,uint,uint"||
                    kind=="api:Assets:Assets::effect:string"||kind=="api:Ui:Ui::load:string,Widget@"||
                    kind=="api:Space:Space::load:string,Vec3,bool";
                if(!known)continue;
                std::string name;
                const auto wire=std::find_if(graph.links.begin(),graph.links.end(),[&](const BlueprintDocument::Link& link){return link.to==n&&link.input=="name";});
                if(wire!=graph.links.end())
                {
                    if(wire->from>=graph.nodes.size()||graph.nodes[wire->from].kind!="literal:string"){++dynamic;continue;}
                    name=graph.nodes[wire->from].ref;
                }
                else for(const auto& value:node.values)if(value.pin=="name")name=value.value;
                // Local assets can be resolved exactly. Global assets may be
                // provided by the installed Redux media or cooked alternatives.
                if(name.rfind("asset:",0)!=0){++dynamic;continue;}
                const auto relative=name.substr(6);
                if(relative.empty()||relative.find("..")!=std::string::npos||relative.find_first_of("\\\\:")!=std::string::npos){++dynamic;continue;}
                if(root.empty()||!platform.fileTime(("own/"+root+"/"+graph.owner+"/asset/"+relative).c_str()))
                {if(firstMissing.empty())firstMissing=graph.owner+": "+name;++missing;}
            }
        }
        char media[128];std::snprintf(media,sizeof(media),"%zu source files / %.2f MiB on disk (not resident RAM)",count,static_cast<double>(bytes)/1048576.0);
        summary=media;
        if(missing)summary+=" | "+std::to_string(missing)+" unresolved local references; first: "+firstMissing;
        if(dynamic)summary+=" | "+std::to_string(dynamic)+" dynamic/global references unverified";
        return true;
    }
    bool BlueprintWorkspace::compileAndExport(Services& s)
    {
        std::vector<BlueprintScreenOutput> screens;BlueprintLocation location;m_diagnostic={};
        if(!compileBlueprintProject(m_document,screens,m_status,&location)){focusDiagnostic(location);return false;}
        if(!validateProject(s,screens))return false;
        static unsigned sequence=0;
        std::string root;
        for(unsigned attempt=0;attempt<100;++attempt)
        {
            const auto folder="bp-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence);
            if(s.platform->createOwnFolder(folder.c_str())){root=folder;break;}
        }
        if(root.empty()){m_status="Cannot create a new export folder.";return false;}
        // Export every script before any screen manifests; install only the complete folder.
        for(const auto& screen:screens)
        {
            const auto folder=root+"/"+screen.id;
            if(!s.platform->createOwnFolder(folder.c_str())||!writeVerified(s,"own/"+folder+"/main.as",screen.source))
            {m_status="Incomplete project export. Do not install own/"+root;return false;}
        }
        for(const auto& screen:screens)
            if(!blueprintCopyAssets(*s.platform,m_document.assetRoot,root,screen.id,m_status))
            {m_status="Incomplete export: "+m_status;return false;}
        if(!blueprintCopySharedAssets(*s.platform,m_document.assetRoot,root,m_status))
        {m_status="Incomplete export: "+m_status;return false;}
        for(const auto& screen:screens)
            if(!writeVerified(s,"own/"+root+"/"+screen.id+"/screen.json",screen.manifest))
            {m_status="Incomplete project export. Do not install own/"+root;return false;}
        m_deviceExport="own/"+root;
        m_status="Exported own/"+root+" | Install into a separate project folder.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());return true;
    }

    bool BlueprintWorkspace::startTrace(Services& s)
    {
        stopTrace();m_liveTraceClock=0;m_traceHits.clear();m_traceValues.clear();m_traceLocation={};m_traceBudget=128;m_traceSelected=m_traceFirst=m_traceDetailFirst=0;m_traceShowStack=false;
        if(!m_traceEnabled)return true;
        if(!s.debugger||!s.debugger->startLocalTrace(*s.host,[](void* user,const char* section,int line,u32 run,asIScriptContext* context)
        {static_cast<BlueprintWorkspace*>(user)->traceLine(section,line,run,context);},this))
        {m_status="Local trace unavailable while the external script debugger is active.";return false;}
        m_traceDebugger=s.debugger;return true;
    }

    void BlueprintWorkspace::stopTrace()
    {
        if(m_traceDebugger)m_traceDebugger->stopLocalTrace();
        m_traceDebugger=nullptr;m_liveTraceView=m_liveTraceKey=false;m_tracePaused=false;m_tracePending=false;m_traceStep=false;m_traceKey=false;
    }

    void BlueprintWorkspace::traceLine(const char* section,int line,unsigned run,asIScriptContext* context)
    {
        if(!m_traceDebugger||m_tracePaused||m_tracePending||line<=0)return;
        BlueprintLocation location;
        for(const auto& screen:m_previewOutputs)
            if(strncmp(section,screen.id.c_str(),screen.id.size())==0&&strcmp(section+screen.id.size(),"/main.as")==0)
            {location=blueprintSourceLocation(screen.map,static_cast<size_t>(line));break;}
        if(location.graph>=m_document.graphs.size()||location.node>=m_document.graphs[location.graph].nodes.size())return;
        const auto& node=m_document.graphs[location.graph].nodes[location.node];
        const bool firstWatch=node.watch||m_traceStep;
        if(!firstWatch&&!m_traceBudget)return;
        if(!m_traceHits.empty())
        {
            const auto& last=m_traceHits.back();
            if(!firstWatch&&last.location.graph==location.graph&&last.location.node==location.node&&last.run==run&&last.line==line)return;
        }
        if(m_traceBudget)--m_traceBudget;
        if(m_traceHits.size()>=32)m_traceHits.erase(m_traceHits.begin());
        TraceHit hit{location,line,run};hit.time=m_liveTraceClock;
        m_traceDebugger->traceValues(context,hit.values);m_traceDebugger->traceStack(context,hit.stack);
        m_traceHits.push_back(std::move(hit));
        // Snapshot first watched statement before it executes. Otherwise keep the last sampled node.
        if(!m_tracePending)
        {
            m_traceLocation=location;m_traceValues=m_traceHits.back().values;
            if(firstWatch)m_tracePending=true;
        }
    }

    void BlueprintWorkspace::focusTraceNode()
    {
        focusDiagnostic(m_traceLocation);m_diagnostic={};
        if(m_traceLocation.graph==m_graph&&m_traceLocation.node<m_document.graphs[m_graph].nodes.size())
            m_y=80.f-m_document.graphs[m_graph].nodes[m_traceLocation.node].y*m_zoom;
    }

    void BlueprintWorkspace::pauseTrace(Services& s)
    {
        m_tracePaused=true;m_tracePending=false;m_traceStep=false;
        m_traceSelected=m_traceHits.empty()?0:m_traceHits.size()-1;m_traceFirst=m_traceHits.size()>8?m_traceHits.size()-8:0;m_traceDetailFirst=0;
        m_tracePointer=s.input->wantsPointer();m_traceTyping=s.input->typing();
        s.screens->setDevelopmentPaused(true);
        s.input->setWantsPointer(true);s.input->setTyping(false);s.platform->lockMouse(false);s.input->consume();
        focusTraceNode();
        m_left=s.input->raw().mouseButtons[0];
        m_status="Trace paused at a safe update boundary. Values were sampled before the highlighted statement.";
    }

    void BlueprintWorkspace::resumeTrace(Services& s,bool step)
    {
        m_tracePaused=false;m_tracePending=false;m_traceStep=step;m_traceBudget=128;
        if(step){m_traceLocation={};m_traceValues.clear();}
        s.input->setWantsPointer(m_tracePointer);s.input->setTyping(m_traceTyping);
        s.input->consume();s.screens->setDevelopmentPaused(false);
    }

    bool BlueprintWorkspace::launchCompiledPreview(Services& s,const std::string& owner)
    {
        std::vector<Screens::PreviewScreen> screens;
        for(const auto& output:m_previewOutputs)
        {
            Screens::PreviewScreen item;
            snprintf(item.info.id,sizeof(item.info.id),"%s",output.id.c_str());
            snprintf(item.info.title,sizeof(item.info.title),"%s",output.id.c_str());
            item.source=output.source;item.manifest=output.manifest;screens.push_back(std::move(item));
        }
        if(!startTrace(s)){focusPreviewError(m_status);return false;}
        s.platform->setEditorChrome(false);
        if(!s.screens->startProjectPreview(screens,owner,m_status,m_document.assetRoot))
        {
            s.platform->setEditorChrome(true);stopTrace();focusPreviewError(m_status);
            s.input->setWantsPointer(true);s.input->setTyping(false);return false;
        }
        m_playing=true;m_playPaused=false;m_previewStart=owner;m_profileNext=0;m_profileRows.clear();clearWire();
        m_status="Playing "+owner+". Ctrl+P pauses; Ctrl+R restarts; Ctrl+O profiler; Escape stops.";
        NX_INFO("Blueprint Studio: preview started: %s",owner.c_str());return true;
    }
    void BlueprintWorkspace::setPlayPaused(Services& s,bool paused)
    {
        if(m_playPaused==paused)return;
        if(paused){m_playPointer=s.input->wantsPointer();m_playTyping=s.input->typing();s.input->setWantsPointer(true);s.input->setTyping(false);s.platform->lockMouse(false);}
        else {s.input->setWantsPointer(m_playPointer);s.input->setTyping(m_playTyping);}
        m_playPaused=paused;s.screens->setDevelopmentPaused(paused);s.input->consume();
        m_left=s.input->raw().mouseButtons[0];
    }
    void BlueprintWorkspace::stopPreview(Services& s)
    {
        if(!m_playing)return;
        const bool scene=m_sceneEditor;
        stopTrace();
        s.screens->stopPreview();
        m_playing=false;m_sceneEditor=false;m_sceneExit=false;m_playPaused=false;m_playPauseKey=m_playStepKey=m_playRestartKey=false;m_profileKey=false;m_profileRows.clear();
        s.platform->setEditorChrome(true);
        s.input->setWantsPointer(true);
        s.input->setTyping(false);
        s.platform->lockMouse(false);
        s.input->consume();
        m_left=s.input->raw().mouseButtons[0];
        if(scene)
        {
            if(m_sceneReturnToPlacement)
            {
                releasePlacementPreview();
                m_assetBrowser=m_assetLibrary=m_visualPlacement=true;
                m_widgetDesigner=false;
                m_status="Returned to scene placement. Saved native scene files are ready.";
                NX_INFO("Blueprint Studio: restored scene placement tab");
            }
            else
            {
                browseAssets(s);
                if(m_assetBrowser)m_status="Returned from Scene editor. Saved scene files are ready.";
            }
            m_sceneReturnToPlacement=false;
        }
        else
        {
            m_status="Preview stopped. Graph and original save unchanged.";
            NX_INFO("Blueprint Studio: preview stopped");
        }
    }

    void BlueprintWorkspace::updatePreview(Services& s,float dt)
    {
        if(!m_playing)return;
        m_liveTraceClock+=std::max(0.f,dt);
        if(m_sceneEditor)
        {
            auto& input=*s.input;const auto& raw=input.raw();
            const bool left=raw.mouseButtons[0],click=left&&!m_left;m_left=left;
            if(m_sceneExit)
            {
                const auto& canvas=*s.canvas;
                const float scale=std::min(canvas.pixelWidth()/kBaseWidth,canvas.pixelHeight()/kBaseHeight);
                const float x=scale>0?(raw.mouseX-(canvas.pixelWidth()-kBaseWidth*scale)*0.5f)/scale:0;
                const float y=scale>0?(raw.mouseY-(canvas.pixelHeight()-kBaseHeight*scale)*0.5f)/scale:0;
                if(input.pressed(Action::Accept)||(click&&in(x,y,650,392,960,442)))
                {stopPreview(s);return;}
                if(input.pressed(Action::Menu)||input.pressed(Action::Cancel)||(click&&in(x,y,320,392,630,442)))
                {m_sceneExit=false;input.consume();}
                return;
            }
            if(input.pressed(Action::Menu)){m_sceneExit=true;input.consume();return;}
        }
        else if(s.input->pressed(Action::Menu)||s.input->raw().keys[static_cast<size_t>(Key::Escape)])
        {stopPreview(s);return;}
        if(!m_sceneEditor&&!m_tracePaused)
        {
            const auto& raw=s.input->raw();const bool ctrl=raw.keys[static_cast<size_t>(Key::Control)];
            const bool o=ctrl&&raw.keys[static_cast<size_t>(Key::O)];if(o&&!m_profileKey){m_profileOpen=!m_profileOpen;m_profileNext=0;}m_profileKey=o;
            const bool p=ctrl&&raw.keys[static_cast<size_t>(Key::P)],n=ctrl&&raw.keys[static_cast<size_t>(Key::N)],r=ctrl&&raw.keys[static_cast<size_t>(Key::R)];
            const bool pause=p&&!m_playPauseKey,step=n&&!m_playStepKey,restart=r&&!m_playRestartKey;
            m_playPauseKey=p;m_playStepKey=n;m_playRestartKey=r;
            bool resumeClick=false,stepClick=false,restartClick=false,stopClick=false;
            if(m_playPaused)
            {
                const bool left=raw.mouseButtons[0],click=left&&!m_left;m_left=left;
                const auto& canvas=*s.canvas;const float scale=std::min(canvas.pixelWidth()/kBaseWidth,canvas.pixelHeight()/kBaseHeight);
                const float x=scale>0?(raw.mouseX-(canvas.pixelWidth()-kBaseWidth*scale)*0.5f)/scale:0;
                const float y=scale>0?(raw.mouseY-(canvas.pixelHeight()-kBaseHeight*scale)*0.5f)/scale:0;
                resumeClick=click&&in(x,y,220,624,416,660);stepClick=click&&in(x,y,428,624,624,660);
                restartClick=click&&in(x,y,636,624,832,660);stopClick=click&&in(x,y,844,624,1040,660);
            }
            if(stopClick){stopPreview(s);return;}
            if(restart||restartClick){const auto owner=m_previewStart;stopPreview(s);m_playRestartKey=r;launchCompiledPreview(s,owner);return;}
            if(pause||resumeClick){setPlayPaused(s,!m_playPaused);return;}
            if(m_playPaused)
            {
                if(step||stepClick){setPlayPaused(s,false);s.screens->update(dt);setPlayPaused(s,true);if(s.screens->failed()){const std::string error=s.screens->error();stopPreview(s);m_status="Preview error: "+error;focusPreviewError(error);}}
                return;
            }
        }
        if(!m_sceneEditor&&m_traceDebugger)
        {
            const auto& raw=s.input->raw();
            const bool live=raw.keys[static_cast<size_t>(Key::Control)]&&raw.keys[static_cast<size_t>(Key::L)];if(live&&!m_liveTraceKey)m_liveTraceView=!m_liveTraceView;m_liveTraceKey=live;
            const bool key=raw.keys[static_cast<size_t>(Key::Control)]&&raw.keys[static_cast<size_t>(Key::T)];
            const bool toggle=key&&!m_traceKey;m_traceKey=key;
            if(m_tracePaused)
            {
                const bool left=raw.mouseButtons[0],click=left&&!m_left;m_left=left;
                const auto& canvas=*s.canvas;const float scale=std::min(canvas.pixelWidth()/kBaseWidth,canvas.pixelHeight()/kBaseHeight);
                const float x=scale>0?(raw.mouseX-(canvas.pixelWidth()-kBaseWidth*scale)*0.5f)/scale:0;
                const float y=scale>0?(raw.mouseY-(canvas.pixelHeight()-kBaseHeight*scale)*0.5f)/scale:0;
                const float wheel=s.input->wheel();
                if(in(x,y,24,415,604,600))
                {
                    if(wheel<0&&m_traceFirst+8<m_traceHits.size())++m_traceFirst;
                    if(wheel>0&&m_traceFirst)--m_traceFirst;
                    if(click&&y>=424)
                    {
                        const auto i=m_traceFirst+static_cast<size_t>((y-424)/22);
                        if(i<m_traceHits.size()){m_traceSelected=i;m_traceDetailFirst=0;m_traceLocation=m_traceHits[i].location;focusTraceNode();}
                    }
                }
                if(click&&in(x,y,620,382,825,414)){m_traceShowStack=false;m_traceDetailFirst=0;}
                if(click&&in(x,y,832,382,1037,414)){m_traceShowStack=true;m_traceDetailFirst=0;}
                if(in(x,y,620,424,1256,600)&&m_traceSelected<m_traceHits.size())
                {
                    const auto& hit=m_traceHits[m_traceSelected];const auto& details=m_traceShowStack?hit.stack:hit.values;
                    if(wheel<0&&m_traceDetailFirst+8<details.size())++m_traceDetailFirst;
                    if(wheel>0&&m_traceDetailFirst)--m_traceDetailFirst;
                }
                if(click&&in(x,y,1044,382,1256,414))focusTraceNode();
                if(toggle||s.input->pressed(Action::Accept)||(click&&in(x,y,270,624,450,660)))resumeTrace(s,false);
                else if(click&&in(x,y,462,624,670,660))resumeTrace(s,true);
                else if(click&&in(x,y,682,624,890,660))stopPreview(s);
                return;
            }
            if(toggle||m_tracePending){pauseTrace(s);return;}
            m_traceBudget=128;
        }
        const double began=s.platform->seconds();const auto runs=s.host->runs();
        s.screens->update(dt);
        if(!m_sceneEditor&&m_profileOpen)samplePreviewProfile(s,(s.platform->seconds()-began)*1000.0,static_cast<unsigned>(s.host->runs()-runs));
        if(m_traceStep){m_traceStep=false;m_tracePending=true;}
        if(s.screens->failed())
        {
            const std::string error=s.screens->error();const bool scene=m_sceneEditor;
            stopPreview(s);
            m_status=(scene?"Scene editor error: ":"Preview error: ")+error;
            if(!scene)focusPreviewError(error);
            NX_ERROR("Blueprint Studio: %s",m_status.c_str());
        }
    }

    void BlueprintWorkspace::samplePreviewProfile(Services& s,double updateMs,unsigned scriptRuns)
    {
        const double now=s.platform->seconds();if(now<m_profileNext)return;m_profileNext=now+0.25;
        m_profileRows.clear();char line[160];
        const auto mib=[](size_t bytes){return static_cast<double>(bytes)/(1024.0*1024.0);};
        const auto mem=memStatsTotal();
        std::snprintf(line,sizeof(line),"Host FPS %.1f | draws %u",static_cast<double>(s.fps),static_cast<unsigned>(s.draws));m_profileRows.emplace_back(line);
        std::snprintf(line,sizeof(line),"Last update %.2f ms | script calls %u",updateMs,scriptRuns);m_profileRows.emplace_back(line);
        std::snprintf(line,sizeof(line),"Tracked RAM %.2f MiB | peak %.2f",mib(mem.bytes),mib(mem.peak));m_profileRows.emplace_back(line);
        if(m_document.target().xbox()){std::snprintf(line,sizeof(line),"%u MiB reference: %.1f%% (not Xbox usage)",m_document.target().ram,100.0*mib(mem.bytes)/m_document.target().ram);m_profileRows.emplace_back(line);}
        std::snprintf(line,sizeof(line),"Host free RAM %.1f MiB",mib(s.platform->freeMemory()));m_profileRows.emplace_back(line);
        std::snprintf(line,sizeof(line),"Texture %.2f | buffers %.2f MiB",mib(memStats(MemTag::Texture).bytes),mib(memStats(MemTag::Buffer).bytes));m_profileRows.emplace_back(line);
        std::snprintf(line,sizeof(line),"Scene %.2f | script %.2f MiB",mib(memStats(MemTag::Scene).bytes),mib(memStats(MemTag::Script).bytes));m_profileRows.emplace_back(line);
        std::snprintf(line,sizeof(line),"Audio %.2f MiB | voices %u",mib(memStats(MemTag::Audio).bytes),static_cast<unsigned>(s.audio->voicesInUse()));m_profileRows.emplace_back(line);
        if(s.world){const auto& stats=s.world->stats();std::snprintf(line,sizeof(line),"Entities %u | contacts %u",static_cast<unsigned>(stats.entities),static_cast<unsigned>(stats.contacts));m_profileRows.emplace_back(line);}
    }
    void BlueprintWorkspace::drawPreviewOverlay(Canvas& c)
    {
        if(liveTraceView()&&m_font){c.design(kBaseWidth,kBaseHeight);c.noClip();c.roundRect(340,638,600,36,5,{0.10f,0.14f,0.20f,0.95f});c.text(*m_font,"LIVE TRACE | Ctrl+L game view | Ctrl+T pause | Esc stop",640,646,text,Align::Center);return;}
        if(m_profileOpen&&!m_sceneEditor&&!tracePaused()&&!playPaused()&&m_font)
        {
            c.design(kBaseWidth,kBaseHeight);c.noClip();c.roundRect(16,16,490,268,7,{0.10f,0.14f,0.20f,0.94f});
            c.text(*m_font,"PREVIEW PROFILER / DESKTOP SAMPLE",28,25,accent);
            for(size_t i=0;i<m_profileRows.size();++i)c.text(*m_font,m_profileRows[i].c_str(),28,53+22.f*static_cast<float>(i),text);
            c.text(*m_font,"Ctrl+O hide | includes engine/editor overhead",28,258,muted);
        }
        if(tracePaused()&&m_font)
        {
            c.design(kBaseWidth,kBaseHeight);c.noClip();
            c.roundRect(12,348,1256,324,8,{0.10f,0.14f,0.20f,0.98f});
            c.text(*m_font,"EXECUTION TRACE / FRAME-BOUNDARY PAUSE",24,358,accent);
            c.text(*m_font,"Historical samples, not live VM state",24,386,muted);
            c.text(*m_font,"Recent statements (scroll and select)",24,407,muted);
            auto tab=[&](float x,float w,const char* label,bool selected){c.roundRect(x,382,w,32,4,selected?Color{0.19f,0.33f,0.50f,1}:Color{0.16f,0.21f,0.28f,1});c.text(*m_font,label,x+w*0.5f,388,text,Align::Center);};
            tab(620,205,"Sampled values",!m_traceShowStack);tab(832,205,"Call stack",m_traceShowStack);tab(1044,212,"Focus node",false);
            c.clip(24,424,580,176);
            for(size_t i=m_traceFirst;i<std::min(m_traceFirst+8,m_traceHits.size());++i)
            {
                const auto& hit=m_traceHits[i];const auto& graph=m_document.graphs[hit.location.graph];const float y=424+22*static_cast<float>(i-m_traceFirst);
                if(i==m_traceSelected)c.rect(24,y,580,22,{0.19f,0.33f,0.50f,1});
                c.text(*m_font,shorten(graph.name+" / "+title(graph.nodes[hit.location.node])+" : "+std::to_string(hit.line),64).c_str(),30,y+2,text);
            }
            c.noClip();c.clip(620,424,636,176);
            if(m_traceSelected<m_traceHits.size())
            {
                const auto& hit=m_traceHits[m_traceSelected];const auto& details=m_traceShowStack?hit.stack:hit.values;
                for(size_t i=m_traceDetailFirst;i<std::min(m_traceDetailFirst+8,details.size());++i)c.text(*m_font,shorten(details[i],72).c_str(),626,424+22*static_cast<float>(i-m_traceDetailFirst),text);
                if(details.empty())c.text(*m_font,"No sampled details",626,424,muted);
            }
            c.noClip();
            for(float x:{270.f,462.f,682.f})c.roundRect(x,624,x==270.f?180.f:208.f,36,4,{0.19f,0.33f,0.50f,1});
            c.text(*m_font,"Continue",360,631,text,Align::Center);c.text(*m_font,"Step update",566,631,text,Align::Center);c.text(*m_font,"Stop preview",786,631,text,Align::Center);
            return;
        }
        if(playPaused()&&m_font)
        {
            c.design(kBaseWidth,kBaseHeight);c.noClip();
            c.roundRect(192,565,876,106,8,{0.10f,0.14f,0.20f,0.98f});
            c.text(*m_font,"PLAY TEST PAUSED / simulation halted",220,578,accent);
            c.text(*m_font,"Ctrl+P resume | Ctrl+N one update | Ctrl+R restart | Esc stop",220,601,muted);
            const char* labels[]={"Resume","Step update","Restart","Stop"};
            for(unsigned i=0;i<4;++i){const float x=220.f+208.f*static_cast<float>(i);c.roundRect(x,624,196,36,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,labels[i],x+98,631,text,Align::Center);}
            return;
        }
        if(!m_sceneEditor||!m_sceneExit||!m_font)return;
        c.design(kBaseWidth,kBaseHeight);c.noClip();
        c.rect(0,0,kBaseWidth,kBaseHeight,{0,0,0,0.72f});
        c.roundRect(300,250,680,220,9,{0.16f,0.16f,0.17f,1});
        c.text(*m_font,"Return to Blueprint Studio?",640,276,text,Align::Center);
        c.paragraph(*m_font,"Only scenes saved using the original editor's Save button are kept. Unsaved scene edits will be discarded.",320,316,640,muted);
        c.roundRect(320,392,310,50,6,{0.19f,0.33f,0.50f,1});
        c.text(*m_font,"Keep editing (Esc / B)",475,407,text,Align::Center);
        c.roundRect(650,392,310,50,6,{0.32f,0.19f,0.23f,1});
        c.text(*m_font,"Return (Enter / A)",805,407,text,Align::Center);
    }

    void BlueprintWorkspace::recoveryWarning(const std::string& message)
    {
        if(m_recoveryWarning!=message)NX_WARN("Blueprint Studio: %s",message.c_str());
        m_recoveryWarning=message;
    }

    void BlueprintWorkspace::returnToRedux(Services& s)
    {
        checkpoint(s,true);
        if(!recoveryProtected())
        {
            m_status="Unsaved graph is not protected. Save as or retry after fixing storage.";
            return;
        }
        close(s);
    }

    void BlueprintWorkspace::checkpoint(Services& s,bool force)
    {
        if(!active()||m_document.graphs.empty())return;
        const double now=s.platform->seconds();
        if(!force&&now<m_nextCheckpoint)return;
        m_nextCheckpoint=now+2.0;saveLayout(s);
        const char* path="own/blueprint-recovery.bp.json";
        if(!m_document.dirty())
        {
            if(m_checkpointPresent)
            {
                // Never clear a newer recovery draft written outside this workspace.
                FileData current;
                const bool exists=s.platform->fileTime(path)!=0;
                const bool readable=!exists||s.platform->readFile(path,current,MemTag::General);
                const bool same=readable&&current.size==m_checkpointBytes.size()&&
                    (!current.size||std::memcmp(current.data,m_checkpointBytes.data(),current.size)==0);
                if(!exists||(!m_protectDamagedRecovery&&same&&s.platform->writeFile(path,"",0)))
                {
                    m_checkpointPresent=false;m_checkpointRevision=static_cast<size_t>(-1);
                    m_checkpointBytes.clear();m_protectDamagedRecovery=false;m_recoveryWarning.clear();
                }
                else if(!same||m_protectDamagedRecovery)
                {
                    m_checkpointPresent=false;m_checkpointRevision=static_cast<size_t>(-1);
                    m_protectDamagedRecovery=true;
                    recoveryWarning("External recovery retained; newer draft not cleared.");
                }
                else recoveryWarning("Could not clear old recovery; saved project is safe.");
            }
            else m_recoveryWarning.clear();
            return;
        }
        // Revalidate even if the graph revision has not changed. External tools
        // may remove or replace a previously verified checkpoint. Reading once
        // per checkpoint interval avoids reserializing unchanged graphs.
        FileData damaged;
        const bool exists=s.platform->fileTime(path)!=0;
        if(exists&&!s.platform->readFile(path,damaged,MemTag::General))
        {recoveryWarning("Cannot read recovery file; autosave deferred.");return;}
        const bool changed=exists&&(damaged.size!=m_checkpointBytes.size()||
            (damaged.size&&std::memcmp(damaged.data,m_checkpointBytes.data(),damaged.size)!=0));
        if(exists&&!changed&&!m_protectDamagedRecovery&&m_checkpointPresent&&
            m_checkpointRevision==m_document.revision()&&!m_checkpointBytes.empty())
        {
            m_recoveryWarning.clear();return;
        }
        std::string output,error;
        if(!m_document.serialize(output,error))
        {recoveryWarning("Recovery serialization failed: "+error);return;}
        if(exists&&damaged.size&&(m_protectDamagedRecovery||changed))
        {
            // Preserve the previous bytes before any overwrite, including a valid
            // draft from another workspace. Verify the archive before continuing.
            static unsigned sequence=0;
            std::string archive;
            for(unsigned attempt=0;attempt<100;++attempt)
            {
                const auto candidate=std::string("own/blueprint-recovery-")+(m_protectDamagedRecovery?"damaged-":"conflict-")+
                    std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence)+".bak";
                if(!s.platform->fileTime(candidate.c_str())){archive=candidate;break;}
            }
            FileData verified;
            if(archive.empty()||!s.platform->writeFile(archive.c_str(),damaged.data,damaged.size)||
                !s.platform->readFile(archive.c_str(),verified,MemTag::General)||verified.size!=damaged.size||
                (damaged.size&&std::memcmp(verified.data,damaged.data,damaged.size)!=0))
            {recoveryWarning("Recovery archive failed; previous draft retained.");return;}
            NX_WARN("Blueprint Studio: previous recovery archived at %s",archive.c_str());
        }
        m_protectDamagedRecovery=false;
        m_checkpointBytes.clear();
        if(damaged.size)m_checkpointBytes.assign(reinterpret_cast<const char*>(damaged.data),damaged.size);
        if(!s.platform->writeFile(path,output.data(),output.size()))
        {recoveryWarning("Recovery write failed; latest edits are only in memory.");return;}
        FileData check;
        if(!s.platform->readFile(path,check,MemTag::General)||check.size!=output.size()||
            std::memcmp(check.data,output.data(),output.size())!=0)
        {
            m_protectDamagedRecovery=true;m_checkpointRevision=static_cast<size_t>(-1);
            recoveryWarning("Recovery verification failed; save a named project.");return;
        }
        m_checkpointPresent=true;m_checkpointRevision=m_document.revision();
        m_checkpointBytes=std::move(output);m_recoveryWarning.clear();
    }

    void BlueprintWorkspace::recover(Services& s)
    {
        // Normal shutdown preserves unsaved work as a draft, not a misleading manual save.
        checkpoint(s,true);
    }

    std::vector<BlueprintWorkspace::VariableRow> BlueprintWorkspace::variableRows() const
    {
        std::vector<VariableRow> rows;
        const auto& owner=m_document.graphs[m_graph].owner;
        for(const auto& g:m_document.graphs)if(g.owner==owner)for(const auto& f:g.fields)rows.push_back({f,false,true});
        for(auto at=m_document.parentBehaviour(owner);!at.empty();at=m_document.parentBehaviour(at))
            for(const auto& g:m_document.graphs)if(g.owner==at)for(const auto& field:g.fields)
            {
                auto found=std::find_if(rows.begin(),rows.end(),[&](const VariableRow& row){return row.field.name==field.name;});
                if(found==rows.end())rows.push_back({field,false,false,true});
                else if(field.exposed){found->field.exposed=true;found->inheritedExposure=true;}
            }
        std::vector<BlueprintPin> inferred;std::string error;
        if(m_document.variables(inferred,error,owner))for(const auto& v:inferred)
            if(blueprintVariableType(v.type)&&std::none_of(rows.begin(),rows.end(),[&](const VariableRow& r){return r.field.name==v.name;}))
                rows.push_back({{v.name,v.type,"",false},false,false});
        for(const auto& f:m_document.moduleGlobals(owner))rows.push_back({f,true,true});
        std::sort(rows.begin(),rows.end(),[](const VariableRow& a,const VariableRow& b){return a.global!=b.global?a.global<b.global:a.field.name<b.field.name;});
        return rows;
    }
    std::vector<std::string> BlueprintWorkspace::variableTypeChoices() const
    {
        std::vector<std::string> types;for(const auto* type:variableTypes)types.emplace_back(type);
        const auto& owner=m_document.graphs[m_graph].owner;
        for(const auto& g:m_document.graphs)if(m_document.dataType(g.name)==&g)types.push_back("BP_Data_"+g.name);
        for(const auto& type:m_document.behaviours())if(m_document.canAttach(owner,type))types.push_back("BP_Behaviour_"+type+"@");
        return types;
    }
    void BlueprintWorkspace::resetVariableEditor(bool global)
    {
        m_variableOld.clear();m_variableName.clear();m_variableType="int";m_variableValue.clear();
        m_variableGlobal=global;m_variableInitialized=false;m_variableTypes=false;m_variableInput=1;
    }
    std::vector<size_t> BlueprintWorkspace::commentMembers(size_t comment) const
    {
        std::vector<size_t> result;const auto& g=m_document.graphs[m_graph];
        if(comment>=g.comments.size())return result;
        for(size_t i=0;i<g.nodes.size();++i)if(std::find(g.comments[comment].members.begin(),g.comments[comment].members.end(),g.nodes[i].id)!=g.comments[comment].members.end())result.push_back(i);
        return result;
    }
    BlueprintDocument::Point BlueprintWorkspace::nodeLayoutPosition(size_t node) const
    {
        const auto& n=m_document.graphs[m_graph].nodes[node];
        if(m_drag&&m_dragMoved&&std::find(m_selectedNodes.begin(),m_selectedNodes.end(),node)!=m_selectedNodes.end())return {n.x+m_dragX-m_grabX,n.y+m_dragY-m_grabY};
        if(m_layoutDragging&&!m_commentResize&&m_layoutMoved&&m_layoutLink==static_cast<size_t>(-1)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),node))
            return {n.x+(m_layoutX-m_layoutStartX)/m_zoom,n.y+(m_layoutY-m_layoutStartY)/m_zoom};
        return {n.x,n.y};
    }
    BlueprintDocument::Point BlueprintWorkspace::routeLayoutPosition(size_t wire,size_t point) const
    {
        const auto& l=m_document.graphs[m_graph].links[wire];const auto& p=l.route[point];
        if(m_drag&&m_dragMoved&&std::find(m_selectedNodes.begin(),m_selectedNodes.end(),l.from)!=m_selectedNodes.end()&&std::find(m_selectedNodes.begin(),m_selectedNodes.end(),l.to)!=m_selectedNodes.end())return {p.x+m_dragX-m_grabX,p.y+m_dragY-m_grabY};
        if(m_layoutDragging&&m_layoutMoved)
        {
            if(m_layoutLink==wire&&m_layoutPoint==point)return {(m_layoutX-kLeft-m_x)/m_zoom,(m_layoutY-kTop-m_y)/m_zoom};
            if(!m_commentResize&&m_layoutLink==static_cast<size_t>(-1)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),l.from)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),l.to))
                return {p.x+(m_layoutX-m_layoutStartX)/m_zoom,p.y+(m_layoutY-m_layoutStartY)/m_zoom};
        }
        return p;
    }
    void BlueprintWorkspace::resizedComment(float& x,float& y,float& w,float& h) const
    {
        x=m_commentX;y=m_commentY;w=m_commentW;h=m_commentH;
        const float dx=(m_layoutX-m_layoutStartX)/m_zoom,dy=(m_layoutY-m_layoutStartY)/m_zoom;
        if(m_commentResize&1u){const float delta=std::min(dx,w-80.f);x+=delta;w-=delta;}
        if(m_commentResize&2u)w=std::max(80.f,w+dx);
        if(m_commentResize&4u){const float delta=std::min(dy,h-48.f);y+=delta;h-=delta;}
        if(m_commentResize&8u)h=std::max(48.f,h+dy);
    }
    bool BlueprintWorkspace::commentBounds(size_t comment,float& x,float& y,float& w,float& h) const
    {
        const auto& item=m_document.graphs[m_graph].comments[comment];
        if((m_layoutDragging&&m_commentResize&&m_layoutComment==comment)||item.width>0)
        {
            float gx=item.x,gy=item.y,gw=item.width,gh=item.height;
            if(m_layoutDragging&&m_layoutComment==comment&&m_commentResize)resizedComment(gx,gy,gw,gh);
            else if(m_layoutDragging&&m_layoutComment==comment&&m_layoutMoved){gx+=(m_layoutX-m_layoutStartX)/m_zoom;gy+=(m_layoutY-m_layoutStartY)/m_zoom;}
            x=kLeft+m_x+gx*m_zoom;y=kTop+m_y+gy*m_zoom;w=gw*m_zoom;h=gh*m_zoom;return true;
        }
        const auto members=commentMembers(comment);if(members.empty())return false;
        float left=1000000.f,top=1000000.f,right=-1000000.f,bottom=-1000000.f;
        for(const auto i:members)
        {
            const auto n=nodeLayoutPosition(i);left=std::min(left,n.x);top=std::min(top,n.y);
            right=std::max(right,n.x+m_rows[i].width);bottom=std::max(bottom,n.y+m_rows[i].height);
        }
        x=kLeft+m_x+(left-18)*m_zoom;y=kTop+m_y+(top-44)*m_zoom;w=(right-left+36)*m_zoom;h=(bottom-top+62)*m_zoom;return true;
    }
    size_t BlueprintWorkspace::hitWire(float x,float y,size_t* segment) const
    {
        float best=9.f;size_t found=static_cast<size_t>(-1);
        for(size_t i=0;i<m_document.graphs[m_graph].links.size();++i)
        {
            std::vector<size_t> segments;const auto points=wireRoute(i,&segments);
            for(size_t j=1;j<points.size();++j){const auto& a=points[j-1];const auto& b=points[j];const float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;if(!len)continue;const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/len,0.f,1.f),dist=std::hypot(x-a.x-t*dx,y-a.y-t*dy);if(dist<best){best=dist;found=i;if(segment)*segment=segments[j-1];}}
        }
        return found;
    }
    std::vector<BlueprintWorkspace::FindResult> BlueprintWorkspace::findResults() const
    {
        const auto lower=[](std::string value){for(char& ch:value)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));return value;};
        std::vector<FindResult> results;const auto query=lower(m_findText);
        if(query.empty())return results;
        for(size_t i=0;i<m_document.graphs.size();++i)
        {
            const auto& g=m_document.graphs[i];const auto prefix=g.owner+"/"+g.name;
            if(lower(prefix).find(query)!=std::string::npos)results.push_back({i,static_cast<size_t>(-1),prefix});
            for(size_t j=0;j<g.nodes.size();++j){const auto& n=g.nodes[j];std::string hay=title(n)+" "+n.kind+" "+n.ref+" "+n.comment;for(const auto& v:n.values)hay+=" "+v.pin+" "+v.value;if(lower(hay).find(query)!=std::string::npos)results.push_back({i,j,prefix+" / "+title(n)});}
            for(const auto& item:g.comments)if(lower(item.title).find(query)!=std::string::npos){size_t node=static_cast<size_t>(-1);for(size_t j=0;j<g.nodes.size();++j)if(std::find(item.members.begin(),item.members.end(),g.nodes[j].id)!=item.members.end()){node=j;break;}results.push_back({i,node,prefix+" / Comment: "+item.title});}
        }
        return results;
    }
    void BlueprintWorkspace::openFindResult(const FindResult& result,Input& input)
    {
        selectGraph(result.graph);m_focus=0;input.setTyping(false);
        if(result.node<m_rows.size()){m_selection=result.node;m_selectedNodes={result.node};const auto& n=m_document.graphs[m_graph].nodes[result.node];m_x=(graphAreaLeft()+graphAreaRight())*0.5f-kLeft-(n.x+m_rows[result.node].width*0.5f)*m_zoom;m_y=(graphAreaTop()+tabTop())*0.5f-kTop-(n.y+m_rows[result.node].height*0.5f)*m_zoom;}
    }
    std::vector<BlueprintDocument::Point> BlueprintWorkspace::wireRoute(size_t link,std::vector<size_t>* segments) const
    {
        const auto& g=m_document.graphs[m_graph];const auto& l=g.links[link];const auto& pins=m_wirePins[link];
        const auto from=nodeLayoutPosition(l.from),to=nodeLayoutPosition(l.to);
        const float x1=kLeft+m_x+(from.x+m_rows[l.from].width)*m_zoom,y1=kTop+m_y+(from.y+pinY(pins.output))*m_zoom;
        const float x2=kLeft+m_x+to.x*m_zoom,y2=kTop+m_y+(to.y+pinY(pins.input))*m_zoom;
        std::vector<BlueprintDocument::Point> anchors{{x1,y1}};
        for(size_t i=0;i<l.route.size();++i){const auto p=routeLayoutPosition(link,i);anchors.push_back({kLeft+m_x+p.x*m_zoom,kTop+m_y+p.y*m_zoom});}
        anchors.push_back({x2,y2});
        const float top=kTop+m_y+std::min(from.y,to.y)*m_zoom;
        const float bottom=kTop+m_y+std::max(from.y+m_rows[l.from].height,to.y+m_rows[l.to].height)*m_zoom;
        const float left=kLeft+m_x+std::min(from.x,to.x)*m_zoom;
        const float right=kLeft+m_x+std::max(from.x+m_rows[l.from].width,to.x+m_rows[l.to].width)*m_zoom;
        return blueprintGraphWire(anchors,m_wireStyle,m_zoom,top,bottom,segments,left,right);
    }
    std::vector<std::string> BlueprintWorkspace::namedSceneTargets() const
    {
        std::vector<std::string> names;
        const auto& current=m_document.graphs[m_graph];
        for(const auto& graph:m_document.graphs)
        {
            if(graph.owner!=current.owner)continue;
            for(size_t i=0;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];if(n.kind!="spawn:box"&&n.kind!="spawn:plane")continue;
                if(std::any_of(graph.links.begin(),graph.links.end(),[&](const BlueprintDocument::Link& l){return l.to==i&&l.input=="name";}))continue;
                std::string label=n.kind=="spawn:box"?"Box":"Floor";
                for(const auto& v:n.values)if(v.pin=="name")label=v.value;
                if(!label.empty()&&std::find(names.begin(),names.end(),label)==names.end())names.push_back(label);
            }
        }
        std::sort(names.begin(),names.end());return names;
    }
    std::vector<BlueprintWorkspace::InputDetail> BlueprintWorkspace::inputDetails(size_t node) const
    {
        std::vector<InputDetail> details;
        if(m_graph>=m_document.graphs.size())return details;
        const auto& g=m_document.graphs[m_graph];if(node>=g.nodes.size())return details;
        const auto& n=g.nodes[node];BlueprintNodeSpec spec;if(!m_document.nodeSpec(g,n,spec))return details;
        for(const auto& p:spec.inputs)
        {
            if(p.type=="exec")continue;
            InputDetail row{p.name,p.type,"Not set","Required",false};
            const auto wire=std::find_if(g.links.begin(),g.links.end(),[&](const BlueprintDocument::Link& l){return l.to==node&&l.input==p.name;});
            if(wire!=g.links.end())
            {row.value=title(g.nodes[wire->from])+" / "+wire->output;row.origin="Connected";}
            else
            {
                row.editable=blueprintInputLiteralType(p.type);
                const auto custom=std::find_if(n.values.begin(),n.values.end(),[&](const BlueprintDocument::InputValue& v){return v.pin==p.name;});
                const auto fallback=std::find_if(spec.defaults.begin(),spec.defaults.end(),[&](const BlueprintPin& d){return d.name==p.name;});
                if(custom!=n.values.end()){row.value=custom->value;row.origin="Custom";}
                else if(fallback!=spec.defaults.end())
                {
                    row.value=fallback->type;row.origin="Default";
                    const auto prefix=p.type+"(";
                    if(row.value.rfind(prefix,0)==0&&row.value.ends_with(")"))row.value=row.value.substr(prefix.size(),row.value.size()-prefix.size()-1);
                    if(row.value=="dt"&&p.type=="float")row.value="Event delta time";
                }
                else if(n.kind=="actor:spawn-asset"&&p.name.rfind("spawn_",0)==0)
                {
                    const auto id=std::find_if(n.values.begin(),n.values.end(),[](const BlueprintDocument::InputValue& v){return v.pin=="id";});
                    if(id!=n.values.end())for(const auto& field:m_document.actorSpawnFields(id->value))if("spawn_"+field.name==p.name)
                    {row.value=field.initialized?field.value:"Native default";row.origin="Class default";break;}
                }
                else if(!row.editable)row.value="Connect a wire";
            }
            details.push_back(std::move(row));
        }
        return details;
    }
    namespace
    {
        Color spectrum(float h,float s,float v,float a=1.f)
        {
            h=std::clamp(h,0.f,1.f)*6.f;const float c=v*s,k=c*(1.f-std::abs(std::fmod(h,2.f)-1.f)),m=v-c;
            if(h<1)return {c+m,k+m,m,a};
            if(h<2)return {k+m,c+m,m,a};
            if(h<3)return {m,c+m,k+m,a};
            if(h<4)return {m,k+m,c+m,a};
            if(h<5)return {k+m,m,c+m,a};
            return {c+m,m,k+m,a};
        }
        void spectrumCoordinates(Color c,float& h,float& s,float& v)
        {
            v=std::max({c.r,c.g,c.b});const float lo=std::min({c.r,c.g,c.b}),delta=v-lo;s=v>0?delta/v:0;h=0;
            if(delta>0){if(v==c.r)h=std::fmod((c.g-c.b)/delta+6.f,6.f);else if(v==c.g)h=(c.b-c.r)/delta+2.f;else h=(c.r-c.g)/delta+4.f;h/=6.f;}
        }
        std::string spectrumHex(Color c)
        {
            const auto byte=[](float n){return static_cast<unsigned>(std::round(std::clamp(n,0.f,1.f)*255.f));};
            return blueprintRGBHex(0x1000000u|(byte(c.r)<<16)|(byte(c.g)<<8)|byte(c.b));
        }
    }
    void BlueprintWorkspace::startRGB(const std::string& value,bool alpha,float opacity)
    {
        m_rgbOpen=true;m_rgbReplace=true;m_rgbApplied=false;m_rgbAlphaEnabled=alpha;m_rgbAlpha=std::clamp(opacity,0.f,1.f);m_rgbText=value.empty()?"FFFFFF":value;
    }
    bool BlueprintWorkspace::updateRGB(Input& input,float x,float y,bool left,bool click,unsigned& colour)
    {
        if(!m_rgbOpen)return false;
        if(click&&(!in(x,y,370,105,910,615)||in(x,y,770,565,890,603))){m_rgbOpen=false;return true;}
        unsigned current=0;blueprintParseRGB(m_rgbText,current);const Color base=rgbColour(current,{1,1,1,1});float h=0,s=0,v=1;spectrumCoordinates(base,h,s,v);
        if(left&&in(x,y,390,148,790,378)){h=std::clamp((x-390)/400.f,0.f,1.f);s=1.f-std::clamp((y-148)/230.f,0.f,1.f);m_rgbText=spectrumHex(spectrum(h,s,v));m_rgbReplace=true;}
        if(left&&in(x,y,390,395,890,419)){v=std::clamp((x-390)/500.f,0.f,1.f);m_rgbText=spectrumHex(spectrum(h,s,v));m_rgbReplace=true;}
        if(m_rgbAlphaEnabled&&left&&in(x,y,390,434,890,458))m_rgbAlpha=std::clamp((x-390)/500.f,0.f,1.f);
        if(click&&in(x,y,390,477,890,515))m_rgbReplace=true;
        bool apply=click&&in(x,y,640,565,760,603);
        for(u32 i=0;i<input.typedCount();++i){const auto ch=input.typed(i);if(ch==13){apply=true;break;}if(ch==8){if(m_rgbReplace)m_rgbText.clear();else if(!m_rgbText.empty())m_rgbText.pop_back();m_rgbReplace=false;}else if(ch<128&&std::string("0123456789abcdefABCDEF#").find(static_cast<char>(ch))!=std::string::npos){if(m_rgbReplace){m_rgbText.clear();m_rgbReplace=false;}appendCharacter(m_rgbText,ch,7);}}
        if(click&&in(x,y,390,565,510,603)){colour=0;m_rgbApplied=true;m_rgbOpen=false;return true;}
        if(apply){unsigned value;if(blueprintParseRGB(m_rgbText,value)){colour=value;m_rgbText=blueprintRGBHex(value);m_rgbApplied=true;m_rgbOpen=false;}else m_status="Enter six hexadecimal RGB digits (optional #).";}
        return true;
    }
    void BlueprintWorkspace::drawRGB(Canvas& c)
    {
        c.rect(0,0,1280,720,{0,0,0,0.48f});c.roundRect(370,105,540,510,7,{0.12f,0.16f,0.21f,1});c.text(*m_font,"COLOUR PICKER",390,116,accent);
        unsigned packed=0;blueprintParseRGB(m_rgbText,packed);const Color base=rgbColour(packed,{1,1,1,1});float h=0,s=0,v=1;spectrumCoordinates(base,h,s,v);
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<60;++x){const float hh=static_cast<float>(x)/59.f,ss=1.f-static_cast<float>(y)/23.f;c.rect(390.f+400.f*static_cast<float>(x)/60.f,148.f+230.f*static_cast<float>(y)/24.f,400.f/60.f+0.5f,230.f/24.f+0.5f,spectrum(hh,ss,v));}
        c.ring(390+400*h,148+230*(1-s),7,2,{0,0,0,1});c.ring(390+400*h,148+230*(1-s),9,1,{1,1,1,1});
        for(unsigned y=0;y<12;++y)for(unsigned x=0;x<5;++x)c.rect(806.f+16.f*static_cast<float>(x),148.f+19.f*static_cast<float>(y),16,19,(x+y)%2?Color{0.32f,0.32f,0.32f,1}:Color{0.7f,0.7f,0.7f,1});
        c.rect(806,148,80,228,{base.r,base.g,base.b,m_rgbAlphaEnabled?m_rgbAlpha:1.f});
        for(unsigned j=0;j<64;++j){const float f=static_cast<float>(j)/63.f;c.rect(390.f+500.f*static_cast<float>(j)/64.f,395,500.f/64.f+0.5f,24,spectrum(h,s,f));if(m_rgbAlphaEnabled){const float xx=390.f+500.f*static_cast<float>(j)/64.f;c.rect(xx,434,500.f/64.f+0.5f,24,j%2?Color{0.3f,0.3f,0.3f,1}:Color{0.7f,0.7f,0.7f,1});c.rect(xx,434,500.f/64.f+0.5f,24,{base.r,base.g,base.b,f});}}
        c.ring(390+500*v,407,7,2,text);if(m_rgbAlphaEnabled)c.ring(390+500*m_rgbAlpha,446,7,2,text);
        c.roundRect(390,477,500,38,4,{0.24f,0.35f,0.48f,1});c.text(*m_font,("HEX  "+m_rgbText).c_str(),400,486,text);
        const auto byte=[](float n){return static_cast<unsigned>(std::round(std::clamp(n,0.f,1.f)*255.f));};
        const auto channels="R "+std::to_string(byte(base.r))+"    G "+std::to_string(byte(base.g))+"    B "+std::to_string(byte(base.b))+(m_rgbAlphaEnabled?"    Alpha "+std::to_string(static_cast<unsigned>(std::round(m_rgbAlpha*100)))+"%":"");
        c.text(*m_font,channels.c_str(),390,520,text);c.text(*m_font,"Enter = Apply | Escape = Cancel",390,542,muted);
        c.roundRect(390,565,120,38,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Default",450,574,text,Align::Center);
        c.roundRect(640,565,120,38,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",700,574,text,Align::Center);
        c.roundRect(770,565,120,38,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",830,574,text,Align::Center);
    }
    void BlueprintWorkspace::beginNodeEditor(size_t node,Input& input)
    {
        if(m_graph>=m_document.graphs.size()||node>=m_document.graphs[m_graph].nodes.size())return;
        m_selection=node;m_selectedNodes={node};clearWire();cancelDrag();m_nodeClickTime=-1;
        m_inspectorRows=inputDetails(node);const auto& n=m_document.graphs[m_graph].nodes[node];
        m_inspectorLiteral=n.kind.rfind("literal:",0)==0;
        if(m_inspectorLiteral)m_inspectorRows.insert(m_inspectorRows.begin(),InputDetail{"Value",n.kind.substr(8),n.ref,"Custom",true});
        m_inspectorRows.insert(m_inspectorRows.begin(),InputDetail{"Comment","text",n.comment,"Node",true});
        m_inspectorRows.insert(m_inspectorRows.begin(),InputDetail{"Display name","text",m_document.nodeDisplayName(m_graph,n),"Shared",true});
        m_inspectorColour=n.colour;m_inspectorColourChanged=false;
        m_inspectorDraft.clear();for(const auto& row:m_inspectorRows)m_inspectorDraft.push_back(row.origin=="Required"||row.value=="Event delta time"||row.value=="Native default"?"":row.value);
        m_inspectorChanged.assign(m_inspectorRows.size(),false);m_inspectorFirst=0;m_inspectorRow=static_cast<size_t>(-1);
        m_focus=15;input.setTyping(true);m_status="Apply changes together. Clear display name to restore default. Connected inputs are read-only.";NX_INFO("Blueprint Studio: Edit Node opened");
    }
    bool BlueprintWorkspace::applyNodeEditor(Input& input)
    {
        std::vector<BlueprintDocument::InputValue> values;const std::string* literalValue=nullptr;
        for(size_t i=2;i<m_inspectorRows.size();++i)if(m_inspectorChanged[i])
        {if(i==2&&m_inspectorLiteral)literalValue=&m_inspectorDraft[i];else values.push_back({m_inspectorRows[i].name,m_inspectorDraft[i]});}
        if(!m_document.editNodeValues(m_graph,m_selection,values,literalValue,m_status,m_inspectorChanged[0]?&m_inspectorDraft[0]:nullptr,m_inspectorChanged[1]?&m_inspectorDraft[1]:nullptr,m_inspectorColourChanged?&m_inspectorColour:nullptr))return false;
        m_focus=0;input.setTyping(false);clearWire();rebuildPins();filterCatalogue();m_diagnostic={};m_status="Node edits applied.";return true;
    }
    bool BlueprintWorkspace::removeSelectedNode(Input& input)
    {
        if(m_graph>=m_document.graphs.size())return false;
        const auto& graph=m_document.graphs[m_graph];m_deleteSelection=m_selectedNodes;
        if(m_deleteSelection.empty()&&m_selection<graph.nodes.size())m_deleteSelection.push_back(m_selection);
        m_deleteSelection.erase(std::remove_if(m_deleteSelection.begin(),m_deleteSelection.end(),[&](size_t n){return n>=graph.nodes.size()||graph.nodes[n].kind=="entry";}),m_deleteSelection.end());
        if(m_deleteSelection.empty()){m_status="Select a deletable node. Event entry is protected.";return false;}
        m_skipDeleteConfirmation=false;
        if(!m_confirmDelete)return confirmNodeDeletion(input);
        m_focus=25;input.setTyping(false);m_status="Confirm node deletion; Undo restores deleted nodes.";return true;
    }
    bool BlueprintWorkspace::confirmNodeDeletion(Input& input)
    {
        if(!m_document.deleteNodes(m_graph,m_deleteSelection,m_status))return false;
        if(m_skipDeleteConfirmation)m_confirmDelete=false;
        const auto count=m_deleteSelection.size();m_deleteSelection.clear();m_skipDeleteConfirmation=false;
        m_focus=0;input.setTyping(false);m_selection=static_cast<size_t>(-1);m_selectedNodes.clear();clearWire();rebuildPins();filterCatalogue();m_diagnostic={};
        m_status=count==1?"Node deleted. Undo restores it.":"Selected nodes deleted. Undo restores them.";return true;
    }
    bool BlueprintWorkspace::beginInputEdit(size_t node,const std::string& pin,Input& input)
    {
        const auto details=inputDetails(node);
        const auto found=std::find_if(details.begin(),details.end(),[&](const InputDetail& d){return d.name==pin;});
        if(found==details.end()||!found->editable)return false;
        m_selection=node;m_editPin=pin;m_editType=found->type;m_edit.clear();
        for(const auto& v:m_document.graphs[m_graph].nodes[node].values)if(v.pin==pin)m_edit=v.value;
        prepareInputEditor(node);clearWire();m_focus=6;input.setTyping(true);
        m_status=m_inputComponents.empty()?"Enter a typed value. Drag the pin to connect a node.":"Click a component or use Tab to move between fields.";
        return true;
    }
    void BlueprintWorkspace::prepareInputEditor(size_t node)
    {
        m_inputComponents.clear();m_inputChoices.clear();m_inputChoiceFirst=m_inputComponent=0;m_inputReplace=true;
        const auto& graph=m_document.graphs[m_graph];const auto& n=graph.nodes[node];
        const auto labels=blueprintInputComponentLabels(m_editType);
        if(!labels.empty())
        {
            std::string value=m_edit;
            if(value.empty())
            {
                BlueprintNodeSpec spec;
                if(m_document.nodeSpec(graph,n,spec))for(const auto& d:spec.defaults)if(d.name==m_editPin&&d.type.rfind(m_editType+"(",0)==0&&d.type.ends_with(")"))
                    value=d.type.substr(m_editType.size()+1,d.type.size()-m_editType.size()-2);
            }
            if(!blueprintInputComponents(m_editType,value,m_inputComponents))
            {m_inputComponents.assign(labels.size(),"0");if(m_editType=="Color")m_inputComponents[3]="1";}
        }
        m_inputChoices=blueprintEnumChoices(m_editType);
        if(!m_inputChoices.empty()&&m_edit.empty())
        {
            BlueprintNodeSpec spec;
            if(m_document.nodeSpec(graph,n,spec))for(const auto& d:spec.defaults)
                if(d.name==m_editPin&&blueprintEnumValue(m_editType,d.type))m_edit=d.type;
        }
        if(n.kind=="camera:orbit-timed"&&m_editPin=="name")m_inputChoices=namedSceneTargets();
    }
    void BlueprintWorkspace::openSceneBrowser(Input& input)
    {
        m_sceneBrowserOwner=m_document.moduleForOwner(m_document.graphs[m_graph].owner);
        if(m_sceneBrowserOwner.empty())m_sceneBrowserOwner=m_document.screens().front();
        m_sceneBrowserAsset.clear();m_sceneFirst=m_sceneAssetFirst=0;m_sceneNaming=false;m_focus=22;input.setTyping(false);
        m_status="Select a scene or shared camera sequence.";
    }
    bool BlueprintWorkspace::beginSequenceAsset(const std::string& id,Input& input)
    {
        if(!m_document.cameraSequenceKeys(id,m_sequenceKeys,m_status))return false;
        m_sequenceAsset=id;m_sequenceKey=0;m_sequenceTime=0;m_sequenceLast=0;m_sequencePlaying=m_sequenceScrub=false;m_sequenceField=-1;m_focus=21;input.setTyping(true);clearWire();m_status="Editing reusable cinematic; all linked nodes use these keys.";return true;
    }
    void BlueprintWorkspace::updateSceneBrowser(Services& s,float x,float y,bool click)
    {
        auto& input=*s.input;const auto scenes=m_document.screens();std::vector<const BlueprintDocument::VisualAsset*> assets;
        for(const auto& asset:m_document.visualAssets())if(asset.kind=="sequence")assets.push_back(&asset);
        if(click&&(!in(x,y,100,64,1180,658)||in(x,y,1030,610,1160,646))){m_focus=0;input.setTyping(false);return;}
        if(m_sceneNaming)
        {
            bool apply=click&&in(x,y,830,556,980,590);
            if(click&&in(x,y,990,556,1140,590)){m_sceneNaming=false;input.setTyping(false);return;}
            for(u32 i=0;i<input.typedCount();++i){const auto ch=input.typed(i);if(ch==13){apply=true;break;}if(ch==8&&!m_sceneNameDraft.empty())m_sceneNameDraft.pop_back();else if(ch>=32)appendCharacter(m_sceneNameDraft,ch,128);}
            if(apply){if(m_document.renameVisualAsset(m_sceneBrowserAsset,m_sceneNameDraft,m_status)){m_sceneNaming=false;input.setTyping(false);}}return;
        }
        m_sceneFirst=std::min(m_sceneFirst,scenes.size()>8?scenes.size()-8:0);m_sceneAssetFirst=std::min(m_sceneAssetFirst,assets.size()>8?assets.size()-8:0);
        if(in(x,y,120,160,450,520)){if(input.wheel()<0&&m_sceneFirst+8<scenes.size())++m_sceneFirst;if(input.wheel()>0&&m_sceneFirst)--m_sceneFirst;}
        if(in(x,y,470,160,1150,520)){if(input.wheel()<0&&m_sceneAssetFirst+8<assets.size())++m_sceneAssetFirst;if(input.wheel()>0&&m_sceneAssetFirst)--m_sceneAssetFirst;}
        if(!click)return;
        if(in(x,y,120,160,450,512)){const auto at=m_sceneFirst+static_cast<size_t>((y-160)/44);if(at<scenes.size())m_sceneBrowserOwner=scenes[at];return;}
        if(in(x,y,470,160,1150,512)){const auto at=m_sceneAssetFirst+static_cast<size_t>((y-160)/44);if(at<assets.size())m_sceneBrowserAsset=assets[at]->id;return;}
        if(in(x,y,640,582,840,616)){m_focus=24;input.setTyping(false);m_status="Validate or export this project.";return;}
        if(in(x,y,120,582,450,616)){m_actionName="Jump";m_actionKey="Space";m_actionButton=8;m_actionField=0;m_actionFirst=0;m_focus=23;input.setTyping(true);m_status="Create a named held-input query.";return;}
        if(in(x,y,120,540,280,574)){for(size_t i=0;i<m_document.graphs.size();++i)if(m_document.graphs[i].owner==m_sceneBrowserOwner&&m_document.graphs[i].name=="update"){selectGraph(i);m_focus=0;input.setTyping(false);return;}}
        if(in(x,y,290,540,450,574)){m_focus=7;m_edit.clear();input.setTyping(true);m_status="New scene: enter a lowercase ID.";return;}
        if(in(x,y,470,540,630,574)){std::string id;if(m_document.createVisualAsset("sequence",id,m_status)){m_sceneBrowserAsset=id;beginSequenceAsset(id,input);}return;}
        if(in(x,y,640,540,800,574)&&!m_sceneBrowserAsset.empty()){beginSequenceAsset(m_sceneBrowserAsset,input);return;}
        if(in(x,y,810,540,970,574)&&!m_sceneBrowserAsset.empty()){size_t g=0,n=0;if(m_document.placeCameraSequence(m_sceneBrowserOwner,m_sceneBrowserAsset,g,n,m_status)){selectGraph(g);m_selection=n;m_selectedNodes={n};m_focus=0;input.setTyping(false);m_status="Placed cinematic. Connect execution and Play trigger."; }return;}
        if(in(x,y,980,540,1150,574)&&!m_sceneBrowserAsset.empty()){if(const auto* asset=m_document.visualAsset(m_sceneBrowserAsset)){m_sceneNameDraft=asset->title;m_sceneNaming=true;input.setTyping(true);}return;}
        if(in(x,y,470,582,630,616)&&!m_sceneBrowserAsset.empty()){if(m_document.removeVisualAsset(m_sceneBrowserAsset,m_status))m_sceneBrowserAsset.clear();}
    }
    void BlueprintWorkspace::updateInputActions(Services& s,float x,float y,bool click)
    {
        auto& input=*s.input;const auto actions=m_document.inputActions();
        if(click&&in(x,y,1000,600,1150,638)){m_focus=22;input.setTyping(false);return;}
        if(in(x,y,120,176,450,516)){if(input.wheel()<0&&m_actionFirst+8<actions.size())++m_actionFirst;if(input.wheel()>0&&m_actionFirst)--m_actionFirst;}
        if(click&&in(x,y,120,176,450,512)){const auto i=m_actionFirst+static_cast<size_t>((y-176)/42);if(i<actions.size()){m_actionName=actions[i].name;m_actionKey=actions[i].key;m_actionButton=actions[i].button;m_actionField=0;}return;}
        if(click&&in(x,y,120,540,450,574)){m_actionName.clear();m_actionKey="Space";m_actionButton=8;m_actionField=1;return;}
        if(click&&in(x,y,480,180,1140,220))m_actionField=1;
        if(click&&in(x,y,480,260,1140,300))m_actionField=2;
        if(click&&in(x,y,480,344,1140,386)){m_actionButton=(m_actionButton+1)%15;m_actionField=0;}
        if(click&&in(x,y,480,400,800,434)){m_actionKey="None";m_actionField=0;}
        bool apply=click&&in(x,y,820,540,1140,574);
        for(u32 i=0;i<input.typedCount();++i){const auto ch=input.typed(i);if(ch==13){apply=true;break;}if(!m_actionField)continue;auto& value=m_actionField==1?m_actionName:m_actionKey;if(ch==8&&!value.empty())value.pop_back();else if(ch>=32)appendCharacter(value,ch,58);}
        if(apply&&m_document.setInputAction(m_actionName,m_actionKey,m_actionButton,m_status)){m_actionField=0;rebuildPins();filterCatalogue();m_status="Saved Input_"+m_actionName+". Add its shared function call to your Blueprint.";}
    }
    void BlueprintWorkspace::drawInputActions(Canvas& c)
    {
        static const char* buttons[]={"D-pad up","D-pad down","D-pad left","D-pad right","Start","Back","Left stick","Right stick","A","B","X","Y","Left shoulder","Right shoulder","None"};
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});c.text(*m_font,"INPUT ACTION EDITOR / HELD QUERIES",120,78,accent);
        c.text(*m_font,"Project-scoped keyboard and controller 1 bindings. Stored as shared Blueprint functions.",120,110,muted);
        const auto button=[&](float x,float y,float width,const char* label){c.roundRect(x,y,width,34,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,label,x+width/2,y+7,text,Align::Center);};
        const auto actions=m_document.inputActions();c.text(*m_font,"NAMED ACTIONS",120,150,accent);
        for(size_t i=0;i<8&&m_actionFirst+i<actions.size();++i){const auto& a=actions[m_actionFirst+i];const float yy=176.f+42.f*static_cast<float>(i);c.rect(120,yy,330,38,a.name==m_actionName?Color{0.20f,0.34f,0.47f,1}:Color{0.20f,0.23f,0.27f,1});c.clip(128,yy,314,38);c.text(*m_font,a.name.c_str(),132,yy+4,text);c.text(*m_font,(a.key+" / "+buttons[a.button]).c_str(),132,yy+20,muted);c.noClip();}
        if(actions.empty())c.text(*m_font,"No input actions yet.",132,190,muted);
        c.text(*m_font,"Action name (identifier)",480,152,accent);c.roundRect(480,180,660,40,4,m_actionField==1?Color{0.24f,0.35f,0.48f,1}:Color{0.20f,0.23f,0.27f,1});c.text(*m_font,m_actionName.c_str(),492,190,text);
        c.text(*m_font,"Keyboard (e.g. Space, E, Shift, or None)",480,232,accent);c.roundRect(480,260,660,40,4,m_actionField==2?Color{0.24f,0.35f,0.48f,1}:Color{0.20f,0.23f,0.27f,1});c.text(*m_font,m_actionKey.c_str(),492,270,text);
        c.text(*m_font,"Controller button / click to cycle",480,316,accent);button(480,344,660,buttons[m_actionButton]);button(480,400,320,"Disable keyboard");
        c.text(*m_font,"Returns true while either binding is held.",480,462,muted);c.text(*m_font,"Use the Input_ action in the shared function catalogue.",480,486,muted);
        button(120,540,330,"+ New action");button(820,540,320,"Apply binding");button(1000,600,150,"Back");
        c.clip(120,600,865,42);c.text(*m_font,m_status.c_str(),120,610,muted);c.noClip();
    }
    void BlueprintWorkspace::updateBuildManager(Services& s,float x,float y,bool click)
    {
        if(!click)return;
        if(in(x,y,140,600,425,638)){m_focus=27;return;}
        if(in(x,y,435,600,720,638)){m_focus=28;return;}
        if(in(x,y,1000,600,1150,638)){m_focus=22;return;}
        if(in(x,y,140,420,470,455)||in(x,y,480,420,790,455)||in(x,y,800,420,1140,455))
        {
            auto target=m_document.target();
            if(x<470)target.platform=(target.platform+1)%BlueprintTarget::platformCount;
            else if(x<790){if(!target.xbox())return;target.ram=target.ram==64?128:target.ram==128?256:64;}
            else target.resolution=(target.resolution+1)%BlueprintTarget::resolutionCount;
            if(m_document.setTarget(target))m_status=target.warning();
            return;
        }
        if(in(x,y,140,275,1140,330)){compileAndExport(s);return;}
        const bool validate=in(x,y,140,195,1140,250),package=in(x,y,140,355,1140,410);
        if(!validate&&!package)return;
        std::vector<BlueprintScreenOutput> outputs;BlueprintLocation location;m_diagnostic={};
        if(!compileBlueprintProject(m_document,outputs,m_status,&location)){focusDiagnostic(location);m_focus=0;return;}
        if(!validateProject(s,outputs)){m_focus=0;return;}
        if(validate)
        {
            m_targetAudit.clear();
            if(!auditTargetAssets(s,m_targetAudit)){m_status="Integrity check failed: "+m_targetAudit;return;}
            m_status="Script integrity passed ("+std::to_string(outputs.size())+" scenes). "+m_targetAudit+" | Xbox/runtime fit UNVERIFIED.";
            return;
        }
        std::string file;
        for(unsigned i=1;i<=10000;++i){const auto candidate="blueprint-package-"+std::to_string(i)+".bp.zip";if(!s.platform->fileTime(("own/"+candidate).c_str())){file=candidate;break;}}
        if(file.empty())m_status="No free project package name.";
        else if(blueprintExportPackage(*s.platform,m_document,file,m_status))m_status="Validated and exported own/"+file;
    }
    void BlueprintWorkspace::drawBuildManager(Canvas& c)
    {
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});c.text(*m_font,"BUILD & PACKAGE MANAGER",120,78,accent);
        c.text(*m_font,m_document.name.c_str(),120,111,text);
        c.text(*m_font,("Scenes: "+std::to_string(m_document.screens().size())+" | Graphs: "+std::to_string(m_document.graphs.size())+" | Linked assets: "+std::to_string(m_document.visualAssets().size())).c_str(),120,140,muted);
        const auto row=[&](float y,const char* title,const char* detail){c.roundRect(140,y,1000,55,5,{0.20f,0.32f,0.45f,1});c.text(*m_font,title,154,y+6,text);c.text(*m_font,detail,154,y+28,muted);};
        row(195,"Check runtime integrity","Compile every scene; check source media readability and direct local references.");
        row(275,"Compile & export native project","Export native screen manifests, generated scripts and project media into a new folder.");
        row(355,"Validate & package editable project","Export a portable Blueprint ZIP containing the document and media.");
        const auto& target=m_document.target();
        const auto setting=[&](float x,float width,const std::string& label){c.roundRect(x,420,width,35,4,{0.20f,0.32f,0.45f,1});c.clip(x+5,420,width-10,35);c.text(*m_font,label.c_str(),x+width/2,428,text,Align::Center);c.noClip();};
        setting(140,330,std::string(target.platformName())+" >");
        setting(480,310,target.xbox()?std::to_string(target.ram)+" MiB RAM >":"Platform RAM / n/a");
        setting(800,340,std::string(target.resolutionName())+" >");
        char estimate[160];std::snprintf(estimate,sizeof(estimate),"Example render surfaces: %.2f MiB (2 x RGBA8 + 1 x D32; not total runtime RAM)",static_cast<double>(target.surfaceBytes())/1048576.0);
        c.text(*m_font,estimate,140,460,muted);
        c.text(*m_font,target.warning().c_str(),140,484,{1.f,0.72f,0.22f,1.f});
        c.text(*m_font,"Exports are source/media. Actual target runtime integrity and performance remain unverified.",140,508,muted);
        c.roundRect(140,600,285,38,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,"Device Manager",282,609,text,Align::Center);
        c.roundRect(435,600,285,38,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,"Play on Xbox",577,609,text,Align::Center);
        c.roundRect(1000,600,150,38,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,"Back",1075,609,text,Align::Center);
        c.clip(120,535,1030,58);c.paragraph(*m_font,m_status.c_str(),120,540,1020,text);c.noClip();
    }
    void BlueprintWorkspace::syncDeviceBreakpoints()
    {
        if(!m_device.ready()||!m_device.sourceIdentity()){m_deviceFeedback="Attach to a matching source-identity runtime first.";return;}
        std::vector<BlueprintScreenOutput> screens;std::string error;
        if(!compileBlueprintProject(m_document,screens,error)){m_deviceFeedback=error;return;}
        std::string commands="{\"cmd\":\"clearBreakpoints\"}\n";size_t total=0;
        for(const auto& screen:screens)
        {
            JsonValue message;message.put("cmd")->setText("breakpoints");message.put("file")->setText("showcase/"+screen.id+"/main.as");
            message.put("fingerprint")->setText(scriptFingerprint(screen.source.data(),screen.source.size()));
            auto* lines=message.put("lines");lines->setArray();
            std::vector<size_t> nodes;
            for(const auto& span:screen.map)
            {
                const auto g=span.location.graph,n=span.location.node;
                if(g>=m_document.graphs.size()||n>=m_document.graphs[g].nodes.size()||!m_document.graphs[g].nodes[n].watch)continue;
                if(std::find(nodes.begin(),nodes.end(),span.firstLine)!=nodes.end())continue;
                nodes.push_back(span.firstLine);lines->add()->setNumber(static_cast<double>(span.firstLine));++total;
            }
            if(!nodes.empty())commands+=message.write(true)+"\n";
        }
        m_deviceFeedback=m_device.sendBreakpoints(commands)?"Sent "+std::to_string(total)+" guarded breakpoint lines; matching source only.":"Breakpoint request too large or unavailable.";
    }
    void BlueprintWorkspace::locateDeviceNode()
    {
        if(!m_device.paused()||m_device.fingerprint().empty()){m_deviceFeedback="Pause a matching runtime and wait for its stack.";return;}
        std::vector<BlueprintScreenOutput> screens;std::string error;
        if(!compileBlueprintProject(m_document,screens,error)){m_deviceFeedback=error;return;}
        for(const auto& screen:screens)
        {
            if(m_device.file()!="showcase/"+screen.id+"/main.as")continue;
            if(m_device.fingerprint()!=scriptFingerprint(screen.source.data(),screen.source.size()))
            {m_deviceFeedback="Source mismatch: re-export/deploy the current project.";return;}
            const auto location=blueprintSourceLocation(screen.map,m_device.line());
            if(location.graph>=m_document.graphs.size()||location.node>=m_document.graphs[location.graph].nodes.size())break;
            m_focus=0;focusDiagnostic(location);m_diagnostic={};
            m_status="Remote paused node (source matched): "+m_device.file()+":"+std::to_string(m_device.line());
            return;
        }
        m_deviceFeedback="No mapped Blueprint node at the remote script location.";
    }
    BlueprintLocation BlueprintWorkspace::mapRemoteLocation(const std::string& file,const std::string& fingerprint,unsigned line) const
    {
        if(fingerprint.empty()||!line)return {};
        for(size_t i=0;i<m_remoteMaps.size();++i)
            if(file=="showcase/"+m_remoteMaps[i].id+"/main.as"&&fingerprint==m_remoteFingerprints[i])
                return blueprintSourceLocation(m_remoteMaps[i].map,line);
        return {};
    }
    void BlueprintWorkspace::toggleRemoteTrace()
    {
        if(!m_device.ready()||!m_device.traceSupported()){m_deviceFeedback="Matching runtime with remote trace is required.";return;}
        m_device.setTrace(!m_device.tracing());m_remoteGraphHits.clear();m_remoteGap=false;
        m_deviceFeedback=m_device.tracing()?"Bounded remote trace enabled.":"Remote trace disabled.";
    }
    void BlueprintWorkspace::updateRemoteGraph(double now)
    {
        m_remoteNow=now;
        if(!m_device.ready())
        {m_remoteGraphHits.clear();m_remoteMaps.clear();m_remoteFingerprints.clear();m_remoteRevision=static_cast<size_t>(-1);m_remotePaused={};m_remoteStopKey.clear();return;}
        if(m_remoteRevision!=m_document.revision()||m_remoteDocumentName!=m_document.name||m_remoteAssetRoot!=m_document.assetRoot)
        {
            m_remoteRevision=m_document.revision();m_remoteDocumentName=m_document.name;m_remoteAssetRoot=m_document.assetRoot;
            m_remoteMaps.clear();m_remoteFingerprints.clear();m_remoteGraphHits.clear();m_remotePaused={};m_remoteStopKey.clear();
            std::string error;
            if(compileBlueprintProject(m_document,m_remoteMaps,error))
                for(auto& output:m_remoteMaps){m_remoteFingerprints.push_back(scriptFingerprint(output.source.data(),output.source.size()));output.source.clear();}
        }
        bool gap=false;const auto hits=m_device.takeTrace(gap);if(gap||!hits.empty())m_remoteGap=gap;
        for(const auto& hit:hits)
        {
            const auto location=mapRemoteLocation(hit.file,hit.fingerprint,hit.line);
            if(location.graph>=m_document.graphs.size()||location.node>=m_document.graphs[location.graph].nodes.size())continue;
            m_remoteGraphHits.push_back({location,now});
            if(m_remoteGraphHits.size()>64)m_remoteGraphHits.erase(m_remoteGraphHits.begin());
        }
        while(!m_remoteGraphHits.empty()&&now-m_remoteGraphHits.front().time>0.8)m_remoteGraphHits.erase(m_remoteGraphHits.begin());
        if(!m_device.paused()){m_remotePaused={};m_remoteStopKey.clear();return;}
        m_remotePaused=mapRemoteLocation(m_device.file(),m_device.fingerprint(),m_device.line());
        if(m_remotePaused.graph>=m_document.graphs.size()||m_remotePaused.node>=m_document.graphs[m_remotePaused.graph].nodes.size())return;
        // Do not steal focus from a modal, text field, asset tab, or a graph drag.
        const auto key=m_device.file()+":"+std::to_string(m_device.line())+":"+m_device.fingerprint();
        if(m_remoteFollow&&m_remoteStopKey!=key&&!m_focus&&!modalOpen()&&!m_actorEditor&&!visualTabOpen()&&!m_drag&&!m_marquee&&!m_pinMenu&&!m_quickMenu&&!m_left)
        {
            m_remoteStopKey=key;focusDiagnostic(m_remotePaused);m_diagnostic={};
            m_status="Remote paused at verified Blueprint node.";
        }
    }
    void BlueprintWorkspace::drawRemoteControls(Canvas& c)
    {
        if(!m_device.ready()||!m_recoveryWarning.empty())return;
        const float y=kBottom+24;
        const char* labels[]={m_device.paused()?"Resume":"Pause","Over","Into","Out",m_device.tracing()?"Trace on":"Trace off",m_remoteFollow?"Follow on":"Follow off","Locate","Device"};
        for(unsigned i=0;i<8;++i)
        {
            const float x=12+100.f*static_cast<float>(i);
            c.roundRect(x,y,96,21,3,{0.20f,0.32f,0.45f,1});c.text(*m_font,labels[i],x+48,y+1,text,Align::Center);
        }
        if(kWidth>1120)c.text(*m_font,m_remoteGap?"Trace capped / gaps":"Remote debug",824,y+1,muted);
    }
    void BlueprintWorkspace::updateDeviceManager(Services& s,float x,float y,bool click)
    {
        auto& input=*s.input;
        bool attach=click&&in(x,y,730,150,900,190);
        if(click&&in(x,y,140,150,710,190)){m_deviceAddressEditing=true;input.setTyping(true);}
        if(m_deviceAddressEditing)for(u32 i=0;i<input.typedCount();++i)
        {
            const auto ch=input.typed(i);
            if(ch==13){attach=true;break;}
            if(ch==8&&!m_deviceAddress.empty())m_deviceAddress.pop_back();
            else if(((ch>='0'&&ch<='9')||ch=='.')&&m_deviceAddress.size()<15)m_deviceAddress+=static_cast<char>(ch);
        }
        if(attach&&!m_device.active()){m_deviceAddressEditing=false;input.setTyping(false);m_device.connect(m_deviceAddress,s.platform->seconds());return;}
        if(!click)return;
        if(in(x,y,920,150,1140,190)){m_device.disconnect();return;}
        if(in(x,y,140,258,1140,298))
        {
            const auto action=static_cast<unsigned>((x-140)/200);
            if(action<5)m_device.control(action);
            return;
        }
        if(in(x,y,140,600,340,638)){syncDeviceBreakpoints();return;}
        if(in(x,y,350,600,550,638)){m_deviceAddressEditing=false;input.setTyping(false);locateDeviceNode();return;}
        if(in(x,y,560,600,760,638)){m_deviceVariables=!m_deviceVariables;return;}
        if(in(x,y,770,600,990,638))
        {
            m_focus=28;m_deviceAddressEditing=false;input.setTyping(false);return;
        }
        if(in(x,y,1000,600,1150,638)){m_deviceAddressEditing=false;input.setTyping(false);m_focus=24;}
    }
    void BlueprintWorkspace::drawDeviceManager(Canvas& c)
    {
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});
        c.text(*m_font,"DEVICE MANAGER / REDUX REMOTE DEBUG",120,78,accent);
        c.text(*m_font,"Start Redux on the console with a media/debug folder. Trusted LAN only; TCP 4711.",140,116,muted);
        const auto button=[&](float x,float y,float w,const char* label,bool enabled)
        {c.roundRect(x,y,w,40,4,enabled?Color{0.20f,0.32f,0.45f,1}:Color{0.20f,0.22f,0.25f,1});c.text(*m_font,label,x+w/2,y+10,enabled?text:muted,Align::Center);};
        c.roundRect(140,150,570,40,4,m_deviceAddressEditing?Color{0.24f,0.35f,0.48f,1}:Color{0.18f,0.22f,0.29f,1});
        c.text(*m_font,m_deviceAddress.empty()?"Enter console IPv4 address":m_deviceAddress.c_str(),154,160,text);
        button(730,150,170,"Attach",!m_device.active());button(920,150,220,"Disconnect",m_device.active());
        c.clip(140,200,1000,28);c.text(*m_font,m_device.status().c_str(),140,205,m_device.ready()?accent:muted);c.noClip();
        c.text(*m_font,("Project target: "+m_document.target().label()).c_str(),140,233,muted);
        constexpr const char* actions[]={"Pause","Continue","Step over","Step into","Step out"};
        for(unsigned i=0;i<5;++i)button(140+200.f*i,258,190,actions[i],m_device.ready()&&(i==0?!m_device.paused():m_device.paused()));
        const auto& rows=m_deviceVariables?m_device.variables():m_device.rows();
        if(rows.empty())c.text(*m_font,m_deviceVariables?"Pause the remote script to inspect its locals.":"No measured telemetry. Older runtimes may not support counters.",140,318,muted);
        for(size_t i=0;i<rows.size()&&i<14;++i)
        {const float x=140+500.f*(i/7),y=318+27.f*(i%7);c.clip(x,y,490,26);c.text(*m_font,rows[i].c_str(),x,y,text);c.noClip();}
        c.clip(140,520,1000,24);c.text(*m_font,m_device.file().empty()?"Script location: none":(m_device.file()+":"+std::to_string(m_device.line())).c_str(),140,520,accent);c.noClip();
        c.clip(140,550,1000,24);c.text(*m_font,("Last log: "+m_device.log()).c_str(),140,550,muted);c.noClip();
        c.clip(140,576,1000,24);c.text(*m_font,m_deviceFeedback.empty()?"Tracked allocations are not total physical memory use.":m_deviceFeedback.c_str(),140,578,muted);c.noClip();
        button(140,600,200,"Sync watches",m_device.ready()&&m_device.sourceIdentity());
        button(350,600,200,"Locate node",m_device.paused()&&!m_device.fingerprint().empty());
        button(560,600,200,m_deviceVariables?"Show telemetry":"Show locals",true);
        button(770,600,220,"Play on Xbox",m_document.target().xbox());
        button(1000,600,150,"Back",true);
    }
    void BlueprintWorkspace::pollXboxDeploy(Services& s)
    {
        if(!m_deployRunning)return;
        const int code=s.platform->pollBlueprintProcess();if(code==-1)return;
        m_deployRunning=false;
        FileData result;
        if(!m_deployResult.empty()&&s.platform->fileTime(m_deployResult.c_str())&&s.platform->readFile(m_deployResult.c_str(),result,MemTag::General))
        {
            JsonTree json;
            if(result.size<=16384&&json.parse(reinterpret_cast<const char*>(result.data),result.size))
            {
                const auto root=json.root();
                m_deployStatus=(code==0&&root["ok"].flag()?"Complete: ":"Failed: ")+std::string(root["message"].text());
                return;
            }
        }
        m_deployStatus="Deployment helper exited without a valid result. Check Python, helper path and permissions.";
    }
    void BlueprintWorkspace::updateXboxDeploy(Services& s,float x,float y,bool click)
    {
        auto& input=*s.input;
        if(click&&in(x,y,1000,600,1150,638)){m_focus=27;m_deployField=-1;m_deployPassword.clear();input.setTyping(false);return;}
        if(m_deployRunning)return;
        const auto screens=m_document.screens();
        if(click&&in(x,y,140,493,1140,526)&&!screens.empty())
        {m_deployStart=(m_deployStart+1)%screens.size();m_deployField=-1;input.setTyping(false);return;}
        if(click&&in(x,y,420,152,1140,488))
        {m_deployField=static_cast<int>((y-152)/42);input.setTyping(true);}
        if(m_deployField>=0&&m_deployField<8)
        {
            auto& value=m_deployField==7?m_deployPassword:m_deployFields[m_deployField];
            const auto& raw=input.raw();const bool paste=raw.keys[static_cast<size_t>(Key::Control)]&&raw.keys[static_cast<size_t>(Key::V)];
            if(paste&&!m_deployPaste)
            {
                std::string clip;if(s.platform->clipboard(clip))for(unsigned char ch:clip)if(ch>=32&&ch<127&&value.size()<256)value+=static_cast<char>(ch);
            }
            m_deployPaste=paste;
            for(u32 i=0;i<input.typedCount();++i)
            {
                const auto ch=input.typed(i);
                if(ch==13){m_deployField=-1;input.setTyping(false);break;}
                if(ch==8&&!value.empty())value.pop_back();
                else if(ch>=32&&ch<127&&value.size()<256)value+=static_cast<char>(ch);
            }
        }
        if(!click||!in(x,y,140,600,430,638))return;
        m_deployField=-1;input.setTyping(false);
        if(!m_document.target().xbox()||screens.empty()){m_deployStatus="Select an Original Xbox project with a start screen.";return;}
        if(m_deployFields[0].empty()||m_deployFields[1].empty()||m_deployFields[2].empty()||m_deployFields[3].empty())
        {m_deployStatus="Set Python executable, deployment helper, Xbox runtime folder and cooker.";return;}
        const bool ftp=!m_deployFields[4].empty();
        if(ftp)
        {
            u32 ip=0;
            if(!netParseAddress(m_deployFields[4].c_str(),ip)||!ip||ip==kBroadcast||m_deployFields[5].empty()||m_deployFields[6].empty()||m_deployPassword.empty())
            {m_deployStatus="For FTP, supply IPv4, user, new remote Games folder and password.";return;}
            if(!s.platform->showMessage("Play on Xbox","Upload a NEW title using plaintext FTP on your trusted LAN? Existing titles will not be overwritten.",true))
            {m_deployStatus="FTP cancelled.";return;}
        }
        if(!compileAndExport(s)){m_deployStatus=m_status;return;}
        const std::string job=m_deviceExport+"-job",output=m_deviceExport+"-xbox";
        if(!s.platform->createOwnFolder(job.substr(4).c_str())){m_deployStatus="Cannot create deployment job folder.";return;}
        std::string exportPath,outputPath,requestPath;
        if(!s.platform->blueprintOwnPath(m_deviceExport.c_str(),exportPath)||!s.platform->blueprintOwnPath(output.c_str(),outputPath)||!s.platform->blueprintOwnPath((job+"/request.json").c_str(),requestPath))
        {m_deployStatus="Cannot resolve desktop export paths.";return;}
        JsonValue request;
        request.put("export")->setText(exportPath);request.put("output")->setText(outputPath);
        request.put("runtime")->setText(m_deployFields[2]);request.put("cook")->setText(m_deployFields[3]);
        request.put("start")->setText(screens[m_deployStart%screens.size()]);
        request.put("host")->setText(m_deployFields[4]);request.put("user")->setText(m_deployFields[5]);request.put("remote")->setText(m_deployFields[6]);
        if(!writeVerified(s,job+"/request.json",request.write(false))){m_deployStatus=m_status;return;}
        m_deployResult=job+"/result.json";
        m_deployRunning=s.platform->startBlueprintProcess(m_deployFields[0],{m_deployFields[1],"--request",requestPath},ftp?m_deployPassword:std::string());
        m_deployPassword.clear();
        m_deployStatus=m_deployRunning?"Cooking / preparing title in a separate process...":"Could not start the deployment helper.";
    }
    void BlueprintWorkspace::drawXboxDeploy(Canvas& c)
    {
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});
        c.text(*m_font,"PLAY ON XBOX / BUILD & DEPLOY",120,78,accent);
        c.text(*m_font,"Cook a fresh development title; optional FTP upload; then launch on the dashboard.",140,110,muted);
        const char* labels[]={"Python executable","deploy_xbox.py path","Extracted Xbox runtime","Redux cook executable","FTP IPv4 (blank = local only)","FTP username","New remote folder","FTP password (not saved)"};
        for(int i=0;i<8;++i)
        {
            const float y=152+42.f*static_cast<float>(i);
            c.text(*m_font,labels[i],140,y+9,muted);
            c.roundRect(420,y,720,36,4,m_deployField==i?Color{0.24f,0.35f,0.48f,1}:Color{0.18f,0.22f,0.29f,1});
            const std::string value=i==7?std::string(m_deployPassword.size(),'*'):m_deployFields[i];
            c.clip(430,y,700,36);c.text(*m_font,value.c_str(),430,y+8,text);c.noClip();
        }
        const auto screens=m_document.screens();
        c.roundRect(140,493,1000,33,4,{0.20f,0.32f,0.45f,1});
        c.text(*m_font,("Start screen: "+(screens.empty()?std::string("None"):screens[m_deployStart%screens.size()])+" >").c_str(),150,500,text);
        c.clip(140,533,1000,62);c.paragraph(*m_font,m_deployStatus.empty()?"Use matching runtime and cooker. FTP is optional; password is not written to the job file.":m_deployStatus.c_str(),140,535,1000,muted);c.noClip();
        c.roundRect(140,600,290,38,4,m_deployRunning?Color{0.20f,0.22f,0.25f,1}:Color{0.20f,0.32f,0.45f,1});
        c.text(*m_font,m_deployRunning?"Working...":"Build / install new title",285,608,text,Align::Center);
        c.roundRect(1000,600,150,38,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,"Back",1075,608,text,Align::Center);
    }
    void BlueprintWorkspace::drawSceneBrowser(Canvas& c)
    {
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});c.text(*m_font,"SCENES & CINEMATICS",120,78,accent);
        c.text(*m_font,"Scenes are native Redux screens. Cinematics are reusable project assets.",120,108,muted);
        c.text(*m_font,"SCENES",120,137,accent);c.text(*m_font,"CAMERA SEQUENCES / shared assets",470,137,accent);
        const auto button=[&](float x,float y,float w,const char* label){c.roundRect(x,y,w,34,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,label,x+w/2,y+7,text,Align::Center);};
        const auto scenes=m_document.screens();for(size_t i=0;i<8&&m_sceneFirst+i<scenes.size();++i){const auto& scene=scenes[m_sceneFirst+i];const float yy=160.f+44.f*static_cast<float>(i);c.rect(120,yy,330,40,scene==m_sceneBrowserOwner?Color{0.20f,0.34f,0.47f,1}:Color{0.20f,0.23f,0.27f,1});c.clip(128,yy,314,40);c.text(*m_font,scene.c_str(),132,yy+10,text);c.noClip();}
        size_t index=0;for(const auto& asset:m_document.visualAssets())if(asset.kind=="sequence")
        {
            const auto i=index++;if(i<m_sceneAssetFirst||i>=m_sceneAssetFirst+8)continue;const float yy=160.f+44.f*static_cast<float>(i-m_sceneAssetFirst);
            unsigned used=0;for(const auto& g:m_document.graphs)if(g.owner==m_sceneBrowserOwner)for(const auto& n:g.nodes)if(n.kind=="camera:sequence")for(const auto& v:n.values)if(v.pin=="asset"&&v.value==asset.id)++used;
            c.rect(470,yy,680,40,asset.id==m_sceneBrowserAsset?Color{0.20f,0.34f,0.47f,1}:Color{0.20f,0.23f,0.27f,1});c.clip(478,yy,500,40);c.text(*m_font,asset.title.c_str(),482,yy+4,text);c.text(*m_font,asset.id.c_str(),482,yy+21,muted);c.noClip();c.text(*m_font,("In scene: "+std::to_string(used)).c_str(),1000,yy+10,muted);
        }
        if(!index)c.text(*m_font,"Create your first reusable cinematic.",482,175,muted);
        button(120,540,160,"Open Blueprint");button(290,540,160,"+ Scene");button(470,540,160,"+ Sequence");button(640,540,160,"Edit Sequence");button(810,540,160,"Place in Scene");button(980,540,170,"Rename");button(120,582,330,"Input Action Editor");button(470,582,160,"Delete Asset");button(640,582,200,"Build & Package");button(1030,610,130,"Close");
        c.clip(120,620,880,26);c.text(*m_font,m_status.c_str(),120,626,muted);c.noClip();
        if(m_sceneNaming){c.rect(100,64,1080,594,{0,0,0,0.65f});c.roundRect(280,440,880,170,6,{0.16f,0.20f,0.25f,1});c.text(*m_font,"Rename cinematic",300,454,accent);c.roundRect(300,490,840,38,4,{0.24f,0.35f,0.48f,1});c.text(*m_font,m_sceneNameDraft.c_str(),310,498,text);c.text(*m_font,"Enter = Apply | Escape = Cancel",300,568,muted);button(830,556,150,"Apply");button(990,556,150,"Cancel");}
    }
    bool BlueprintWorkspace::beginSequenceEditor(Input& input)
    {
        if(m_selection>=m_document.graphs[m_graph].nodes.size())return false;
        const auto& node=m_document.graphs[m_graph].nodes[m_selection];if(node.kind!="camera:sequence")return false;
        m_sequenceAsset.clear();for(const auto& v:node.values)if(v.pin=="asset"&&!v.value.empty())return beginSequenceAsset(v.value,input);
        std::string raw="0,0,2,8,0,1,0,60,0;3,5,3,5,0,1,0,50,0;6,0,2,4,0,1,0,60,0";
        for(const auto& v:node.values)if(v.pin=="keys")raw=v.value;
        if(!blueprintCameraKeys(raw,m_sequenceKeys,m_status))return false;
        m_sequenceKey=0;m_sequenceTime=0;m_sequenceLast=0;m_sequencePlaying=m_sequenceScrub=false;m_sequenceField=-1;m_focus=21;input.setTyping(true);clearWire();m_status="Edit camera keys. Apply commits the sequence.";return true;
    }
    bool BlueprintWorkspace::finishSequenceField()
    {
        if(m_sequenceField<0)return true;
        if(m_sequenceField==8){auto candidate=m_sequenceKeys;candidate[m_sequenceKey].event=m_sequenceText;std::vector<BlueprintCameraKey> checked;if(!blueprintCameraKeys(blueprintCameraKeyText(candidate),checked,m_status))return false;m_sequenceKeys=std::move(checked);m_sequenceField=-1;return true;}
        char* end=nullptr;const float v=std::strtof(m_sequenceText.c_str(),&end);
        if(m_sequenceText.empty()||end!=m_sequenceText.c_str()+m_sequenceText.size()||!std::isfinite(v)){m_status="Enter a finite number.";return false;}
        auto candidate=m_sequenceKeys;auto& key=candidate[m_sequenceKey];
        if(m_sequenceField==0)key.time=v;else if(m_sequenceField<=3)key.from[m_sequenceField-1]=v;else if(m_sequenceField<=6)key.to[m_sequenceField-4]=v;else key.fov=v;
        std::vector<BlueprintCameraKey> checked;if(!blueprintCameraKeys(blueprintCameraKeyText(candidate),checked,m_status))return false;
        m_sequenceKeys=std::move(checked);m_sequenceTime=m_sequenceKeys[m_sequenceKey].time;m_sequenceField=-1;return true;
    }
    void BlueprintWorkspace::updateSequenceEditor(Services& s,float x,float y,bool left,bool click)
    {
        auto& input=*s.input;
        if(click&&(!in(x,y,100,64,1180,658)||in(x,y,1040,610,1160,646))){m_focus=0;input.setTyping(false);return;}
        const double now=s.platform->seconds();if(m_sequencePlaying&&m_sequenceLast>0){m_sequenceTime=std::min(m_sequenceKeys.back().time,m_sequenceTime+static_cast<float>(std::clamp(now-m_sequenceLast,0.0,0.1)));if(m_sequenceTime>=m_sequenceKeys.back().time)m_sequencePlaying=false;}m_sequenceLast=now;
        bool apply=click&&in(x,y,910,610,1030,646);
        for(u32 i=0;i<input.typedCount();++i)
        {
            const auto ch=input.typed(i);if(ch==13){if(m_sequenceField>=0){finishSequenceField();return;}apply=true;break;}
            if(m_sequenceField<0)continue;
            if(ch==8){if(m_sequenceReplace)m_sequenceText.clear();else if(!m_sequenceText.empty())m_sequenceText.pop_back();m_sequenceReplace=false;}
            else if(ch<128&&(m_sequenceField==8?(std::isalnum(static_cast<unsigned char>(ch))||ch==95):std::string("0123456789.eE+-").find(static_cast<char>(ch))!=std::string::npos)){if(m_sequenceReplace){m_sequenceText.clear();m_sequenceReplace=false;}appendCharacter(m_sequenceText,ch,48);}
        }
        if(apply){if(finishSequenceField()&&(m_sequenceAsset.empty()?m_document.setInputValue(m_graph,m_selection,"keys",blueprintCameraKeyText(m_sequenceKeys),m_status):m_document.setCameraSequenceKeys(m_sequenceAsset,m_sequenceKeys,m_status))){m_focus=0;input.setTyping(false);rebuildPins();m_status="Camera sequence applied."; }return;}
        if(click&&!finishSequenceField())return;
        if(click&&in(x,y,800,540,1150,574)&&m_sequenceAsset.empty()){std::string id;if(m_document.promoteCameraSequence(m_graph,m_selection,"Camera Sequence",m_sequenceKeys,id,m_status)){m_sequenceAsset=id;rebuildPins();m_status="Saved as reusable sequence asset."; }return;}
        if(click&&in(x,y,120,115,230,148)){if(m_sequenceTime>=m_sequenceKeys.back().time)m_sequenceTime=0;m_sequencePlaying=!m_sequencePlaying;return;}
        if(click&&in(x,y,240,115,350,148)){m_sequencePlaying=false;m_sequenceTime=0;return;}
        if(click&&in(x,y,360,115,500,148))
        {
            m_sequencePlaying=false;
            if(m_sequenceKeys.size()>=64){m_status="Maximum 64 keys.";return;}
            auto key=blueprintCameraSample(m_sequenceKeys,m_sequenceTime);key.time=m_sequenceTime;key.cut=false;key.smooth=false;key.event.clear();
            if(std::any_of(m_sequenceKeys.begin(),m_sequenceKeys.end(),[&](const BlueprintCameraKey& k){return std::abs(k.time-key.time)<0.001f;})){key=m_sequenceKeys.back();key.event.clear();key.time+=1.f;if(key.time>3600){m_status="Maximum sequence duration is 3600 seconds.";return;}}
            const auto at=std::lower_bound(m_sequenceKeys.begin(),m_sequenceKeys.end(),key.time,[](const BlueprintCameraKey& k,float time){return k.time<time;});
            m_sequenceKey=static_cast<size_t>(at-m_sequenceKeys.begin());m_sequenceKeys.insert(at,key);m_sequenceTime=key.time;return;
        }
        if(click&&in(x,y,510,115,650,148)){if(m_sequenceKey>0&&m_sequenceKeys.size()>2){m_sequenceKeys.erase(m_sequenceKeys.begin()+static_cast<std::ptrdiff_t>(m_sequenceKey));m_sequenceKey=std::min(m_sequenceKey,m_sequenceKeys.size()-1);m_sequenceTime=m_sequenceKeys[m_sequenceKey].time;}else m_status="Keep the first key and at least two keys.";return;}
        if(click&&in(x,y,660,115,820,148)){auto& key=m_sequenceKeys[m_sequenceKey];if(key.cut){key.cut=false;key.smooth=true;}else if(key.smooth)key.smooth=false;else key.cut=true;return;}
        const float duration=m_sequenceKeys.back().time;
        if(click&&in(x,y,280,175,1150,320))
        {
            m_sequencePlaying=false;const float time=duration*std::clamp((x-280)/870.f,0.f,1.f);
            if(y>=207){float distance=9;for(size_t i=0;i<m_sequenceKeys.size();++i){const float d=std::abs(x-(280+870*m_sequenceKeys[i].time/duration));if(d<distance){distance=d;m_sequenceKey=i;}}if(distance<9){m_sequenceTime=m_sequenceKeys[m_sequenceKey].time;return;}}
            m_sequenceScrub=true;m_sequenceTime=time;
        }
        if(m_sequenceScrub){if(left)m_sequenceTime=duration*std::clamp((x-280)/870.f,0.f,1.f);else m_sequenceScrub=false;}
        if(click&&in(x,y,800,475,1150,512)){m_sequenceField=8;m_sequenceText=m_sequenceKeys[m_sequenceKey].event;m_sequenceReplace=true;m_sequencePlaying=false;return;}
        if(click&&in(x,y,120,374,1152,421))
        {
            const int field=std::min(7,static_cast<int>((x-120)/129.f));const auto& key=m_sequenceKeys[m_sequenceKey];const float values[]={key.time,key.from[0],key.from[1],key.from[2],key.to[0],key.to[1],key.to[2],key.fov};
            m_sequenceField=field;m_sequenceText=std::to_string(values[field]);m_sequenceReplace=true;m_sequencePlaying=false;
        }
    }
    void BlueprintWorkspace::drawSequenceEditor(Canvas& c)
    {
        c.roundRect(100,64,1080,594,8,{0.15f,0.17f,0.20f,1});c.text(*m_font,("SEQUENCER / "+(m_sequenceAsset.empty()?m_document.graphs[m_graph].owner:m_sequenceAsset)).c_str(),120,78,accent);
        const auto button=[&](float x,float y,float w,const char* label){c.roundRect(x,y,w,33,4,{0.20f,0.32f,0.45f,1});c.text(*m_font,label,x+w/2,y+7,text,Align::Center);};
        button(120,115,110,m_sequencePlaying?"Pause":"Play");button(240,115,110,"Rewind");button(360,115,140,"+ Camera key");button(510,115,140,"Delete key");button(660,115,160,m_sequenceKeys[m_sequenceKey].cut?"Cut":m_sequenceKeys[m_sequenceKey].smooth?"Ease in/out":"Linear");
        const float duration=m_sequenceKeys.back().time;
        c.text(*m_font,("Time "+std::to_string(m_sequenceTime).substr(0,5)+" / "+std::to_string(duration).substr(0,5)+" s").c_str(),850,123,text);
        c.rect(120,165,1030,165,{0.10f,0.12f,0.15f,1});const char* tracks[]={"Camera position","Look target","Field of view"};
        for(unsigned i=0;i<3;++i){const float yy=207.f+38.f*static_cast<float>(i);c.rect(120,yy,1030,35,{0.22f,0.24f,0.28f,1});c.text(*m_font,tracks[i],132,yy+8,text);}
        for(unsigned i=0;i<=10;++i){const float xx=280.f+87.f*static_cast<float>(i);c.line(xx,184,xx,320,1,{0.33f,0.35f,0.38f,1});c.text(*m_font,std::to_string(duration*static_cast<float>(i)/10.f).substr(0,4).c_str(),xx,169,muted,Align::Center);}
        for(size_t i=0;i<m_sequenceKeys.size();++i){const float xx=280+870*m_sequenceKeys[i].time/duration;for(unsigned j=0;j<3;++j)c.circle(xx,224.f+38.f*static_cast<float>(j),i==m_sequenceKey?6.f:4.f,i==m_sequenceKey?Color{1,0.75f,0.25f,1}:accent);if(!m_sequenceKeys[i].event.empty())c.rect(xx-4,309,8,8,{0.83f,0.49f,0.93f,1});}
        const float cursor=280+870*m_sequenceTime/duration;c.line(cursor,181,cursor,320,2,{1,0.45f,0.25f,1});
        const auto& key=m_sequenceKeys[m_sequenceKey];const float values[]={key.time,key.from[0],key.from[1],key.from[2],key.to[0],key.to[1],key.to[2],key.fov};const char* labels[]={"Time (s)","Position X","Position Y","Position Z","Target X","Target Y","Target Z","FOV"};
        c.text(*m_font,"SELECTED CAMERA KEY",120,344,accent);
        for(unsigned i=0;i<8;++i){const float xx=120.f+129.f*static_cast<float>(i);c.text(*m_font,labels[i],xx,374,muted);c.roundRect(xx,395,123,26,3,static_cast<int>(i)==m_sequenceField?Color{0.24f,0.35f,0.48f,1}:Color{0.20f,0.23f,0.28f,1});const auto value=static_cast<int>(i)==m_sequenceField?m_sequenceText:std::to_string(values[i]);c.clip(xx+4,395,115,26);c.text(*m_font,value.c_str(),xx+5,400,text);c.noClip();}
        c.text(*m_font,"CAMERA PATH PREVIEW / XZ (schematic)",120,438,muted);
        float loX=key.from[0],hiX=loX,loZ=key.from[2],hiZ=loZ;
        for(const auto& k:m_sequenceKeys){loX=std::min({loX,k.from[0],k.to[0]});hiX=std::max({hiX,k.from[0],k.to[0]});loZ=std::min({loZ,k.from[2],k.to[2]});hiZ=std::max({hiZ,k.from[2],k.to[2]});}
        const float scale=std::min(650.f/std::max(1.f,hiX-loX),100.f/std::max(1.f,hiZ-loZ));const auto px=[&](float v){return 445+(v-(loX+hiX)/2)*scale;};const auto pz=[&](float v){return 520+(v-(loZ+hiZ)/2)*scale;};
        c.rect(120,460,650,120,{0.10f,0.12f,0.15f,1});
        for(size_t i=1;i<m_sequenceKeys.size();++i){const auto& a=m_sequenceKeys[i-1];const auto& b=m_sequenceKeys[i];if(!b.cut)c.line(px(a.from[0]),pz(a.from[2]),px(b.from[0]),pz(b.from[2]),2,accent);}
        const auto sample=blueprintCameraSample(m_sequenceKeys,m_sequenceTime);c.line(px(sample.from[0]),pz(sample.from[2]),px(sample.to[0]),pz(sample.to[2]),2,{1,0.75f,0.25f,1});c.circle(px(sample.from[0]),pz(sample.from[2]),5,accent);c.circle(px(sample.to[0]),pz(sample.to[2]),4,{1,0.75f,0.25f,1});
        c.text(*m_font,"Camera  /  Target",800,470,text);c.text(*m_font,"Event at selected key (local void function)",800,447,muted);
        c.roundRect(800,475,350,37,4,m_sequenceField==8?Color{0.24f,0.35f,0.48f,1}:Color{0.20f,0.23f,0.28f,1});const auto marker=m_sequenceField==8?m_sequenceText:key.event;c.clip(805,475,340,37);c.text(*m_font,marker.empty()?"Optional function name":marker.c_str(),810,483,text);c.noClip();
        if(m_sequenceAsset.empty())button(800,540,350,"Save as reusable sequence");
        c.clip(120,582,1040,25);c.text(*m_font,m_status.c_str(),120,586,muted);c.noClip();
        c.text(*m_font,"Enter = Apply (field first) | Escape = Cancel",120,620,muted);button(910,610,120,"Apply");button(1040,610,120,"Cancel");
    }
    bool BlueprintWorkspace::beginCurveEditor(Input& input)
    {
        std::vector<std::string> values;
        if(!m_document.cubicPoints(m_graph,m_selection,values,m_status))return false;
        float minX=0,maxX=0,minY=0,maxY=0;
        for(size_t i=0;i<4;++i)
        {
            std::vector<std::string> parts;
            if(!blueprintInputComponents("Vec2",values[i],parts))return false;
            const float x=std::strtof(parts[0].c_str(),nullptr),y=std::strtof(parts[1].c_str(),nullptr);
            if(std::abs(x)>1000000||std::abs(y)>1000000){m_status="Visual editor supports coordinates within +/-1,000,000. Use the input inspector for larger values.";return false;}
            m_curveX[i]=x;m_curveY[i]=y;
            if(i==0){minX=maxX=x;minY=maxY=y;}
            else {minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);}
        }
        m_curveCenterX=(minX+maxX)*0.5f;m_curveCenterY=(minY+maxY)*0.5f;
        m_curveScale=std::min(650.f/std::max(1.f,maxX-minX),330.f/std::max(1.f,maxY-minY));
        m_curveDrag=-1;m_curveChanged=false;m_focus=17;input.setTyping(false);clearWire();
        m_status="Drag the four control points. Apply saves all four in one undo step.";NX_INFO("Blueprint Studio: cubic curve editor opened");return true;
    }
    bool BlueprintWorkspace::jointPoint(const InputDetail& detail,float& x,float& y) const
    {
        if(detail.type!="Vec2"&&detail.type!="Vec3")return false;
        if(!detail.editable)return false;
        std::vector<std::string> parts;
        const auto value=detail.origin=="Required"?(detail.type=="Vec2"?"0,0":"0,0,0"):detail.value;
        if(!blueprintInputComponents(detail.type,value,parts))return false;
        const unsigned a=m_jointPlane==2?1:0,b=m_jointPlane==0?1:2;
        if(a>=parts.size()||b>=parts.size())return false;
        x=std::strtof(parts[a].c_str(),nullptr);y=std::strtof(parts[b].c_str(),nullptr);
        return std::isfinite(x)&&std::isfinite(y);
    }
    void BlueprintWorkspace::beginJointEditor(Input& input)
    {
        m_jointPlane=0;m_jointPin.clear();m_jointExtent=4;
        for(const auto& detail:inputDetails(m_selection))
        {
            float x=0,y=0;
            if(jointPoint(detail,x,y))
            {
                if(m_jointPin.empty())m_jointPin=detail.name;
                m_jointExtent=std::max(m_jointExtent,std::max(std::abs(x),std::abs(y))*1.15f);
            }
        }
        m_focus=18;input.setTyping(false);clearWire();
        m_status="Select a constant vector and click the grid. Other values: click Edit.";
    }
    void BlueprintWorkspace::updateJointEditor(Input& input,float x,float y,bool click)
    {
        if(!click)return;
        if(in(x,y,990,110,1090,144)){m_focus=0;return;}
        const auto details=inputDetails(m_selection);
        const bool three=std::any_of(details.begin(),details.end(),[](const InputDetail& row){return row.type=="Vec3";});
        if(in(x,y,230,155,510,186))
        {
            if(three)m_jointPlane=std::min(2u,static_cast<unsigned>((x-230)/94));
            m_jointExtent=4;
            for(const auto& detail:details){float a=0,b=0;if(jointPoint(detail,a,b))m_jointExtent=std::max(m_jointExtent,std::max(std::abs(a),std::abs(b))*1.15f);}
            return;
        }
        if(in(x,y,520,155,610,186)){m_jointExtent=std::max(0.25f,m_jointExtent*0.5f);return;}
        if(in(x,y,620,155,710,186)){m_jointExtent=std::min(1000000.f,m_jointExtent*2.f);return;}
        for(size_t i=0;i<details.size();++i)
        {
            const auto& detail=details[i];const float yy=196+40*static_cast<float>(i);
            if(!in(x,y,730,yy,1090,yy+36)||!detail.editable)continue;
            if(x>=1000){beginInputEdit(m_selection,detail.name,input);return;}
            if(detail.type=="Vec2"||detail.type=="Vec3"){m_jointPin=detail.name;m_status="Selected "+detail.name+"; click the grid to place it.";}
            else beginInputEdit(m_selection,detail.name,input);
            return;
        }
        if(!m_jointPin.empty()&&in(x,y,230,196,710,556))
        {
            const float u=(x-470)/170*m_jointExtent,v=(376-y)/170*m_jointExtent;
            if(m_document.setJointProjection(m_graph,m_selection,m_jointPin,m_jointPlane,u,v,m_status))
            {rebuildPins();m_diagnostic={};m_status="Updated "+m_jointPin+" (undo available).";}
        }
    }
    void BlueprintWorkspace::drawJointEditor(Canvas& c)
    {
        const auto details=inputDetails(m_selection);
        const bool three=std::any_of(details.begin(),details.end(),[](const InputDetail& row){return row.type=="Vec3";});
        const Color panel={0.19f,0.33f,0.50f,1},dark={0.13f,0.15f,0.18f,1};
        const auto button=[&](float x,float y,float width,const char* label,bool active)
        {c.roundRect(x,y,width,31,4,active?panel:dark);c.text(*m_font,label,x+width/2,y+5,text,Align::Center);};
        c.roundRect(210,95,900,530,8,{0.16f,0.16f,0.17f,1});
        const auto& node=m_document.graphs[m_graph].nodes[m_selection];
        c.text(*m_font,("PHYSICS JOINT / "+title(node)).c_str(),230,117,accent);button(990,110,100,"Close",false);
        button(230,155,90,"XY",m_jointPlane==0);if(three){button(324,155,90,"XZ",m_jointPlane==1);button(418,155,90,"YZ",m_jointPlane==2);}
        button(520,155,90,"Zoom +",false);button(620,155,90,"Zoom -",false);
        c.rect(230,196,480,360,dark);c.push();c.clip(230,196,480,360);
        for(int i=-4;i<=4;++i)
        {
            const float offset=42.5f*static_cast<float>(i);const Color ink=i==0?muted:Color{0.23f,0.26f,0.30f,1};
            c.line(470+offset,196,470+offset,556,1,ink);c.line(230,376+offset,710,376+offset,1,ink);
        }
        c.text(*m_font,m_jointPlane==2?"Y":"X",690,379,muted);c.text(*m_font,m_jointPlane==0?"Y":"Z",475,200,muted);
        std::vector<Vec2> anchors;
        for(const auto& row:details)
        {
            float u=0,v=0;if(!jointPoint(row,u,v))continue;
            if(row.name!="axis")anchors.push_back({470+u/m_jointExtent*170,376-v/m_jointExtent*170});
        }
        if(anchors.size()>=2)c.line(anchors[0].x,anchors[0].y,anchors[1].x,anchors[1].y,2,muted);
        for(const auto& row:details)
        {
            float u=0,v=0;if(!jointPoint(row,u,v))continue;
            const float px=470+u/m_jointExtent*170,py=376-v/m_jointExtent*170;
            const auto ink=row.name==m_jointPin?accent:Color{0.35f,0.82f,0.58f,1};
            if(row.name=="axis")c.line(470,376,px,py,2,ink);
            c.circle(px,py,6,ink);c.text(*m_font,row.name.c_str(),px+9,py-16,ink);
        }
        c.pop();
        for(size_t i=0;i<details.size();++i)
        {
            const auto& row=details[i];const float yy=196+40*static_cast<float>(i);
            c.rect(730,yy,360,36,row.name==m_jointPin?panel:dark);
            c.push();c.clip(738,yy,255,36);
            c.text(*m_font,(row.name+" / "+row.type).c_str(),738,yy+1,text);c.text(*m_font,row.value.c_str(),738,yy+18,muted);c.pop();
            if(row.editable)button(1000,yy+2,90,"Edit",false);
        }
        c.clip(230,569,860,42);c.text(*m_font,m_status.c_str(),230,573,muted);c.noClip();
        c.text(*m_font,"World-coordinate schematic | Connected body/parameter pins remain graph-driven.",230,595,muted);
    }
    void BlueprintWorkspace::update(Services& s)
    {
        auto& input = *s.input;
        m_device.update(s.platform->seconds());updateRemoteGraph(s.platform->seconds());pollXboxDeploy(s);
        resizeWorkspace(*s.canvas);
        rememberVisualTab();refreshWidgetPreview(s);refreshPlacementPreview(s);refreshContentPreviews(s);
        if (input.pressed(Action::Menu))
        {
            if(!m_notice.empty()){m_notice.clear();m_left=input.raw().mouseButtons[0];return;}
            if(m_audioEditor){stopTone(s);m_audioEditor=false;m_status="Audio editor closed.";return;}
            if(m_contentPanelDrag||m_contentResize){m_contentPanelDrag=m_contentResize=false;m_status="Content panel move ended.";return;}
            if(!m_contentDrag.empty()){m_contentDrag.clear();m_status="Content drag cancelled.";return;}
            if(m_actorEditor){if(m_visualInput){m_rgbOpen=false;m_visualInput=0;m_visualText.clear();input.setTyping(false);}else if(m_actorDragAxis>=0){cancelActorDrag();m_status="Component drag cancelled.";}else{leaveActorViewport(true);}return;}
            if(m_assetBrowser){if(m_visualInput){m_rgbOpen=false;m_visualInput=0;m_visualText.clear();input.setTyping(false);m_status="Input cancelled.";return;}if(m_placementDragging){cancelVisualPlacement();m_status="Placement cancelled.";return;}if(m_visualPlacement){m_visualPlacement=false;return;}if(m_widgetDesigner){if(m_widgetPreviewDrag){m_widgetPreviewDrag=false;m_status="Widget drag cancelled.";return;}if(m_widgetDragging){m_widgetDragging=false;m_widgetDragId.clear();return;}m_widgetDesigner=false;releaseWidgetPreview();return;}m_assetBrowser=false;m_status="Asset browser closed.";return;}
            if(m_browsing){if(m_projectCreate){m_projectCreate=0;m_projectName.clear();input.setTyping(false);}else{m_browsing=false;m_status="Project browser closed.";}return;}
            if(m_rgbOpen){m_rgbOpen=false;return;}
            if(m_moveWire!=static_cast<size_t>(-1)){m_moveWire=static_cast<size_t>(-1);m_status="Connection move cancelled.";return;}
            if(m_focus==28){m_deployField=-1;m_deployPassword.clear();input.setTyping(false);m_focus=27;return;}
            if(m_focus==27){m_deviceAddressEditing=false;input.setTyping(false);m_focus=24;return;}
            if(m_focus==22&&m_sceneNaming){m_sceneNaming=false;input.setTyping(false);return;}
            if(m_focus){if(m_focus==25){m_deleteSelection.clear();m_skipDeleteConfirmation=false;}m_focus=0;input.setTyping(false);m_status="Input cancelled.";return;}
            if(m_marquee){m_marquee=false;m_selectedNodes=m_marqueeBase;m_selection=m_selectedNodes.empty()?static_cast<size_t>(-1):m_selectedNodes.back();m_left=input.raw().mouseButtons[0];return;}
            if(m_minimapDragging){m_minimapDragging=false;return;}
            if(m_pinContext){m_pinContext=false;return;}
            if(m_nodeContext){m_nodeContext=false;return;}
            if(m_layoutDragging||m_layoutMenu){m_commentResize=0;m_layoutDragging=m_layoutMenu=false;m_status="Layout action cancelled.";return;}
            if(m_quickDrag||m_quickMenu){m_quickDrag=m_quickMenu=false;m_status="Variable placement cancelled.";return;}
            if(m_pinMenu){closePinMenu(s);m_status="Node search cancelled.";return;}
            if(m_pending!=static_cast<size_t>(-1)){clearWire();m_status="Connection cancelled.";return;}
            if(m_drag){cancelDrag();m_left=input.raw().mouseButtons[0];m_status="Drag cancelled.";return;}
            returnToRedux(s);return;
        }
        std::string dropped;
        if (!m_marquee && !m_drag && !m_layoutDragging && !m_layoutMenu && !m_pinMenu && !m_pinDrag && !m_quickDrag && !m_quickMenu && !m_focus && !m_browsing && s.platform->takeDropped(dropped))
        {
            if(m_audioEditor)m_status="Close audio synthesis before importing files.";
            else if(m_assetBrowser&&m_assetLibrary)m_status="Switch to Media before importing files.";
            else if(m_assetBrowser)importAsset(s,dropped);
            else if(blueprintFileName(dropped)||blueprintPackageName(dropped))openFile(s,dropped,false);
            else if(dropped.ends_with(".as"))importSource(s,dropped);
            else m_status="Open Assets before dropping a scene or media file.";
        }
        auto& canvas = *s.canvas;
        const auto& raw = input.raw();
        // Match draw()'s adaptive canvas and modal transform.
        const float scale = std::min(canvas.pixelWidth()/kWidth, canvas.pixelHeight()/kHeight);
        if (!(scale > 0)) return;
        float x = (raw.mouseX - (canvas.pixelWidth()-kWidth*scale)*0.5f)/scale;
        float y = (raw.mouseY - (canvas.pixelHeight()-kHeight*scale)*0.5f)/scale;
        const bool over = in(x,y,graphAreaLeft(),graphAreaTop(),graphAreaRight(),tabTop());
        const bool left = raw.mouseButtons[0], pan = raw.mouseButtons[1] || raw.mouseButtons[2];
        const auto held=[&](Key key){return raw.keys[static_cast<size_t>(key)];};
        const bool control=held(Key::Control);
        const unsigned keys=(control&&held(Key::S)?1u:0u)|(control&&held(Key::Z)&&!held(Key::Shift)?2u:0u)|
            (control&&(held(Key::Y)||(held(Key::Z)&&held(Key::Shift)))?4u:0u)|(!control&&held(Key::Q)?8u:0u)|(control&&held(Key::F)?16u:0u)|(!control&&held(Key::C)?32u:0u)|(held(Key::Delete)?64u:0u)|(held(Key::Enter)?128u:0u);
        const unsigned pressed=keys&~m_shortcuts;m_shortcuts=keys;
        const bool click=left&&!m_left;
        if(!left)m_paletteDragging=false;
        const bool right=raw.mouseButtons[1];
        const auto placeInlineCaret=[&](const std::string& value,float position)
        {
            size_t caret=0;
            for(size_t p=0;p<value.size();)
            {
                size_t end=p+1;while(end<value.size()&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)++end;
                const float a=m_font?canvas.measure(*m_font,value.substr(0,p).c_str()).x:8.f*static_cast<float>(p);
                const float b=m_font?canvas.measure(*m_font,value.substr(0,end).c_str()).x:8.f*static_cast<float>(end);
                if(position<(a+b)*.5f)break;
                caret=end;p=end;
            }
            return caret;
        };
        const auto selectInlineField=[&](float fx,float fw)
        {
            m_inlineSelectAll=false;
            const auto labels=blueprintInputComponentLabels(m_editType);
            if(!labels.empty()&&m_inputComponents.size()==labels.size())
            {
                const float cell=fw/static_cast<float>(labels.size());
                m_inputComponent=std::min(labels.size()-1,static_cast<size_t>(std::max(0.f,x-fx)/cell));
                m_inlineCaret=placeInlineCaret(m_inputComponents[m_inputComponent],(x-fx-cell*static_cast<float>(m_inputComponent))/m_zoom-17.f);
            }
            else m_inlineCaret=placeInlineCaret(m_edit,(x-fx)/m_zoom-4.f);
        };

        struct HitPin{size_t node=static_cast<size_t>(-1);bool output=false;std::string name,type;};
        const auto hitPin=[&](float px,float py)
        {
            HitPin hit;
            if(m_graph>=m_document.graphs.size())return hit;
            for(size_t i=m_rows.size();i>0;--i)
            {
                const auto pos=nodeLayoutPosition(i-1);const auto& row=m_rows[i-1];
                for(unsigned side=0;side<2;++side)
                {
                    const auto& pins=side?row.outputs:row.inputs;const auto& types=side?row.outputTypes:row.inputTypes;
                    const float xx=kLeft+m_x+(pos.x+(side?row.width:0))*m_zoom,yy=kTop+m_y+pos.y*m_zoom;
                    for(size_t p=0;p<pins.size();++p)if(std::abs(px-xx)<=std::min(9.f,10.f*m_zoom)&&std::abs(py-yy-pinY(p)*m_zoom)<=std::min(9.f,0.45f*kPinStep*m_zoom))
                        return HitPin{i-1,side!=0,pins[p],types[p]};
                }
            }
            return hit;
        };
        m_pinHover.clear();
        if(over&&!m_focus&&!modalOpen()&&!m_pinMenu&&!m_pinContext&&!m_nodeContext&&!m_drag&&!m_marquee&&!m_assetBrowser&&!m_actorEditor&&!m_layoutMenu)
        {
            const auto hit=hitPin(x,y);
            if(hit.node<m_rows.size())
            {
                m_pinHover=hit.name+" : "+hit.type;m_pinHoverX=x;m_pinHoverY=y;
                if(m_pinDrag&&m_pending<m_rows.size())
                {
                    if(hit.output==m_pendingOutput||hit.node==m_pending)m_pinHover+=" | incompatible direction";
                    else
                    {
                        const auto& row=m_rows[m_pending];const auto& pins=m_pendingOutput?row.outputs:row.inputs;
                        const auto it=std::find(pins.begin(),pins.end(),m_pendingPin);
                        if(it!=pins.end())
                        {
                            const auto& type=(m_pendingOutput?row.outputTypes:row.inputTypes)[static_cast<size_t>(it-pins.begin())];
                            const auto& from=m_pendingOutput?type:hit.type;const auto& to=m_pendingOutput?hit.type:type;
                            m_pinHover+=from==to||m_document.classAssignable(from,to)?" | matching type":blueprintLosslessConversion(from,to)?" | safe conversion":" | incompatible types";
                        }
                    }
                }
            }
        }
        if(m_moveWire!=static_cast<size_t>(-1))
        {
            m_moveWireX=x;m_moveWireY=y;
            if(!left){bool done=false;for(size_t i=m_rows.size();i>0&&!done;--i){const auto& n=m_document.graphs[m_graph].nodes[i-1];const auto& row=m_rows[i-1];const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom,px=nx+(m_moveWireOutput?row.width*m_zoom:0);const auto& pins=m_moveWireOutput?row.outputs:row.inputs;for(size_t j=0;j<pins.size();++j)if(std::abs(x-px)<=9&&std::abs(y-ny-pinY(j)*m_zoom)<=9){done=true;if(m_document.moveConnection(m_graph,m_moveWire,i-1,pins[j],m_moveWireOutput,m_status)){rebuildPins();m_diagnostic={};m_status="Connection moved.";}break;}}if(!done)m_status="Connection move cancelled.";m_moveWire=m_highlightWire=static_cast<size_t>(-1);}
            m_left=left;return;
        }
        if(m_pinContext)
        {
            if(right){m_pinContext=false;m_left=left;return;}
            if(click)
            {
                m_pinContext=false;
                if(in(x,y,m_pinContextX,m_pinContextY+30,m_pinContextX+260,m_pinContextY+150))
                {
                    const int action=static_cast<int>((y-m_pinContextY-30)/30);
                    if(action==0){if(m_document.disconnect(m_graph,m_pinContextNode,m_pinContextName,m_pinContextOutput)){rebuildPins();m_diagnostic={};m_status="Pin links removed.";}else m_status="Pin has no links.";}
                    else if(action==1){if(!m_pinContextOutput&&m_document.clearInputValue(m_graph,m_pinContextNode,m_pinContextName)){rebuildPins();m_diagnostic={};m_status="Input reset.";}else m_status="No editable override to reset.";}
                    else if(action==2){m_pending=m_pinContextNode;m_pendingPin=m_pinContextName;m_pendingOutput=m_pinContextOutput;openPinMenu(x,y);if(m_pinMenu)input.setTyping(true);}
                    else if(action==3){m_selection=m_pinContextNode;m_editPin=m_pinContextName;m_edit="";m_focus=26;input.setTyping(true);m_status="Name the new variable.";}
                }
            }
            m_left=left;return;
        }
        if(m_nodeContext)
        {
            if(right){m_nodeContext=false;m_left=left;return;}
            if(click)
            {
                m_nodeContext=false;
                if(in(x,y,m_nodeContextX,m_nodeContextY+32,m_nodeContextX+270,m_nodeContextY+288))
                {
                    const int action=static_cast<int>((y-m_nodeContextY-32)/32);
                    if(action==6){m_extractCollapsed=true;m_extractNodes=m_selectedNodes;m_edit.clear();m_focus=12;input.setTyping(true);m_status="Name the private collapsed graph.";}
                    else if(action==7){const auto owner=m_document.graphs[m_graph].owner,name=m_document.graphs[m_graph].name;if(m_document.expandCollapsed(m_graph,m_selection,m_status)){for(size_t k=0;k<m_document.graphs.size();++k)if(m_document.graphs[k].owner==owner&&m_document.graphs[k].name==name){selectGraph(k);break;}m_status="Expanded graph; undo restores the collapsed node.";}}
                    else if(action==2)removeSelectedNode(input);
                    else if(action==3){if(m_document.disconnectNode(m_graph,m_selection)){clearWire();rebuildPins();m_diagnostic={};m_status="All node connections removed. Undo restores them.";}else m_status="Node has no connections.";}
                    else {beginNodeEditor(m_selection,input);if(action==1||action==5){m_inspectorRow=action==1?0u:1u;m_inspectorReplace=true;}if(action==4)m_status="Choose a header colour, then Apply.";}
                }
            }
            m_left=left;return;
        }
        if(!m_notice.empty()){if(click||input.pressed(Action::Accept))m_notice.clear();m_left=left;return;}
        if(m_marquee)
        {
            m_marqueeEndX=std::clamp(x,graphAreaLeft(),graphAreaRight());m_marqueeEndY=std::clamp(y,graphAreaTop(),tabTop());
            const float x1=std::min(m_marqueeX,m_marqueeEndX),x2=std::max(m_marqueeX,m_marqueeEndX),y1=std::min(m_marqueeY,m_marqueeEndY),y2=std::max(m_marqueeY,m_marqueeEndY);
            m_selectedNodes=m_marqueeMode?m_marqueeBase:std::vector<size_t>{};const auto& nodes=m_document.graphs[m_graph].nodes;
            if(x2-x1>3||y2-y1>3)for(size_t i=0;i<nodes.size();++i)
            {
                const float nx=kLeft+m_x+nodes[i].x*m_zoom,ny=kTop+m_y+nodes[i].y*m_zoom;
                if(nx>x2||nx+m_rows[i].width*m_zoom<x1||ny>y2||ny+m_rows[i].height*m_zoom<y1)continue;
                const auto found=std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i);
                if(found==m_selectedNodes.end())m_selectedNodes.push_back(i);else if(m_marqueeMode==2)m_selectedNodes.erase(found);
            }
            m_selection=m_selectedNodes.empty()?static_cast<size_t>(-1):m_selectedNodes.back();
            if(x2-x1>3||y2-y1>3)m_wireClickTime=-1;
            if(!left)m_marquee=false;
            m_left=left;return;
        }
        if(m_actorEditor){updateActorEditor(s,x,y,left,click,pressed);m_left=left;return;}
        if(visualTabOpen()&&!m_visualInput&&!m_placementDragging&&!m_widgetDragging&&!m_widgetPreviewDrag&&updateGraphTabs(s,x,y,click)){m_left=left;return;}
        if(!modalOpen()&&!m_focus&&!m_layoutDragging&&!m_layoutMenu&&!m_pinMenu&&!m_pinDrag&&!m_drag&&!m_quickDrag&&!m_quickMenu)
        {
            if(updateContentDrawer(s,x,y,left,click)||updateGraphTabs(s,x,y,click)||updateGraphMap(x,y,left,click)){m_left=left;return;}
        }
        if(m_device.ready()&&!m_focus&&!modalOpen()&&!visualTabOpen()&&m_recoveryWarning.empty()&&click&&in(x,y,12,kBottom+24,812,kBottom+45))
        {
            const auto action=static_cast<unsigned>((x-12)/100.f);
            if(action==0)m_device.control(m_device.paused()?1:0);
            else if(action<4)m_device.control(action+1);
            else if(action==4)toggleRemoteTrace();
            else if(action==5)m_remoteFollow=!m_remoteFollow;
            else if(action==6)locateDeviceNode();
            else if(action==7)m_focus=27;
            m_left=left;return;
        }
        if(!modalOpen()&&!m_focus&&click&&in(x,y,kWidth*0.56f,kBottom+2,kWidth-12,kBottom+24))
        {m_focus=27;m_left=left;return;}
        if(modalOpen()){x-=modalX();y-=modalY();}
        if(m_focus==26)
        {
            bool apply=click&&in(x,y,790,400,930,440);
            if(click&&in(x,y,640,400,780,440)){m_focus=0;input.setTyping(false);m_left=left;return;}
            for(u32 i=0;i<input.typedCount();++i){const auto ch=input.typed(i);if(ch==13)apply=true;else if(ch==8&&!m_edit.empty())m_edit.pop_back();else if(ch>=32&&ch<127)appendCharacter(m_edit,ch,64);}
            if(apply&&m_document.promotePin(m_graph,m_pinContextNode,m_pinContextName,m_pinContextOutput,m_edit,m_status)){m_focus=0;input.setTyping(false);rebuildPins();filterCatalogue();m_diagnostic={};m_status="Promoted pin to variable. Undo restores original graph.";}
            m_left=left;return;
        }
        if(m_focus==25)
        {
            if(click&&in(x,y,350,355,930,385))m_skipDeleteConfirmation=!m_skipDeleteConfirmation;
            if(click&&in(x,y,660,405,790,445)){m_deleteSelection.clear();m_skipDeleteConfirmation=false;m_focus=0;m_status="Deletion cancelled.";}
            else if((pressed&128u)||(click&&in(x,y,800,405,930,445))){if(confirmNodeDeletion(input))saveLayout(s);}
            m_left=left;return;
        }
        if(m_focus==28){updateXboxDeploy(s,x,y,click);m_left=left;return;}
        if(m_focus==27){updateDeviceManager(s,x,y,click);m_left=left;return;}
        if(m_focus==24){updateBuildManager(s,x,y,click);m_left=left;return;}
        if(m_focus==23){updateInputActions(s,x,y,click);m_left=left;return;}
        if(m_focus==22){updateSceneBrowser(s,x,y,click);m_left=left;return;}
        if(m_focus==21){updateSequenceEditor(s,x,y,left,click);m_left=left;return;}
        if(m_focus==20){const auto results=findResults();if(click&&!in(x,y,220,100,1060,620)){m_focus=0;input.setTyping(false);m_left=left;return;}if(input.wheel()<0&&m_findFirst+10<results.size())++m_findFirst;if(input.wheel()>0&&m_findFirst)--m_findFirst;if(click&&in(x,y,240,190,1040,570)){const auto at=m_findFirst+static_cast<size_t>((y-190)/38);if(at<results.size()){openFindResult(results[at],input);m_left=left;return;}}for(u32 i=0;i<input.typedCount();++i){const auto ch=input.typed(i);if(ch==13){if(!results.empty())openFindResult(results[std::min(m_findFirst,results.size()-1)],input);break;}if(ch==8&&!m_findText.empty())m_findText.pop_back();else if(ch>=32)appendCharacter(m_findText,ch,128);m_findFirst=0;}m_left=left;return;}
        else if(!m_focus&&!m_layoutDragging&&!m_layoutMenu&&!m_pinMenu&&!m_pinDrag&&!m_drag&&!m_quickDrag&&!m_quickMenu&&resizePanels(x,y,left,click,false)){m_left=left;return;}
        if(m_layoutDragging)
        {
            m_layoutX=x;m_layoutY=y;
            if(std::abs(x-m_layoutStartX)>3||std::abs(y-m_layoutStartY)>3)m_layoutMoved=true;
            if(!left)
            {
                m_layoutDragging=false;
                if(m_layoutLink!=static_cast<size_t>(-1))
                {
                    if(m_layoutMoved)m_document.moveRoutePoint(m_graph,m_layoutLink,m_layoutPoint,(x-kLeft-m_x)/m_zoom,(y-kTop-m_y)/m_zoom);
                    m_layoutLink=static_cast<size_t>(-1);
                }
                else if(m_layoutComment!=static_cast<size_t>(-1))
                {
                    if(m_commentResize){float gx,gy,gw,gh;resizedComment(gx,gy,gw,gh);if(m_layoutMoved)m_document.resizeComment(m_graph,m_layoutComment,gx,gy,gw,gh);m_commentResize=0;}
                    else if(m_layoutMoved)m_document.moveNodes(m_graph,m_layoutMembers,(x-m_layoutStartX)/m_zoom,(y-m_layoutStartY)/m_zoom);
                    else {m_edit=m_document.graphs[m_graph].comments[m_layoutComment].title;m_commentColour=m_document.graphs[m_graph].comments[m_layoutComment].colour;m_focus=11;input.setTyping(true);m_status="Edit comment title or colour.";}
                }
            }
            m_left=left;return;
        }
        if(m_layoutMenu)
        {
            if(click)
            {
                const auto nodes=m_selectedNodes.empty()&&m_selection<m_document.graphs[m_graph].nodes.size()?std::vector<size_t>{m_selection}:m_selectedNodes;
                if(in(x,y,300,35,550,65))
                {std::vector<float> widths;for(const auto& row:m_rows)widths.push_back(row.width);if(m_document.arrange(m_graph,m_status,widths)){fitGraph();m_status="Arranged graph; manual routes reset. Undo restores layout.";}}
                else if(in(x,y,300,65,550,95))
                {if(m_document.alignNodes(m_graph,nodes,false,m_status))m_status="Aligned selected nodes left.";}
                else if(in(x,y,300,95,550,125))
                {if(m_document.alignNodes(m_graph,nodes,true,m_status))m_status="Aligned selected nodes top.";}
                else if(in(x,y,300,125,550,155))
                {
                    if(m_document.addComment(m_graph,nodes,"Comment",m_status))
                    {m_layoutComment=m_document.graphs[m_graph].comments.size()-1;m_edit="Comment";m_commentColour=0;m_focus=11;input.setTyping(true);}
                }
                else if(in(x,y,300,155,550,185))
                {m_extractCollapsed=false;m_extractNodes=nodes;m_edit.clear();m_focus=12;input.setTyping(true);m_status="Name the extracted function.";}
                else if(in(x,y,300,185,550,215)&&m_document.graphs[m_graph].kind=="function")
                {m_edit=m_document.graphs[m_graph].name;m_focus=13;input.setTyping(true);m_status="Rename function and update its calls.";}
                else if(in(x,y,300,215,550,245)&&m_document.graphs[m_graph].kind=="function")
                {m_edit=m_document.graphs[m_graph].name+"_Copy";m_focus=14;input.setTyping(true);m_status="Name the duplicate function.";}
                else if(in(x,y,300,245,550,275))openSelectedFunction();
                else if(in(x,y,300,275,550,305))returnToCaller();
                else if(in(x,y,300,305,550,335)&&m_document.graphs[m_graph].kind=="function")
                {
                    const bool enable=!m_document.graphs[m_graph].inlineMacro;
                    if(m_document.setInlineMacro(m_graph,enable,m_status)){rebuildPins();filterCatalogue();m_status=enable?"Inline macro enabled. Calls expand in place.":"Converted to a callable function.";}
                }
                else if(in(x,y,300,335,550,365)&&m_document.graphs[m_graph].kind=="function")
                {
                    const bool enable=!m_document.graphs[m_graph].sharedLibrary;
                    if(m_document.setSharedLibrary(m_graph,enable,m_status)){rebuildPins();filterCatalogue();m_status=enable?"Published to project library.":"Made function local.";}
                }
                else if(in(x,y,300,365,550,395))
                {
                    std::string bytes;
                    if(m_document.exportLibrary(bytes,m_status))
                    {
                        std::string file;
                        for(unsigned i=1;i<=10000;++i){const auto candidate="blueprint-library-"+std::to_string(i)+".bplib.bp.json";if(!s.platform->fileTime(("own/"+candidate).c_str())){file=candidate;break;}}
                        if(file.empty())m_status="No free library package name.";
                        else if(writeVerified(s,"own/"+file,bytes))m_status="Exported "+file;
                    }
                }
                else if(in(x,y,300,395,550,425)){m_uiScale=(m_uiScale+1)%4;m_status="Changed desktop UI density (small windows fit automatically).";}
                else if(in(x,y,300,425,550,455)){m_leftPanel=248;m_graphRightPanel=268;m_actorRightPanel=310;m_uiScale=0;m_actorCollapsed=0;m_actorDetailsScroll=0;m_contentDrawer=false;m_contentFloating=false;m_contentPanelX=0.25f;m_contentPanelY=0.65f;m_contentPanelWidth=620;m_contentDockTop=false;m_contentPanelHeight=174;m_contentDockSide=0;m_contentSideWidth=300;m_graphTree=true;m_minimap=m_minimapDragging=false;m_graphHideLeft=m_graphHideRight=false;m_navigationFirst=0;m_navigationCollapsed.clear();m_wireStyle=0;resizeWorkspace(canvas);fitGraph();m_status="Reset editor panel sizes and density.";}
                else if(in(x,y,300,455,550,485)){m_minimap=!m_minimap;m_status=m_minimap?"Graph overview enabled. Click or drag to navigate.":"Graph overview hidden.";NX_INFO("Blueprint Studio: minimap %s",m_minimap?"enabled":"hidden");}
                else if(in(x,y,300,485,550,515)){m_graphHideLeft=!m_graphHideLeft;m_status=m_graphHideLeft?"Navigator hidden.":"Navigator shown.";NX_INFO("Blueprint Studio: %s",m_status.c_str());}
                else if(in(x,y,300,515,550,545)){m_graphHideRight=!m_graphHideRight;m_status=m_graphHideRight?"Node library hidden.":"Node library shown.";NX_INFO("Blueprint Studio: %s",m_status.c_str());}
                else if(in(x,y,300,545,550,575))
                {
                    std::string file;
                    for(unsigned i=1;i<=10000;++i)
                    {
                        const auto candidate="blueprint-package-"+std::to_string(i)+".bp.zip";
                        if(!s.platform->fileTime(("own/"+candidate).c_str())){file=candidate;break;}
                    }
                    if(file.empty())m_status="No free project package name.";
                    else if(blueprintExportPackage(*s.platform,m_document,file,m_status))m_status="Exported own/"+file;
                }
                else if(in(x,y,300,575,550,605))
                {m_wireStyle=(m_wireStyle+1)%3;saveLayout(s);m_status=std::string("Wire style: ")+(m_wireStyle==0?"Curved":m_wireStyle==1?"Orthogonal":"Straight");}
                else if(in(x,y,300,605,550,635)){m_confirmDelete=!m_confirmDelete;saveLayout(s);m_status=m_confirmDelete?"Node deletion confirmation enabled.":"Node deletion confirmation disabled.";}
                m_layoutMenu=false;
            }
            m_left=left;return;
        }
        if(m_pinMenu)
        {
            auto choose=[&](size_t index)
            {
                if(index>=m_contextResults.size())return false;
                const auto choice=m_contextResults[index];const size_t added=m_document.graphs[m_graph].nodes.size();
                if(!m_document.placeContextNode(m_graph,m_pending,m_pendingPin,m_pendingOutput,choice.kind,choice.reference,choice.pin,(m_pinX-kLeft-m_x)/m_zoom,(m_pinY-kTop-m_y)/m_zoom,m_status))return false;
                closePinMenu(s);m_diagnostic={};rebuildPins();m_selection=added;m_selectedNodes.clear();m_status="Added "+choice.title+".";return true;
            };
            for(u32 i=0;i<input.typedCount();++i)
            {
                const auto ch=input.typed(i);
                if(ch==8&&!m_pinQuery.empty()){size_t end=m_pinQuery.size()-1;while(end&&(static_cast<unsigned char>(m_pinQuery[end])&0xc0)==0x80)--end;m_pinQuery.resize(end);filterCatalogue();}
                else if(ch==13){choose(m_paletteFirst);break;}
                else if(ch>=32&&appendCharacter(m_pinQuery,ch,80))filterCatalogue();
            }
            if(!m_pinMenu){m_left=left;return;}
            if(input.wheel()<0&&m_paletteFirst+8<m_contextResults.size())++m_paletteFirst;
            if(input.wheel()>0&&m_paletteFirst)--m_paletteFirst;
            if(click)
            {
                if(in(x,y,m_pinMenuX,m_pinMenuY+64,m_pinMenuX+300,m_pinMenuY+304))
                {
                    const auto index=m_paletteFirst+static_cast<size_t>((y-m_pinMenuY-64)/30);
                    choose(index);
                }
                else if(!in(x,y,m_pinMenuX,m_pinMenuY,m_pinMenuX+300,m_pinMenuY+320))closePinMenu(s);
            }
            m_left=left;return;
        }
        if(m_pinDrag)
        {
            m_pinX=x;m_pinY=y;
            if(!left)
            {
                m_pinDrag=false;
                const bool moved=std::abs(x-m_pinStartX)>5||std::abs(y-m_pinStartY)>5;
                if(over&&moved)
                {
                    const auto& nodes=m_document.graphs[m_graph].nodes;bool hit=false;
                    for(size_t i=nodes.size();i>0&&!hit;--i)
                    {
                        const auto& n=nodes[i-1];const auto& row=m_rows[i-1];
                        const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;
                        const auto& pins=m_pendingOutput?row.inputs:row.outputs;
                        const float px=nx+(m_pendingOutput?0:row.width*m_zoom);
                        for(size_t p=0;p<pins.size();++p)if(std::abs(x-px)<=9&&std::abs(y-ny-pinY(p)*m_zoom)<=std::min(9.f,0.45f*kPinStep*m_zoom))
                        {
                            hit=true;
                            const bool ok=m_pendingOutput?m_document.connectCompatible(m_graph,m_pending,m_pendingPin,i-1,pins[p],m_status):m_document.connectCompatible(m_graph,i-1,pins[p],m_pending,m_pendingPin,m_status);
                            if(ok){clearWire();rebuildPins();m_status="Connected pins.";}break;
                        }
                        if(in(x,y,nx,ny,nx+row.width*m_zoom,ny+row.height*m_zoom))hit=true;
                    }
                    if(!hit){openPinMenu(x,y);if(m_pinMenu)input.setTyping(true);}
                }
                else if(!moved&&!m_pendingOutput&&m_pending<m_rows.size())
                {
                    const auto& row=m_rows[m_pending];const auto pin=std::find(row.inputs.begin(),row.inputs.end(),m_pendingPin);
                    if(pin!=row.inputs.end()&&blueprintInputLiteralType(row.inputTypes[static_cast<size_t>(pin-row.inputs.begin())]))
                    {
                        const auto name=m_pendingPin;beginInputEdit(m_pending,name,input);
                    }
                }
                else if(!over&&moved)clearWire();
            }
            m_left=left;return;
        }
        if(m_quickMenu)
        {
            if(click)
            {
                const bool get=in(x,y,m_quickMenuX,m_quickMenuY+28,m_quickMenuX+210,m_quickMenuY+58);
                const bool set=in(x,y,m_quickMenuX,m_quickMenuY+58,m_quickMenuX+210,m_quickMenuY+88);
                if(set&&!blueprintArrayElement(m_quickVariable.field.type).empty()){m_status="Use array operations to modify elements.";m_quickMenu=false;m_left=left;return;}
                if(get||set)
                {
                    if(m_document.addVariableNode(m_graph,m_quickVariable.field.name,m_quickVariable.global,set,(m_quickX-kLeft-m_x)/m_zoom,(m_quickY-kTop-m_y)/m_zoom,m_status))
                    {m_diagnostic={};clearWire();rebuildPins();m_selection=m_document.graphs[m_graph].nodes.size()-1;m_selectedNodes.clear();m_status=std::string(set?"Set ":"Get ")+m_quickVariable.field.name+" added.";}
                }
                m_quickMenu=false;
            }
            m_left=left;return;
        }
        if(m_quickDrag)
        {
            m_quickX=x;m_quickY=y;
            if(!left)
            {
                m_quickDrag=false;
                if(over)
                {
                    m_quickMenu=true;m_quickMenuX=std::clamp(x,kLeft+4,kRight-214);m_quickMenuY=std::clamp(y,kTop+4,kBottom-92);
                    m_status="Choose Get or Set for "+m_quickVariable.field.name+".";
                }
                else m_status="Drag a variable onto the graph to place it.";
            }
            m_left=left;return;
        }
        if(m_assetBrowser)
        {
            if(m_audioEditor){updateTone(s,x,y,click);m_left=left;return;}
            if(m_assetLibrary){updateVisualLibrary(s,x,y,left,click,pressed);m_left=left;return;}
            if(click&&in(x,y,280,116,380,150)){if(m_sharedMedia){m_status="Switch to Screen media for the audio editor.";m_left=left;return;}m_clipSource.clear();m_audioClip=BlueprintAudioClip{};m_clipEdit=BlueprintClipEdit{};m_audioEditor=true;m_toneEffects=false;refreshTone();m_status="Create a sound effect, then save a native WAV asset.";m_left=left;return;}
            if(click&&in(x,y,384,116,524,150)){m_assetLibrary=true;m_visualInput=0;m_visualText.clear();m_visualFirst=0;m_status="Project-wide reusable actors and widget pages.";m_left=left;return;}
            if(input.wheel()<0&&m_assetFirst+10<m_assetFiles.size())++m_assetFirst;
            if(input.wheel()>0&&m_assetFirst)--m_assetFirst;
            if(click&&(in(x,y,860,116,1000,150)||!in(x,y,260,100,1020,625))){m_assetBrowser=false;m_left=left;return;}
            if(click&&in(x,y,704,116,850,150))
            {
                if(m_sharedMedia)blueprintListSharedAssets(*s.platform,m_document.assetRoot,m_assetFiles,m_status);
                else blueprintListAssets(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles,m_status);
                m_assetFirst=0;m_left=left;return;
            }
            if(click&&in(x,y,810,155,1000,185))
            {
                const bool next=!m_sharedMedia;
                const bool listed=next?blueprintListSharedAssets(*s.platform,m_document.assetRoot,m_assetFiles,m_status):
                    blueprintListAssets(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles,m_status);
                if(listed){m_sharedMedia=next;m_assetFirst=0;m_status=next?"Shared media: reference with global:":"Screen media: reference with asset:";}
                m_left=left;return;
            }
            if(click&&in(x,y,534,116,694,150))launchSceneEditor(s);
            if(click&&in(x,y,280,190,1000,550))
            {
                const auto row=m_assetFirst+static_cast<size_t>((y-190)/36);
                if(m_sharedMedia&&row<m_assetFiles.size())m_status="Reference: global:"+m_assetFiles[row];
                else if(row<m_assetFiles.size()&&m_assetFiles[row].ends_with(".bptone.json"))
                {
                    BlueprintTone candidate;
                    if(blueprintLoadTonePreset(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles[row],candidate,m_status))
                    {stopTone(s);m_clipSource.clear();m_audioClip=BlueprintAudioClip{};m_tone=candidate;m_toneRight=false;m_audioEditor=true;m_toneEffects=false;refreshTone();if(!m_toneWav.empty())m_status="Loaded editable audio preset.";}
                }
                else if(row<m_assetFiles.size()&&m_assetFiles[row].ends_with(".bpclip.json"))
                {
                    std::string source;BlueprintAudioClip clip;BlueprintClipEdit edit;BlueprintTone output;
                    if(blueprintLoadClipPreset(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles[row],source,clip,edit,output,m_status))
                    {
                        stopTone(s);m_clipSource=source;m_audioClip=std::move(clip);m_clipEdit=edit;m_tone=output;
                        m_audioEditor=true;m_toneEffects=false;m_toneRight=false;refreshTone();
                        if(!m_toneWav.empty())m_status="Loaded non-destructive clip preset.";
                    }
                }
                else if(row<m_assetFiles.size()&&blueprintAudioAsset(m_assetFiles[row]))
                {
                    BlueprintAudioClip candidate;
                    if(blueprintLoadAudioClip(*s.platform,m_document.assetRoot,assetScreen(),m_assetFiles[row],candidate,m_status))
                    {
                        stopTone(s);m_audioClip=std::move(candidate);m_clipSource=m_assetFiles[row];m_clipEdit=BlueprintClipEdit{};
                        m_tone=BlueprintTone{};m_tone.channels=m_audioClip.channels;
                        if(m_audioClip.rate==11025||m_audioClip.rate==22050||m_audioClip.rate==44100||m_audioClip.rate==48000)m_tone.rate=m_audioClip.rate;
                        m_audioEditor=true;m_toneEffects=false;refreshTone();if(!m_toneWav.empty())m_status="Clip opened. Save creates a new WAV; original is unchanged.";
                    }
                }
            }
            m_left=left;return;
        }
        if(m_browsing)
        {
            std::string droppedProject;
            if(s.platform->takeDropped(droppedProject))
            {
                if(blueprintFileName(droppedProject)||blueprintPackageName(droppedProject))openFile(s,droppedProject,false);
                else if(standaloneSourceName(droppedProject))importSource(s,droppedProject);
                else m_status="Drop a Blueprint project, portable ZIP or AngelScript source.";
                if(!m_browsing){m_projectCreate=0;input.setTyping(false);}
                m_left=left;return;
            }
            const auto selectTemplate=[&](unsigned kind)
            {
                m_projectCreate=kind;m_projectName=kind==1?"":kind==2?"First Person":kind==3?"Third Person":kind==4?"Cinematic Showcase":kind==5?"Flight":kind==6?"Rolling Ball":"Segmented Health HUD";
                input.setTyping(true);m_status="Enter a unique project name, then Create.";
            };
            if(m_projectCreate)
            {
                for(u32 i=0;i<input.typedCount();++i)
                {
                    const auto ch=input.typed(i);
                    if(ch==13){createTemplate(s,m_projectCreate==1?"blank":m_projectCreate==2?"first-person":m_projectCreate==3?"third-person":m_projectCreate==4?"cinematic":m_projectCreate==5?"flight":m_projectCreate==6?"rolling-ball":"hud-health",m_projectName);break;}
                    if(ch==8&&!m_projectName.empty())
                    {
                        size_t end=m_projectName.size()-1;while(end&&(static_cast<unsigned char>(m_projectName[end])&0xc0)==0x80)--end;m_projectName.resize(end);
                    }
                    else if(ch>=32)appendCharacter(m_projectName,ch,64);
                }
                if(!m_browsing){m_left=left;return;}
                if(click&&in(x,y,28,410,250,448)){m_projectName.clear();input.setTyping(true);}
                if(click&&in(x,y,28,458,250,496)){createTemplate(s,m_projectCreate==1?"blank":m_projectCreate==2?"first-person":m_projectCreate==3?"third-person":m_projectCreate==4?"cinematic":m_projectCreate==5?"flight":m_projectCreate==6?"rolling-ball":"hud-health",m_projectName);m_left=left;return;}
            }
            if(click&&in(x,y,28,166,250,246))selectTemplate(1);
            else if(click&&in(x,y,28,256,250,310))selectTemplate(2);
            else if(click&&in(x,y,28,318,250,374))selectTemplate(3);
            else if(click&&in(x,y,28,502,135,554))selectTemplate(4);
            else if(click&&in(x,y,143,502,250,554))selectTemplate(7);
            else if(click&&in(x,y,28,559,135,615))selectTemplate(5);
            else if(click&&in(x,y,143,559,250,615))selectTemplate(6);
            else if(click&&in(x,y,860,116,1000,150))
            {m_browsing=false;m_projectCreate=0;input.setTyping(false);}
            else if(click&&in(x,y,550,116,650,150))
            {m_browsing=false;m_projectCreate=0;input.setTyping(false);saveCurrent(s);}
            else if(click&&in(x,y,660,116,850,150))
            {m_browsing=false;m_projectCreate=0;m_edit.clear();m_focus=5;input.setTyping(true);m_status="Type a new project name to save a copy.";}
            else if(click&&in(x,y,1010,116,1235,150))returnToRedux(s);
            else if(in(x,y,280,166,1240,598))
            {
                if(input.wheel()<0&&m_fileFirst+12<m_files.size())++m_fileFirst;
                if(input.wheel()>0&&m_fileFirst)--m_fileFirst;
                if(click)
                {
                    const size_t row=m_fileFirst+static_cast<size_t>((y-166)/36);
                    if(row<m_files.size())
                    {
                        const auto file=m_files[row];
                        if(projectFolderName(file))importProjectFolder(s,file);
                        else if(standaloneSourceName(file))importSource(s,file);
                        else openFile(s,file,true);
                        if(!m_browsing){m_projectCreate=0;input.setTyping(false);}
                    }
                }
            }
            m_left=left;return;
        }
        if(m_rgbOpen&&(m_focus==15||m_focus==11||m_focus==6)){auto& colour=m_focus==15?m_inspectorColour:m_focus==11?m_commentColour:m_pinColour;const auto before=colour;updateRGB(input,x,y,left,click,colour);if(m_focus==15&&colour!=before)m_inspectorColourChanged=true;if(m_focus==6&&!m_rgbOpen&&m_rgbApplied){blueprintInputComponents("Color",colour?blueprintRGBHex(colour):"FFFFFF",m_inputComponents);m_inputComponents[3]=std::to_string(m_rgbAlpha);}m_left=left;return;}
        if(click&&((m_focus==10&&!in(x,y,150,100,1130,625))||(m_focus==17&&!in(x,y,210,100,1070,620))||(m_focus==15&&!in(x,y,160,68,1140,652))||(m_focus==6&&!in(x,y,340,226,940,m_inputChoices.empty()?450.f:628.f))||(m_focus==5&&!in(x,y,340,240,940,445))||(m_focus==9&&!in(x,y,200,226,1080,466))||(m_focus>=11&&m_focus<=14&&!in(x,y,260,230,1020,450))))
        {m_focus=0;input.setTyping(false);m_left=left;return;}
        if(m_focus==10)
        {
            const auto rows=variableRows();const auto owner=m_document.graphs[m_graph].owner;
            const bool canGlobal=!m_document.moduleForOwner(owner).empty();
            const auto selectedField=std::find_if(rows.begin(),rows.end(),[&](const VariableRow& row){return !row.global&&row.field.name==m_variableOld;});
            auto apply=[&]()
            {
                if(m_variableName.empty()){m_status="Enter a variable name.";return;}
                if(m_variableOld.empty()&&std::any_of(rows.begin(),rows.end(),[&](const VariableRow& r){return r.global==m_variableGlobal&&r.field.name==m_variableName;}))
                {m_status="Name already exists. Select that variable to edit it.";return;}
                const std::string command=std::string(m_variableGlobal?"global ":"field ")+(m_variableOld.empty()||m_variableOld==m_variableName?"":m_variableOld+">")+m_variableName+":"+m_variableType+(m_variableInitialized?"="+m_variableValue:"");
                if(m_document.editVariableDeclaration(owner,command,m_status))
                {m_variableOld=m_variableName;clearWire();rebuildPins();filterCatalogue();m_diagnostic={};m_status="Variable saved. Undo is available after closing.";}
            };
            if(in(x,y,170,190,620,550))
            {
                if(input.wheel()<0&&m_variableFirst+10<rows.size())++m_variableFirst;
                if(input.wheel()>0&&m_variableFirst)--m_variableFirst;
            }
            for(u32 i=0;i<input.typedCount();++i)
            {
                const auto ch=input.typed(i);
                if(ch==13){apply();break;}
                if(!m_variableInput)continue;
                auto& value=m_variableInput==1?m_variableName:m_variableValue;
                if(ch==8&&!value.empty()){size_t end=value.size()-1;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;value.resize(end);}
                else if(ch>=32&&(m_variableInput==2||(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'))appendCharacter(value,ch,m_variableInput==1?64u:4096u);
            }
            const auto types=variableTypeChoices();
            if(m_variableTypes&&in(x,y,650,302,1100,302+8*28))
            {
                if(input.wheel()<0&&m_variableTypeFirst+24<types.size())m_variableTypeFirst+=8;
                if(input.wheel()>0&&m_variableTypeFirst>=8)m_variableTypeFirst-=8;
            }
            if(click)
            {
                if(m_variableTypes&&in(x,y,650,302,1100,302+8*28))
                {
                    const size_t i=m_variableTypeFirst+static_cast<size_t>((y-302)/28)+8*static_cast<size_t>((x-650)/150);
                    if(i<types.size()){m_variableType=types[i];if(!blueprintLiteralType(m_variableType)){m_variableInitialized=false;m_variableValue.clear();}}
                    m_variableTypes=false;
                }
                else if(m_variableTypes){m_variableTypes=false;}
                else if(in(x,y,974,116,1110,150)){m_focus=0;input.setTyping(false);m_status="Variables closed.";}
                else if(in(x,y,170,156,370,186)){resetVariableEditor(false);m_status="New instance variable.";}
                else if(in(x,y,380,156,620,186)&&canGlobal){resetVariableEditor(true);m_status="New module variable.";}
                else if(in(x,y,170,190,620,550))
                {
                    const auto index=m_variableFirst+static_cast<size_t>((y-190)/36);
                    if(index<rows.size())
                    {
                        const auto& r=rows[index];m_variableOld=m_variableName=r.field.name;m_variableType=r.field.type;m_variableValue=r.field.value;
                        m_variableGlobal=r.global;m_variableInitialized=r.field.initialized;m_variableInput=0;
                        m_status=r.explicitField?"Edit selected declaration.":"Inferred field: Apply creates an explicit declaration.";
                    }
                }
                else if(in(x,y,650,222,1100,258))m_variableInput=1;
                else if(in(x,y,650,266,1100,302)){m_variableTypes=true;m_variableTypeFirst=0;m_variableInput=0;}
                else if(in(x,y,650,322,1100,354)&&blueprintLiteralType(m_variableType)){m_variableInitialized=!m_variableInitialized;m_variableInput=m_variableInitialized?2:0;}
                else if(in(x,y,650,370,1100,406)&&m_variableInitialized)m_variableInput=2;
                else if(in(x,y,650,510,1100,544)&&!m_variableGlobal&&owner.rfind("behaviour:",0)==0&&selectedField!=rows.end()&&selectedField->explicitField&&!selectedField->inheritedExposure)
                {
                    if(m_document.setActorFieldExposed(owner,m_variableOld,!selectedField->field.exposed,m_status))
                    {rebuildPins();filterCatalogue();m_status="Spawn exposure updated.";}
                }
                else if(in(x,y,650,464,860,500))apply();
                else if(in(x,y,870,464,1100,500)&&!m_variableOld.empty())
                {
                    if(m_document.editVariableDeclaration(owner,std::string(m_variableGlobal?"global -":"field -")+m_variableOld,m_status))
                    {resetVariableEditor(m_variableGlobal);clearWire();rebuildPins();filterCatalogue();m_diagnostic={};m_status="Declaration removed.";}
                }
            }
            const auto count=variableRows().size();m_variableFirst=std::min(m_variableFirst,count>10?count-10:0);
            m_left=left;return;
        }
        if(m_focus==18){updateJointEditor(input,x,y,click);m_left=left;return;}
        if(m_focus==17)
        {
            if(click&&in(x,y,940,555,1050,595)){m_focus=0;m_curveDrag=-1;m_status="Curve edit cancelled.";}
            else if(input.pressed(Action::Accept)||(click&&in(x,y,810,555,930,595)))
            {
                std::vector<std::string> points;
                for(size_t i=0;i<4;++i){char pair[96];snprintf(pair,sizeof(pair),"%.9g,%.9g",m_curveX[i],m_curveY[i]);points.emplace_back(pair);}
                if(!m_curveChanged||m_document.setCubicPoints(m_graph,m_selection,points,m_status))
                {rebuildPins();m_focus=0;m_curveDrag=-1;m_diagnostic={};m_status="Curve points applied.";NX_INFO("Blueprint Studio: cubic curve points applied");}
            }
            else
            {
                if(click&&in(x,y,240,165,1040,535))
                {
                    float best=225;m_curveDrag=-1;
                    for(int i=0;i<4;++i)
                    {
                        const float dx=x-(640+(m_curveX[i]-m_curveCenterX)*m_curveScale),dy=y-(350+(m_curveY[i]-m_curveCenterY)*m_curveScale);
                        const float distance=dx*dx+dy*dy;if(distance<=best){best=distance;m_curveDrag=i;}
                    }
                }
                if(left&&m_curveDrag>=0)
                {
                    const auto i=m_curveDrag;
                    const float px=m_curveCenterX+(std::clamp(x,250.f,1030.f)-640)/m_curveScale;
                    const float py=m_curveCenterY+(std::clamp(y,175.f,525.f)-350)/m_curveScale;
                    if(px!=m_curveX[i]||py!=m_curveY[i]){m_curveX[i]=px;m_curveY[i]=py;m_curveChanged=true;}
                }
                if(!left)m_curveDrag=-1;
            }
            m_left=left;return;
        }
        if(m_focus==16||m_focus==19)
        {
            const bool literal=m_focus==19;
            const auto apply=[&](){if(!m_inlineDirty)return true;std::string value=m_edit;if(!m_inputComponents.empty()){value.clear();for(size_t i=0;i<m_inputComponents.size();++i){if(i)value+=",";value+=m_inputComponents[i];}}return literal?m_document.setLiteral(m_graph,m_selection,value,m_status):m_document.setInputValue(m_graph,m_selection,m_editPin,value,m_status);};
            if(click)
            {
                if(m_selection<m_rows.size())
                {
                    const auto& row=m_rows[m_selection];const auto pos=nodeLayoutPosition(m_selection);
                    const float nx=kLeft+m_x+pos.x*m_zoom,ny=kTop+m_y+pos.y*m_zoom;
                    if(literal&&row.literalFieldW>12.f&&in(x,y,nx+13*m_zoom,ny+(pinY(0)-9)*m_zoom,nx+(13.f+row.literalFieldW)*m_zoom,ny+(pinY(0)+9)*m_zoom)){selectInlineField(nx+13*m_zoom,row.literalFieldW*m_zoom);m_left=left;return;}
                    if(!literal)for(size_t p=0;p<row.inputs.size();++p)if(row.inputs[p]==m_editPin&&row.fieldW[p]>12.f&&in(x,y,nx+row.fieldX[p]*m_zoom,ny+(pinY(p)-9)*m_zoom,nx+(row.fieldX[p]+row.fieldW[p])*m_zoom,ny+(pinY(p)+9)*m_zoom)){selectInlineField(nx+row.fieldX[p]*m_zoom,row.fieldW[p]*m_zoom);m_left=left;return;}
                }
                if(!apply()){m_left=left;return;}
                m_focus=0;input.setTyping(false);clearWire();rebuildPins();filterCatalogue();m_diagnostic={};m_status="Inline edit applied.";
            }
            else
            {
            auto active=[&]() -> std::string& {return m_inputComponents.empty()?m_edit:m_inputComponents[m_inputComponent];};
            const unsigned arrows=(held(Key::Left)?1u:0u)|(held(Key::Right)?2u:0u)|(control&&held(Key::A)?4u:0u);
            const unsigned fresh=arrows&~m_inlineKeys;m_inlineKeys=arrows;
            const auto previous=[&](const std::string& value){if(m_inlineCaret){--m_inlineCaret;while(m_inlineCaret&&(static_cast<unsigned char>(value[m_inlineCaret])&0xc0)==0x80)--m_inlineCaret;}};
            if(fresh&4u){m_inlineSelectAll=true;m_inlineCaret=active().size();}
            if(fresh&1u){if(m_inlineSelectAll){m_inlineCaret=0;m_inlineSelectAll=false;}else previous(active());}
            if(fresh&2u)m_inlineSelectAll=false;
            if((fresh&2u)&&m_inlineCaret<active().size()){++m_inlineCaret;while(m_inlineCaret<active().size()&&(static_cast<unsigned char>(active()[m_inlineCaret])&0xc0)==0x80)++m_inlineCaret;}
            for(u32 i=0;i<input.typedCount();++i)
            {
                const auto ch=input.typed(i);
                if(ch==13)
                {
                    if(!apply())break;
                    m_focus=0;input.setTyping(false);clearWire();rebuildPins();filterCatalogue();m_diagnostic={};
                    m_status="Inline input applied.";NX_INFO("Blueprint Studio: inline input applied: %s",m_editPin.c_str());break;
                }
                if(ch==9&&!m_inputComponents.empty()){m_inputComponent=(m_inputComponent+1)%m_inputComponents.size();m_inlineCaret=active().size();m_inlineSelectAll=false;continue;}
                auto& value=active();
                if(m_inlineSelectAll&&(ch==8||ch==127||(ch>=32&&!control))){value.clear();m_inlineCaret=0;m_inlineSelectAll=false;m_inlineDirty=true;}
                if(control&&ch!=8)continue;
                if(ch==8&&m_inlineCaret){const auto end=m_inlineCaret;previous(value);value.erase(m_inlineCaret,end-m_inlineCaret);m_inlineDirty=true;}
                else if(ch==127&&m_inlineCaret<value.size()){size_t end=m_inlineCaret+1;while(end<value.size()&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)++end;value.erase(m_inlineCaret,end-m_inlineCaret);m_inlineDirty=true;}
                else if(ch>=32&&ch!=127&&(m_inputComponents.empty()||(ch<128&&std::string("0123456789.eE+-").find(static_cast<char>(ch))!=std::string::npos)))
                {std::string part;appendCharacter(part,ch,4096);if(value.size()+part.size()<=4096){value.insert(m_inlineCaret,part);m_inlineCaret+=part.size();m_inlineDirty=true;}}
            }
            m_left=left;return;
            }
        }
        if(m_focus==15)
        {
            if(click&&in(x,y,650,603,840,640)){removeSelectedNode(input);m_left=left;return;}
            if(click&&in(x,y,760,82,1104,116)){startRGB(m_inspectorColour?blueprintRGBHex(m_inspectorColour):"34577A");m_left=left;return;}
            const auto count=m_inspectorRows.size();m_inspectorFirst=std::min(m_inspectorFirst,count>9?count-9:0);
            if(input.wheel()<0&&m_inspectorFirst+9<count)++m_inspectorFirst;
            if(input.wheel()>0&&m_inspectorFirst)--m_inspectorFirst;
            if(click&&in(x,y,990,603,1120,640)){m_focus=0;input.setTyping(false);m_left=left;return;}
            if(click&&in(x,y,850,603,980,640)){applyNodeEditor(input);m_left=left;return;}
            if(click&&in(x,y,180,156,1100,552))
            {
                const auto row=m_inspectorFirst+static_cast<size_t>((y-156)/44);
                if(row<count&&m_inspectorRows[row].editable){m_inspectorRow=row;m_inspectorReplace=true;}
            }
            for(u32 i=0;i<input.typedCount();++i)
            {
                const auto ch=input.typed(i);
                if(ch==13){applyNodeEditor(input);break;}
                if(ch==9&&count)
                {
                    for(size_t n=0;n<count;++n){m_inspectorRow=m_inspectorRow<count?(m_inspectorRow+1)%count:0;if(m_inspectorRows[m_inspectorRow].editable){m_inspectorReplace=true;if(m_inspectorRow<m_inspectorFirst||m_inspectorRow>=m_inspectorFirst+9)m_inspectorFirst=std::min(m_inspectorRow,count>9?count-9:0);break;}}
                    continue;
                }
                if(m_inspectorRow>=count||!m_inspectorRows[m_inspectorRow].editable)continue;
                auto& value=m_inspectorDraft[m_inspectorRow];
                if(ch==8||ch>=32)
                {
                    if(m_inspectorReplace){value.clear();m_inspectorReplace=false;}
                    if(ch==8&&!value.empty()){size_t end=value.size()-1;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;value.resize(end);}
                    else if(ch>=32)appendCharacter(value,ch,4096);
                    m_inspectorChanged[m_inspectorRow]=true;
                }
            }
            m_left=left;return;
        }
        const bool inputApply=m_focus==6&&click&&in(x,y,650,391,780,430);
        if(m_focus==6&&click&&in(x,y,790,391,920,430)){m_focus=0;input.setTyping(false);m_left=left;return;}
        if(m_focus==6&&!m_inputComponents.empty())
        {
            if(m_editType=="Color"&&m_inputComponents.size()==4&&click&&in(x,y,550,391,640,430)){const auto byte=[](const std::string& v){return static_cast<unsigned>(std::round(std::clamp(std::strtof(v.c_str(),nullptr),0.f,1.f)*255.f));};m_pinColour=0x1000000u|(byte(m_inputComponents[0])<<16)|(byte(m_inputComponents[1])<<8)|byte(m_inputComponents[2]);startRGB(blueprintRGBHex(m_pinColour),true,std::strtof(m_inputComponents[3].c_str(),nullptr));m_left=left;return;}
            if(click&&in(x,y,360,285,920,341))
            {m_inputComponent=std::min(m_inputComponents.size()-1,static_cast<size_t>((x-360)/(560.f/static_cast<float>(m_inputComponents.size()))));m_inputReplace=true;}
            for(u32 i=0;i<input.typedCount()+(inputApply?1u:0u);++i)
            {
                const auto ch=i<input.typedCount()?input.typed(i):13u;auto& part=m_inputComponents[m_inputComponent];
                if(ch==9){m_inputComponent=(m_inputComponent+1)%m_inputComponents.size();m_inputReplace=true;}
                else if(ch==8){if(m_inputReplace){part.clear();m_inputReplace=false;}else if(!part.empty())part.pop_back();}
                else if(ch==13)
                {
                    std::string joined;for(const auto& component:m_inputComponents){if(!joined.empty())joined+=",";joined+=component;}
                    // Preserve empty components as invalid rather than collapsing their positions.
                    if(std::any_of(m_inputComponents.begin(),m_inputComponents.end(),[](const std::string& component){return component.empty();})){m_status="Every component requires a finite number.";break;}
                    if(m_document.setInputValue(m_graph,m_selection,m_editPin,joined,m_status)){rebuildPins();m_focus=0;input.setTyping(false);m_diagnostic={};m_status="Input components applied.";}break;
                }
                else if(ch<128&&std::string("0123456789.eE+-").find(static_cast<char>(ch))!=std::string::npos)
                {if(m_inputReplace){part.clear();m_inputReplace=false;}appendCharacter(part,ch,64);}
            }
            if(click&&in(x,y,360,391,540,430)){m_document.clearInputValue(m_graph,m_selection,m_editPin);rebuildPins();m_focus=0;input.setTyping(false);m_status="Input reset to default, or requires a wire.";}
            m_left=left;return;
        }
        if(m_focus==6&&!m_inputChoices.empty())
        {
            if(in(x,y,360,452,920,620))
            {
                const float scroll=input.wheel();if(scroll<0&&m_inputChoiceFirst+5<m_inputChoices.size())++m_inputChoiceFirst;if(scroll>0&&m_inputChoiceFirst)--m_inputChoiceFirst;
                if(click&&y>=472)
                {
                    const auto index=m_inputChoiceFirst+static_cast<size_t>((y-472)/28);
                    if(index<m_inputChoices.size()){m_edit=m_inputChoices[index];m_status="Selected value. Enter = Apply.";}
                }
            }
        }
        if(m_focus)
        {
            const int focus=m_focus;
            if(focus==11&&click&&in(x,y,820,244,1000,280)){startRGB(m_commentColour?blueprintRGBHex(m_commentColour):"405F6B");m_left=left;return;}
            const bool form=focus==5||focus==9||(focus>=11&&focus<=14);
            const float footerY=focus==9?420.f:focus==5?400.f:400.f;
            const bool applyClick=inputApply||(form&&click&&in(x,y,720,footerY,840,footerY+36));
            if(form&&click&&in(x,y,850,footerY,focus==5?920.f:980.f,footerY+36)){m_focus=0;input.setTyping(false);m_left=left;return;}
            auto& value=focus==1?m_query:m_edit;
            bool changed=false;
            for(u32 i=0;i<input.typedCount()+(applyClick?1u:0u);++i)
            {
                const u32 ch=i<input.typedCount()?input.typed(i):13u;
                if(ch==8&&!value.empty())
                {
                    size_t end=value.size()-1;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
                    value.resize(end);changed=true;
                }
                else if(ch>=32&&(focus==1||focus==2||focus==6||focus==10||focus==11||ch<127)&&appendCharacter(value,ch,focus==1?80u:(focus==2||focus==6||focus==10)?4096u:focus==9?1024u:focus==11?256u:64u))changed=true;
                else if(ch==13)
                {
                    if(focus==2){if(!m_document.setLiteral(m_graph,m_selection,m_edit,m_status))break;rebuildPins();}
                    if(focus==3)
                    {
                        if(!m_document.setReference(m_graph,m_selection,m_edit,m_status))break;
                        clearWire();rebuildPins();
                    }
                    if(focus==12)
                    {
                        const auto& g=m_document.graphs[m_graph];const auto caller=std::make_pair(g.owner,g.name);
                        if(!m_document.collapseToFunction(m_graph,m_extractNodes,m_edit,m_status,m_extractCollapsed))break;
                        if(m_graphBack.size()>=32)m_graphBack.erase(m_graphBack.begin());
                        m_graphBack.push_back(caller);selectGraph(m_document.graphs.size()-1);
                    }
                    if(focus==13)
                    {
                        const auto owner=m_document.graphs[m_graph].owner,old=m_document.graphs[m_graph].name;
                        if(!m_document.renameFunction(m_graph,m_edit,m_status))break;
                        for(auto& key:m_graphBack)if(key.first==owner&&key.second==old)key.second=m_edit;
                        selectGraph(m_graph);
                    }
                    if(focus==14)
                    {
                        if(!m_document.duplicateFunction(m_graph,m_edit,m_status))break;
                        selectGraph(m_document.graphs.size()-1);
                    }
                    if(focus==11&&!m_document.editComment(m_graph,m_layoutComment,m_edit,m_commentColour,m_status))break;
                    if(focus==9)
                    {
                        if(!m_document.editFunctionSignature(m_graph,m_edit,m_status))break;
                        clearWire();rebuildPins();
                    }
                    if(focus==5&&!saveNamed(s,m_edit))break;
                    if(focus==6)
                    {
                        if(!m_document.setInputValue(m_graph,m_selection,m_editPin,m_edit,m_status))break;
                        clearWire();rebuildPins();filterCatalogue();
                    }
                    if(focus==8)
                    {
                        const size_t first=m_document.graphs.size();
                        if(!m_document.addBehaviour(m_edit,m_status))break;
                        selectGraph(first);
                    }
                    if(focus==7)
                    {
                        const size_t first=m_document.graphs.size();
                        if(!m_document.addScreen(m_edit,m_status))break;
                        selectGraph(first);
                    }
                    if(focus==4)
                    {
                        if(!m_document.addFunction(m_edit,m_status,m_document.graphs[m_graph].owner))break;
                        selectGraph(m_document.graphs.size()-1);
                    }
                    m_focus=0;input.setTyping(false);if(focus!=5){m_status="Ready.";if(focus!=1)m_diagnostic={};}break;
                }
            }
            if(changed&&focus==1)filterCatalogue();
            if(focus==6&&click&&in(x,y,360,391,540,430))
            {
                const bool cleared=m_document.clearInputValue(m_graph,m_selection,m_editPin);m_focus=0;input.setTyping(false);
                clearWire();rebuildPins();filterCatalogue();
                m_status=cleared?"Input reset to its class/API default, or requires a wire.":"Input unchanged; clear dependent spawn pins first.";
            }
            if(click&&m_focus==1&&!in(x,y,kRight+8,kTop+38,kWidth-8,kTop+68)){m_focus=0;input.setTyping(false);}
            else {m_left=left;return;}
        }
        if(!m_drag)
        {
            if((pressed&64u)&&!m_focus&&!m_pinDrag&&!m_pan&&m_pending==static_cast<size_t>(-1)){if(removeSelectedNode(input)&&!m_confirmDelete)saveLayout(s);m_left=left;return;}
            if((pressed&16u)&&!m_focus){m_focus=20;m_findText.clear();m_findFirst=0;input.setTyping(true);m_left=left;return;}
            if((pressed&32u)&&!m_focus){const auto nodes=m_selectedNodes.empty()&&m_selection<m_rows.size()?std::vector<size_t>{m_selection}:m_selectedNodes;if(!nodes.empty()&&m_document.addComment(m_graph,nodes,"Comment",m_status)){m_layoutComment=m_document.graphs[m_graph].comments.size()-1;m_edit="Comment";m_commentColour=0;m_focus=11;input.setTyping(true);}m_left=left;return;}
            if((pressed&8u)&&m_selectedNodes.size()>1){if(m_document.alignNodes(m_graph,m_selectedNodes,true,m_status))m_status="Aligned selected nodes horizontally.";m_left=left;return;}
            if(click&&in(x,y,18,39,245,69)&&!m_actorTabId.empty()&&m_document.visualAsset(m_actorTabId))
            {openActorViewport(m_actorTabId);m_left=left;return;}
            if(click&&in(x,y,300,5,404,33)){m_layoutMenu=true;clearWire();m_left=left;return;}
            if(click&&in(x,y,1134,39,1266,69)){returnToRedux(s);m_left=left;return;}
            if(click&&in(x,y,410,5,560,33)&&m_graph<m_document.graphs.size()&&m_document.graphs[m_graph].kind=="function")
            {m_edit=m_document.functionSignature(m_graph);m_focus=9;input.setTyping(true);m_status="Signature: void(value:int), event(value:int), coroutine(), enum(Idle,Run), struct(x:int).";m_left=left;return;}
            if(click&&in(x,y,410,5,560,33)&&m_graph<m_document.graphs.size()&&m_document.graphs[m_graph].owner.rfind("behaviour:",0)!=0)
            {
                const auto owner=m_document.graphs[m_graph].owner;
                const auto& graphs=m_document.graphs;
                const auto found=std::find_if(graphs.begin(),graphs.end(),[&](const BlueprintDocument::Graph& g){return g.owner==owner&&g.name=="onLevelTouch";});
                const size_t first=found==graphs.end()?graphs.size():static_cast<size_t>(found-graphs.begin());
                if(m_document.addLevelEvents(owner,m_status)){selectGraph(first);m_status="Added native Level trigger and agent events.";}
                else if(first<graphs.size()){selectGraph(first);m_status="Level trigger event. Choose onLevelAgent in the graph list for agent events.";}
                m_left=left;return;
            }
            if(click&&in(x,y,566,5,734,33)){m_edit.clear();m_focus=8;input.setTyping(true);m_status="Type a behaviour name, then Enter.";m_left=left;return;}
            if(click&&in(x,y,740,5,966,33)){browseFiles(s);m_left=left;return;}
            if(click&&in(x,y,972,5,1082,33)){browseAssets(s);m_left=left;return;}
            if(click&&in(x,y,1088,5,1174,33))
            {
                if(m_document.toggleWatch(m_graph,m_selection)){m_traceEnabled=true;m_status="Toggled pause-after-node watch. Trace enabled.";}
                else m_status="Select an execution node to watch.";
                m_left=left;return;
            }
            if(click&&in(x,y,1180,5,1268,33))
            {m_traceEnabled=!m_traceEnabled;m_status=m_traceEnabled?"Trace enabled. Ctrl+L live graph; Ctrl+T pauses during play test.":"Trace disabled.";m_left=left;return;}
            if(m_document.graphs.empty()){m_left=left;return;}
            if(click&&in(x,y,504,39,604,69))
            {
                BlueprintLocation location;m_diagnostic={};m_previewOutputs.clear();
                checkpoint(s,true);
                if(!compileBlueprintProject(m_document,m_previewOutputs,m_status,&location))focusDiagnostic(location);
                else if(validateProject(s,m_previewOutputs))
                {
                    const auto selected=m_document.graphs[m_graph].owner;
                    const auto scope=m_document.moduleForOwner(selected);
                    const auto owner=scope.empty()?m_document.screens().front():scope;
                    if(!m_document.assetRoot.empty()&&!s.platform->fileTime(("own/"+m_document.assetRoot).c_str()))
                        m_status="Project asset folder is missing. Restore it before play testing.";
                    else launchCompiledPreview(s,owner);
                }
                m_left=left;return;
            }
            if(click&&in(x,y,610,39,742,69))compileAndExport(s);
            if((pressed&1u)||(click&&in(x,y,748,39,858,69))){saveCurrent(s);m_left=left;return;}
            if(((pressed&2u)||(click&&in(x,y,864,39,948,69)))&&m_document.undo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Undid graph change.";}
            if(((pressed&4u)||(click&&in(x,y,954,39,1038,69)))&&m_document.redo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Redid graph change.";}
            if(click&&in(x,y,1044,39,1128,69))fitGraph();
        }
        if(!m_graphHideLeft&&click&&in(x,y,kLeft-82,kNavTop-29,kLeft-4,kNavTop-2)){openSceneBrowser(input);m_left=left;return;}
        if(!m_graphHideLeft&&in(x,y,0,kTop,kLeft,kNavTop-30))
        {
            const auto components=componentNavigation();const size_t count=3;
            m_componentNavFirst=std::min(m_componentNavFirst,components.size()>count?components.size()-count:0);
            if(input.wheel()<0&&m_componentNavFirst+count<components.size())++m_componentNavFirst;
            if(input.wheel()>0&&m_componentNavFirst)--m_componentNavFirst;
            if(click&&in(x,y,kLeft-76,kTop+2,kLeft-4,kTop+26)){browseAssets(s);m_assetLibrary=true;m_left=left;return;}
            if(click&&y>=kTop+30){const auto index=m_componentNavFirst+static_cast<size_t>((y-kTop-30)/29);if(index<components.size()&&index<m_componentNavFirst+count){const auto item=components[index];openActorViewport(item.id);if(item.index!=static_cast<size_t>(-1)){m_visualComponent=item.index;m_visualComponentFirst=item.index>2?item.index-2:0;}}m_left=left;return;}
        }
        if(!m_graphHideLeft&&click&&in(x,y,0,kBottom-66,kLeft/3.f,kBottom-36))
        {
            m_variableFirst=0;resetVariableEditor();m_focus=10;input.setTyping(true);m_status="Select a variable or create a new one.";m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,kLeft/3.f,kBottom-66,2*kLeft/3.f,kBottom-36))
        {
            m_edit.clear();m_focus=7;input.setTyping(true);m_status="Type a lowercase screen ID, then Enter.";m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,2*kLeft/3.f,kBottom-66,kLeft,kBottom-36))
        {
            const std::string owner=m_document.graphs[m_graph].owner;
            const bool removed=owner.rfind("behaviour:",0)==0?m_document.removeBehaviour(owner.substr(10),m_status):m_document.removeScreen(owner,m_status);
            if(removed){m_first=0;selectGraph(0);m_status="Removed owner. Undo restores it.";}
            m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,0,kBottom-34,kLeft/3.f,kBottom-4))
        {
            m_edit.clear();m_focus=4;input.setTyping(true);m_status="Type a new function name, then Enter.";m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,kLeft/3.f,kBottom-34,2*kLeft/3.f,kBottom-4))
        {
            if(m_document.removeFunction(m_graph,m_status)){m_first=0;selectGraph(0);m_status="Removed function.";}
            m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,2*kLeft/3.f,kBottom-34,kLeft,kBottom-4))
        {
            if(!preserveBeforeSwitch(s)){}
            else{m_actorTabId.clear();m_actorTabScreen.clear();m_contentFirst=0;m_contentDrag.clear();resetGraphTabs();m_document.create();m_filePath.clear();m_fileBaseline.clear();m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_status="New Blueprint.";}
            m_left=left;return;
        }
        if(!m_graphHideRight&&!m_drag&&(m_paletteDragging||in(x,y,kRight,kTop,kWidth,kBottom)))
        {
            const bool back=m_query.empty()&&!m_paletteGroup.empty();
            const size_t visible=kPaletteRows-(back?1u:0u);
            const size_t maxFirst=m_paletteRows.size()>visible?m_paletteRows.size()-visible:0;
            const float listTop=kTop+80+(back?26.f:0.f),listHeight=26.f*static_cast<float>(visible);
            m_paletteFirst=std::min(m_paletteFirst,maxFirst);
            if(!left)m_paletteDragging=false;
            if(y>=listTop&&y<listTop+listHeight)
            {
                const float wheel=input.wheel();
                const auto steps=static_cast<size_t>(std::clamp(std::ceil(std::abs(wheel)*3.f),1.f,static_cast<float>(visible)));
                if(wheel<0)m_paletteFirst=std::min(maxFirst,m_paletteFirst+steps);
                if(wheel>0)m_paletteFirst=m_paletteFirst>steps?m_paletteFirst-steps:0;
            }
            if(maxFirst&&click&&in(x,y,kWidth-20,listTop,kWidth-4,listTop+listHeight))m_paletteDragging=true;
            if(m_paletteDragging&&left)
            {
                const float thumb=std::max(18.f,listHeight*static_cast<float>(visible)/static_cast<float>(m_paletteRows.size()));
                const float fraction=std::clamp((y-listTop-thumb*.5f)/std::max(1.f,listHeight-thumb),0.f,1.f);
                m_paletteFirst=static_cast<size_t>(std::round(fraction*static_cast<float>(maxFirst)));
                m_left=left;return;
            }
            if(back&&click&&in(x,y,kRight+8,kTop+80,kWidth-8,kTop+104))
            {m_paletteGroup.clear();m_query.clear();filterCatalogue();m_left=left;return;}
            if(click&&in(x,y,kRight+8,kTop+38,kWidth-8,kTop+68)){m_focus=1;input.setTyping(true);m_left=left;return;}
            if(click&&in(x,y,kRight+8,listTop,kWidth-(maxFirst?20:8),listTop+listHeight))
            {
                const size_t row=m_paletteFirst+static_cast<size_t>((y-listTop)/26);
                if(row<m_paletteRows.size())
                {
                    if(!m_paletteRows[row].spec){m_paletteGroup=m_paletteRows[row].group;rebuildPaletteRows();m_left=left;return;}
                    float px=(((graphAreaLeft()+graphAreaRight())*0.5f-kLeft)-m_x)/m_zoom,py=((kBottom-kTop)*0.4f-m_y)/m_zoom;
                    const auto& nodes=m_document.graphs[m_graph].nodes;
                    const auto* spec=m_paletteRows[row].spec;
                    const float height=std::max(kNodeHeight,kPinStart+kPinStep*static_cast<float>(std::max(spec->inputs.size(),spec->outputs.size()))+8);
                    for(size_t attempt=0;attempt<=nodes.size();++attempt)
                    {
                        bool overlap=false;
                        float next=py;
                        for(size_t i=0;i<nodes.size();++i)
                        {
                            const auto& n=nodes[i];
                            if(px<n.x+m_rows[i].width+12&&px+640.f+12>n.x&&py<n.y+m_rows[i].height+12&&py+height+12>n.y)
                            {overlap=true;next=std::max(next,n.y+m_rows[i].height+16);}
                        }
                        if(!overlap)break;
                        py=next;
                    }
                    if(m_document.addNode(m_graph,spec->id,px,py,m_status))
                    {
                        m_diagnostic={};clearWire();rebuildPins();m_selection=m_document.graphs[m_graph].nodes.size()-1;m_selectedNodes.clear();
                        if(kTop+m_y+py*m_zoom>kBottom-40)m_y=(kBottom-kTop-height*m_zoom)*0.5f-py*m_zoom;
                        m_status="Added "+spec->title;
                    }
                }
                m_left=left;return;
            }
            if(click&&m_selection<m_document.graphs[m_graph].nodes.size())
            {
                const auto& n=m_document.graphs[m_graph].nodes[m_selection];
                if(n.kind=="camera:sequence"&&in(x,y,kRight+8,kTop+456+kPaletteExtra,kWidth-8,kTop+486+kPaletteExtra)){beginSequenceEditor(input);}
                else if(blueprintPhysicsJoint(n.kind)&&in(x,y,kRight+8,kTop+456+kPaletteExtra,kWidth-8,kTop+486+kPaletteExtra)){beginJointEditor(input);}
                else if(n.kind=="curve:cubic"&&in(x,y,kRight+8,kTop+456+kPaletteExtra,kWidth-8,kTop+486+kPaletteExtra))
                {beginCurveEditor(input);}
                else if(in(x,y,kRight+8,kTop+456+kPaletteExtra,kWidth-8,kTop+486+kPaletteExtra)&&(n.kind.rfind("literal:",0)==0||n.kind.rfind("var:",0)==0||n.kind.rfind("global:",0)==0||n.kind.rfind("capture:",0)==0||blueprintNamedCall(n.kind)||blueprintBehaviourReference(n.kind)||n.kind.rfind("data:",0)==0||n.kind.rfind("typed:",0)==0))
                {m_edit=n.ref;m_focus=n.kind.rfind("literal:",0)==0?2:3;input.setTyping(true);}
                else if(in(x,y,kRight+8,kTop+520+kPaletteExtra,kWidth-8,kTop+552+kPaletteExtra))
                {beginNodeEditor(m_selection,input);}
                else if(in(x,y,kRight+8,kBottom-40,kWidth-8,kBottom-8))removeSelectedNode(input);
            }
            m_left=left;return;
        }
        if(!m_graphHideLeft&&in(x,y,0,kVariableTop,kLeft,kVariableBottom))
        {
            const auto vars=variableRows();
            m_quickVariableFirst=std::min(m_quickVariableFirst,vars.size()>kVariableRows?vars.size()-kVariableRows:0);
            if(input.wheel()<0&&m_quickVariableFirst+kVariableRows<vars.size())++m_quickVariableFirst;
            if(input.wheel()>0&&m_quickVariableFirst)--m_quickVariableFirst;
            if(click)
            {
                const size_t row=m_quickVariableFirst+static_cast<size_t>((y-kVariableTop)/31);
                if(row<vars.size()){m_quickVariable=vars[row];m_quickDrag=true;m_quickX=x;m_quickY=y;clearWire();m_status="Drag "+m_quickVariable.field.name+" into the graph.";}
            }
            m_left=left;return;
        }
        if(!m_graphHideLeft&&click&&in(x,y,kLeft-77,kVariableTop-24,kLeft-4,kVariableTop-2))
        {
            m_graphTree=!m_graphTree;m_navigationFirst=0;
            revealNavigationGraph(m_graph);saveLayout(s);
            m_status=m_graphTree?"Categorized graph navigation. Click a section to collapse or expand.":"Flat graph navigation.";
            NX_INFO("Blueprint Studio: graph navigator %s",m_graphTree?"categorized":"flat");
            m_left=left;return;
        }
        if (!m_graphHideLeft && left && !m_left && in(x,y,0,kNavTop,kLeft,kNavTop+kRows*31))
        {
            const size_t row = static_cast<size_t>((y-kNavTop)/31);
            if(m_graphTree)
            {
                const auto rows=navigationRows();
                m_navigationFirst=std::min(m_navigationFirst,rows.size()>kRows?rows.size()-kRows:0);
                const auto at=m_navigationFirst+row;
                if(at<rows.size())
                {
                    const auto& item=rows[at];
                    if(item.graph<m_document.graphs.size())selectGraph(item.graph);
                    else if(!item.key.empty())
                    {
                        const auto found=std::find(m_navigationCollapsed.begin(),m_navigationCollapsed.end(),item.key);
                        if(found==m_navigationCollapsed.end())m_navigationCollapsed.push_back(item.key);
                        else m_navigationCollapsed.erase(found);
                        const auto count=navigationRows().size();
                        m_navigationFirst=std::min(m_navigationFirst,count>kRows?count-kRows:0);
                    }
                }
            }
            else if (row < kRows) selectGraph(m_first + row);
        }
        if (left && !m_left && over)
        {
            const auto& layout=m_document.graphs[m_graph];
            for(size_t i=0;i<layout.links.size();++i)for(size_t j=0;j<layout.links[i].route.size();++j)
            {
                const auto& p=layout.links[i].route[j];
                if(std::abs(x-(kLeft+m_x+p.x*m_zoom))>8||std::abs(y-(kTop+m_y+p.y*m_zoom))>8)continue;
                if(control)m_document.deleteRoutePoint(m_graph,i,j);
                else {m_layoutDragging=true;m_layoutMoved=false;m_layoutLink=i;m_layoutPoint=j;m_layoutStartX=m_layoutX=x;m_layoutStartY=m_layoutY=y;}
                clearWire();m_left=left;return;
            }
            for(size_t i=layout.comments.size();i>0;--i)
            {
                float cx,cy,cw,ch;if(!commentBounds(i-1,cx,cy,cw,ch)||!in(x,y,cx-5,cy-5,cx+cw+5,cy+ch+5))continue;
                const float grip=std::max(5.f,8.f*m_zoom);unsigned edges=0;
                if(std::abs(x-cx)<grip)edges|=1u;else if(std::abs(x-cx-cw)<grip)edges|=2u;
                if(std::abs(y-cy)<grip)edges|=4u;else if(std::abs(y-cy-ch)<grip)edges|=8u;
                if(!edges&&!in(x,y,cx,cy,cx+cw,cy+26*m_zoom))continue;
                m_commentResize=edges;m_commentX=(cx-kLeft-m_x)/m_zoom;m_commentY=(cy-kTop-m_y)/m_zoom;m_commentW=cw/m_zoom;m_commentH=ch/m_zoom;
                if(control){m_document.deleteComment(m_graph,i-1);m_layoutComment=static_cast<size_t>(-1);m_commentResize=0;}
                else {m_layoutDragging=true;m_layoutMoved=false;m_layoutLink=static_cast<size_t>(-1);m_layoutComment=i-1;m_layoutMembers=commentMembers(i-1);m_layoutStartX=m_layoutX=x;m_layoutStartY=m_layoutY=y;}
                clearWire();m_left=left;return;
            }
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
                    const float px=nx+(side?rows[i-1].width*m_zoom:0);
                    for(size_t p=0;p<pins.size();++p)
                    {
                        if(std::abs(x-px)>std::min(9.f,10.f*m_zoom)||std::abs(y-(ny+pinY(p)*m_zoom))>std::min(9.f,0.45f*kPinStep*m_zoom))continue;
                        m_nodeClickTime=-1;m_selection=i-1;m_selectedNodes.clear();
                        if(held(Key::Alt)||control){m_highlightWire=static_cast<size_t>(-1);if(m_document.disconnect(m_graph,i-1,pins[p],side!=0)){m_diagnostic={};rebuildPins();m_status="Disconnected pin.";}clearWire();}
                        else if(m_pending!=static_cast<size_t>(-1)&&m_pendingOutput!=(side!=0))
                        {
                            const bool ok=m_pendingOutput?m_document.connectCompatible(m_graph,m_pending,m_pendingPin,i-1,pins[p],m_status):m_document.connectCompatible(m_graph,i-1,pins[p],m_pending,m_pendingPin,m_status);
                            if(ok){m_diagnostic={};rebuildPins();clearWire();m_status="Connected pins.";}
                        }
                        else
                        {
                            m_pending=i-1;m_pendingPin=pins[p];m_pendingOutput=side!=0;m_pinDrag=true;
                            m_pinStartX=m_pinX=x;m_pinStartY=m_pinY=y;m_status="Drag to a compatible pin or empty graph space.";
                        }
                        m_left=left;return;
                    }
                }
            }
            if(m_pending!=static_cast<size_t>(-1)&&std::none_of(nodes.begin(),nodes.end(),[&](const BlueprintDocument::Node& n){const auto i=static_cast<size_t>(&n-nodes.data());const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;return in(x,y,nx,ny,nx+rows[i].width*m_zoom,ny+rows[i].height*m_zoom);}))
            {openPinMenu(x,y);if(m_pinMenu)input.setTyping(true);m_left=left;return;}
            // All visible, unconnected literal fields are editable directly.
            if(!control&&!held(Key::Shift)&&m_pending==static_cast<size_t>(-1))
            {
                for(size_t i=nodes.size();i>0;--i)
                {
                    const auto index=i-1;const auto& n=nodes[index];const auto& row=rows[index];
                    const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;
                    if(!in(x,y,nx,ny,nx+row.width*m_zoom,ny+row.height*m_zoom))continue;
                    if(n.kind.rfind("literal:",0)==0&&row.inputs.empty()&&!row.outputs.empty())
                    {
                        const float py=ny+pinY(0)*m_zoom;
                        if(row.literalFieldW>12.f&&in(x,y,nx+13*m_zoom,py-9*m_zoom,nx+(13.f+row.literalFieldW)*m_zoom,py+9*m_zoom))
                        {m_nodeClickTime=-1;m_selection=index;m_selectedNodes={index};m_edit=n.ref;m_editPin="literal";m_editType=row.outputTypes.front();m_inputComponents.clear();blueprintInputComponents(m_editType,m_edit,m_inputComponents);if(m_editType=="bool"){m_document.setLiteral(m_graph,index,m_edit=="true"?"false":"true",m_status);rebuildPins();m_diagnostic={};m_left=left;return;}m_inlineDirty=false;m_inlineKeys=0;selectInlineField(nx+13*m_zoom,row.literalFieldW*m_zoom);m_focus=19;input.setTyping(true);m_left=left;return;}
                    }
                    const auto details=inputDetails(index);
                    for(size_t p=0;p<row.inputs.size();++p)
                    {
                        const float py=ny+pinY(p)*m_zoom;
                        if(row.fieldW[p]<=12.f||!in(x,y,nx+row.fieldX[p]*m_zoom,py-9*m_zoom,nx+(row.fieldX[p]+row.fieldW[p])*m_zoom,py+9*m_zoom))continue;
                        const auto field=std::find_if(details.begin(),details.end(),[&](const InputDetail& d){return d.name==row.inputs[p]&&d.editable;});
                        if(field==details.end())continue;
                        if(beginInputEdit(index,field->name,input))
                        {
                            m_nodeClickTime=-1;m_selectedNodes={index};m_edit=(field->origin=="Required"||field->value=="Event delta time"||field->value=="Native default")?"":field->value;
                            if(m_editType=="bool"){m_document.setInputValue(m_graph,index,m_editPin,m_edit=="true"?"false":"true",m_status);m_focus=0;input.setTyping(false);rebuildPins();m_diagnostic={};m_left=left;return;}
                            m_inlineDirty=false;m_inlineKeys=0;selectInlineField(nx+row.fieldX[p]*m_zoom,row.fieldW[p]*m_zoom);
                            m_focus=16;m_status="Click to place caret; Tab changes component; Enter = Apply; Escape = Cancel.";
                            m_left=left;return;
                        }
                    }
                    break;
                }
            }
            clearWire();
            bool hitNode=false;
            for(size_t i=nodes.size();i>0;--i)
            {
                const auto& n=nodes[i-1];const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;
                if(!in(x,y,nx,ny,nx+rows[i-1].width*m_zoom,ny+rows[i-1].height*m_zoom))continue;
                hitNode=true;const auto found=std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i-1);
                if(control)
                {
                    m_nodeClickTime=-1;
                    if(found==m_selectedNodes.end())m_selectedNodes.push_back(i-1);else m_selectedNodes.erase(found);
                    m_selection=m_selectedNodes.empty()?static_cast<size_t>(-1):m_selectedNodes.back();m_left=left;return;
                }
                const double now=s.platform->seconds();
                if(!held(Key::Shift)&&m_nodeClickTime>=0&&now-m_nodeClickTime<0.35&&m_nodeClickGraph==m_graph&&m_nodeClickIndex==i-1&&std::hypot(x-m_nodeClickX,y-m_nodeClickY)<9)
                {m_selection=i-1;const auto& kind=n.kind;if(kind=="call:function"||kind=="call:library"||kind=="call:parent"||kind=="call:event"||kind=="async:start")openSelectedFunction();else if(kind=="camera:sequence")beginSequenceEditor(input);else beginNodeEditor(i-1,input);m_left=left;return;}
                m_nodeClickTime=held(Key::Shift)?-1:now;m_nodeClickGraph=m_graph;m_nodeClickIndex=i-1;m_nodeClickX=x;m_nodeClickY=y;
                if(found==m_selectedNodes.end())m_selectedNodes={i-1};
                m_selection=i-1;m_drag=true;m_dragMoved=false;m_dragX=n.x;m_dragY=n.y;
                m_pressX=x;m_pressY=y;m_grabX=n.x;m_grabY=n.y;break;
            }
            if(!hitNode)
            {
                m_nodeClickTime=-1;
                size_t segment=0;const size_t wire=hitWire(x,y,&segment);
                if(wire<graph.links.size())
                {
                    if(held(Key::Shift)){m_highlightWire=m_highlightWire==wire?static_cast<size_t>(-1):wire;m_left=left;return;}
                    if(control){const auto& link=graph.links[wire];const auto from=nodeLayoutPosition(link.from),to=nodeLayoutPosition(link.to);const float ax=kLeft+m_x+(from.x+rows[link.from].width)*m_zoom,ay=kTop+m_y+(from.y+pinY(m_wirePins[wire].output))*m_zoom,bx=kLeft+m_x+to.x*m_zoom,by=kTop+m_y+(to.y+pinY(m_wirePins[wire].input))*m_zoom;m_moveWireOutput=std::hypot(x-ax,y-ay)<std::hypot(x-bx,y-by);m_moveWire=wire;m_moveWireX=x;m_moveWireY=y;m_left=left;return;}
                    const double now=s.platform->seconds();
                    if(m_lastClickedWire==wire&&m_wireClickTime>=0&&now-m_wireClickTime<0.35&&std::hypot(x-m_wireClickX,y-m_wireClickY)<9)
                    {
                        if(m_document.insertRoutePoint(m_graph,wire,segment,(x-kLeft-m_x)/m_zoom,(y-kTop-m_y)/m_zoom)){m_status="Reroute added. Drag to reposition.";m_layoutDragging=true;m_layoutMoved=false;m_layoutLink=wire;m_layoutPoint=segment;m_layoutStartX=m_layoutX=x;m_layoutStartY=m_layoutY=y;}
                        m_wireClickTime=-1;m_lastClickedWire=static_cast<size_t>(-1);m_left=left;return;
                    }
                    m_wireClickTime=now;m_lastClickedWire=wire;m_wireClickX=x;m_wireClickY=y;m_left=left;return;
                }
                m_highlightWire=m_lastClickedWire=static_cast<size_t>(-1);m_wireClickTime=-1;
                m_marqueeMode=control?2u:held(Key::Shift)?1u:0u;m_marqueeBase=m_selectedNodes;if(!m_marqueeMode){m_selectedNodes.clear();m_selection=static_cast<size_t>(-1);}m_marquee=true;m_marqueeX=m_marqueeEndX=x;m_marqueeY=m_marqueeEndY=y;
                m_left=left;return;
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
                    m_nodeClickTime=-1;
                    m_dragX=std::clamp(m_grabX+(x-m_pressX)/m_zoom,-1000000.0f,1000000.0f);
                    m_dragY=std::clamp(m_grabY+(y-m_pressY)/m_zoom,-1000000.0f,1000000.0f);
                    if(held(Key::Shift)){m_dragX=std::round(m_dragX/16.f)*16.f;m_dragY=std::round(m_dragY/16.f)*16.f;}
                }
            }
            else
            {
                if(m_dragMoved&&m_document.moveNodes(m_graph,m_selectedNodes,m_dragX-m_grabX,m_dragY-m_grabY))m_status="Layout changed. Ctrl+S saves your Blueprint.";
                cancelDrag();
            }
            m_pan=false;m_left=left;m_lastX=x;m_lastY=y;
            return;
        }
        // A stationary right click opens the context menu; right-button dragging still pans.
        if(right&&!m_rightDown&&over){m_rightDown=true;m_rightMoved=false;m_rightX=x;m_rightY=y;}
        if(right&&m_rightDown&&(std::abs(x-m_rightX)>5||std::abs(y-m_rightY)>5))m_rightMoved=true;
        if(!right&&m_rightDown)
        {
            m_rightDown=false;
            if(!m_rightMoved&&over)
            {
                clearWire();
                if(control)
                {
                    float best=9.f;size_t bestLink=static_cast<size_t>(-1),bestPoint=0;
                    const auto& links=m_document.graphs[m_graph].links;
                    for(size_t i=0;i<links.size();++i)
                    {
                        std::vector<size_t> segments;const auto points=wireRoute(i,&segments);
                        for(size_t j=1;j<points.size();++j)
                        {
                            const auto& a=points[j-1];const auto& b=points[j];const float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
                            if(len==0)continue;
                            const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/len,0.f,1.f);
                            const float distance=std::hypot(x-a.x-t*dx,y-a.y-t*dy);
                            if(distance<best){best=distance;bestLink=i;bestPoint=segments[j-1];}
                        }
                    }
                    if(bestLink!=static_cast<size_t>(-1)&&m_document.insertRoutePoint(m_graph,bestLink,bestPoint,(x-kLeft-m_x)/m_zoom,(y-kTop-m_y)/m_zoom))m_status="Reroute added. Drag it, or Ctrl+click to remove.";
                    else m_status="Ctrl+right-click a wire to add a reroute.";
                }
                else
                {
                    const auto pin=hitPin(x,y);
                    if(pin.node<m_rows.size()){m_pinContext=true;m_pinContextNode=pin.node;m_pinContextName=pin.name;m_pinContextType=pin.type;m_pinContextOutput=pin.output;m_pinContextX=std::clamp(x,0.f,kWidth-260.f);m_pinContextY=std::clamp(y,0.f,kHeight-150.f);m_pan=false;m_left=left;return;}
                    const auto& nodes=m_document.graphs[m_graph].nodes;
                    for(size_t i=nodes.size();i>0;--i){const auto p=nodeLayoutPosition(i-1);const float nx=kLeft+m_x+p.x*m_zoom,ny=kTop+m_y+p.y*m_zoom;if(!in(x,y,nx,ny,nx+m_rows[i-1].width*m_zoom,ny+m_rows[i-1].height*m_zoom))continue;m_selection=i-1;if(std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i-1)==m_selectedNodes.end())m_selectedNodes={i-1};m_nodeContext=true;m_nodeContextX=std::clamp(x,0.f,kWidth-270.f);m_nodeContextY=std::clamp(y,0.f,kHeight-288.f);m_nodeClickTime=-1;break;}
                    if(!m_nodeContext){openPinMenu(x,y);input.setTyping(true);}
                }
                m_pan=false;m_left=left;return;
            }
        }
        if (pan && m_pan && (!right||m_rightMoved||raw.mouseButtons[2])) { m_x += x-m_lastX; m_y += y-m_lastY; }
        m_pan = pan && (over || m_pan);
        m_lastX=x; m_lastY=y; m_left=left;
        const float wheel = input.wheel();
        if (over && wheel != 0)
        {
            const float previous = m_zoom;
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, std::clamp(wheel,-8.0f,8.0f)),0.35f,2.0f);
            m_x = x-kLeft-(x-kLeft-m_x)*m_zoom/previous;
            m_y = y-kTop-(y-kTop-m_y)*m_zoom/previous;
        }
        else if (!m_graphHideLeft && in(x,y,0,kNavTop,kLeft,kNavTop+kRows*31) && wheel != 0)
        {
            if(m_graphTree)
            {
                const auto count=navigationRows().size();
                m_navigationFirst=std::min(m_navigationFirst,count>kRows?count-kRows:0);
                if(wheel<0&&m_navigationFirst+kRows<count)++m_navigationFirst;
                if(wheel>0&&m_navigationFirst)--m_navigationFirst;
            }
            else
            {
                if (wheel < 0 && m_first+kRows < m_document.graphs.size()) ++m_first;
                if (wheel > 0 && m_first) --m_first;
            }
        }
        if (input.repeated(Action::Left)) m_x += 24;
        if (input.repeated(Action::Right)) m_x -= 24;
        if (input.repeated(Action::Up)) m_y += 24;
        if (input.repeated(Action::Down)) m_y -= 24;
        if (!control && !held(Key::Shift) && input.pressed(Action::Alternate)) selectGraph((m_graph+1)%m_document.graphs.size());
    }

    void BlueprintWorkspace::draw(Canvas& c)
    {
        if (!active()) return;
        resizeWorkspace(c);
        c.design(kWidth,kHeight);
        if(m_actorEditor){drawActorEditor(c);return;}
        c.rect(0,0,kWidth,kHeight,{0.10f,0.10f,0.11f,1});
        c.rect(0,0,kWidth,kTop,{0.16f,0.16f,0.17f,1});
        c.rect(0,34,kWidth,1,{0.30f,0.30f,0.32f,1});
        c.text(*m_font,"NEXGEN  /  BLUEPRINT STUDIO",18,13,text);
        if(kWidth>1530)c.text(*m_font,"EVENT GRAPH / NATIVE EDITOR",kWidth-18,13,muted,Align::Right);
        c.roundRect(300,5,104,28,3,{0.23f,0.24f,0.26f,1});c.text(*m_font,"Layout",352,9,text,Align::Center);
        c.roundRect(1088,5,86,28,3,{0.23f,0.24f,0.26f,1});c.text(*m_font,"Watch",1131,9,text,Align::Center);
        c.roundRect(1180,5,88,28,4,m_traceEnabled?Color{0.23f,0.48f,0.29f,1}:Color{0.19f,0.33f,0.50f,1});
        c.text(*m_font,m_traceEnabled?"Trace on":"Trace off",1224,9,text,Align::Center);
        if(!m_document.graphs.empty()&&m_document.graphs[m_graph].kind=="function")
        {c.roundRect(410,5,150,28,3,{0.23f,0.24f,0.26f,1});c.text(*m_font,"Signature",485,9,text,Align::Center);}
        else if(m_graph<m_document.graphs.size()&&m_document.graphs[m_graph].owner.rfind("behaviour:",0)!=0)
        {c.roundRect(410,5,150,28,3,{0.23f,0.24f,0.26f,1});c.text(*m_font,"Level events",485,9,text,Align::Center);}
        c.roundRect(566,5,168,28,3,{0.23f,0.24f,0.26f,1});
        c.clip(566,5,168,28);c.text(*m_font,m_focus==8?(m_edit.empty()?"Type name...":m_edit.c_str()):"New behaviour",650,9,text,Align::Center);c.noClip();
        c.roundRect(740,5,226,28,3,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Projects",853,9,text,Align::Center);
        c.roundRect(972,5,110,28,3,{0.23f,0.24f,0.26f,1});c.text(*m_font,"Assets",1027,9,text,Align::Center);
        const std::string titleText=(m_document.name.empty()?"No document open":m_document.name)+(m_document.dirty()?" *":"");
        if(!m_actorTabId.empty()&&m_document.visualAsset(m_actorTabId))
        {c.rect(12,39,233,30,{0.25f,0.25f,0.27f,1});c.text(*m_font,"Actor viewport",128,44,text,Align::Center);c.clip(252,38,244,34);c.text(*m_font,shorten(titleText,32).c_str(),258,43,accent);c.noClip();}
        else {c.clip(12,38,480,34);c.text(*m_font,shorten(titleText,65).c_str(),18,43,accent);c.noClip();}
        const auto button=[&](float x,float width,const char* label,bool enabled){
            const bool play=x==504;const Color ink=enabled?(play?Color{0.45f,0.90f,0.59f,1}:text):Color{0.42f,0.44f,0.47f,1};
            c.roundRect(x,39,width,30,3,enabled?(play?Color{0.16f,0.29f,0.21f,1}:Color{0.23f,0.24f,0.26f,1}):Color{0.17f,0.18f,0.19f,1});
            c.roundLine(x,39,width,30,3,1,{0.32f,0.33f,0.35f,1});
            const float a=x+12,b=54;
            if(play){c.line(a-3,b-6,a+6,b,2,ink);c.line(a+6,b,a-3,b+6,2,ink);c.line(a-3,b+6,a-3,b-6,2,ink);}
            else if(x==748){c.rect(a-5,b-6,12,12,ink);c.rect(a-2,b-5,7,4,{0.23f,0.24f,0.26f,1});c.rect(a-2,b+1,6,4,{0.23f,0.24f,0.26f,1});}
            else if(x==864||x==954||x==1134)
            {
                const float dir=x==954?1.f:-1.f;
                c.line(a-dir*5,b,a+dir*5,b,2,ink);c.line(a+dir*5,b,a,b-5,2,ink);c.line(a+dir*5,b,a,b+5,2,ink);
            }
            else if(x==1044){c.roundLine(a-5,b-5,11,11,1,1,ink);c.rect(a-1,b-1,3,3,ink);}
            else {c.line(a,b+4,a,b-6,2,ink);c.line(a,b-6,a-4,b-2,2,ink);c.line(a,b-6,a+4,b-2,2,ink);c.line(a-5,b+6,a+5,b+6,2,ink);}
            c.clip(x+22,39,width-25,30);c.text(*m_font,label,x+width*0.5f+9,44,enabled?text:muted,Align::Center);c.noClip();
        };
        button(504,100,"Play test",!m_document.graphs.empty());
        button(610,132,"Export project",!m_document.graphs.empty());button(748,110,"Save",!m_document.graphs.empty());button(864,84,"Undo",m_document.canUndo());
        button(954,84,"Redo",m_document.canRedo());button(1044,84,"Fit",!m_document.graphs.empty());button(1134,132,"Return",true);
        if(!m_graphHideLeft){c.rect(0,kTop,kLeft,kBottom-kTop,{0.17f,0.17f,0.18f,1});c.rect(kLeft-2,kTop,4,kBottom-kTop,{0.32f,0.32f,0.34f,1});}
        if(!m_graphHideRight){c.rect(kRight,kTop,kWidth-kRight,kBottom-kTop,{0.17f,0.17f,0.18f,1});c.rect(kRight-2,kTop,4,kBottom-kTop,{0.32f,0.32f,0.34f,1});}
        c.clip(graphAreaLeft(),graphAreaTop(),graphAreaRight()-graphAreaLeft(),std::max(1.f,tabTop()-graphAreaTop()));
        const float step=32*m_zoom;
        for(float gx=kLeft+std::fmod(m_x,step);gx<kRight;gx+=step)
            c.line(gx,kTop,gx,kBottom,1,{0.14f,0.14f,0.15f,1});
        for(float gy=kTop+std::fmod(m_y,step);gy<kBottom;gy+=step)
            c.line(kLeft,gy,kRight,gy,1,{0.14f,0.14f,0.15f,1});
        if (m_document.graphs.empty())
        {
            c.text(*m_font,"Drop a Blueprint JSON or supported .as source file",630,330,text,Align::Center);
            c.text(*m_font,"The original file is never modified.",630,360,muted,Align::Center);
        }
        else
        {
            const auto& graph=m_document.graphs[m_graph];
            const auto& rows=m_rows;
            const auto nodeX=[&](size_t i){return nodeLayoutPosition(i).x;};
            const auto nodeY=[&](size_t i){return nodeLayoutPosition(i).y;};
#if !defined(_WIN32)
            // Opt-in X11 regression diagnostics: actual draw coordinates, not a simulated UI.
            if(std::getenv("NX_BLUEPRINT_TEST_GEOMETRY"))
            {
                std::string geometry;
                for(size_t i=0;i<graph.nodes.size();++i)
                {
                    geometry+=" "+std::to_string(i)+":"+std::to_string(kLeft+m_x+nodeX(i)*m_zoom)+","+
                        std::to_string(kTop+m_y+nodeY(i)*m_zoom)+","+std::to_string(rows[i].width*m_zoom)+","+std::to_string(m_zoom);
                }
                const auto trace=std::to_string(m_graph)+geometry;
                if(trace!=m_testGraphGeometry){m_testGraphGeometry=trace;NX_INFO("BP_GEOMETRY %s",trace.c_str());}
            }
#endif
            for(size_t i=0;i<graph.comments.size();++i)
            {
                float cx,cy,cw,ch;if(!commentBounds(i,cx,cy,cw,ch))continue;
                if(cx+cw<kLeft||cx>kRight||cy+ch<kTop||cy>kBottom)continue;
                const Color ink=rgbColour(graph.comments[i].colour,{0.25f,0.37f,0.42f,1});
                c.rect(cx,cy,cw,ch,{ink.r,ink.g,ink.b,0.25f});c.roundLine(cx,cy,cw,ch,5,1.5f,ink);
                c.rect(cx,cy,cw,26*m_zoom,ink);const float grip=std::max(5.f,8.f*m_zoom);c.rect(cx+cw-grip,cy+ch-grip,grip,grip,ink);c.rect(cx,cy+ch-grip,grip,grip,ink);c.push();c.clip(cx+4,cy,cw-8,26*m_zoom);c.text(*m_font,graph.comments[i].title.c_str(),cx+8,cy+4,text);c.pop();
            }
            for(size_t wire=0;wire<graph.links.size();++wire)
            {
                const auto& l=graph.links[wire];const auto& pins=m_wirePins[wire];const auto points=wireRoute(wire);
                Color color=pinColor(rows[l.from].outputTypes[pins.output]);
                if(m_highlightWire<graph.links.size()&&wire!=m_highlightWire)color.a*=0.18f;
                if(wire==m_moveWire)color.a*=0.20f;
                float pulse=0;
                if(liveTraceView()&&rows[l.from].outputTypes[pins.output]=="exec")for(size_t i=1;i<m_traceHits.size();++i){const auto& a=m_traceHits[i-1];const auto& b=m_traceHits[i];if(a.run==b.run&&a.location.graph==m_graph&&b.location.graph==m_graph&&a.location.node==l.from&&b.location.node==l.to)pulse=std::max(pulse,static_cast<float>(std::clamp(1.0-(m_liveTraceClock-b.time)/0.8,0.0,1.0)));}
                if(pulse>0)color={color.r+(1-color.r)*pulse,color.g+(0.72f-color.g)*pulse,color.b+(0.22f-color.b)*pulse,1};
                if(tracePaused()&&rows[l.from].outputTypes[pins.output]=="exec")
                    for(size_t i=1;i<m_traceHits.size()&&i<=m_traceSelected;++i)
                    {
                        const auto& a=m_traceHits[i-1];const auto& b=m_traceHits[i];
                        if(a.run==b.run&&a.location.graph==m_graph&&b.location.graph==m_graph&&a.location.node==l.from&&b.location.node==l.to){color={1.f,0.72f,0.22f,1.f};break;}
                    }
                for(size_t j=1;j<points.size();++j)
                {
                    const auto& a=points[j-1];const auto& b=points[j];
                    if(std::max(a.x,b.x)<kLeft||std::min(a.x,b.x)>kRight||std::max(a.y,b.y)<kTop||std::min(a.y,b.y)>kBottom)continue;
                    c.line(a.x,a.y,b.x,b.y,wire==m_highlightWire?3.5f:2.f+2*pulse,color);
                }
                if(points.size()>1){const auto& a=points[points.size()/2-1];const auto& b=points[points.size()/2];const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);if(len>1){const float xx=(a.x+b.x)*.5f,yy=(a.y+b.y)*.5f,ux=dx/len,uy=dy/len;c.line(xx,yy,xx-6*ux+4*uy,yy-6*uy-4*ux,1.8f,color);c.line(xx,yy,xx-6*ux-4*uy,yy-6*uy+4*ux,1.8f,color);}}
                for(size_t j=0;j<l.route.size();++j){const auto p=routeLayoutPosition(wire,j);c.circle(kLeft+m_x+p.x*m_zoom,kTop+m_y+p.y*m_zoom,6,color);}
            }
            if(m_moveWire<graph.links.size()){const auto points=wireRoute(m_moveWire);if(!points.empty()){const auto& fixed=m_moveWireOutput?points.back():points.front();c.line(fixed.x,fixed.y,m_moveWireX,m_moveWireY,2,accent);}}
            if(m_pending<graph.nodes.size())
            {
                const auto n=nodeLayoutPosition(m_pending);const auto& pins=m_pendingOutput?rows[m_pending].outputs:rows[m_pending].inputs;
                const auto pin=std::find(pins.begin(),pins.end(),m_pendingPin);
                if(pin!=pins.end())
                {
                    const float px=kLeft+m_x+(n.x+(m_pendingOutput?rows[m_pending].width:0))*m_zoom,py=kTop+m_y+(n.y+pinY(static_cast<size_t>(pin-pins.begin())))*m_zoom;
                    c.ring(px,py,9,2,accent);
                    if(m_pinDrag||m_pinMenu)
                    {
                        const auto ink=pinColor((m_pendingOutput?rows[m_pending].outputTypes:rows[m_pending].inputTypes)[static_cast<size_t>(pin-pins.begin())]);
                        const std::vector<BlueprintDocument::Point> anchors=m_pendingOutput?std::vector<BlueprintDocument::Point>{{px,py},{m_pinX,m_pinY}}:std::vector<BlueprintDocument::Point>{{m_pinX,m_pinY},{px,py}};
                        const auto path=blueprintGraphWire(anchors,m_wireStyle,m_zoom,std::min(py,m_pinY),std::max(py,m_pinY),nullptr);
                        for(size_t j=1;j<path.size();++j)c.line(path[j-1].x,path[j-1].y,path[j].x,path[j].y,2,ink);
                    }
                }
            }
            std::vector<std::vector<bool>> wiredInputs(rows.size()),wiredOutputs(rows.size());
            for(size_t i=0;i<rows.size();++i){wiredInputs[i].resize(rows[i].inputs.size(),false);wiredOutputs[i].resize(rows[i].outputs.size(),false);}
            for(size_t i=0;i<graph.links.size()&&i<m_wirePins.size();++i)
            {
                const auto to=graph.links[i].to;const auto pin=m_wirePins[i].input;
                if(to<wiredInputs.size()&&pin<wiredInputs[to].size())wiredInputs[to][pin]=true;
                const auto from=graph.links[i].from;const auto output=m_wirePins[i].output;
                if(from<wiredOutputs.size()&&output<wiredOutputs[from].size())wiredOutputs[from][output]=true;
            }
            for(size_t i=0;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];
                const float x=kLeft+m_x+nodeX(i)*m_zoom,y=kTop+m_y+nodeY(i)*m_zoom,w=rows[i].width*m_zoom,h=rows[i].height*m_zoom;
                if(x+w<kLeft||x>kRight||y+h<kTop||y>kBottom) continue;
                const auto* spec=blueprintSpec(n.kind);
                const bool event=n.kind=="entry"||n.kind.rfind("event:",0)==0;
                const bool pure=spec&&spec->pure;
                const bool flow=n.kind=="branch"||n.kind=="sequence"||n.kind=="repeat"||n.kind.rfind("flow:",0)==0;
                const Color header=rgbColour(n.colour,event?Color{0.49f,0.19f,0.23f,1}:pure?Color{0.22f,0.39f,0.27f,1}:flow?Color{0.35f,0.29f,0.44f,1}:Color{0.20f,0.34f,0.48f,1});
                c.roundRect(x+3*m_zoom,y+4*m_zoom,w,h,5,{0,0,0,0.16f});
                c.roundRect(x,y,w,h,5,{0.115f,0.12f,0.14f,0.83f});
                const float headerHeight=std::max(11.f,24*m_zoom);
                c.roundRect(x,y,w,headerHeight,4,header);
                c.rect(x,y+headerHeight-1,w,1,{0.065f,0.07f,0.08f,1});
                c.roundLine(x,y,w,h,5,1,{0.29f,0.30f,0.33f,1});
                const float ix=x+10*m_zoom,iy=y+12*m_zoom;
                if(event){c.line(ix-4*m_zoom,iy,ix+4*m_zoom,iy,1.5f*m_zoom,text);c.line(ix,iy-4*m_zoom,ix,iy+4*m_zoom,1.5f*m_zoom,text);}
                else if(pure)c.circle(ix,iy,3*m_zoom,text);
                else {c.line(ix-3*m_zoom,iy-4*m_zoom,ix+3*m_zoom,iy,1.5f*m_zoom,text);c.line(ix+3*m_zoom,iy,ix-3*m_zoom,iy+4*m_zoom,1.5f*m_zoom,text);}
                if(n.watch)c.circle(x+w-12*m_zoom,y+12*m_zoom,5*m_zoom,{1,0.72f,0.22f,1});
                if(liveTraceView()){float pulse=0;for(const auto& hit:m_traceHits)if(hit.location.graph==m_graph&&hit.location.node==i)pulse=std::max(pulse,static_cast<float>(std::clamp(1.0-(m_liveTraceClock-hit.time)/0.8,0.0,1.0)));if(pulse>0)c.roundLine(x,y,w,h,5,2+pulse,{1,0.72f,0.22f,pulse});}
                if(m_device.tracing())
                {
                    float recent=0;for(const auto& hit:m_remoteGraphHits)if(hit.location.graph==m_graph&&hit.location.node==i)recent=std::max(recent,static_cast<float>(std::clamp(1.0-(m_remoteNow-hit.time)/0.8,0.0,1.0)));
                    if(recent>0)c.roundLine(x,y,w,h,5,2+recent,{0.20f,0.85f,0.78f,recent});
                }
                if(m_device.paused()&&m_remotePaused.graph==m_graph&&m_remotePaused.node==i)c.roundLine(x,y,w,h,5,3,{0.20f,0.85f,0.78f,1});
                else if(tracePaused()&&m_traceLocation.graph==m_graph&&m_traceLocation.node==i)c.roundLine(x,y,w,h,5,3,{1,0.72f,0.22f,1});
                else if(m_diagnostic.graph==m_graph&&m_diagnostic.node==i)c.roundLine(x,y,w,h,5,3,{0.98f,0.35f,0.38f,1});
                else if(i==m_selection||std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i)!=m_selectedNodes.end())c.roundLine(x,y,w,h,5,2,accent);
                // Draw only the visible rows of tall nodes.
                const size_t first=static_cast<size_t>(std::max(0.0f,std::floor(((kTop-y)/m_zoom-kPinStart)/kPinStep)-1));
                const size_t last=static_cast<size_t>(std::max(0.0f,std::ceil(((kBottom-y)/m_zoom-kPinStart)/kPinStep)+2));
                const auto drawPin=[&](float px,float py,const std::string& type,bool connected)
                {
                    const Color ink=pinColor(type);const float r=5*m_zoom;
                    if(type=="exec")
                    {
                        c.line(px-r,py-r,px+r,py,1.7f*m_zoom,ink);c.line(px+r,py,px-r,py+r,1.7f*m_zoom,ink);
                        c.line(px-r,py+r,px-r,py-r,1.7f*m_zoom,ink);
                    }
                    else
                    {
                        c.circle(px,py,r,ink);
                        if(!connected)c.circle(px,py,2.7f*m_zoom,{0.115f,0.12f,0.14f,1});
                    }
                };
                for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)
                    drawPin(x,y+pinY(p)*m_zoom,rows[i].inputTypes[p],wiredInputs[i][p]);
                for(size_t p=first;p<std::min(last,rows[i].outputs.size());++p)
                    drawPin(x+w,y+pinY(p)*m_zoom,rows[i].outputTypes[p],wiredOutputs[i][p]);
                const auto graphText=[&](const std::string& label,float tx,float ty,float width,float height,float scale,Color ink,Align align=Align::Left)
                {
                    if(width<=0||height<=0)return;
                    c.push();c.clip(tx,ty,width,height);
                    c.translate(align==Align::Right?tx+width:tx,ty+(height-c.measure(*m_font,label.c_str()).y*scale)*0.5f);
                    c.scale(scale,scale);c.text(*m_font,label.c_str(),0,0,ink,align);c.pop();
                };
                const auto drawInline=[&](const std::string& type,const std::string& value,float fx,float py,float fw,bool editing)
                {
                    const auto labels=blueprintInputComponentLabels(type);
                    std::vector<std::string> parts;
                    if(editing&&!m_inputComponents.empty())parts=m_inputComponents;
                    else if(!labels.empty()&&!blueprintInputComponents(type,value,parts)){parts.assign(labels.size(),"0");if(type=="Color")parts[3]="1";}
                    const auto field=[&](float bx,float bw,const std::string& label,const std::string& part,bool active)
                    {
                        c.roundRect(bx,py-9*m_zoom,bw,18*m_zoom,3,active?Color{0.23f,0.37f,0.49f,1}:Color{0.21f,0.22f,0.24f,1});
                        const float inset=(label.empty()?4.f:17.f)*m_zoom;
                        if(!label.empty()){const Color axis=label=="X"||label=="R"?Color{0.95f,0.45f,0.45f,1}:label=="Y"||label=="G"?Color{0.44f,0.85f,0.47f,1}:label=="Z"||label=="B"?Color{0.42f,0.68f,1.f,1}:muted;graphText(label,bx+3*m_zoom,py-9*m_zoom,13*m_zoom,18*m_zoom,m_zoom,axis);}
                        if(active&&m_inlineSelectAll)c.rect(bx+inset,py-8*m_zoom,std::max(0.f,bw-inset-2*m_zoom),16*m_zoom,{0.18f,0.43f,0.70f,1});
                        graphText(part.empty()&&!active?"Edit":part,bx+inset,py-9*m_zoom,bw-inset-3*m_zoom,18*m_zoom,m_zoom,text);
                        if(active){const float at=bx+inset+c.measure(*m_font,part.substr(0,m_inlineCaret).c_str()).x*m_zoom;if(at<bx+bw-2*m_zoom)c.rect(at,py-7*m_zoom,std::max(1.f,m_zoom),14*m_zoom,accent);}
                    };
                    if(type=="bool"){const float side=14*m_zoom;c.roundRect(fx,py-side*.5f,side,side,2,{0.21f,0.22f,0.24f,1});if(value=="true")c.rect(fx+3*m_zoom,py-side*.5f+3*m_zoom,side-6*m_zoom,side-6*m_zoom,accent);graphText(value=="true"?"True":"False",fx+side+4*m_zoom,py-9*m_zoom,fw-side-4*m_zoom,18*m_zoom,m_zoom,text);}
                    else if(!parts.empty()&&parts.size()==labels.size()){const float cell=fw/static_cast<float>(parts.size());for(size_t k=0;k<parts.size();++k)field(fx+cell*static_cast<float>(k),cell-2*m_zoom,labels[k],parts[k],editing&&k==m_inputComponent);}
                    else field(fx,fw,"",value,editing);
                };
                const float titleScale=std::max(0.5f,m_zoom);
                const float titleInset=std::max(12.f,20*m_zoom),titleRight=n.watch?std::max(9.f,20*m_zoom):4.f*m_zoom;
                graphText(rows[i].title,x+titleInset,y,w-titleInset-titleRight,headerHeight,titleScale,text);
                if(!n.comment.empty()){const float cy=y+h-24*m_zoom;c.rect(x+4*m_zoom,cy,w-8*m_zoom,20*m_zoom,{0.28f,0.24f,0.13f,1});graphText(n.comment,x+9*m_zoom,cy,w-18*m_zoom,20*m_zoom,m_zoom,text);}
                {
                    // Field geometry is visible at every zoom, independent of selection.
                    const float pad=13*m_zoom;
                    if(first==0&&n.kind.rfind("literal:",0)==0&&rows[i].inputs.empty()&&!rows[i].outputs.empty())
                    {
                        const float fx=x+pad,py=y+pinY(0)*m_zoom,fw=rows[i].literalFieldW*m_zoom;
                        const bool editing=m_focus==19&&m_selection==i;
                        if(fw>12*m_zoom)drawInline(rows[i].outputTypes.front(),editing?m_edit:n.ref,fx,py,fw,editing);
                    }
                    for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)
                    {
                        const auto& pin=rows[i].inputs[p];const bool wired=wiredInputs[i][p];
                        const float py=y+pinY(p)*m_zoom;
                        const float right=p<rows[i].outputs.size()?(c.measure(*m_font,rows[i].outputs[p].c_str()).x+25)*m_zoom:0;
                        if(!wired&&rows[i].fieldW[p]>12.f)
                        {
                            const float fx=x+rows[i].fieldX[p]*m_zoom,fw=rows[i].fieldW[p]*m_zoom;
                            if(fw>12*m_zoom)
                            {
                                const bool editing=m_focus==16&&i==m_selection&&m_editPin==pin;
                                graphText(pin,x+pad,py-10*m_zoom,fx-x-pad-4*m_zoom,20*m_zoom,m_zoom,muted);
                                drawInline(rows[i].inputTypes[p],editing?m_edit:rows[i].fieldValue[p],fx,py,fw,editing);
                                continue;
                            }
                        }
                        graphText(pin,x+pad,py-10*m_zoom,w-2*pad-right,20*m_zoom,m_zoom,muted);
                    }
                    for(size_t p=first;p<std::min(last,rows[i].outputs.size());++p)
                    {
                        const float width=std::min(w-2*pad,(c.measure(*m_font,rows[i].outputs[p].c_str()).x+4)*m_zoom);
                        graphText(rows[i].outputs[p],x+w-pad-width,y+pinY(p)*m_zoom-10*m_zoom,width,20*m_zoom,m_zoom,muted,Align::Right);
                    }
                }
            }
        }
        if(m_marquee){const float x=std::min(m_marqueeX,m_marqueeEndX),y=std::min(m_marqueeY,m_marqueeEndY),w=std::abs(m_marqueeEndX-m_marqueeX),h=std::abs(m_marqueeEndY-m_marqueeY);c.rect(x,y,w,h,{0.25f,0.55f,0.85f,0.15f});c.roundLine(x,y,w,h,1,1,accent);}
        c.noClip();
        if(!m_graphHideLeft)
        {
        c.rect(0,kTop,kLeft,28,{0.13f,0.15f,0.18f,1});c.text(*m_font,"COMPONENTS",10,kTop+5,accent);c.roundRect(kLeft-76,kTop+2,72,24,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Assets",kLeft-40,kTop+5,text,Align::Center);
        const auto components=componentNavigation();const auto componentFirst=std::min(m_componentNavFirst,components.size()>3?components.size()-3:0);
        for(size_t i=0;i<3&&componentFirst+i<components.size();++i){const auto& item=components[componentFirst+i];const float yy=kTop+30.f+29.f*static_cast<float>(i);const float indent=10.f+12.f*static_cast<float>(std::min(item.depth,5u));c.rect(5,yy,kLeft-10,26,{0.20f,0.23f,0.27f,1});c.roundLine(indent,yy+7,10,10,2,1,accent);c.clip(indent+16,yy,kLeft-indent-21,26);c.text(*m_font,item.label.c_str(),indent+16,yy+5,text);c.noClip();}
        if(components.empty())c.text(*m_font,"No associated actor components",10,kTop+42,muted);
        c.rect(0,kNavTop-30,kLeft,28,{0.13f,0.15f,0.18f,1});c.text(*m_font,"MY BLUEPRINT",10,kNavTop-24,accent);c.roundRect(kLeft-82,kNavTop-28,78,24,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Scenes",kLeft-43,kNavTop-24,text,Align::Center);
        const auto navigation=navigationRows();
        const size_t navFirst=m_graphTree?std::min(m_navigationFirst,navigation.size()>kRows?navigation.size()-kRows:0):m_first;
        for(size_t row=0;row<kRows&&navFirst+row<navigation.size();++row)
        {
            const auto& item=navigation[navFirst+row];const auto i=item.graph;const float y=kNavTop+static_cast<float>(row)*31;
            const float indent=10+12.f*item.depth;
            if(i==m_graph){c.rect(4,y+2,kLeft-8,28,{0.18f,0.30f,0.43f,1});c.rect(4,y+2,3,28,accent);}
            c.clip(5,y,kLeft-10,31);
            if(i>=m_document.graphs.size())
            {
                c.rect(5,y+2,kLeft-10,28,{0.22f,0.22f,0.24f,1});
                const bool closed=std::find(m_navigationCollapsed.begin(),m_navigationCollapsed.end(),item.key)!=m_navigationCollapsed.end();
                if(closed){c.line(indent,y+11,indent+5,y+15,1,muted);c.line(indent+5,y+15,indent,y+19,1,muted);}
                else {c.line(indent,y+12,indent+4,y+17,1,muted);c.line(indent+4,y+17,indent+8,y+12,1,muted);}
                c.text(*m_font,item.label.c_str(),indent+15,y+6,item.depth?muted:text);
            }
            else
            {
                const auto& g=m_document.graphs[i];
                const Color ink=g.kind=="event"?Color{0.86f,0.37f,0.42f,1}:g.inlineMacro?Color{0.67f,0.48f,0.82f,1}:accent;
                c.roundLine(indent,y+10,10,10,2,1,ink);
                c.text(*m_font,shorten(m_graphTree?item.label:g.owner+" / "+(g.sharedLibrary?"[L] ":"")+(g.inlineMacro?"[M] ":"")+(g.dataRole.empty()?"":"["+g.dataRole+"] ")+g.name,32).c_str(),indent+17,y+6,text);
            }
            c.noClip();
        }
        c.text(*m_font,"VARIABLES",10,kVariableTop-20,accent);
        c.roundRect(kLeft-77,kVariableTop-24,73,22,3,{0.24f,0.24f,0.26f,1});
        c.text(*m_font,m_graphTree?"Groups":"List",kLeft-40,kVariableTop-23,text,Align::Center);
        const auto quickRows=variableRows();
        const auto quickFirst=std::min(m_quickVariableFirst,quickRows.size()>kVariableRows?quickRows.size()-kVariableRows:0);
        for(size_t i=0;i<kVariableRows&&quickFirst+i<quickRows.size();++i)
        {
            const auto& r=quickRows[quickFirst+i];const float y=kVariableTop+31*static_cast<float>(i);
            c.rect(5,y+2,kLeft-10,28,{0.22f,0.22f,0.24f,1});c.circle(14,y+16,4,pinColor(r.field.type));
            c.clip(24,y,kLeft-30,31);c.text(*m_font,shorten(std::string(r.global?"Global / ":"")+r.field.name+" : "+r.field.type,28).c_str(),24,y+6,text);c.noClip();
        }
        if(quickRows.empty())c.text(*m_font,"Create variables below.",12,kVariableTop+8,muted);
        const char* actions[]={"Variables","+ Screen","- Screen","+ Function","- Function","New BP"};
        for(unsigned i=0;i<6;++i){const float cell=kLeft/3.f,xx=cell*static_cast<float>(i%3),yy=kBottom-(i<3?66.f:34.f);c.roundRect(xx+2,yy,cell-4,30,4,i==2||i==4?Color{0.32f,0.19f,0.23f,1}:Color{0.19f,0.33f,0.50f,1});c.clip(xx+3,yy,cell-6,30);c.text(*m_font,actions[i],xx+cell/2,yy+6,text,Align::Center);c.noClip();}
        }
        if(!m_graphHideRight)
        {
        c.text(*m_font,"NODE LIBRARY",kRight+12,kTop+10,accent);
        c.roundRect(kRight+8,kTop+38,kWidth-kRight-16,30,4,m_focus==1?Color{0.24f,0.35f,0.48f,1}:Color{0.16f,0.21f,0.28f,1});
        c.clip(kRight+12,kTop+38,kWidth-kRight-24,30);
        c.text(*m_font,m_query.empty()?"Search nodes...":m_query.c_str(),kRight+14,kTop+43,text);c.noClip();
        const bool back=m_query.empty()&&!m_paletteGroup.empty();
        const size_t visible=kPaletteRows-(back?1u:0u),maxFirst=m_paletteRows.size()>visible?m_paletteRows.size()-visible:0;
        const float listTop=kTop+80+(back?26.f:0.f),listHeight=26.f*static_cast<float>(visible);
        if(back){c.rect(kRight+8,kTop+80,kWidth-kRight-16,24,{0.22f,0.30f,0.39f,1});c.text(*m_font,"< All categories",kRight+14,kTop+82,accent);}
        for(size_t row=0;row<visible&&m_paletteFirst+row<m_paletteRows.size();++row)
        {
            const auto& item=m_paletteRows[m_paletteFirst+row];const float y=listTop+26*static_cast<float>(row);
            c.rect(kRight+8,y,kWidth-kRight-(maxFirst?28:16),24,item.spec?Color{0.15f,0.20f,0.27f,1}:Color{0.22f,0.30f,0.39f,1});
            c.clip(kRight+12,y,kWidth-kRight-(maxFirst?36:24),24);
            c.text(*m_font,item.label.c_str(),kRight+14,y+2,item.spec?text:accent);c.noClip();
        }
        if(maxFirst){const float thumb=std::max(18.f,listHeight*static_cast<float>(visible)/static_cast<float>(m_paletteRows.size()));c.roundRect(kWidth-18,listTop,10,listHeight,3,{0.12f,0.15f,0.19f,1});c.roundRect(kWidth-18,listTop+(listHeight-thumb)*static_cast<float>(std::min(m_paletteFirst,maxFirst))/static_cast<float>(maxFirst),10,thumb,3,{0.40f,0.53f,0.66f,1});}
        const auto summary=m_query.empty()?(m_paletteGroup.empty()?"Choose a category":m_paletteGroup):std::to_string(m_palette.size())+" search results";
        c.clip(kRight+12,kTop+346+kPaletteExtra,kWidth-kRight-24,26);
        c.text(*m_font,summary.c_str(),kRight+12,kTop+346+kPaletteExtra,muted);c.noClip();
        c.text(*m_font,"DETAILS",kRight+12,kTop+378+kPaletteExtra,accent);
        if(!m_document.graphs.empty()&&m_selection<m_document.graphs[m_graph].nodes.size())
        {
            const auto& n=m_document.graphs[m_graph].nodes[m_selection];
            const auto* spec=blueprintSpec(n.kind);
            c.clip(kRight+8,kTop+404+kPaletteExtra,kWidth-kRight-16,44);
            c.paragraph(*m_font,spec?spec->title.c_str():n.kind.c_str(),kRight+12,kTop+408+kPaletteExtra,kWidth-kRight-24,text);c.noClip();
            if(n.kind.rfind("literal:",0)==0||n.kind.rfind("var:",0)==0||n.kind.rfind("global:",0)==0||n.kind.rfind("capture:",0)==0||blueprintNamedCall(n.kind)||blueprintBehaviourReference(n.kind)||n.kind.rfind("data:",0)==0||n.kind.rfind("typed:",0)==0)
            {
                c.roundRect(kRight+8,kTop+456+kPaletteExtra,kWidth-kRight-16,30,4,(m_focus==2||m_focus==3)?Color{0.24f,0.35f,0.48f,1}:Color{0.16f,0.21f,0.28f,1});
                c.clip(kRight+12,kTop+456+kPaletteExtra,kWidth-kRight-24,30);
                c.text(*m_font,((m_focus==2||m_focus==3)?m_edit:n.ref).c_str(),kRight+14,kTop+461+kPaletteExtra,text);c.noClip();
                c.text(*m_font,"Click to edit, Enter = Apply",kRight+12,kTop+490+kPaletteExtra,muted);
            }
            if(blueprintPhysicsJoint(n.kind))
            {c.roundRect(kRight+8,kTop+456+kPaletteExtra,kWidth-kRight-16,30,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Configure joint",(kWidth+kRight)*0.5f,kTop+461+kPaletteExtra,text,Align::Center);}
            if(n.kind=="camera:sequence"){c.roundRect(kRight+8,kTop+456+kPaletteExtra,kWidth-kRight-16,30,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Open Sequencer",(kWidth+kRight)*0.5f,kTop+461+kPaletteExtra,text,Align::Center);}
            else if(n.kind=="curve:cubic")
            {c.roundRect(kRight+8,kTop+456+kPaletteExtra,kWidth-kRight-16,30,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Edit curve", (kWidth+kRight)*0.5f,kTop+461+kPaletteExtra,text,Align::Center);}
            if(m_selection<m_document.graphs[m_graph].nodes.size())
            {c.roundRect(kRight+8,kTop+520+kPaletteExtra,kWidth-kRight-16,32,5,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Edit Node", (kWidth+kRight)*0.5f,kTop+526+kPaletteExtra,text,Align::Center);}
            c.roundRect(kRight+8,kBottom-40,kWidth-kRight-16,32,5,{0.38f,0.19f,0.23f,1});
            c.text(*m_font,"Delete selected", (kWidth+kRight)*0.5f,kBottom-34,text,Align::Center);
        }
        }
        c.rect(0,kBottom,kWidth,kHeight-kBottom,{0.16f,0.16f,0.17f,1});
        c.clip(12,kBottom+2,kWidth*0.55f,24);c.text(*m_font,shorten(m_status,110).c_str(),12,kBottom+6,text);c.noClip();
        c.clip(kWidth*0.56f,kBottom+2,kWidth*0.43f,24);c.text(*m_font,((m_device.ready()?"Device connected / ":"Device / ")+m_document.target().label()).c_str(),kWidth-12,kBottom+6,m_document.target().highResolutionWarning()||m_document.target().ram==256?Color{1.f,0.72f,0.22f,1.f}:muted,Align::Right);c.noClip();
        if(!m_recoveryWarning.empty())
            c.text(*m_font,shorten(m_recoveryWarning,105).c_str(),12,kBottom+26,{1.f,0.72f,0.22f,1.f});
        else
        {
            if(m_device.ready())drawRemoteControls(c);
            else
            {
                c.clip(12,kBottom+24,900,22);
                c.text(*m_font,"Drag empty space: select | Shift+drag: snap | Q: align | Double-click wire: reroute",12,kBottom+26,muted);
                c.noClip();
            }
            const char* state=!m_document.dirty()?"Saved / clean":recoveryProtected()?"Recovery protected":"Recovery pending";
            c.text(*m_font,state,kWidth-12,kBottom+26,recoveryProtected()?muted:Color{1.f,0.72f,0.22f,1.f},Align::Right);
        }
        drawGraphMap(c);
        if(m_contentFloating){drawGraphTabs(c);drawContentDrawer(c);}else{drawContentDrawer(c);drawGraphTabs(c);}
        if(m_layoutMenu)
        {
            c.roundRect(300,35,250,600,5,{0.16f,0.16f,0.17f,1});
            const char* labels[]={"Arrange graph","Align selected left","Align selected top","Comment selection","Collapse to function","Rename function","Duplicate function","Open selected function","Back to caller",m_document.graphs[m_graph].inlineMacro?"Convert to function":"Convert to inline macro",m_document.graphs[m_graph].sharedLibrary?"Make library graph local":"Publish to project library","Export project library",m_uiScale==0?"UI scale: Auto":m_uiScale==1?"UI scale: 100%":m_uiScale==2?"UI scale: 125%":"UI scale: 150%","Reset editor layout",m_minimap?"Hide graph overview":"Show graph overview",m_graphHideLeft?"Show navigator":"Hide navigator",m_graphHideRight?"Show node library":"Hide node library","Export project ZIP",m_wireStyle==0?"Wire style: Curved":m_wireStyle==1?"Wire style: Orthogonal":"Wire style: Straight",m_confirmDelete?"Confirm node deletion: On":"Confirm node deletion: Off"};
            const auto& activeGraph=m_document.graphs[m_graph];
            const bool hasSelection=m_selection<activeGraph.nodes.size()||!m_selectedNodes.empty();
            for(size_t i=0;i<20;++i)
            {
                const bool enabled=i==0||i>=11||(i<=2&&m_selectedNodes.size()>=2)||((i==3||i==4)&&hasSelection)||((i==5||i==6)&&activeGraph.kind=="function")||
                    (i==7&&m_selection<activeGraph.nodes.size()&&(activeGraph.nodes[m_selection].kind=="call:function"||activeGraph.nodes[m_selection].kind=="call:library"))||(i==8&&!m_graphBack.empty())||((i==9||i==10)&&activeGraph.kind=="function");
                const float yy=35+30*static_cast<float>(i);c.rect(304,yy+2,242,26,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});c.text(*m_font,labels[i],312,yy+5,enabled?text:muted);
            }
        }
        if(!m_pinHover.empty()&&!m_pinContext&&!m_pinMenu&&!modalOpen())
        {
            const float ww=std::min(450.f,c.measure(*m_font,m_pinHover.c_str()).x+16),xx=std::clamp(m_pinHoverX+12,0.f,kWidth-ww),yy=std::clamp(m_pinHoverY+12,0.f,kHeight-26);
            c.roundRect(xx,yy,ww,26,4,{0.12f,0.16f,0.22f,0.98f});c.clip(xx+6,yy,ww-12,26);c.text(*m_font,m_pinHover.c_str(),xx+8,yy+4,text);c.noClip();
        }
        if(m_pinContext)
        {
            const float x=m_pinContextX,y=m_pinContextY;c.roundRect(x,y,260,150,6,{0.16f,0.16f,0.17f,1});
            c.clip(x+6,y,248,28);c.text(*m_font,(m_pinContextName+" : "+m_pinContextType).c_str(),x+8,y+5,accent);c.noClip();
            const char* labels[]={"Break links","Reset input to default","Add compatible node","Promote to variable"};
            for(unsigned n=0;n<4;++n){const float yy=y+30+30.f*static_cast<float>(n);c.rect(x+4,yy+2,252,26,{0.22f,0.22f,0.24f,1});c.text(*m_font,labels[n],x+10,yy+5,text);}
        }
        if(m_nodeContext&&m_selection<m_rows.size())
        {
            const float x=m_nodeContextX,y=m_nodeContextY;c.roundRect(x,y,270,288,6,{0.16f,0.16f,0.17f,1});c.clip(x+8,y,254,32);c.text(*m_font,m_rows[m_selection].title.c_str(),x+10,y+7,accent);c.noClip();
            const auto& nodeTitle=m_rows[m_selection].title;
            const std::string labels[]={"Edit "+nodeTitle,"Rename "+nodeTitle,"Delete "+nodeTitle,"Disconnect","Change Colour","Add comment","Collapse to graph","Expand collapsed graph"};
            for(unsigned i=0;i<8;++i){const float yy=y+32.f+32.f*static_cast<float>(i);c.rect(x+4,yy+2,262,28,i==2?Color{0.38f,0.19f,0.23f,1}:Color{0.22f,0.22f,0.24f,1});c.clip(x+10,yy+2,250,28);c.text(*m_font,labels[i].c_str(),x+12,yy+6,text);c.noClip();}
        }
        if(m_pinMenu)
        {
            const float x=m_pinMenuX,y=m_pinMenuY;
            c.roundRect(x,y,300,320,6,{0.16f,0.16f,0.17f,1});
            c.clip(x+6,y,288,28);c.text(*m_font,(m_pinType.empty()?"ADD NODE":"COMPATIBLE / "+m_pinType).c_str(),x+8,y+5,accent);c.noClip();
            c.rect(x+6,y+28,288,30,{0.24f,0.35f,0.48f,1});c.clip(x+8,y+28,284,30);c.text(*m_font,m_pinQuery.empty()?"Search nodes...":m_pinQuery.c_str(),x+10,y+33,text);c.noClip();
            for(size_t i=0;i<8&&m_paletteFirst+i<m_contextResults.size();++i)
            {const float ry=y+64+30*static_cast<float>(i);c.rect(x+6,ry,288,28,{0.22f,0.22f,0.24f,1});c.clip(x+8,ry,284,28);c.text(*m_font,m_contextResults[m_paletteFirst+i].title.c_str(),x+10,ry+5,text);c.noClip();}
            if(m_contextResults.empty())c.text(*m_font,"No compatible results.",x+10,y+75,muted);
            c.clip(x+6,y+304,288,16);c.text(*m_font,(std::to_string(m_contextResults.size())+" results | Enter to add").c_str(),x+8,y+304,muted);c.noClip();
        }
        if(m_quickDrag)
        {
            const float x=std::clamp(m_quickX+12,0.f,kWidth-210),y=std::clamp(m_quickY+12,0.f,kHeight-32);
            c.roundRect(x,y,210,32,4,{0.19f,0.33f,0.50f,1});c.clip(x+6,y,198,32);c.text(*m_font,m_quickVariable.field.name.c_str(),x+8,y+7,text);c.noClip();
        }
        if(m_quickMenu)
        {
            const float x=m_quickMenuX,y=m_quickMenuY;
            c.roundRect(x,y,210,90,5,{0.16f,0.16f,0.17f,1});
            c.clip(x+6,y,198,28);c.text(*m_font,m_quickVariable.field.name.c_str(),x+8,y+5,accent);c.noClip();
            c.rect(x+4,y+28,202,29,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Get variable",x+10,y+34,text);
            c.rect(x+4,y+58,202,29,{0.19f,0.33f,0.50f,1});c.text(*m_font,blueprintArrayElement(m_quickVariable.field.type).empty()?"Set variable":"Use array operations",x+10,y+64,blueprintArrayElement(m_quickVariable.field.type).empty()?text:muted);
        }
        if(modalOpen())
        {
            c.rect(0,0,kWidth,kHeight,{0,0,0,0.70f});
            c.push();c.translate(modalX(),modalY());
            if(m_assetBrowser&&m_audioEditor)drawTone(c);
            else if(m_assetBrowser&&m_assetLibrary)drawVisualLibrary(c);
            else if(m_assetBrowser)
            {
                c.roundRect(260,100,760,525,9,{0.16f,0.16f,0.17f,1});
                c.roundRect(280,116,100,34,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Audio",330,123,text,Align::Center);
                c.roundRect(384,116,140,34,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Content",454,123,text,Align::Center);
                c.roundRect(534,116,160,34,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Scene editor",614,123,text,Align::Center);
                c.roundRect(704,116,146,34,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Refresh",777,123,text,Align::Center);
                c.roundRect(860,116,140,34,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Close",930,123,text,Align::Center);
                const auto scope=m_sharedMedia?"own/"+m_document.assetRoot+"/_global":"own/"+m_document.assetRoot+"/"+assetScreen()+"/asset";
                c.clip(280,155,520,30);c.text(*m_font,scope.c_str(),280,160,muted);c.noClip();
                c.roundRect(810,155,190,30,4,{0.19f,0.33f,0.50f,1});
                c.text(*m_font,m_sharedMedia?"Shared / global:":"Screen / asset:",905,160,text,Align::Center);
                for(size_t i=0;i<10&&m_assetFirst+i<m_assetFiles.size();++i)
                {
                    const float y=190+36*static_cast<float>(i);
                    c.rect(280,y,720,33,{0.22f,0.22f,0.24f,1});
                    c.clip(286,y,708,33);c.text(*m_font,(std::string(m_sharedMedia?"global:":"asset:")+m_assetFiles[m_assetFirst+i]).c_str(),290,y+8,text);c.noClip();
                }
                if(m_assetFiles.empty())c.text(*m_font,"Drop a file here to import it.",640,330,muted,Align::Center);
                c.text(*m_font,m_sharedMedia?"Drop files to share across screens; click to show global: reference.":"Click a sound or preset to edit; drop files to import.",640,565,muted,Align::Center);
                c.clip(280,591,720,26);c.text(*m_font,m_status.c_str(),280,595,text);c.noClip();
            }
            else if(m_browsing)
            {
                c.rect(0,0,1280,720,{0.075f,0.09f,0.115f,1});
                c.rect(0,0,1280,94,{0.13f,0.16f,0.21f,1});
                c.text(*m_font,"NEXGEN  /  BLUEPRINT STUDIO",28,22,accent);
                c.text(*m_font,"PROJECT LIBRARY",28,51,text);
                const auto projectButton=[&](float x,float y,float width,const char* label,bool selected=false)
                {c.roundRect(x,y,width,34,4,selected?Color{0.19f,0.36f,0.56f,1}:Color{0.19f,0.23f,0.30f,1});c.text(*m_font,label,x+width/2,y+7,text,Align::Center);};
                c.text(*m_font,"CREATE PROJECT",28,120,accent);c.text(*m_font,"MY PROJECTS / IMPORT",280,122,accent);
                projectButton(550,116,100,"Save");projectButton(660,116,190,"Save copy");projectButton(860,116,140,"Editor");projectButton(1010,116,225,"Return to Redux");
                c.roundRect(28,166,222,80,5,m_projectCreate==1?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});
                c.text(*m_font,"BLANK",42,179,text);c.text(*m_font,"Empty event graphs",42,210,muted);
                c.roundRect(28,256,222,54,5,m_projectCreate==2?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});
                c.text(*m_font,"FIRST PERSON",42,263,text);c.text(*m_font,"Robot / capsule walker",42,285,muted);
                c.roundRect(28,318,222,56,5,m_projectCreate==3?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});c.text(*m_font,"THIRD PERSON",42,325,text);c.text(*m_font,"Robot / chase camera",42,347,muted);
                if(m_projectCreate)
                {
                    c.text(*m_font,"PROJECT NAME",28,386,accent);
                    c.roundRect(28,410,222,38,4,{0.19f,0.25f,0.34f,1});
                    c.clip(38,410,202,38);c.text(*m_font,m_projectName.empty()?"Enter name...":m_projectName.c_str(),38,419,text);c.noClip();
                    projectButton(28,458,222,"Create & open",true);
                }
                c.roundRect(28,502,107,52,5,m_projectCreate==4?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});c.text(*m_font,"Cinematic",81.5f,518,text,Align::Center);
                c.roundRect(143,502,107,52,5,m_projectCreate==7?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});c.text(*m_font,"Health HUD",196.5f,518,text,Align::Center);
                c.roundRect(28,559,107,56,5,m_projectCreate==5?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});
                c.text(*m_font,"Flight",81.5f,577,text,Align::Center);
                c.roundRect(143,559,107,56,5,m_projectCreate==6?Color{0.19f,0.36f,0.56f,1}:Color{0.16f,0.20f,0.27f,1});
                c.text(*m_font,"Rolling ball",196.5f,577,text,Align::Center);
                for(size_t i=0;i<12&&m_fileFirst+i<m_files.size();++i)
                {
                    const float y=166+36*static_cast<float>(i);
                    const auto& file=m_files[m_fileFirst+i];
                    c.rect(280,y,955,33,(!m_filePath.empty()&&"own/"+file==m_filePath)?Color{0.20f,0.34f,0.47f,1}:Color{0.16f,0.20f,0.26f,1});
                    const std::string label=(blueprintPackageName(file)?"PACKAGE  /  ":recoveryArchiveName(file)?"RECOVERY  /  ":standaloneSourceName(file)?"SOURCE  /  ":projectFolderName(file)?"PROJECT FOLDER  /  ":"PROJECT  /  ")+file;
                    c.clip(290,y,935,33);c.text(*m_font,label.c_str(),290,y+8,text);c.noClip();
                }
                if(m_files.empty())c.text(*m_font,"No saved projects yet. Choose a template to begin.",760,330,muted,Align::Center);
                c.rect(0,625,1280,95,{0.13f,0.16f,0.21f,1});
                c.text(*m_font,("Active: "+m_document.name+(m_document.dirty()?"  /  Unsaved changes":"")+" / "+m_document.target().label()).c_str(),28,638,accent);
                c.clip(28,665,1220,40);c.text(*m_font,m_status.c_str(),28,673,text);c.noClip();
            }
            else if(m_focus==26)
            {
                c.roundRect(330,250,620,210,8,{0.16f,0.18f,0.22f,1});c.text(*m_font,"PROMOTE PIN TO VARIABLE",350,265,accent);
                c.text(*m_font,(m_pinContextName+" : "+m_pinContextType).c_str(),350,300,text);c.rect(350,332,580,38,{0.24f,0.35f,0.48f,1});c.text(*m_font,m_edit.c_str(),360,340,text);
                c.text(*m_font,"Cancel",710,413,text,Align::Center);c.roundRect(790,400,140,40,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Create",860,411,text,Align::Center);
            }
            else if(m_focus==25)
            {
                c.roundRect(330,250,620,215,8,{0.16f,0.18f,0.22f,1});
                c.text(*m_font,m_deleteSelection.size()==1?"Delete selected node?":"Delete selected nodes?",350,266,accent);
                const auto question=m_deleteSelection.size()==1?std::string("Are you sure you wish to delete this node?"):"Delete "+std::to_string(m_deleteSelection.size())+" selected nodes?";
                c.text(*m_font,question.c_str(),350,303,text);c.text(*m_font,"Connected wires will also be removed. Undo restores them.",350,328,muted);
                c.roundRect(350,359,20,20,3,{0.25f,0.30f,0.36f,1});if(m_skipDeleteConfirmation)c.rect(354,363,12,12,accent);
                c.text(*m_font,"Don't ask me again",380,360,text);
                c.roundRect(660,405,130,40,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",725,415,text,Align::Center);
                c.roundRect(800,405,130,40,4,{0.48f,0.21f,0.25f,1});c.text(*m_font,"Delete",865,415,text,Align::Center);
            }
            else if(m_focus==28)drawXboxDeploy(c);
            else if(m_focus==27)drawDeviceManager(c);
            else if(m_focus==24)drawBuildManager(c);
            else if(m_focus==23)drawInputActions(c);
            else if(m_focus==22)drawSceneBrowser(c);
            else if(m_focus==21)drawSequenceEditor(c);
            else if(m_focus==20){c.roundRect(220,100,840,520,8,{0.16f,0.18f,0.22f,1});c.text(*m_font,"FIND IN BLUEPRINT",240,115,accent);c.roundRect(240,145,800,36,4,{0.24f,0.35f,0.48f,1});c.text(*m_font,m_findText.empty()?"Search nodes, functions, comments and values...":m_findText.c_str(),250,152,text);const auto results=findResults();for(size_t i=0;i<10&&m_findFirst+i<results.size();++i){const float yy=190.f+38.f*static_cast<float>(i);c.rect(240,yy,800,34,{0.22f,0.25f,0.3f,1});c.clip(250,yy,780,34);c.text(*m_font,results[m_findFirst+i].label.c_str(),250,yy+7,text);c.noClip();}c.text(*m_font,(std::to_string(results.size())+" results | Enter = Open | Escape = Close").c_str(),240,585,muted);}
            else if(m_focus==18)drawJointEditor(c);
            else if(m_focus==17)
            {
                c.roundRect(210,100,860,520,9,{0.16f,0.16f,0.17f,1});
                c.text(*m_font,"CUBIC BEZIER / CONTROL POINTS",235,119,accent);
                c.text(*m_font,"Drag endpoints or handles. The path uses Redux's native Curve.",235,141,muted);
                c.rect(240,165,800,370,{0.10f,0.12f,0.15f,1});
                c.push();c.clip(240,165,800,370);
                for(int i=0;i<=20;++i)c.line(240.f+40.f*static_cast<float>(i),165,240.f+40.f*static_cast<float>(i),535,1,{0.18f,0.21f,0.25f,1});
                for(int i=0;i<=9;++i)c.line(240,170.f+40.f*static_cast<float>(i),1040,170.f+40.f*static_cast<float>(i),1,{0.18f,0.21f,0.25f,1});
                Curve preview;Vec2 controlPoints[4];
                for(int i=0;i<4;++i){controlPoints[i]={640+(m_curveX[i]-m_curveCenterX)*m_curveScale,350+(m_curveY[i]-m_curveCenterY)*m_curveScale};preview.add(controlPoints[i]);}
                for(int i=1;i<4;++i)c.line(controlPoints[i-1].x,controlPoints[i-1].y,controlPoints[i].x,controlPoints[i].y,1.5f,muted);
                Vec2 samples[65];if(preview.count()==4){const auto count=preview.points(65,samples);if(count>1)c.polyline(samples,count,3,accent);}
                for(int i=0;i<4;++i)
                {
                    const auto p=controlPoints[i];c.circle(p.x,p.y,7,(i==0||i==3)?Color{0.28f,0.85f,0.6f,1}:accent);c.ring(p.x,p.y,9,1.5f,text);
                    char label[80];snprintf(label,sizeof(label),"P%d (%.2f, %.2f)",i,m_curveX[i],m_curveY[i]);
                    c.text(*m_font,label,p.x+12,p.y-20,text);
                }
                c.pop();
                c.clip(235,558,560,38);c.text(*m_font,m_status.c_str(),235,565,muted);c.noClip();
                c.roundRect(810,555,120,40,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",870,565,text,Align::Center);
                c.roundRect(940,555,110,40,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",995,565,text,Align::Center);c.text(*m_font,"Enter = Apply | Escape = Cancel",235,598,muted);
            }
            else if(m_focus==10)
            {
                c.roundRect(150,100,980,525,9,{0.16f,0.16f,0.17f,1});
                const auto owner=m_document.graphs[m_graph].owner;
                const auto rows=variableRows();
                auto variableButton=[&](float x,float y,float w,const char* label,bool enabled=true)
                {c.roundRect(x,y,w,34,4,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.16f,0.20f,0.26f,1});c.text(*m_font,label,x+w*0.5f,y+7,enabled?text:muted,Align::Center);};
                c.text(*m_font,("VARIABLES / "+owner).c_str(),170,122,accent);
                variableButton(974,116,136,"Close");variableButton(170,156,200,"+ Instance field");variableButton(380,156,240,"+ Module variable",!m_document.moduleForOwner(owner).empty());
                for(size_t i=0;i<10&&m_variableFirst+i<rows.size();++i)
                {
                    const auto& r=rows[m_variableFirst+i];const float y=190+36*static_cast<float>(i);
                    const bool selected=r.global==m_variableGlobal&&r.field.name==m_variableOld;
                    c.rect(170,y,450,33,selected?Color{0.19f,0.33f,0.50f,1}:Color{0.22f,0.22f,0.24f,1});
                    const auto label=std::string(r.global?"Global / ":"Field / ")+r.field.name+" : "+r.field.type+(r.inherited?" (inherited)":r.explicitField?"":" (inferred)")+(r.field.exposed?" [spawn]":"");
                    c.clip(178,y,434,33);c.text(*m_font,label.c_str(),180,y+7,text);c.noClip();
                }
                if(rows.empty())c.text(*m_font,"No declared variables yet.",180,208,muted);
                c.text(*m_font,(std::to_string(rows.size())+" variables | wheel to scroll").c_str(),170,556,muted);
                c.text(*m_font,m_variableGlobal?"MODULE VARIABLE":"INSTANCE FIELD",650,192,accent);
                const auto field=[&](float y,const std::string& value,bool focused)
                {c.roundRect(650,y,450,36,4,focused?Color{0.24f,0.35f,0.48f,1}:Color{0.22f,0.22f,0.24f,1});c.clip(658,y,434,36);c.text(*m_font,value.c_str(),660,y+8,text);c.noClip();};
                field(222,m_variableName.empty()?"Name":m_variableName,m_variableInput==1);
                field(266,"Type: "+m_variableType+"  v",m_variableTypes);
                variableButton(650,322,450,m_variableInitialized?"Initial value: enabled":"Initial value: default",blueprintLiteralType(m_variableType));
                if(m_variableInitialized)field(370,m_variableValue.empty()?"":m_variableValue,m_variableInput==2);
                else c.text(*m_font,"Uses the native default for this type.",650,380,muted);
                c.text(*m_font,"String values are entered without quotes.",650,420,muted);
                variableButton(650,464,210,"Apply");variableButton(870,464,230,"Delete",!m_variableOld.empty());
                if(owner.rfind("behaviour:",0)==0&&!m_variableGlobal)
                {
                    const auto selectedField=std::find_if(rows.begin(),rows.end(),[&](const VariableRow& row){return !row.global&&row.field.name==m_variableOld;});
                    const bool selected=selectedField!=rows.end(),exposed=selected&&selectedField->field.exposed;
                    variableButton(650,510,450,exposed?"Expose on spawn: enabled":"Expose on spawn: disabled",
                        selected&&selectedField->explicitField&&!selectedField->inheritedExposure&&blueprintLiteralType(selectedField->field.type));
                }
                if(m_variableTypes)
                {
                    const auto types=variableTypeChoices();
                    for(size_t i=0;i<24&&m_variableTypeFirst+i<types.size();++i)
                    {const float yy=302+28*static_cast<float>(i%8),xx=650+150*static_cast<float>(i/8);c.rect(xx,yy,150,28,{0.24f,0.35f,0.48f,1});c.clip(xx+2,yy,146,28);c.text(*m_font,shorten(types[m_variableTypeFirst+i],19).c_str(),xx+5,yy+3,text);c.noClip();}
                }
                c.clip(170,576,940,25);c.text(*m_font,m_status.c_str(),170,576,text);c.noClip();
                c.text(*m_font,"Enter = Apply | Escape closes",640,601,muted,Align::Center);
            }
            else if(m_focus==15)
            {
                c.roundRect(160,68,980,584,8,{0.16f,0.16f,0.17f,1});
                const auto& selected=m_document.graphs[m_graph].nodes[m_selection];
                c.push();c.clip(180,82,470,34);c.text(*m_font,("EDIT NODE / "+title(selected)).c_str(),180,88,accent);c.pop();
                c.text(*m_font,"Colour",680,90,muted);c.roundRect(760,82,344,32,4,rgbColour(m_inspectorColour,{0.20f,0.34f,0.48f,1}));c.text(*m_font,m_inspectorColour?blueprintRGBHex(m_inspectorColour).c_str():"Default / Choose colour",932,88,text,Align::Center);
                c.text(*m_font,"Input / type",190,130,muted);c.text(*m_font,"Value",430,130,muted);c.text(*m_font,"State",920,130,muted);
                for(size_t i=m_inspectorFirst;i<std::min(m_inspectorFirst+9,m_inspectorRows.size());++i)
                {
                    const auto& row=m_inspectorRows[i];const float yy=156+44*static_cast<float>(i-m_inspectorFirst);
                    c.rect(180,yy,920,40,{0.15f,0.20f,0.27f,1});
                    c.push();c.clip(190,yy,225,40);c.text(*m_font,row.name.c_str(),190,yy+2,text);c.text(*m_font,row.type.c_str(),190,yy+20,muted);c.pop();
                    if(row.editable)c.roundRect(425,yy+4,480,32,4,i==m_inspectorRow?Color{0.26f,0.42f,0.59f,1}:Color{0.22f,0.28f,0.35f,1});
                    c.push();c.clip(430,yy,474,40);c.text(*m_font,(i==0&&m_inspectorDraft[i].empty()?title(selected)+" (default)":i==1&&m_inspectorDraft[i].empty()?std::string("Add comment..."):row.editable?m_inspectorDraft[i]:row.value).c_str(),435,yy+11,text);c.pop();
                    c.text(*m_font,m_inspectorChanged[i]?"Modified":row.origin.c_str(),920,yy+10,muted);
                }
                if(m_inspectorRows.empty())c.text(*m_font,"This node has no editable literal inputs.",190,175,muted);
                c.clip(180,556,920,40);c.paragraph(*m_font,m_status.c_str(),180,558,920,muted);c.noClip();
                c.text(*m_font,"Enter = Apply | Escape = Cancel",180,615,muted);
                c.roundRect(650,603,190,37,4,{0.38f,0.19f,0.23f,1});c.text(*m_font,"Delete Node",745,611,text,Align::Center);
                c.roundRect(850,603,130,37,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",915,611,text,Align::Center);
                c.roundRect(990,603,130,37,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",1055,611,text,Align::Center);
            }
            else if(m_focus>=12&&m_focus<=14)
            {
                c.roundRect(260,230,760,220,8,{0.16f,0.16f,0.17f,1});c.text(*m_font,m_focus==12?(m_extractCollapsed?"COLLAPSE TO GRAPH":"COLLAPSE TO FUNCTION"):m_focus==13?"RENAME FUNCTION":"DUPLICATE FUNCTION",280,250,accent);
                c.rect(280,290,720,36,{0.24f,0.35f,0.48f,1});c.clip(286,290,708,36);c.text(*m_font,m_edit.c_str(),290,297,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),280,340,720,muted);c.text(*m_font,"Enter = Apply | Escape = Cancel",280,409,muted);c.roundRect(720,400,120,36,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",780,407,text,Align::Center);c.roundRect(850,400,130,36,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",915,407,text,Align::Center);
            }
            else if(m_focus==11)
            {
                c.roundRect(260,230,760,220,8,{0.16f,0.16f,0.17f,1});c.text(*m_font,"COMMENT TITLE",280,250,accent);c.roundRect(820,244,180,36,4,rgbColour(m_commentColour,{0.25f,0.37f,0.42f,1}));c.text(*m_font,"Change Colour",910,252,text,Align::Center);
                c.rect(280,290,720,36,{0.24f,0.35f,0.48f,1});c.clip(286,290,708,36);c.text(*m_font,m_edit.c_str(),290,297,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),280,340,720,muted);c.text(*m_font,"Enter = Apply | Escape = Cancel",280,409,muted);c.roundRect(720,400,120,36,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",780,407,text,Align::Center);c.roundRect(850,400,130,36,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",915,407,text,Align::Center);
            }
            else if(m_focus==9)
            {
                c.roundRect(200,226,880,240,9,{0.16f,0.16f,0.17f,1});
                c.text(*m_font,"FUNCTION SIGNATURE",220,245,accent);
                c.roundRect(220,285,840,36,4,{0.24f,0.35f,0.48f,1});
                c.clip(224,285,832,36);c.text(*m_font,m_edit.c_str(),230,293,text);c.noClip();
                c.paragraph(*m_font,"Format: float(amount:float, count:int) | void() for no return. Up to 8 parameters.",220,330,840,muted);
                c.paragraph(*m_font,m_status.c_str(),220,375,840,muted);
                c.text(*m_font,"Enter = Apply | Escape = Cancel",220,429,muted);c.roundRect(720,420,120,36,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",780,427,text,Align::Center);c.roundRect(850,420,130,36,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",915,427,text,Align::Center);
            }
            else if(m_focus==6)
            {
                c.roundRect(340,226,600,224,9,{0.16f,0.16f,0.17f,1});
                c.text(*m_font,("INPUT: "+m_editPin+" ("+m_editType+")").c_str(),360,245,accent);
                if(m_inputComponents.empty())
                {
                    c.roundRect(360,285,560,36,4,{0.24f,0.35f,0.48f,1});
                    c.clip(364,285,552,36);c.text(*m_font,m_edit.c_str(),370,293,text);c.noClip();
                }
                else
                {
                    const auto labels=blueprintInputComponentLabels(m_editType);const float width=560.f/static_cast<float>(labels.size());
                    for(size_t i=0;i<labels.size();++i)
                    {
                        const float xx=360+width*static_cast<float>(i);
                        c.text(*m_font,labels[i].c_str(),xx+6,282,muted);
                        c.roundRect(xx+2,303,width-4,34,4,i==m_inputComponent?Color{0.26f,0.42f,0.59f,1}:Color{0.17f,0.23f,0.31f,1});
                        c.push();c.clip(xx+5,303,width-10,34);c.text(*m_font,m_inputComponents[i].c_str(),xx+8,310,text);c.pop();
                    }
                }
                c.paragraph(*m_font,m_status.c_str(),360,345,560,muted);
                c.roundRect(360,391,180,39,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Reset value",450,400,text,Align::Center);
                if(m_editType=="Color"){c.roundRect(550,391,90,39,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"RGB / Hex",595,400,text,Align::Center);}
                c.roundRect(650,391,130,39,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",715,400,text,Align::Center);
                c.roundRect(790,391,130,39,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",855,400,text,Align::Center);
                c.text(*m_font,"Enter = Apply | Escape = Cancel",360,431,muted);
                if(!m_inputChoices.empty())
                {
                    c.roundRect(340,450,600,178,8,{0.16f,0.16f,0.17f,1});c.text(*m_font,blueprintEnumChoices(m_editType).empty()?"Known named entities (scroll to choose)":"Enum values (scroll to choose)",360,451,accent);
                    for(size_t i=m_inputChoiceFirst;i<std::min(m_inputChoiceFirst+5,m_inputChoices.size());++i)
                    {
                        const float yy=472+28*static_cast<float>(i-m_inputChoiceFirst);c.rect(360,yy,560,26,m_edit==m_inputChoices[i]?Color{0.24f,0.35f,0.48f,1}:Color{0.17f,0.23f,0.31f,1});
                        c.push();c.clip(365,yy,550,26);c.text(*m_font,m_inputChoices[i].c_str(),370,yy+3,text);c.pop();
                    }
                }
            }
            else
            {
                c.roundRect(340,240,600,205,9,{0.16f,0.16f,0.17f,1});
                c.text(*m_font,"SAVE BLUEPRINT AS",360,260,accent);
                c.roundRect(360,300,560,36,4,{0.24f,0.35f,0.48f,1});
                c.clip(364,300,552,36);c.text(*m_font,m_edit.empty()?"Type a new name...":m_edit.c_str(),370,308,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),360,351,560,muted);
                c.text(*m_font,"Enter = Apply | Escape = Cancel",360,409,muted);c.roundRect(720,400,120,36,4,{0.19f,0.4f,0.3f,1});c.text(*m_font,"Apply",780,407,text,Align::Center);c.roundRect(850,400,70,36,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",885,407,text,Align::Center);
            }
            if(m_rgbOpen)drawRGB(c);
            c.pop();
            if(visualTabOpen())drawGraphTabs(c);
        }
        if(!m_notice.empty())
        {
            c.rect(0,0,kWidth,kHeight,{0,0,0,0.62f});c.push();c.translate(modalX(),modalY());
            c.roundRect(300,220,680,250,8,{0.16f,0.18f,0.22f,1});c.text(*m_font,"BLUEPRINT STUDIO",325,243,accent);
            c.paragraph(*m_font,m_notice.c_str(),325,285,630,text);c.roundRect(780,405,170,38,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"OK",865,414,text,Align::Center);c.pop();
        }
    }
}
