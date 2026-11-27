#include "app/blueprint_workspace.h"
#include "tools/blueprint/native_compile.h"
#include "tools/blueprint/native_assets.h"
#include "tools/blueprint/native_import.h"
#include "script/script_host.h"
#include "script/debugger.h"
#include "screens/screens.h"
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
#include <cstdio>
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
        constexpr size_t kRows = 8;
        constexpr float kVariableTop=350,kVariableBottom=443;
        constexpr size_t kVariableRows=3;
        constexpr const char* variableTypes[]={"bool","int","uint","float","double","string"};
        constexpr Color text = {0.88f, 0.91f, 0.96f, 1};
        constexpr Color muted = {0.57f, 0.65f, 0.76f, 1};
        constexpr Color accent = {0.27f, 0.65f, 0.98f, 1};
        bool in(float x, float y, float left, float top, float right, float bottom)
        { return x >= left && x < right && y >= top && y < bottom; }
        std::string title(const BlueprintDocument::Node& node)
        {
            if(node.kind.rfind("capture:",0)==0)return "Capture "+node.ref;
            if(node.kind.rfind("global:",0)==0)return (node.kind.rfind("global:get:",0)==0?"Get global ":"Set global ")+node.ref;
            if(node.kind.rfind("var:",0)==0)return (node.kind.rfind("var:get:",0)==0?"Get ":"Set ")+node.ref;
            if(node.kind=="call:function")return "Call "+node.ref;
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
            if(type=="int"||type=="uint")return {0.34f,0.82f,0.81f,1};
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

    void BlueprintWorkspace::open(Services& s)
    {
        if (active()) return;
        if(!s.platform->acquireBlueprintLease())
        {
            m_status="Cannot open Blueprint Studio: save directory is busy or unavailable.";
            NX_ERROR("Blueprint Studio: %s",m_status.c_str());return;
        }
        m_font = s.assets->font("global/freesans.sfn", 18);
        if (!m_font || m_font->failed)
        {
            if (m_font) s.assets->release(m_font);
            m_font = nullptr;
            s.platform->releaseBlueprintLease();
            NX_ERROR("Blueprint Studio: cannot load the native workspace font.");
            return;
        }
        m_wantedPointer = s.input->wantsPointer();
        s.input->setWantsPointer(true);
        s.platform->lockMouse(false);
        s.input->consume();
        m_left = m_pan = m_drag = false;
        m_shortcuts = 0;
        m_previousTyping=s.input->typing();m_focus=0;m_browsing=false;m_assetBrowser=false;s.input->setTyping(false);
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
        filterCatalogue();
        NX_INFO("Blueprint Studio: workspace ready (%s)",m_status.c_str());
    }

    void BlueprintWorkspace::close(Services& s)
    {
        if (!active()) return;
        stopPreview(s);
        checkpoint(s,true);
        cancelDrag();clearWire();m_quickDrag=m_quickMenu=m_pinMenu=m_rightDown=false;m_layoutMenu=m_layoutDragging=false;m_graphBack.clear();m_focus=0;m_browsing=false;m_assetBrowser=false;s.input->setTyping(m_previousTyping);
        s.assets->release(m_font);
        m_font = nullptr;
        s.platform->releaseBlueprintLease();
        s.input->setWantsPointer(m_wantedPointer);
        s.input->consume();
    }

    void BlueprintWorkspace::selectGraph(size_t index)
    {
        if (index >= m_document.graphs.size()) return;
        cancelDrag();clearWire();m_diagnostic={};
        m_quickDrag=m_quickMenu=m_pinMenu=m_rightDown=false;m_quickVariableFirst=0;
        m_layoutMenu=m_layoutDragging=false;m_layoutComment=m_layoutLink=static_cast<size_t>(-1);m_selectedNodes.clear();
        m_graph = index; m_selection = static_cast<size_t>(-1);
        rebuildPins();fitGraph();filterCatalogue();
        if (index < m_first) m_first = index;
        if (index >= m_first + kRows) m_first = index - kRows + 1;
    }

    void BlueprintWorkspace::openSelectedFunction()
    {
        const auto& g=m_document.graphs[m_graph];
        if(m_selection>=g.nodes.size()||g.nodes[m_selection].kind!="call:function"){m_status="Select a function call first.";return;}
        const auto* target=m_document.function(g.owner,g.nodes[m_selection].ref);
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
        for(auto& row:m_rows)
            row.height=std::max(kNodeHeight,kPinStart+kPinStep*static_cast<float>(std::max(row.inputs.size(),row.outputs.size()))+8);
    }

    void BlueprintWorkspace::filterCatalogue()
    {
        m_palette.clear();m_contextResults.clear();m_paletteFirst=0;
        if(m_graph>=m_document.graphs.size())return;
        auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
        const auto query=lower(m_query);
        const auto contextQuery=lower(m_pinQuery);
        const auto& graph=m_document.graphs[m_graph];const bool behaviour=graph.owner.rfind("behaviour:",0)==0;
        const auto behaviours=m_document.behaviours();
        const bool hasBehaviours=std::any_of(behaviours.begin(),behaviours.end(),[&](const std::string& name){return m_document.canAttach(graph.owner,name);});
        const auto& globals=m_document.moduleGlobals(graph.owner);
        for(const auto& node:blueprintCatalogue())
            if((node.id.rfind("global:",0)!=0||std::any_of(globals.begin(),globals.end(),[&](const BlueprintDocument::Field& f){return f.type==node.id.substr(11);}))&&node.id!="entry"&&(node.id!="return:function"||(graph.kind=="function"&&!graph.inlineMacro))&&(node.id!="event:entity"||behaviour)&&
                (node.id!="event:other"||(behaviour&&(graph.name=="onHit"||graph.name=="onLeave")))&&
                (node.id!="event:levelName"||(!behaviour&&(graph.name=="onLevelTouch"||graph.name=="onLevelAgent")))&&
                (node.id!="event:levelWho"||(!behaviour&&graph.name=="onLevelTouch"))&&
                (node.id!="event:levelWhat"||(!behaviour&&graph.name=="onLevelAgent"))&&
                ((node.id!="attach:behaviour"&&node.id!="level:attach")||(!behaviour&&hasBehaviours))&&(node.id!="attach:direct"||hasBehaviours)&&(!behaviour||node.id.rfind("level:",0)!=0)&&(node.id!="event:delta"||m_document.graphs[m_graph].name=="update")&&
                (node.id!="event:canvas"||m_document.graphs[m_graph].name=="draw")&&
                (m_pinMenu||lower(node.category+" "+node.title+" "+node.id).find(query)!=std::string::npos))m_palette.push_back(&node);
        if(!m_pinMenu)return;
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
                if(exact||(m_pendingOutput?blueprintLosslessConversion(m_pinType,p.type):blueprintLosslessConversion(p.type,m_pinType)))
                    m_contextResults.push_back({spec.id,reference,label+" / "+p.name+(exact?"":" (convert)"),p.name,!exact});
            }
        };
        for(const auto* base:m_palette)
        {
            if(base->id=="call:function")
            {
                for(const auto& fn:m_document.graphs)if(fn.owner==graph.owner&&fn.kind=="function"&&m_document.canCallFunction(m_graph,fn.name))
                {
                    BlueprintNodeSpec resolved;
                    if(m_document.nodeSpec(graph,{"",base->id,fn.name},resolved))consider(resolved,fn.name,std::string(fn.inlineMacro?"Macro ":"Call ")+fn.name);
                }
            }
            else if(base->id=="return:function")
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
        for(const auto& l:graph.links)for(const auto& p:l.route){left=std::min(left,p.x-6);top=std::min(top,p.y-6);right=std::max(right,p.x+6);bottom=std::max(bottom,p.y+6);}
        if(!graph.comments.empty()){left-=18;top-=44;right+=18;bottom+=18;}
        m_zoom=std::clamp(std::min((kRight-kLeft-48)/(right-left),(kBottom-kTop-48)/(bottom-top)),0.25f,1.0f);
        m_x=24-left*m_zoom;m_y=24-top*m_zoom;
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
        if(m_document.dirty()){m_status="Save changes before importing source.";return false;}
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
        m_document=std::move(candidate);m_filePath.clear();m_fileBaseline.clear();
        m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;
        m_status="Imported "+file+(m_document.assetRoot.empty()?" as editable graphs.":" with companion assets.")+" Save as a new project.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        checkpoint(s,true);return true;
    }

    bool BlueprintWorkspace::importProjectFolder(Services& s,const std::string& folder)
    {
        if(m_document.dirty()){m_status="Save changes before importing a project.";return false;}
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
        verified.markRecovered();m_document=std::move(verified);m_filePath.clear();m_fileBaseline.clear();
        m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_browsing=false;
        m_status="Imported "+std::to_string(owners.size())+" screens from "+folder+". Save as a new project.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());
        checkpoint(s,true);return true;
    }

    bool BlueprintWorkspace::openFile(Services& s,const std::string& file,bool managed)
    {
        if(m_document.dirty()){m_status="Save changes before opening another Blueprint.";return false;}
        const bool archive=recoveryArchiveName(file);
        if(!blueprintFileName(file)&&!archive){m_status="Choose a Blueprint or recovery archive.";return false;}
        const std::string path="own/"+file;
        FileData data;
        if(!s.platform->readFile(path.c_str(),data,MemTag::General))
        {m_status="Could not read Blueprint.";return false;}
        if(!m_document.load(reinterpret_cast<const char*>(data.data),data.size,m_status))return false;
        m_filePath.clear();m_fileBaseline.clear();
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
    void BlueprintWorkspace::browseFiles(Services& s)
    {
        if(m_document.dirty()){m_status="Save changes before opening another Blueprint.";return;}
        m_files.clear();m_fileFirst=0;
        s.platform->list("own",false,[](const char* name,void* user)
        {
            auto& files=*static_cast<std::vector<std::string>*>(user);
            if(files.size()<4096&&(blueprintFileName(name)||recoveryArchiveName(name)||standaloneSourceName(name)))files.emplace_back(name);
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
        m_status="Select a project, recovery, source or project folder." ;
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
        if(m_filePath.empty()||m_document.assetRoot.empty())
        {m_status="Save as a named project before importing assets.";return;}
        if(!blueprintEnsureAssetFolder(*s.platform,m_document.assetRoot,screen,m_status)||
            !blueprintListAssets(*s.platform,m_document.assetRoot,screen,m_assetFiles,m_status))return;
        m_assetBrowser=true;m_assetFirst=0;clearWire();cancelDrag();
        m_status="Drop a native asset or scene JSON into this window.";
    }
    void BlueprintWorkspace::launchSceneEditor(Services& s)
    {
        const auto screen=assetScreen();
        if(screen.empty()||m_filePath.empty()||m_document.assetRoot.empty())
        {m_status="Save a named project and select its screen first.";return;}
        checkpoint(s,true);
        if(!blueprintPrepareSceneEditor(*s.platform,m_document.assetRoot,screen,m_status))return;
        if(!s.screens->startSceneEditor(m_document.assetRoot,screen,m_status))
        {
            s.input->setWantsPointer(true);s.input->setTyping(false);s.platform->lockMouse(false);
            return;
        }
        m_sceneEditor=true;m_sceneExit=false;m_playing=true;
        m_left=s.input->raw().mouseButtons[0];
        m_status="Scene editor: Save your scene, then Escape to return.";
    }
    void BlueprintWorkspace::importAsset(Services& s,const std::string& file)
    {
        const auto screen=assetScreen();
        if(screen.empty()||m_filePath.empty()||m_document.assetRoot.empty())
        {m_status="Save as a named project and select a screen before importing assets.";return;}
        if(!blueprintImportAsset(*s.platform,m_document.assetRoot,screen,file,m_status))return;
        if(!blueprintListAssets(*s.platform,m_document.assetRoot,screen,m_assetFiles,m_status))return;
        m_status="Imported asset:"+blueprintAssetImportPath(file);
    }
    bool BlueprintWorkspace::saveNamed(Services& s,const std::string& name)
    {
        std::string file;
        if(!blueprintManagedName(name,file))
        {m_status="Use 1-64 letters, numbers, spaces, underscores or hyphens.";return false;}
        const std::string path="own/"+file;
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
            m_status="File changed outside Studio. Use Save as to preserve both versions.";
            NX_WARN("Blueprint Studio: %s",m_status.c_str());return false;
        }
        if(!m_document.dirty()){m_status="Already saved.";return true;}
        std::string bytes;
        if(!m_document.serialize(bytes,m_status)||!writeVerified(s,m_filePath,bytes))return false;
        m_fileBaseline=std::move(bytes);m_document.markSaved();checkpoint(s,true);
        m_status="Saved "+m_document.name;return true;
    }

    void BlueprintWorkspace::focusDiagnostic(BlueprintLocation location)
    {
        m_diagnostic={};
        if(location.graph>=m_document.graphs.size()||location.node>=m_document.graphs[location.graph].nodes.size())return;
        selectGraph(location.graph);m_selection=location.node;m_diagnostic=location;
        const auto& n=m_document.graphs[m_graph].nodes[m_selection];
        m_zoom=1;m_x=(kRight-kLeft-kNodeWidth)*0.5f-n.x;
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
        m_status="Exported own/"+root+" | Install into a separate project folder.";
        NX_INFO("Blueprint Studio: %s",m_status.c_str());return true;
    }

    bool BlueprintWorkspace::startTrace(Services& s)
    {
        stopTrace();m_traceHits.clear();m_traceValues.clear();m_traceLocation={};m_traceBudget=128;m_traceSelected=m_traceFirst=m_traceDetailFirst=0;m_traceShowStack=false;
        if(!m_traceEnabled)return true;
        if(!s.debugger||!s.debugger->startLocalTrace(*s.host,[](void* user,const char* section,int line,u32 run,asIScriptContext* context)
        {static_cast<BlueprintWorkspace*>(user)->traceLine(section,line,run,context);},this))
        {m_status="Local trace unavailable while the external script debugger is active.";return false;}
        m_traceDebugger=s.debugger;return true;
    }

    void BlueprintWorkspace::stopTrace()
    {
        if(m_traceDebugger)m_traceDebugger->stopLocalTrace();
        m_traceDebugger=nullptr;m_tracePaused=false;m_tracePending=false;m_traceStep=false;m_traceKey=false;
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
        TraceHit hit{location,line,run};
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

    void BlueprintWorkspace::stopPreview(Services& s)
    {
        if(!m_playing)return;
        const bool scene=m_sceneEditor;
        stopTrace();
        s.screens->stopPreview();
        m_playing=false;m_sceneEditor=false;m_sceneExit=false;
        s.input->setWantsPointer(true);
        s.input->setTyping(false);
        s.platform->lockMouse(false);
        s.input->consume();
        m_left=s.input->raw().mouseButtons[0];
        if(scene)
        {
            browseAssets(s);
            if(m_assetBrowser)m_status="Returned from Scene editor. Saved scene files are ready.";
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
        if(m_sceneEditor)
        {
            auto& input=*s.input;const auto& raw=input.raw();
            const bool left=raw.mouseButtons[0],click=left&&!m_left;m_left=left;
            if(m_sceneExit)
            {
                const auto& canvas=*s.canvas;
                const float scale=std::min(canvas.pixelWidth()/kWidth,canvas.pixelHeight()/kHeight);
                const float x=scale>0?(raw.mouseX-(canvas.pixelWidth()-kWidth*scale)*0.5f)/scale:0;
                const float y=scale>0?(raw.mouseY-(canvas.pixelHeight()-kHeight*scale)*0.5f)/scale:0;
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
        else if(m_traceDebugger)
        {
            const auto& raw=s.input->raw();
            const bool key=raw.keys[static_cast<size_t>(Key::Control)]&&raw.keys[static_cast<size_t>(Key::T)];
            const bool toggle=key&&!m_traceKey;m_traceKey=key;
            if(m_tracePaused)
            {
                const bool left=raw.mouseButtons[0],click=left&&!m_left;m_left=left;
                const auto& canvas=*s.canvas;const float scale=std::min(canvas.pixelWidth()/kWidth,canvas.pixelHeight()/kHeight);
                const float x=scale>0?(raw.mouseX-(canvas.pixelWidth()-kWidth*scale)*0.5f)/scale:0;
                const float y=scale>0?(raw.mouseY-(canvas.pixelHeight()-kHeight*scale)*0.5f)/scale:0;
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
        s.screens->update(dt);
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

    void BlueprintWorkspace::drawPreviewOverlay(Canvas& c)
    {
        if(tracePaused()&&m_font)
        {
            c.design(kWidth,kHeight);c.noClip();
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
        if(!m_sceneEditor||!m_sceneExit||!m_font)return;
        c.design(kWidth,kHeight);c.noClip();
        c.rect(0,0,kWidth,kHeight,{0,0,0,0.72f});
        c.roundRect(300,250,680,220,9,{0.12f,0.16f,0.22f,1});
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
        m_nextCheckpoint=now+2.0;
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
        std::vector<BlueprintPin> inferred;std::string error;
        if(m_document.variables(inferred,error,owner))for(const auto& v:inferred)
            if(blueprintLiteralType(v.type)&&std::none_of(rows.begin(),rows.end(),[&](const VariableRow& r){return r.field.name==v.name;}))
                rows.push_back({{v.name,v.type,"",false},false,false});
        for(const auto& f:m_document.moduleGlobals(owner))rows.push_back({f,true,true});
        std::sort(rows.begin(),rows.end(),[](const VariableRow& a,const VariableRow& b){return a.global!=b.global?a.global<b.global:a.field.name<b.field.name;});
        return rows;
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
        if(m_drag&&node==m_selection)return {m_dragX,m_dragY};
        if(m_layoutDragging&&m_layoutMoved&&m_layoutLink==static_cast<size_t>(-1)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),node))
            return {n.x+(m_layoutX-m_layoutStartX)/m_zoom,n.y+(m_layoutY-m_layoutStartY)/m_zoom};
        return {n.x,n.y};
    }
    BlueprintDocument::Point BlueprintWorkspace::routeLayoutPosition(size_t wire,size_t point) const
    {
        const auto& l=m_document.graphs[m_graph].links[wire];const auto& p=l.route[point];
        if(m_layoutDragging&&m_layoutMoved)
        {
            if(m_layoutLink==wire&&m_layoutPoint==point)return {(m_layoutX-kLeft-m_x)/m_zoom,(m_layoutY-kTop-m_y)/m_zoom};
            if(m_layoutLink==static_cast<size_t>(-1)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),l.from)&&std::binary_search(m_layoutMembers.begin(),m_layoutMembers.end(),l.to))
                return {p.x+(m_layoutX-m_layoutStartX)/m_zoom,p.y+(m_layoutY-m_layoutStartY)/m_zoom};
        }
        return p;
    }
    bool BlueprintWorkspace::commentBounds(size_t comment,float& x,float& y,float& w,float& h) const
    {
        const auto members=commentMembers(comment);if(members.empty())return false;
        float left=1000000.f,top=1000000.f,right=-1000000.f,bottom=-1000000.f;
        for(const auto i:members)
        {
            const auto n=nodeLayoutPosition(i);left=std::min(left,n.x);top=std::min(top,n.y);
            right=std::max(right,n.x+kNodeWidth);bottom=std::max(bottom,n.y+m_rows[i].height);
        }
        x=kLeft+m_x+(left-18)*m_zoom;y=kTop+m_y+(top-44)*m_zoom;w=(right-left+36)*m_zoom;h=(bottom-top+62)*m_zoom;return true;
    }
    std::vector<BlueprintDocument::Point> BlueprintWorkspace::wireRoute(size_t link) const
    {
        const auto& g=m_document.graphs[m_graph];const auto& l=g.links[link];const auto& pins=m_wirePins[link];
        const auto from=nodeLayoutPosition(l.from),to=nodeLayoutPosition(l.to);
        const float x1=kLeft+m_x+(from.x+kNodeWidth)*m_zoom,y1=kTop+m_y+(from.y+pinY(pins.output))*m_zoom;
        const float x2=kLeft+m_x+to.x*m_zoom,y2=kTop+m_y+(to.y+pinY(pins.input))*m_zoom;
        std::vector<BlueprintDocument::Point> points{{x1,y1}};
        if(l.route.empty()){const float mid=(x1+x2)*0.5f;points.push_back({mid,y1});points.push_back({mid,y2});}
        else for(size_t i=0;i<l.route.size();++i){const auto p=routeLayoutPosition(link,i);points.push_back({kLeft+m_x+p.x*m_zoom,kTop+m_y+p.y*m_zoom});}
        points.push_back({x2,y2});return points;
    }
    void BlueprintWorkspace::update(Services& s)
    {
        auto& input = *s.input;
        if (input.pressed(Action::Menu))
        {
            if(m_assetBrowser){m_assetBrowser=false;m_status="Asset browser closed.";return;}
            if(m_browsing){m_browsing=false;m_status="Open cancelled.";return;}
            if(m_focus){m_focus=0;input.setTyping(false);m_status="Input cancelled.";return;}
            if(m_layoutDragging||m_layoutMenu){m_layoutDragging=m_layoutMenu=false;m_status="Layout action cancelled.";return;}
            if(m_quickDrag||m_quickMenu){m_quickDrag=m_quickMenu=false;m_status="Variable placement cancelled.";return;}
            if(m_pinMenu){closePinMenu(s);m_status="Node search cancelled.";return;}
            if(m_pending!=static_cast<size_t>(-1)){clearWire();m_status="Connection cancelled.";return;}
            if(m_drag){cancelDrag();m_left=input.raw().mouseButtons[0];m_status="Drag cancelled.";return;}
            returnToRedux(s);return;
        }
        std::string dropped;
        if (!m_drag && !m_layoutDragging && !m_layoutMenu && !m_pinMenu && !m_pinDrag && !m_quickDrag && !m_quickMenu && !m_focus && !m_browsing && s.platform->takeDropped(dropped))
        {
            if(m_assetBrowser)importAsset(s,dropped);
            else if(blueprintFileName(dropped))openFile(s,dropped,false);
            else if(dropped.ends_with(".as"))importSource(s,dropped);
            else m_status="Open Assets before dropping a scene or media file.";
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
        const bool right=raw.mouseButtons[1];
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
                    if(m_layoutMoved)m_document.moveNodes(m_graph,m_layoutMembers,(x-m_layoutStartX)/m_zoom,(y-m_layoutStartY)/m_zoom);
                    else {m_edit=m_document.graphs[m_graph].comments[m_layoutComment].title;m_focus=11;input.setTyping(true);m_status="Edit comment title.";}
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
                {if(m_document.arrange(m_graph,m_status)){fitGraph();m_status="Arranged graph; manual routes reset. Undo restores layout.";}}
                else if(in(x,y,300,65,550,95))
                {if(m_document.alignNodes(m_graph,nodes,false,m_status))m_status="Aligned selected nodes left.";}
                else if(in(x,y,300,95,550,125))
                {if(m_document.alignNodes(m_graph,nodes,true,m_status))m_status="Aligned selected nodes top.";}
                else if(in(x,y,300,125,550,155))
                {
                    if(m_document.addComment(m_graph,nodes,"Comment",m_status))
                    {m_layoutComment=m_document.graphs[m_graph].comments.size()-1;m_edit="Comment";m_focus=11;input.setTyping(true);}
                }
                else if(in(x,y,300,155,550,185))
                {m_extractNodes=nodes;m_edit.clear();m_focus=12;input.setTyping(true);m_status="Name the extracted function.";}
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
                        const float px=nx+(m_pendingOutput?0:kNodeWidth*m_zoom);
                        for(size_t p=0;p<pins.size();++p)if(std::abs(x-px)<=9&&std::abs(y-ny-pinY(p)*m_zoom)<=9)
                        {
                            hit=true;
                            const bool ok=m_pendingOutput?m_document.connectCompatible(m_graph,m_pending,m_pendingPin,i-1,pins[p],m_status):m_document.connectCompatible(m_graph,i-1,pins[p],m_pending,m_pendingPin,m_status);
                            if(ok){clearWire();rebuildPins();m_status="Connected pins.";}break;
                        }
                        if(in(x,y,nx,ny,nx+kNodeWidth*m_zoom,ny+row.height*m_zoom))hit=true;
                    }
                    if(!hit){openPinMenu(x,y);if(m_pinMenu)input.setTyping(true);}
                }
                else if(!moved&&!m_pendingOutput&&m_pending<m_rows.size())
                {
                    const auto& row=m_rows[m_pending];const auto pin=std::find(row.inputs.begin(),row.inputs.end(),m_pendingPin);
                    if(pin!=row.inputs.end()&&blueprintLiteralType(row.inputTypes[static_cast<size_t>(pin-row.inputs.begin())]))
                    {
                        m_editPin=m_pendingPin;m_editType=row.inputTypes[static_cast<size_t>(pin-row.inputs.begin())];m_edit.clear();
                        for(const auto& v:m_document.graphs[m_graph].nodes[m_pending].values)if(v.pin==m_editPin)m_edit=v.value;
                        clearWire();m_focus=6;input.setTyping(true);m_status="Enter a typed value. Drag the pin to connect a node.";
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
            if(input.wheel()<0&&m_assetFirst+10<m_assetFiles.size())++m_assetFirst;
            if(input.wheel()>0&&m_assetFirst)--m_assetFirst;
            if(click&&in(x,y,860,116,1000,150))m_assetBrowser=false;
            if(click&&in(x,y,704,116,850,150))browseAssets(s);
            if(click&&in(x,y,534,116,694,150))launchSceneEditor(s);
            m_left=left;return;
        }
        if(m_browsing)
        {
            if(input.wheel()<0&&m_fileFirst+12<m_files.size())++m_fileFirst;
            if(input.wheel()>0&&m_fileFirst)--m_fileFirst;
            if(click&&in(x,y,860,116,1000,150))m_browsing=false;
            else if(click&&in(x,y,280,166,1000,166+12*36))
            {
                const size_t row=m_fileFirst+static_cast<size_t>((y-166)/36);
                if(row<m_files.size())
                {
                    const auto file=m_files[row];
                    if(projectFolderName(file))importProjectFolder(s,file);
                    else if(standaloneSourceName(file))importSource(s,file);
                    else openFile(s,file,true);
                }
            }
            m_left=left;return;
        }
        if(m_focus==10)
        {
            const auto rows=variableRows();const auto owner=m_document.graphs[m_graph].owner;
            const bool canGlobal=!m_document.moduleForOwner(owner).empty();
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
            if(click)
            {
                if(m_variableTypes&&in(x,y,650,302,1100,302+6*28))
                {m_variableType=variableTypes[static_cast<size_t>((y-302)/28)];m_variableTypes=false;}
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
                else if(in(x,y,650,266,1100,302)){m_variableTypes=true;m_variableInput=0;}
                else if(in(x,y,650,322,1100,354)){m_variableInitialized=!m_variableInitialized;m_variableInput=m_variableInitialized?2:0;}
                else if(in(x,y,650,370,1100,406)&&m_variableInitialized)m_variableInput=2;
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
        if(m_focus)
        {
            const int focus=m_focus;
            auto& value=focus==1?m_query:m_edit;
            bool changed=false;
            for(u32 i=0;i<input.typedCount();++i)
            {
                const u32 ch=input.typed(i);
                if(ch==8&&!value.empty())
                {
                    size_t end=value.size()-1;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
                    value.resize(end);changed=true;
                }
                else if(ch>=32&&(focus==1||focus==2||focus==6||focus==10||focus==11||ch<127)&&appendCharacter(value,ch,focus==1?80u:(focus==2||focus==6||focus==10)?4096u:focus==9?1024u:focus==11?256u:64u))changed=true;
                else if(ch==13)
                {
                    if(focus==2&&!m_document.setLiteral(m_graph,m_selection,m_edit,m_status))break;
                    if(focus==3)
                    {
                        if(!m_document.setReference(m_graph,m_selection,m_edit,m_status))break;
                        clearWire();rebuildPins();
                    }
                    if(focus==12)
                    {
                        const auto& g=m_document.graphs[m_graph];const auto caller=std::make_pair(g.owner,g.name);
                        if(!m_document.collapseToFunction(m_graph,m_extractNodes,m_edit,m_status))break;
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
                    if(focus==11&&!m_document.renameComment(m_graph,m_layoutComment,m_edit,m_status))break;
                    if(focus==9)
                    {
                        if(!m_document.editFunctionSignature(m_graph,m_edit,m_status))break;
                        clearWire();rebuildPins();
                    }
                    if(focus==5&&!saveNamed(s,m_edit))break;
                    if(focus==6&&!m_document.setInputValue(m_graph,m_selection,m_editPin,m_edit,m_status))break;
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
                m_document.clearInputValue(m_graph,m_selection,m_editPin);m_focus=0;input.setTyping(false);
                m_status="Input reset to its API default, or requires a wire.";
            }
            if(click&&m_focus==1&&!in(x,y,kRight+8,kTop+38,kWidth-8,kTop+68)){m_focus=0;input.setTyping(false);}
            else {m_left=left;return;}
        }
        if(!m_drag)
        {
            if(click&&in(x,y,300,5,404,33)){m_layoutMenu=true;clearWire();m_left=left;return;}
            if(click&&in(x,y,1134,39,1266,69)){returnToRedux(s);m_left=left;return;}
            if(click&&in(x,y,410,5,560,33)&&m_graph<m_document.graphs.size()&&m_document.graphs[m_graph].kind=="function")
            {m_edit=m_document.functionSignature(m_graph);m_focus=9;input.setTyping(true);m_status="Edit the typed signature.";m_left=left;return;}
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
            if(click&&in(x,y,740,5,850,33)){browseFiles(s);m_left=left;return;}
            if(click&&in(x,y,856,5,966,33)){m_edit.clear();m_focus=5;input.setTyping(true);m_status="Type a new Blueprint name and press Enter.";m_left=left;return;}
            if(click&&in(x,y,972,5,1082,33)){browseAssets(s);m_left=left;return;}
            if(click&&in(x,y,1088,5,1174,33))
            {
                if(m_document.toggleWatch(m_graph,m_selection)){m_traceEnabled=true;m_status="Toggled pause-after-node watch. Trace enabled.";}
                else m_status="Select an execution node to watch.";
                m_left=left;return;
            }
            if(click&&in(x,y,1180,5,1268,33))
            {m_traceEnabled=!m_traceEnabled;m_status=m_traceEnabled?"Trace enabled. Ctrl+T pauses during play test.":"Trace disabled.";m_left=left;return;}
            if(m_document.graphs.empty()){m_left=left;return;}
            if(click&&in(x,y,504,39,604,69))
            {
                BlueprintLocation location;m_diagnostic={};m_previewOutputs.clear();
                checkpoint(s,true);
                if(!compileBlueprintProject(m_document,m_previewOutputs,m_status,&location))focusDiagnostic(location);
                else if(validateProject(s,m_previewOutputs))
                {
                    std::vector<Screens::PreviewScreen> screens;
                    for(const auto& output:m_previewOutputs)
                    {
                        Screens::PreviewScreen item;
                        snprintf(item.info.id,sizeof(item.info.id),"%s",output.id.c_str());
                        snprintf(item.info.title,sizeof(item.info.title),"%s",output.id.c_str());
                        item.source=output.source;item.manifest=output.manifest;screens.push_back(std::move(item));
                    }
                    const auto selected=m_document.graphs[m_graph].owner;
                    const auto scope=m_document.moduleForOwner(selected);
                    const auto owner=scope.empty()?m_document.screens().front():scope;
                    if(!m_document.assetRoot.empty()&&!s.platform->fileTime(("own/"+m_document.assetRoot).c_str()))
                        m_status="Project asset folder is missing. Restore it before play testing.";
                    else if(startTrace(s))
                    {
                        if(s.screens->startProjectPreview(screens,owner,m_status,m_document.assetRoot))
                        {
                            m_playing=true;clearWire();m_status="Playing "+owner+(m_traceDebugger?". Ctrl+T pauses trace; Escape stops.":". Escape returns to Studio.");
                            NX_INFO("Blueprint Studio: preview started: %s",owner.c_str());
                        }
                        else
                        {
                            stopTrace();focusPreviewError(m_status);
                            s.input->setWantsPointer(true);s.input->setTyping(false);
                        }
                    }
                    else
                    {
                        focusPreviewError(m_status);
                        s.input->setWantsPointer(true);s.input->setTyping(false);
                    }
                }
                m_left=left;return;
            }
            if(click&&in(x,y,610,39,742,69))compileAndExport(s);
            if((pressed&1u)||(click&&in(x,y,748,39,858,69))){saveCurrent(s);m_left=left;return;}
            if(((pressed&2u)||(click&&in(x,y,864,39,948,69)))&&m_document.undo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Undid graph change.";}
            if(((pressed&4u)||(click&&in(x,y,954,39,1038,69)))&&m_document.redo()){selectGraph(std::min(m_graph,m_document.graphs.size()-1));m_status="Redid graph change.";}
            if(click&&in(x,y,1044,39,1128,69))fitGraph();
        }
        if(click&&in(x,y,8,kBottom-225,kLeft-8,kBottom-193))
        {
            m_variableFirst=0;resetVariableEditor();m_focus=10;input.setTyping(true);m_status="Select a variable or create a new one.";m_left=left;return;
        }
        if(click&&in(x,y,8,kBottom-188,kLeft-8,kBottom-156))
        {
            m_edit.clear();m_focus=7;input.setTyping(true);m_status="Type a lowercase screen ID, then Enter.";m_left=left;return;
        }
        if(click&&in(x,y,8,kBottom-151,kLeft-8,kBottom-119))
        {
            const std::string owner=m_document.graphs[m_graph].owner;
            const bool removed=owner.rfind("behaviour:",0)==0?m_document.removeBehaviour(owner.substr(10),m_status):m_document.removeScreen(owner,m_status);
            if(removed){m_first=0;selectGraph(0);m_status="Removed owner. Undo restores it.";}
            m_left=left;return;
        }
        if(click&&in(x,y,8,kBottom-114,kLeft-8,kBottom-82))
        {
            m_edit.clear();m_focus=4;input.setTyping(true);m_status="Type a new function name, then Enter.";m_left=left;return;
        }
        if(click&&in(x,y,8,kBottom-77,kLeft-8,kBottom-45))
        {
            if(m_document.removeFunction(m_graph,m_status)){m_first=0;selectGraph(0);m_status="Removed function.";}
            m_left=left;return;
        }
        if(click&&in(x,y,8,kBottom-40,kLeft-8,kBottom-8))
        {
            if(m_document.dirty())m_status="Save changes before creating a new Blueprint.";
            else{m_document.create();m_filePath.clear();m_fileBaseline.clear();m_checkpointRevision=static_cast<size_t>(-1);m_graphBack.clear();m_first=0;selectGraph(0);m_status="New Blueprint.";}
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
                if(row<m_palette.size())
                {
                    float px=((kRight-kLeft)*0.5f-m_x)/m_zoom,py=((kBottom-kTop)*0.4f-m_y)/m_zoom;
                    const auto& nodes=m_document.graphs[m_graph].nodes;
                    const auto* spec=m_palette[row];
                    const float height=std::max(kNodeHeight,kPinStart+kPinStep*static_cast<float>(std::max(spec->inputs.size(),spec->outputs.size()))+8);
                    for(size_t attempt=0;attempt<=nodes.size();++attempt)
                    {
                        bool overlap=false;
                        float next=py;
                        for(size_t i=0;i<nodes.size();++i)
                        {
                            const auto& n=nodes[i];
                            if(px<n.x+kNodeWidth+12&&px+kNodeWidth+12>n.x&&py<n.y+m_rows[i].height+12&&py+height+12>n.y)
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
                if(in(x,y,kRight+8,kTop+456,kWidth-8,kTop+486)&&(n.kind.rfind("literal:",0)==0||n.kind.rfind("var:",0)==0||n.kind.rfind("global:",0)==0||n.kind.rfind("capture:",0)==0||n.kind=="call:function"||n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach"))
                {m_edit=n.ref;m_focus=n.kind.rfind("literal:",0)==0?2:3;input.setTyping(true);}
                else if(in(x,y,kRight+8,kBottom-40,kWidth-8,kBottom-8)&&m_document.deleteNode(m_graph,m_selection,m_status)){m_diagnostic={};clearWire();m_selection=static_cast<size_t>(-1);rebuildPins();m_selectedNodes.clear();m_status="Deleted node.";}
            }
            m_left=left;return;
        }
        if(in(x,y,0,kVariableTop,kLeft,kVariableBottom))
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
        if (left && !m_left && in(x,y,0,kTop,kLeft,kTop+kRows*31))
        {
            const size_t row = static_cast<size_t>((y-kTop)/31);
            if (row < kRows) selectGraph(m_first + row);
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
                float cx,cy,cw,ch;if(!commentBounds(i-1,cx,cy,cw,ch)||!in(x,y,cx,cy,cx+cw,cy+26*m_zoom))continue;
                if(control){m_document.deleteComment(m_graph,i-1);m_layoutComment=static_cast<size_t>(-1);}
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
                    const float px=nx+(side?kNodeWidth*m_zoom:0);
                    for(size_t p=0;p<pins.size();++p)
                    {
                        if(std::abs(x-px)>9||std::abs(y-(ny+pinY(p)*m_zoom))>9)continue;
                        m_selection=i-1;m_selectedNodes.clear();
                        if(control){if(m_document.disconnect(m_graph,i-1,pins[p],side!=0)){m_diagnostic={};rebuildPins();m_status="Disconnected pin.";}clearWire();}
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
            if(m_pending!=static_cast<size_t>(-1)&&std::none_of(nodes.begin(),nodes.end(),[&](const BlueprintDocument::Node& n){const auto i=static_cast<size_t>(&n-nodes.data());const float nx=kLeft+m_x+n.x*m_zoom,ny=kTop+m_y+n.y*m_zoom;return in(x,y,nx,ny,nx+kNodeWidth*m_zoom,ny+rows[i].height*m_zoom);}))
            {openPinMenu(x,y);if(m_pinMenu)input.setTyping(true);m_left=left;return;}
            clearWire();m_selection = static_cast<size_t>(-1);
            if(!held(Key::Shift))m_selectedNodes.clear();
            for (size_t i = nodes.size(); i > 0; --i)
            {
                const auto& n = nodes[i-1];
                const float nx = kLeft+m_x+n.x*m_zoom, ny = kTop+m_y+n.y*m_zoom;
                if (in(x,y,nx,ny,nx+kNodeWidth*m_zoom,ny+rows[i-1].height*m_zoom))
                {
                    m_selection=i-1;
                    if(held(Key::Shift))
                    {
                        const auto found=std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i-1);
                        if(found==m_selectedNodes.end())m_selectedNodes.push_back(i-1);else m_selectedNodes.erase(found);
                        m_selection=m_selectedNodes.empty()?static_cast<size_t>(-1):m_selectedNodes.back();
                        m_left=left;return;
                    }
                    m_selectedNodes.push_back(i-1);
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
                if(m_dragMoved&&m_document.move(m_graph,m_selection,m_dragX,m_dragY))m_status="Layout changed. Ctrl+S saves your Blueprint.";
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
                        const auto points=wireRoute(i);
                        for(size_t j=1;j<points.size();++j)
                        {
                            const auto& a=points[j-1];const auto& b=points[j];const float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
                            if(len==0)continue;
                            const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/len,0.f,1.f);
                            const float distance=std::hypot(x-a.x-t*dx,y-a.y-t*dy);
                            if(distance<best){best=distance;bestLink=i;bestPoint=links[i].route.empty()?0:j-1;}
                        }
                    }
                    if(bestLink!=static_cast<size_t>(-1)&&m_document.insertRoutePoint(m_graph,bestLink,bestPoint,(x-kLeft-m_x)/m_zoom,(y-kTop-m_y)/m_zoom))m_status="Reroute added. Drag it, or Ctrl+click to remove.";
                    else m_status="Ctrl+right-click a wire to add a reroute.";
                }
                else {openPinMenu(x,y);input.setTyping(true);}
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
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, std::clamp(wheel,-8.0f,8.0f)),0.25f,2.0f);
            m_x = x-kLeft-(x-kLeft-m_x)*m_zoom/previous;
            m_y = y-kTop-(y-kTop-m_y)*m_zoom/previous;
        }
        else if (in(x,y,0,kTop,kLeft,kTop+kRows*31) && wheel != 0)
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
        c.roundRect(300,5,104,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Layout",352,9,text,Align::Center);
        c.roundRect(1088,5,86,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Watch",1131,9,text,Align::Center);
        c.roundRect(1180,5,88,28,4,m_traceEnabled?Color{0.23f,0.48f,0.29f,1}:Color{0.19f,0.33f,0.50f,1});
        c.text(*m_font,m_traceEnabled?"Trace on":"Trace off",1224,9,text,Align::Center);
        if(!m_document.graphs.empty()&&m_document.graphs[m_graph].kind=="function")
        {c.roundRect(410,5,150,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Signature",485,9,text,Align::Center);}
        else if(m_graph<m_document.graphs.size()&&m_document.graphs[m_graph].owner.rfind("behaviour:",0)!=0)
        {c.roundRect(410,5,150,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Level events",485,9,text,Align::Center);}
        c.roundRect(566,5,168,28,4,{0.19f,0.33f,0.50f,1});
        c.clip(566,5,168,28);c.text(*m_font,m_focus==8?(m_edit.empty()?"Type name...":m_edit.c_str()):"New behaviour",650,9,text,Align::Center);c.noClip();
        c.roundRect(740,5,110,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Open",795,9,text,Align::Center);
        c.roundRect(856,5,110,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Save as",911,9,text,Align::Center);
        c.roundRect(972,5,110,28,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Assets",1027,9,text,Align::Center);
        const std::string titleText=(m_document.name.empty()?"No document open":m_document.name)+(m_document.dirty()?" *":"");
        c.clip(12,38,480,34);c.text(*m_font,shorten(titleText,65).c_str(),18,43,accent);c.noClip();
        const auto button=[&](float x,float width,const char* label,bool enabled){
            c.roundRect(x,39,width,30,5,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.14f,0.18f,0.24f,1});
            c.text(*m_font,label,x+width*0.5f,44,enabled?text:muted,Align::Center);
        };
        button(504,100,"Play test",!m_document.graphs.empty());
        button(610,132,"Export project",!m_document.graphs.empty());button(748,110,"Save",!m_document.graphs.empty());button(864,84,"Undo",m_document.canUndo());
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
            c.text(*m_font,"Drop a Blueprint JSON or supported .as source file",630,330,text,Align::Center);
            c.text(*m_font,"The original file is never modified.",630,360,muted,Align::Center);
        }
        else
        {
            const auto& graph=m_document.graphs[m_graph];
            const auto& rows=m_rows;
            const auto nodeX=[&](size_t i){return nodeLayoutPosition(i).x;};
            const auto nodeY=[&](size_t i){return nodeLayoutPosition(i).y;};
            for(size_t i=0;i<graph.comments.size();++i)
            {
                float cx,cy,cw,ch;if(!commentBounds(i,cx,cy,cw,ch))continue;
                if(cx+cw<kLeft||cx>kRight||cy+ch<kTop||cy>kBottom)continue;
                c.rect(cx,cy,cw,ch,{0.17f,0.24f,0.29f,0.65f});c.roundLine(cx,cy,cw,ch,5,1,{0.38f,0.49f,0.55f,1});
                c.rect(cx,cy,cw,26*m_zoom,{0.25f,0.37f,0.42f,1});c.push();c.clip(cx+4,cy,cw-8,26*m_zoom);c.text(*m_font,graph.comments[i].title.c_str(),cx+8,cy+4,text);c.pop();
            }
            for(size_t wire=0;wire<graph.links.size();++wire)
            {
                const auto& l=graph.links[wire];const auto& pins=m_wirePins[wire];const auto points=wireRoute(wire);
                Color color=pinColor(rows[l.from].outputTypes[pins.output]);
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
                    c.line(a.x,a.y,b.x,b.y,2,color);
                }
                for(size_t j=0;j<l.route.size();++j){const auto p=routeLayoutPosition(wire,j);c.circle(kLeft+m_x+p.x*m_zoom,kTop+m_y+p.y*m_zoom,6,color);}
            }
            if(m_pending<graph.nodes.size())
            {
                const auto& n=graph.nodes[m_pending];const auto& pins=m_pendingOutput?rows[m_pending].outputs:rows[m_pending].inputs;
                const auto pin=std::find(pins.begin(),pins.end(),m_pendingPin);
                if(pin!=pins.end())
                {
                    const float px=kLeft+m_x+(n.x+(m_pendingOutput?kNodeWidth:0))*m_zoom,py=kTop+m_y+(n.y+pinY(static_cast<size_t>(pin-pins.begin())))*m_zoom;
                    c.ring(px,py,9,2,accent);
                    if(m_pinDrag||m_pinMenu)c.line(px,py,m_pinX,m_pinY,2,pinColor((m_pendingOutput?rows[m_pending].outputTypes:rows[m_pending].inputTypes)[static_cast<size_t>(pin-pins.begin())]));
                }
            }
            for(size_t i=0;i<graph.nodes.size();++i)
            {
                const auto& n=graph.nodes[i];
                const float x=kLeft+m_x+nodeX(i)*m_zoom,y=kTop+m_y+nodeY(i)*m_zoom,w=kNodeWidth*m_zoom,h=rows[i].height*m_zoom;
                if(x+w<kLeft||x>kRight||y+h<kTop||y>kBottom) continue;
                c.roundRect(x,y,w,h,5,{0.15f,0.20f,0.27f,1});
                c.rect(x,y,w,24*m_zoom,n.kind=="entry"?Color{0.54f,0.20f,0.25f,1}:Color{0.19f,0.35f,0.53f,1});
                if(n.watch)c.circle(x+w-12*m_zoom,y+12*m_zoom,5*m_zoom,{1,0.72f,0.22f,1});
                if(tracePaused()&&m_traceLocation.graph==m_graph&&m_traceLocation.node==i)c.roundLine(x,y,w,h,5,3,{1,0.72f,0.22f,1});
                else if(m_diagnostic.graph==m_graph&&m_diagnostic.node==i)c.roundLine(x,y,w,h,5,3,{0.98f,0.35f,0.38f,1});
                else if(i==m_selection||std::find(m_selectedNodes.begin(),m_selectedNodes.end(),i)!=m_selectedNodes.end())c.roundLine(x,y,w,h,5,2,accent);
                // Draw only the visible rows of tall nodes.
                const size_t first=static_cast<size_t>(std::max(0.0f,std::floor(((kTop-y)/m_zoom-kPinStart)/kPinStep)-1));
                const size_t last=static_cast<size_t>(std::max(0.0f,std::ceil(((kBottom-y)/m_zoom-kPinStart)/kPinStep)+2));
                for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)
                    c.circle(x,y+pinY(p)*m_zoom,4*m_zoom,pinColor(rows[i].inputTypes[p]));
                for(size_t p=first;p<std::min(last,rows[i].outputs.size());++p)
                    c.circle(x+w,y+pinY(p)*m_zoom,4*m_zoom,pinColor(rows[i].outputTypes[p]));
                if(m_zoom>=0.65f)
                {
                    c.push();c.clip(x+2,y+1,w-4,24*m_zoom);
                    c.text(*m_font,shorten(n.kind=="call:function"&&m_document.function(graph.owner,n.ref)&&m_document.function(graph.owner,n.ref)->inlineMacro?"Macro "+n.ref:title(n),24).c_str(),x+8,y+3,text);c.pop();
                    // Clip each column separately to prevent long pin names overlapping.
                    c.push();c.clip(x+6,y+24*m_zoom,w*0.5f-8,h-24*m_zoom);
                    for(size_t p=first;p<std::min(last,rows[i].inputs.size());++p)
                    {
                        const auto& pin=rows[i].inputs[p];
                        std::string label=pin;
                        const auto custom=std::find_if(n.values.begin(),n.values.end(),[&](const BlueprintDocument::InputValue& v){return v.pin==pin;});
                        if(custom!=n.values.end())label+=" = "+custom->value;
                        c.text(*m_font,shorten(label,15).c_str(),x+8,y+pinY(p)*m_zoom-8,muted);
                    }
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
            c.text(*m_font,shorten(g.owner+" / "+(g.inlineMacro?"[M] ":"")+g.name,32).c_str(),12,y+6,text);c.noClip();
        }
        c.text(*m_font,"MY BLUEPRINT / VARIABLES",10,330,accent);
        const auto quickRows=variableRows();
        const auto quickFirst=std::min(m_quickVariableFirst,quickRows.size()>kVariableRows?quickRows.size()-kVariableRows:0);
        for(size_t i=0;i<kVariableRows&&quickFirst+i<quickRows.size();++i)
        {
            const auto& r=quickRows[quickFirst+i];const float y=kVariableTop+31*static_cast<float>(i);
            c.rect(5,y+2,kLeft-10,28,{0.17f,0.22f,0.30f,1});c.circle(14,y+16,4,pinColor(r.field.type));
            c.clip(24,y,kLeft-30,31);c.text(*m_font,shorten(std::string(r.global?"Global / ":"")+r.field.name+" : "+r.field.type,28).c_str(),24,y+6,text);c.noClip();
        }
        if(quickRows.empty())c.text(*m_font,"Create variables below.",12,kVariableTop+8,muted);
        c.roundRect(8,kBottom-225,kLeft-16,32,5,{0.19f,0.33f,0.50f,1});
        c.text(*m_font,"Variables",kLeft*0.5f,kBottom-219,text,Align::Center);
        c.roundRect(8,kBottom-188,kLeft-16,32,5,{0.19f,0.33f,0.50f,1});
        c.clip(8,kBottom-188,kLeft-16,32);
        c.text(*m_font,m_focus==7?(m_edit.empty()?"Type screen ID...":m_edit.c_str()):"New screen",kLeft*0.5f,kBottom-182,text,Align::Center);c.noClip();
        c.roundRect(8,kBottom-151,kLeft-16,32,5,{0.32f,0.19f,0.23f,1});
        c.text(*m_font,m_document.graphs[m_graph].owner.rfind("behaviour:",0)==0?"Delete behaviour":"Delete screen",kLeft*0.5f,kBottom-145,text,Align::Center);
        c.roundRect(8,kBottom-114,kLeft-16,32,5,{0.19f,0.33f,0.50f,1});
        c.clip(8,kBottom-114,kLeft-16,32);
        c.text(*m_font,m_focus==4?(m_edit.empty()?"Type name...":m_edit.c_str()):"New function",kLeft*0.5f,kBottom-108,text,Align::Center);c.noClip();
        c.roundRect(8,kBottom-77,kLeft-16,32,5,{0.32f,0.19f,0.23f,1});
        c.text(*m_font,"Delete function",kLeft*0.5f,kBottom-71,m_document.graphs[m_graph].kind=="function"?text:muted,Align::Center);
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
            if(n.kind.rfind("literal:",0)==0||n.kind.rfind("var:",0)==0||n.kind.rfind("global:",0)==0||n.kind.rfind("capture:",0)==0||n.kind=="call:function"||n.kind=="attach:behaviour"||n.kind=="attach:direct"||n.kind=="level:attach")
            {
                c.roundRect(kRight+8,kTop+456,kWidth-kRight-16,30,4,(m_focus==2||m_focus==3)?Color{0.24f,0.35f,0.48f,1}:Color{0.16f,0.21f,0.28f,1});
                c.clip(kRight+12,kTop+456,kWidth-kRight-24,30);
                c.text(*m_font,((m_focus==2||m_focus==3)?m_edit:n.ref).c_str(),kRight+14,kTop+461,text);c.noClip();
                c.text(*m_font,"Click to edit, Enter confirms",kRight+12,kTop+490,muted);
            }
            c.roundRect(kRight+8,kBottom-40,kWidth-kRight-16,32,5,{0.38f,0.19f,0.23f,1});
            c.text(*m_font,"Delete selected", (kWidth+kRight)*0.5f,kBottom-34,text,Align::Center);
        }
        c.rect(0,kBottom,kWidth,kHeight-kBottom,{0.12f,0.16f,0.22f,1});
        c.text(*m_font,shorten(m_status,110).c_str(),12,kBottom+6,text);
        if(!m_recoveryWarning.empty())
            c.text(*m_font,shorten(m_recoveryWarning,105).c_str(),12,kBottom+26,{1.f,0.72f,0.22f,1.f});
        else
        {
            c.clip(12,kBottom+24,900,22);
            c.text(*m_font,"Shift+click: select | Layout: arrange/group | Ctrl+right-click wire: reroute",12,kBottom+26,muted);
            c.noClip();
            const char* state=!m_document.dirty()?"Saved / clean":recoveryProtected()?"Recovery protected":"Recovery pending";
            c.text(*m_font,state,kWidth-12,kBottom+26,recoveryProtected()?muted:Color{1.f,0.72f,0.22f,1.f},Align::Right);
        }
        if(m_layoutMenu)
        {
            c.roundRect(300,35,250,300,5,{0.12f,0.16f,0.22f,1});
            const char* labels[]={"Arrange graph","Align selected left","Align selected top","Comment selection","Collapse to function","Rename function","Duplicate function","Open selected function","Back to caller",m_document.graphs[m_graph].inlineMacro?"Convert to function":"Convert to inline macro"};
            const auto& activeGraph=m_document.graphs[m_graph];
            const bool hasSelection=m_selection<activeGraph.nodes.size()||!m_selectedNodes.empty();
            for(size_t i=0;i<10;++i)
            {
                const bool enabled=i==0||(i<=2&&m_selectedNodes.size()>=2)||((i==3||i==4)&&hasSelection)||((i==5||i==6)&&activeGraph.kind=="function")||
                    (i==7&&m_selection<activeGraph.nodes.size()&&activeGraph.nodes[m_selection].kind=="call:function")||(i==8&&!m_graphBack.empty())||(i==9&&activeGraph.kind=="function");
                const float yy=35+30*static_cast<float>(i);c.rect(304,yy+2,242,26,enabled?Color{0.19f,0.33f,0.50f,1}:Color{0.17f,0.22f,0.30f,1});c.text(*m_font,labels[i],312,yy+5,enabled?text:muted);
            }
        }
        if(m_pinMenu)
        {
            const float x=m_pinMenuX,y=m_pinMenuY;
            c.roundRect(x,y,300,320,6,{0.12f,0.16f,0.22f,1});
            c.clip(x+6,y,288,28);c.text(*m_font,(m_pinType.empty()?"ADD NODE":"COMPATIBLE / "+m_pinType).c_str(),x+8,y+5,accent);c.noClip();
            c.rect(x+6,y+28,288,30,{0.24f,0.35f,0.48f,1});c.clip(x+8,y+28,284,30);c.text(*m_font,m_pinQuery.empty()?"Search nodes...":m_pinQuery.c_str(),x+10,y+33,text);c.noClip();
            for(size_t i=0;i<8&&m_paletteFirst+i<m_contextResults.size();++i)
            {const float ry=y+64+30*static_cast<float>(i);c.rect(x+6,ry,288,28,{0.17f,0.22f,0.30f,1});c.clip(x+8,ry,284,28);c.text(*m_font,m_contextResults[m_paletteFirst+i].title.c_str(),x+10,ry+5,text);c.noClip();}
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
            c.roundRect(x,y,210,90,5,{0.12f,0.16f,0.22f,1});
            c.clip(x+6,y,198,28);c.text(*m_font,m_quickVariable.field.name.c_str(),x+8,y+5,accent);c.noClip();
            c.rect(x+4,y+28,202,29,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Get variable",x+10,y+34,text);
            c.rect(x+4,y+58,202,29,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Set variable",x+10,y+64,text);
        }
        if(m_assetBrowser||m_browsing||m_focus==5||m_focus==6||m_focus==9||m_focus==10||m_focus==11||m_focus==12||m_focus==13||m_focus==14)
        {
            c.rect(0,0,kWidth,kHeight,{0,0,0,0.70f});
            if(m_assetBrowser)
            {
                c.roundRect(260,100,760,525,9,{0.12f,0.16f,0.22f,1});
                c.text(*m_font,("ASSETS / "+assetScreen()).c_str(),280,122,accent);
                c.roundRect(534,116,160,34,4,{0.19f,0.33f,0.50f,1});c.text(*m_font,"Scene editor",614,123,text,Align::Center);
                c.roundRect(704,116,146,34,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Refresh",777,123,text,Align::Center);
                c.roundRect(860,116,140,34,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Close",930,123,text,Align::Center);
                c.clip(280,155,720,26);c.text(*m_font,("own/"+m_document.assetRoot+"/"+assetScreen()+"/asset").c_str(),280,160,muted);c.noClip();
                for(size_t i=0;i<10&&m_assetFirst+i<m_assetFiles.size();++i)
                {
                    const float y=190+36*static_cast<float>(i);
                    c.rect(280,y,720,33,{0.17f,0.22f,0.30f,1});
                    c.clip(286,y,708,33);c.text(*m_font,("asset:"+m_assetFiles[m_assetFirst+i]).c_str(),290,y+8,text);c.noClip();
                }
                if(m_assetFiles.empty())c.text(*m_font,"Drop a file here to import it.",640,330,muted,Align::Center);
                c.text(*m_font,"Import media here; Scene editor opens Redux\x27s original tool.",640,565,muted,Align::Center);
                c.clip(280,591,720,26);c.text(*m_font,m_status.c_str(),280,595,text);c.noClip();
            }
            else if(m_browsing)
            {
                c.roundRect(260,100,760,525,9,{0.12f,0.16f,0.22f,1});
                c.text(*m_font,"OPEN / IMPORT BLUEPRINT",280,122,accent);
                c.roundRect(860,116,140,34,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Cancel",930,123,text,Align::Center);
                for(size_t i=0;i<12&&m_fileFirst+i<m_files.size();++i)
                {
                    const float y=166+36*static_cast<float>(i);
                    c.rect(280,y,720,33,{0.17f,0.22f,0.30f,1});
                    const auto& file=m_files[m_fileFirst+i];
                    const std::string label=(recoveryArchiveName(file)?"RECOVERY  /  ":standaloneSourceName(file)?"SOURCE  /  ":projectFolderName(file)?"PROJECT FOLDER  /  ":"")+file;
                    c.clip(286,y,708,33);c.text(*m_font,label.c_str(),290,y+8,text);c.noClip();
                }
                if(m_files.empty())c.text(*m_font,"No projects, archives, source or project folders in own/.",640,330,muted,Align::Center);
                c.clip(280,603,720,22);c.text(*m_font,shorten(m_status,90).c_str(),280,603,muted);c.noClip();
            }
            else if(m_focus==10)
            {
                c.roundRect(150,100,980,525,9,{0.12f,0.16f,0.22f,1});
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
                    c.rect(170,y,450,33,selected?Color{0.19f,0.33f,0.50f,1}:Color{0.17f,0.22f,0.30f,1});
                    const auto label=std::string(r.global?"Global / ":"Field / ")+r.field.name+" : "+r.field.type+(r.explicitField?"":" (inferred)");
                    c.clip(178,y,434,33);c.text(*m_font,label.c_str(),180,y+7,text);c.noClip();
                }
                if(rows.empty())c.text(*m_font,"No primitive variables yet.",180,208,muted);
                c.text(*m_font,(std::to_string(rows.size())+" variables | wheel to scroll").c_str(),170,556,muted);
                c.text(*m_font,m_variableGlobal?"MODULE VARIABLE":"INSTANCE FIELD",650,192,accent);
                const auto field=[&](float y,const std::string& value,bool focused)
                {c.roundRect(650,y,450,36,4,focused?Color{0.24f,0.35f,0.48f,1}:Color{0.17f,0.22f,0.30f,1});c.clip(658,y,434,36);c.text(*m_font,value.c_str(),660,y+8,text);c.noClip();};
                field(222,m_variableName.empty()?"Name":m_variableName,m_variableInput==1);
                field(266,"Type: "+m_variableType+"  v",m_variableTypes);
                variableButton(650,322,450,m_variableInitialized?"Initial value: enabled":"Initial value: default");
                if(m_variableInitialized)field(370,m_variableValue.empty()?"":m_variableValue,m_variableInput==2);
                else c.text(*m_font,"Uses the native default for this type.",650,380,muted);
                c.text(*m_font,"String values are entered without quotes.",650,420,muted);
                variableButton(650,464,210,"Apply");variableButton(870,464,230,"Delete",!m_variableOld.empty());
                if(m_variableTypes)for(size_t i=0;i<6;++i)
                {const float y=302+28*static_cast<float>(i);c.rect(650,y,450,28,{0.24f,0.35f,0.48f,1});c.text(*m_font,variableTypes[i],660,y+3,text);}
                c.clip(170,576,940,25);c.text(*m_font,m_status.c_str(),170,576,text);c.noClip();
                c.text(*m_font,"Enter applies | Escape closes",640,601,muted,Align::Center);
            }
            else if(m_focus>=12&&m_focus<=14)
            {
                c.roundRect(260,230,760,220,8,{0.12f,0.16f,0.22f,1});c.text(*m_font,m_focus==12?"COLLAPSE TO FUNCTION":m_focus==13?"RENAME FUNCTION":"DUPLICATE FUNCTION",280,250,accent);
                c.rect(280,290,720,36,{0.24f,0.35f,0.48f,1});c.clip(286,290,708,36);c.text(*m_font,m_edit.c_str(),290,297,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),280,340,720,muted);c.text(*m_font,"Enter confirms | Escape cancels",640,415,text,Align::Center);
            }
            else if(m_focus==11)
            {
                c.roundRect(260,230,760,220,8,{0.12f,0.16f,0.22f,1});c.text(*m_font,"COMMENT TITLE",280,250,accent);
                c.rect(280,290,720,36,{0.24f,0.35f,0.48f,1});c.clip(286,290,708,36);c.text(*m_font,m_edit.c_str(),290,297,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),280,340,720,muted);c.text(*m_font,"Enter confirms | Escape cancels",640,415,text,Align::Center);
            }
            else if(m_focus==9)
            {
                c.roundRect(200,226,880,240,9,{0.12f,0.16f,0.22f,1});
                c.text(*m_font,"FUNCTION SIGNATURE",220,245,accent);
                c.roundRect(220,285,840,36,4,{0.24f,0.35f,0.48f,1});
                c.clip(224,285,832,36);c.text(*m_font,m_edit.c_str(),230,293,text);c.noClip();
                c.paragraph(*m_font,"Format: float(amount:float, count:int) | void() for no return. Up to 8 parameters.",220,330,840,muted);
                c.paragraph(*m_font,m_status.c_str(),220,375,840,muted);
                c.text(*m_font,"Enter confirms | Escape cancels",640,438,text,Align::Center);
            }
            else if(m_focus==6)
            {
                c.roundRect(340,226,600,224,9,{0.12f,0.16f,0.22f,1});
                c.text(*m_font,("INPUT: "+m_editPin+" ("+m_editType+")").c_str(),360,245,accent);
                c.roundRect(360,285,560,36,4,{0.24f,0.35f,0.48f,1});
                c.clip(364,285,552,36);c.text(*m_font,m_edit.c_str(),370,293,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),360,330,560,muted);
                c.roundRect(360,391,180,39,4,{0.22f,0.31f,0.43f,1});c.text(*m_font,"Reset value",450,400,text,Align::Center);
                c.text(*m_font,"Enter confirms | Escape cancels",730,400,text,Align::Center);
            }
            else
            {
                c.roundRect(340,240,600,205,9,{0.12f,0.16f,0.22f,1});
                c.text(*m_font,"SAVE BLUEPRINT AS",360,260,accent);
                c.roundRect(360,300,560,36,4,{0.24f,0.35f,0.48f,1});
                c.clip(364,300,552,36);c.text(*m_font,m_edit.empty()?"Type a new name...":m_edit.c_str(),370,308,text);c.noClip();
                c.paragraph(*m_font,m_status.c_str(),360,351,560,muted);
                c.text(*m_font,"Enter saves | Escape cancels",640,412,text,Align::Center);
            }
        }
    }
}
