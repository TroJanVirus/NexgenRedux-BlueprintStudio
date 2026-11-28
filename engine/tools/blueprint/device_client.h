#pragma once
#include "net/socket.h"
#include "base/json_tree.h"
#include "base/json_value.h"
#include "script/studio_launch.h"
#include "script/studio_live_source.h"
#include "script/studio_preview.h"
#include <algorithm>
#include <string>
#include <vector>

namespace nx
{
    // Native, single-client Redux debug protocol. No blocking DNS, no automatic
    // reconnect, no arbitrary remote command entry. Intended for trusted LANs.
    class BlueprintDevice : NonCopyable
    {
    public:
        ~BlueprintDevice(){disconnect();}
        bool active()const{return m_network;}
        bool ready()const{return m_ready;}
        bool paused()const{return m_paused;}
        bool sourceIdentity()const{return m_sourceIdentity;}
        bool traceSupported()const{return m_traceSupported;}
        struct Actor {u32 id=0,parent=0;std::string name;bool spatial=false;};
        struct ActorProperty {std::string name,value;bool editable=false;};
        struct PinValue{std::string name,type,value;};
        const std::vector<PinValue>& pinValues()const{return m_pinValues;}
        bool pinsCapped()const{return m_pinsCapped;}
        bool pinsSupported()const{return m_pinsSupported;}
        bool previewSupported()const{return m_previewSupported;}
        bool previewBusy()const{return m_previewBusy;}
        const std::string& previewStatus()const{return m_previewStatus;}
        u32 previewWidth()const{return m_previewWidth;}
        u32 previewHeight()const{return m_previewHeight;}
        u32 previewSequence()const{return m_previewSequence;}
        const std::vector<u8>& previewPixels()const{return m_previewPixels;}
        bool requestPreview(double now)
        {
            if(!m_ready||!m_previewSupported||m_previewBusy||m_paused||liveBusy()||actorBusy()||m_tracing||m_traceOutstanding||m_out.size()>1024)return false;
            queue("{\"cmd\":\"studioPreview\",\"id\":15}");m_previewBusy=true;m_previewDeadline=now+5;return true;
        }
        bool actorsSupported()const{return m_actorsSupported;}
        bool actorBusy()const{return m_actorRequest!=0;}
        const std::vector<Actor>& actors()const{return m_actors;}
        const std::vector<ActorProperty>& actorProperties()const{return m_actorProperties;}
        u32 actorEpoch()const{return m_actorEpoch;}
        u32 actorOffset()const{return m_actorOffset;}
        u32 actorCount()const{return m_actorCount;}
        u32 selectedActor()const{return m_actorSelected;}
        const std::string& actorScreen()const{return m_actorScreen;}
        const std::string& actorStatus()const{return m_actorStatus;}
        bool requestActors(u32 offset,double now)
        {
            if(!m_ready||!m_actorsSupported||liveBusy()||actorBusy()||m_out.size()>4096)return false;
            JsonValue req;req.put("cmd")->setText("studioActors");req.put("id")->setNumber(11);req.put("offset")->setNumber(offset);
            queue(req.write(true));m_actorRequest=11;m_actorDeadline=now+5;m_actorStatus="Refreshing actor list...";return true;
        }
        bool inspectActor(u32 entity,double now)
        {
            if(!m_ready||!m_actorsSupported||!m_actorEpoch||liveBusy()||actorBusy()||m_out.size()>4096)return false;
            JsonValue req;req.put("cmd")->setText("studioActor");req.put("id")->setNumber(12);req.put("epoch")->setNumber(m_actorEpoch);req.put("entity")->setNumber(entity);
            queue(req.write(true));m_actorSelected=entity;m_actorProperties.clear();m_actorRequest=12;m_actorDeadline=now+5;m_actorStatus="Reading runtime components...";return true;
        }
        bool editActor(const std::string& property,const std::string& value,double now)
        {
            if(!m_ready||!m_actorsSupported||!m_actorEpoch||liveBusy()||actorBusy()||m_out.size()>4096)return false;
            const auto allowed=std::find_if(m_actorProperties.begin(),m_actorProperties.end(),[&](const ActorProperty& p){return p.name==property&&p.editable;});
            if(allowed==m_actorProperties.end()||value.size()>96)return false;
            JsonValue req;req.put("cmd")->setText("studioActorSet");req.put("id")->setNumber(13);req.put("epoch")->setNumber(m_actorEpoch);req.put("entity")->setNumber(m_actorSelected);
            req.put("property")->setText(property);req.put("value")->setText(value);
            queue(req.write(true));m_actorRequest=13;m_actorDeadline=now+5;m_actorStatus="Applying runtime property...";return true;
        }
        bool liveSupported()const{return m_liveSupported;}
        bool liveBusy()const{return m_liveStage!=0;}
        const std::string& liveScreen()const{return m_liveScreen;}
        const std::string& liveFingerprint()const{return m_liveFingerprint;}
        const std::string& liveStatus()const{return m_liveStatus;}
        bool pushLive(const std::string& source,const std::string& token,double now)
        {
            if(!m_ready||m_paused||!m_liveSupported||liveBusy()||actorBusy()||m_liveScreen.empty()||
                !StudioLiveSource::validHash(m_liveFingerprint)||!studioLaunchIdentity(token)||
                source.empty()||source.size()>StudioLiveSource::limit||m_out.size()>2048)return false;
            const auto hash=scriptFingerprint(source.data(),source.size());
            if(hash==m_liveFingerprint){m_liveStatus="Current source is already running.";return true;}
            JsonValue request;request.put("cmd")->setText("liveBegin");request.put("id")->setNumber(9);
            request.put("token")->setText(token);request.put("screen")->setText(m_liveScreen);
            request.put("expected")->setText(m_liveFingerprint);request.put("fingerprint")->setText(hash);
            request.put("size")->setNumber(static_cast<double>(source.size()));
            setTrace(false);
            queue(request.write(true));m_liveBytes=source;m_liveOffset=0;m_liveStage=1;m_liveDeadline=now+10;
            m_liveStatus="Sending source snapshot.";return true;
        }
        const std::string& launchId()const{return m_launchId;}
        const std::string& launchStatus()const{return m_launchStatus;}
        const std::string& address()const{return m_address;}
        bool launchTitle(const std::string& folder,const std::string& token)
        {
            if(!m_ready||m_paused||m_launchId.empty()||!studioLaunchIdentity(token)||studioLaunchPath(folder).empty())return false;
            JsonValue request;request.put("cmd")->setText("launchTitle");request.put("id")->setNumber(8);
            request.put("folder")->setText(folder);request.put("token")->setText(token);
            const auto bytes=request.write(true);
            if(m_out.size()+bytes.size()+1>8192)return false;
            queue(bytes);m_launchStatus="Launch requested; awaiting runtime response.";return true;
        }
        bool tracing()const{return m_tracing;}
        struct RemoteHit {std::string file,fingerprint;unsigned line=0,run=0,depth=0;};
        std::vector<RemoteHit> takeTrace(bool& gap){gap=m_traceGap;m_traceGap=false;std::vector<RemoteHit> result;result.swap(m_hits);return result;}
        void setTrace(bool on){if(!m_ready||!m_traceSupported)return;m_tracing=on;m_hits.clear();m_traceGap=false;m_traceOutstanding=false;queue(on?"{\"cmd\":\"trace\",\"on\":true,\"id\":6}":"{\"cmd\":\"trace\",\"on\":false,\"id\":6}");}
        const std::string& fingerprint()const{return m_fingerprint;}
        const std::vector<std::string>& variables()const{return m_variables;}
        bool sendBreakpoints(const std::string& commands){if(!m_ready||!m_sourceIdentity||m_out.size()+commands.size()>8192)return false;m_out+=commands;return true;}
        const std::string& status()const{return m_status;}
        const std::vector<std::string>& rows()const{return m_rows;}
        const std::string& log()const{return m_log;}
        const std::string& file()const{return m_file;}
        unsigned line()const{return m_line;}
        void disconnect()
        {
            m_socket.close();if(m_network)netStop();m_network=m_ready=m_paused=m_hello=m_sourceIdentity=m_traceSupported=m_tracing=m_traceOutstanding=m_traceGap=false;m_hits.clear();
            m_in.clear();m_out.clear();m_rows.clear();m_log.clear();m_file.clear();m_fingerprint.clear();m_variables.clear();m_line=0;
            m_previewSupported=m_previewBusy=false;m_previewPixels.clear();m_previewWidth=m_previewHeight=0;m_previewStatus.clear();++m_previewSequence;
            m_pinsSupported=m_pinsCapped=false;m_pinValues.clear();m_actorsSupported=false;m_actorRequest=0;m_actorEpoch=m_actorOffset=m_actorCount=m_actorSelected=0;m_actors.clear();m_actorProperties.clear();m_actorScreen.clear();m_actorStatus.clear();
            m_status="Disconnected";m_launchId.clear();m_address.clear();m_liveSupported=false;m_liveStage=0;m_liveScreen.clear();m_liveFingerprint.clear();std::string().swap(m_liveBytes);
        }
        bool connect(const std::string& address,double now)
        {
            disconnect();u32 ip=0;
            if(!netParseAddress(address.c_str(),ip)||!ip||ip==kBroadcast)
            {m_status="Enter a valid device IPv4 address.";return false;}
            if(!netStart()){m_status="Network unavailable.";return false;}
            m_network=true;
            if(!m_socket.connect({ip,4711})){fail("Could not connect to TCP 4711.");return false;}
            m_address=address;m_launchStatus.clear();m_deadline=now+8;m_status="Connecting to Redux debugger...";return true;
        }
        void control(unsigned action)
        {
            if(!m_ready||liveBusy())return;
            if(action==0&&!m_paused)queue("{\"cmd\":\"pause\"}");
            else if(m_paused)
            {
                constexpr const char* commands[]={"{\"cmd\":\"pause\"}","{\"cmd\":\"continue\"}","{\"cmd\":\"next\"}","{\"cmd\":\"stepIn\"}","{\"cmd\":\"stepOut\"}"};
                if(action>0&&action<5)queue(commands[action]);
            }
        }
        void update(double now)
        {
            if(!m_network)return;
            const auto state=m_socket.update();
            if(state==TcpState::Closed){fail("Device disconnected or refused connection.");return;}
            if(!m_ready&&now>m_deadline){fail("Redux debugger handshake timed out.");return;}
            if(state!=TcpState::Open)return;
            if(!m_hello){m_hello=true;queue("{\"cmd\":\"hello\",\"id\":1}");}
            if(m_previewBusy&&now>m_previewDeadline){fail("Preview response timed out.");return;}
            if(actorBusy()&&now>m_actorDeadline){fail("Actor request timed out; reconnect to refresh.");return;}
            if(liveBusy()&&now>m_liveDeadline){m_liveStatus="Live Link timed out; reconnect to inspect actual running source.";fail("Live Link response timed out.");return;}
            if(m_ready&&!liveBusy()&&now>=m_poll){m_poll=now+1;queue("{\"cmd\":\"telemetry\",\"id\":4}");if(m_liveSupported)queue("{\"cmd\":\"liveInfo\",\"id\":10}");}
            if(m_ready&&!liveBusy()&&m_tracing&&!m_traceOutstanding&&now>=m_tracePoll){m_tracePoll=now+0.25;m_traceOutstanding=true;queue("{\"cmd\":\"traceDrain\",\"id\":7}");}
            if(!m_out.empty())
            {
                const auto sent=m_socket.send(m_out.data(),m_out.size());m_out.erase(0,sent);
            }
            char chunk[4096];
            // Bound receive work per editor frame and reject oversized messages.
            for(unsigned i=0;i<8;++i)
            {
                const auto got=m_socket.receive(chunk,sizeof(chunk));if(!got)break;
                m_in.append(chunk,got);
                if(m_in.size()>65536){fail("Debugger response exceeded limit.");return;}
            }
            for(unsigned i=0;i<32;++i)
            {
                const auto end=m_in.find('\n');if(end==std::string::npos)break;
                const auto line=m_in.substr(0,end);m_in.erase(0,end+1);consume(line,now);
                if(!m_network)return;
            }
        }
    private:
        void fail(const char* why){disconnect();m_status=why;}
        void queue(const std::string& value)
        {
            if(m_out.size()+value.size()+1>8192)return;
            m_out+=value;m_out+='\n';
        }
        void sendLiveChunk(double now)
        {
            JsonValue request;request.put("id")->setNumber(9);
            if(m_liveOffset==m_liveBytes.size())
            {
                request.put("cmd")->setText("liveCommit");m_liveStage=3;m_liveDeadline=now+30;
                m_liveStatus="Validating and restarting active screen.";
            }
            else
            {
                const size_t size=std::min(StudioLiveSource::chunkLimit,m_liveBytes.size()-m_liveOffset);
                std::string hex;hex.reserve(size*2);const char* digits="0123456789abcdef";
                for(size_t i=0;i<size;++i){const auto byte=static_cast<unsigned char>(m_liveBytes[m_liveOffset+i]);hex+=digits[byte>>4];hex+=digits[byte&15];}
                request.put("cmd")->setText("liveChunk");request.put("offset")->setNumber(static_cast<double>(m_liveOffset));
                request.put("hex")->setText(hex);m_liveOffset+=size;m_liveStage=2;m_liveDeadline=now+10;
                m_liveStatus="Sending source: "+std::to_string(m_liveOffset)+" / "+std::to_string(m_liveBytes.size())+" bytes";
            }
            queue(request.write(true));
        }
        void consume(const std::string& line,double now)
        {
            JsonTree tree;if(!tree.parse(line.data(),line.size()))return;
            const auto root=tree.root();const std::string event=root["event"].text();
            const auto id=root["id"].exact();
            if(event=="hello"&&id==1&&!m_ready)
            {
                if(root["version"].exact()!=1){fail("Unsupported debugger protocol.");return;}
                m_sourceIdentity=root["sourceIdentity"].flag();m_traceSupported=root["remoteTrace"].flag();m_actorsSupported=root["studioActors"].flag();m_pinsSupported=root["studioPins"].flag();m_previewSupported=root["studioPreview"].flag();m_launchId=root["launchId"].text();if(!studioLaunchIdentity(m_launchId))m_launchId.clear();m_liveSupported=root["liveSource"].flag()&&!m_launchId.empty();m_ready=true;m_poll=now;m_status="Connected; waiting for device telemetry.";
                queue("{\"cmd\":\"ready\",\"id\":2}");
            }
            else if(event=="stopped"&&m_ready)
            {
                m_paused=true;m_pinValues.clear();m_pinsCapped=false;m_status=std::string("Stopped: ")+root["reason"].text();m_file.clear();m_fingerprint.clear();m_variables.clear();m_line=0;
                queue("{\"cmd\":\"stack\",\"id\":3}");
                queue("{\"cmd\":\"variables\",\"id\":5,\"frame\":0,\"scope\":\"locals\",\"limit\":12}");
                if(m_pinsSupported)queue("{\"cmd\":\"studioPins\",\"id\":14}");
            }
            else if(event=="continued")
            {m_paused=false;m_pinValues.clear();m_pinsCapped=false;m_status="Running";m_file.clear();m_fingerprint.clear();m_variables.clear();m_line=0;}
            else if(event=="launchFailed"){m_launchStatus=root["text"].text();}
            else if(id==8){m_launchStatus=root["message"].text();}
            else if(id>=11&&id<=13&&id==m_actorRequest)
            {
                m_actorRequest=0;
                const bool ok=root["actorOk"].flag();
                if(!ok)
                {
                    m_actorStatus=root["message"].text();m_actorProperties.clear();
                    if(id==11){m_actors.clear();m_actorEpoch=0;}
                }
                else if(id==11)
                {
                    const auto epoch=root["epoch"].exact(),offset=root["offset"].exact(),count=root["count"].exact();
                    if(!(epoch>0&&epoch<=4294967295.0&&offset>=0&&offset<=65536&&count>=0&&count<=65536))
                    {m_actorStatus="Invalid actor snapshot.";m_actors.clear();m_actorProperties.clear();m_actorEpoch=0;return;}
                    if(m_actorEpoch!=static_cast<u32>(epoch)||m_actorOffset!=static_cast<u32>(offset)){m_actorProperties.clear();m_actorSelected=0;}
                    m_actorEpoch=static_cast<u32>(epoch);m_actorOffset=static_cast<u32>(offset);m_actorCount=static_cast<u32>(count);
                    m_actorScreen=root["screen"].text();m_actors.clear();
                    for(auto item=root["entities"].first();item.exists()&&m_actors.size()<48;item=item.next())
                    {
                        const auto actorId=item["entity"].exact(),parent=item["parent"].exact();
                        if(actorId<0||actorId>4294967295.0||parent<0||parent>4294967295.0)continue;
                        m_actors.push_back({static_cast<u32>(actorId),static_cast<u32>(parent),std::string(item["name"].text()).substr(0,64),item["space"].flag()});
                    }
                    m_actorStatus="Snapshot received; select an actor.";
                }
                else if(id==12)
                {
                    m_actorProperties.clear();
                    if(root["epoch"].exact()!=m_actorEpoch||root["entity"].exact()!=m_actorSelected)
                    {m_actorStatus="Actor identity changed; refresh.";return;}
                    for(auto p=root["properties"].first();p.exists()&&m_actorProperties.size()<24;p=p.next())
                        m_actorProperties.push_back({std::string(p["name"].text()).substr(0,32),std::string(p["value"].text()).substr(0,192),p["changes"].flag()});
                    m_actorStatus="Runtime properties; changes are temporary.";
                }
                else
                {
                    m_actorStatus=root["message"].text();inspectActor(m_actorSelected,now);
                }
            }
            else if(id==10&&m_liveSupported&&!liveBusy())
            {
                m_liveScreen=root["screen"].text();m_liveFingerprint=root["fingerprint"].text();
                if(!StudioLiveSource::validHash(m_liveFingerprint))m_liveFingerprint.clear();
            }
            else if(id==9&&liveBusy())
            {
                const bool ok=root["liveOk"].flag();
                if(!ok||m_liveStage==3)
                {
                    m_liveStatus=root["message"].text();m_liveStage=0;std::string().swap(m_liveBytes);
                    m_liveFingerprint.clear();m_poll=now;
                }
                else if(m_liveStage==1||(m_liveStage==2&&root["received"].exact()==static_cast<double>(m_liveOffset)))
                    sendLiveChunk(now);
                else {m_liveStatus="Unexpected upload acknowledgement.";fail("Live Link acknowledgement mismatch.");}
            }
            else if(event=="output")
            {m_log=std::string(root["text"].text()).substr(0,240);}
            else if(id==3&&m_paused)
            {
                const auto top=root["frames"][0u];
                m_file=std::string(top["file"].text()).substr(0,256);
                m_fingerprint=std::string(top["fingerprint"].text()).substr(0,32);
                const auto at=top["line"].exact();if(at>0&&at<1000000)m_line=static_cast<unsigned>(at);
            }
            else if(id==15&&m_previewBusy)
            {
                m_previewBusy=false;
                if(!root["previewOk"].flag()){m_previewStatus=root["message"].text();return;}
                const auto w=root["width"].exact(),h=root["height"].exact();
                if(!(w>=1&&w<=kStudioPreviewWidth&&h>=1&&h<=kStudioPreviewHeight)||
                    w!=static_cast<u32>(w)||h!=static_cast<u32>(h)||
                    !studioPreviewDecode(root["pixels"].text(),static_cast<u32>(w),static_cast<u32>(h),m_previewPixels))
                {m_previewStatus="Malformed preview frame rejected.";return;}
                m_previewWidth=static_cast<u32>(w);m_previewHeight=static_cast<u32>(h);++m_previewSequence;
                m_previewStatus="Last captured console frame (RGB565).";
            }
            else if(id==14&&m_paused)
            {
                m_pinValues.clear();m_pinsCapped=false;
                if(m_file!=root["file"].text()||m_fingerprint!=root["fingerprint"].text()||root["line"].exact()!=m_line)return;
                m_pinsCapped=root["capped"].flag();
                for(auto item=root["pins"].first();item.exists()&&m_pinValues.size()<64;item=item.next())
                    m_pinValues.push_back({std::string(item["name"].text()).substr(0,96),std::string(item["type"].text()).substr(0,96),std::string(item["value"].text()).substr(0,192)});
            }
            else if(id==5&&m_paused)
            {
                m_variables.clear();
                for(auto item=root["variables"].first();item.exists()&&m_variables.size()<12;item=item.next())
                    m_variables.push_back(std::string(item["name"].text()).substr(0,64)+" : "+std::string(item["type"].text()).substr(0,64)+" = "+std::string(item["value"].text()).substr(0,96));
            }
            else if(id==7)
            {
                m_traceOutstanding=false;
                if(!m_tracing)return;
                m_traceGap=m_traceGap||root["gap"].flag();
                for(auto item=root["trace"].first();item.exists()&&m_hits.size()<64;item=item.next())
                {
                    const auto hitLine=item["line"].exact(),run=item["run"].exact(),depth=item["depth"].exact();
                    if(hitLine<1||hitLine>1000000||run<0||run>4294967295.0||depth<0||depth>1000000){m_traceGap=true;continue;}
                    m_hits.push_back({std::string(item["file"].text()).substr(0,192),std::string(item["fingerprint"].text()).substr(0,32),static_cast<unsigned>(hitLine),static_cast<unsigned>(run),static_cast<unsigned>(depth)});
                }
            }
            else if(id==4&&root["telemetry"].isObject())
            {
                const auto t=root["telemetry"];m_rows.clear();
                const auto number=[&](const char* label,const char* key,double divisor,const char* suffix)
                {
                    if(!t[key].isNumber()){m_rows.emplace_back(std::string(label)+": unknown");return;}
                    const double n=t[key].exact()/divisor;
                    if(n<0||n>1000000000){m_rows.emplace_back(std::string(label)+": unknown");return;}
                    const auto tenth=static_cast<u64>(n*10+0.5);
                    m_rows.emplace_back(std::string(label)+": "+std::to_string(tenth/10)+"."+std::to_string(tenth%10)+suffix);
                };
                m_rows.emplace_back(std::string("Device: ")+t["platform"].text("unknown"));
                number("FPS (last reported)","fps",1,"");number("Draws (last frame)","draws",1,"");
                if(t["physicalBytes"].exact()>0)number("Physical RAM","physicalBytes",1048576," MiB");
                else m_rows.emplace_back("Physical RAM: unknown");
                if(t["physicalBytes"].exact()>0&&t["freeBytes"].isNumber())number("Free RAM","freeBytes",1048576," MiB");
                else m_rows.emplace_back("Free RAM: unknown");
                number("Tracked allocations","trackedBytes",1048576," MiB");number("Tracked peak","peakBytes",1048576," MiB");
                number("Textures","textureBytes",1048576," MiB");number("Buffers","bufferBytes",1048576," MiB");
                number("Scene","sceneBytes",1048576," MiB");number("Scripts","scriptBytes",1048576," MiB");
                number("Audio","audioBytes",1048576," MiB");number("Audio voices","voices",1,"");
                m_status=m_paused?"Paused; FPS is the last running sample.":"Connected / live device counters";
            }
        }
        TcpSocket m_socket;
        bool m_network=false,m_ready=false,m_paused=false,m_hello=false,m_sourceIdentity=false;
        bool m_traceSupported=false,m_tracing=false,m_traceOutstanding=false,m_traceGap=false;
        bool m_liveSupported=false,m_actorsSupported=false,m_pinsSupported=false,m_pinsCapped=false;
        std::vector<PinValue> m_pinValues;
        bool m_previewSupported=false,m_previewBusy=false;
        double m_previewDeadline=0;
        u32 m_previewWidth=0,m_previewHeight=0,m_previewSequence=0;
        std::vector<u8> m_previewPixels;
        std::string m_previewStatus;
        int m_actorRequest=0;
        double m_actorDeadline=0;
        u32 m_actorEpoch=0,m_actorOffset=0,m_actorCount=0,m_actorSelected=0;
        std::string m_actorStatus,m_actorScreen;
        std::vector<Actor> m_actors;
        std::vector<ActorProperty> m_actorProperties;
        unsigned m_liveStage=0;
        size_t m_liveOffset=0;
        double m_liveDeadline=0;
        std::string m_liveScreen,m_liveFingerprint,m_liveStatus,m_liveBytes;
        double m_tracePoll=0;
        std::vector<RemoteHit> m_hits;
        double m_deadline=0,m_poll=0;
        std::string m_in,m_out,m_status="Disconnected",m_log,m_file,m_fingerprint,m_launchId,m_launchStatus,m_address;
        unsigned m_line=0;
        std::vector<std::string> m_rows,m_variables;
    };
}
