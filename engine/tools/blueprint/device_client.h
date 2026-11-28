#pragma once
#include "net/socket.h"
#include "base/json_tree.h"
#include "base/json_value.h"
#include "script/studio_launch.h"
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
            m_status="Disconnected";m_launchId.clear();m_address.clear();
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
            if(!m_ready)return;
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
            if(m_ready&&now>=m_poll){m_poll=now+1;queue("{\"cmd\":\"telemetry\",\"id\":4}");}
            if(m_ready&&m_tracing&&!m_traceOutstanding&&now>=m_tracePoll){m_tracePoll=now+0.25;m_traceOutstanding=true;queue("{\"cmd\":\"traceDrain\",\"id\":7}");}
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
        void consume(const std::string& line,double now)
        {
            JsonTree tree;if(!tree.parse(line.data(),line.size()))return;
            const auto root=tree.root();const std::string event=root["event"].text();
            const auto id=root["id"].exact();
            if(event=="hello"&&id==1&&!m_ready)
            {
                if(root["version"].exact()!=1){fail("Unsupported debugger protocol.");return;}
                m_sourceIdentity=root["sourceIdentity"].flag();m_traceSupported=root["remoteTrace"].flag();m_launchId=root["launchId"].text();if(!studioLaunchIdentity(m_launchId))m_launchId.clear();m_ready=true;m_poll=now;m_status="Connected; waiting for device telemetry.";
                queue("{\"cmd\":\"ready\",\"id\":2}");
            }
            else if(event=="stopped"&&m_ready)
            {
                m_paused=true;m_status=std::string("Stopped: ")+root["reason"].text();m_file.clear();m_fingerprint.clear();m_variables.clear();m_line=0;
                queue("{\"cmd\":\"stack\",\"id\":3}");
                queue("{\"cmd\":\"variables\",\"id\":5,\"frame\":0,\"scope\":\"locals\",\"limit\":12}");
            }
            else if(event=="continued")
            {m_paused=false;m_status="Running";m_file.clear();m_fingerprint.clear();m_variables.clear();m_line=0;}
            else if(event=="launchFailed"){m_launchStatus=root["text"].text();}
            else if(id==8){m_launchStatus=root["message"].text();}
            else if(event=="output")
            {m_log=std::string(root["text"].text()).substr(0,240);}
            else if(id==3&&m_paused)
            {
                const auto top=root["frames"][0u];
                m_file=std::string(top["file"].text()).substr(0,256);
                m_fingerprint=std::string(top["fingerprint"].text()).substr(0,32);
                const auto at=top["line"].exact();if(at>0&&at<1000000)m_line=static_cast<unsigned>(at);
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
        double m_tracePoll=0;
        std::vector<RemoteHit> m_hits;
        double m_deadline=0,m_poll=0;
        std::string m_in,m_out,m_status="Disconnected",m_log,m_file,m_fingerprint,m_launchId,m_launchStatus,m_address;
        unsigned m_line=0;
        std::vector<std::string> m_rows,m_variables;
    };
}
