#include "script/debugger.h"
#include "script/studio_launch.h"
#include "platform/console.h"
#include "screens/screens.h"
#include "script/studio_actor_value.h"
#include "script/studio_preview.h"
#include "script/debugger_numbers.h"
#include "script/breakpoint_value.h"
#include "bindings/script_refs.h"
#include "scene/space.h"
#include "scene/world.h"
#include "base/json_value.h"
#include "base/log.h"
#include "base/memory.h"

#include "scriptarray/scriptarray.h"

#include <algorithm>
#include <angelscript.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nx
{
    namespace
    {
        ScriptDebugger* g_debugger = nullptr;
        LogSink g_previousSink = nullptr;

        constexpr u32 kMostChildren = 500;

        // `string get_text() const property` and the like, of the engine's own types: what can be asked
        // without running any script.
        bool isGetter(asIScriptFunction* f)
        {
            asDWORD flags = 0;
            return f && f->GetFuncType() == asFUNC_SYSTEM && strncmp(f->GetName(), "get_", 4) == 0 && f->GetParamCount() == 0 &&
                   f->GetReturnTypeId(&flags) != asTYPEID_VOID && f->IsProperty();
        }
        constexpr size_t kLongestText = 400;

        void putNumber(JsonValue& to, const char* key, double number)
        {
            to.put(key)->setNumber(number);
        }

        void putText(JsonValue& to, const char* key, const std::string& text)
        {
            to.put(key)->setText(text);
        }

        // Paths as the engine has them and as a debugger sends them may differ in slashes and case.
        bool samePath(const std::string& a, const char* b)
        {
            const size_t n = strlen(b);
            if (a.size() != n)
            {
                return false;
            }
            for (size_t i = 0; i < n; i++)
            {
                char x = a[i] == '\\' ? '/' : a[i];
                char y = b[i] == '\\' ? '/' : b[i];
                x = (x >= 'A' && x <= 'Z') ? static_cast<char>(x + 32) : x;
                y = (y >= 'A' && y <= 'Z') ? static_cast<char>(y + 32) : y;
                if (x != y)
                {
                    return false;
                }
            }
            return true;
        }

        std::string formatNumber(double value)
        {
            char text[48];
            snprintf(text, sizeof(text), "%.9g", value);
            return text;
        }
    }

    bool ScriptDebugger::start(ScriptHost& host, u16 port, bool onlyHere)
    {
#ifdef NX_DESKTOP_BLUEPRINT
        if(m_localHost)return false;
#endif
        if (!netStart())
        {
            NX_ERROR("debugger: no network");
            return false;
        }
        if (!m_listener.listen(port, onlyHere))
        {
            NX_ERROR("debugger: cannot listen on port %u", static_cast<unsigned>(port));
            netStop();
            return false;
        }
        m_host = &host;
        loadLaunchIdentity();
        m_stringType = host.engine()->GetTypeIdByDecl("string");
        host.setWatcher(this);
        g_debugger = this;
        g_previousSink = logSink();
        setLogSink([](LogLevel level, const char* message) {
            g_previousSink(level, message);
            if (g_debugger)
            {
                static const char* const levels[] = { "info", "warn", "error" };
                JsonValue line;
                putText(line, "event", "output");
                putText(line, "level", levels[static_cast<int>(level)]);
                putText(line, "text", message);
                Lock lock(g_debugger->m_logLock);
                if(g_debugger->m_log.size()<32768)
                {
                    g_debugger->m_log += line.write(true);
                    g_debugger->m_log += '\n';
                }
            }
        });
        NX_INFO("debugger: waiting on port %u", static_cast<unsigned>(port));
        return true;
    }

    void ScriptDebugger::stop()
    {
#ifdef NX_DESKTOP_BLUEPRINT
        stopLocalTrace();
#endif
        if (m_host == nullptr)
        {
            return;
        }
        setLogSink(g_previousSink);
        g_debugger = nullptr;
        m_host->setWatcher(nullptr);
        m_host = nullptr;
        m_pendingLaunch.clear();m_launchKey.clear();m_launchId.clear();m_liveUpload.clear();
        resetRemoteTrace();resetProfiler();m_remotePad.clear();
        m_client.close();
        m_listener.close();
        netStop();
    }

    void ScriptDebugger::waitForReady()
    {
        while (m_host && !m_ready)
        {
            update();
            if (m_idle)
            {
                m_idle(m_idleUser);
            }
            sleepMilliseconds(10);
        }
    }

    void ScriptDebugger::loadLaunchIdentity()
    {
        m_launchId.clear();m_launchKey.clear();
        const auto* s=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
        if(!s||!s->development||!s->platform)return;
        if(!s->platform->fileTime("studio-install.id")||!s->platform->fileTime("studio-launch.key"))return;
        FileData identity,key;
        if(!s->platform->readFile("studio-install.id",identity,MemTag::General)||identity.size!=32||
            !s->platform->readFile("studio-launch.key",key,MemTag::General)||key.size!=32)return;
        const std::string id(reinterpret_cast<const char*>(identity.data),identity.size);
        const std::string secret(reinterpret_cast<const char*>(key.data),key.size);
        if(studioLaunchIdentity(id)&&studioLaunchIdentity(secret)){m_launchId=id;m_launchKey=secret;}
    }
    void ScriptDebugger::update()
    {
        if (m_host == nullptr)
        {
            return;
        }
        if (m_client.state() != TcpState::Open)
        {
            m_client.close();
            Endpoint from;
            if (m_listener.accept(m_client, from))
            {
                // A new one starts with nothing set.
                m_pendingLaunch.clear();m_liveUpload.clear();
                m_breakpoints.clear();
                resetProfiler();m_remotePad.clear();m_last=Where();m_from=Where();
                resetRemoteTrace();
                m_files.clear();
                m_mode = Mode::Run;
                m_in.clear();
                m_out.clear();
                {
                    Lock lock(m_logLock);
                    m_log.clear();
                }
                NX_INFO("debugger: connected");
            }
        }
        m_actorFrame=true;pump();m_actorFrame=false;
        if(!connected())m_liveUpload.clear();
        else if(m_liveUpload.ready&&!m_paused&&m_mode==Mode::Run)applyLiveUpload();
        // Only the frame boundary may leave the running title. Never launch
        // from a script callback or while the VM is suspended.
        if(!m_pendingLaunch.empty())
        {
            if(!connected()){m_pendingLaunch.clear();return;}
            if(!m_out.empty()||m_paused)return;
            const auto image=m_pendingLaunch;m_pendingLaunch.clear();
            if(!consoleLaunch(image.c_str()))
            {
                JsonValue event;putText(event,"event","launchFailed");putText(event,"text","Console refused to launch the requested title.");
                send(event.write(true));flush();
            }
        }
    }

    void ScriptDebugger::pump()
    {
        if (m_client.state() != TcpState::Open)
        {
            m_resume=true;return;
        }
        {
            Lock lock(m_logLock);
            if(m_out.size()+m_log.size()<1024*1024)m_out += m_log;
            m_log.clear();
        }
        char buffer[4096];
        size_t got;
        while ((got = m_client.receive(buffer, sizeof(buffer))) > 0)
        {
            m_in.append(buffer, got);
            if(m_in.size()>65536){m_client.close();break;}
        }
        size_t end;
        while (m_client.state()==TcpState::Open&&(end = m_in.find('\n')) != std::string::npos)
        {
            const std::string line = m_in.substr(0, end);
            m_in.erase(0, end + 1);
            handle(line.data(), line.size());
        }
        flush();
        if (m_client.state() != TcpState::Open)
        {
            // Gone: nothing is to stop the scripts any more.
            m_breakpoints.clear();
            resetProfiler();m_remotePad.clear();m_last=Where();m_from=Where();
            resetRemoteTrace();
            m_files.clear();
            m_mode = Mode::Run;
            m_resume = true;
            m_ready = true;
        }
    }

    void ScriptDebugger::send(const std::string& line)
    {
        if(m_out.size()+line.size()+1>1024*1024){m_client.close();return;}
        m_out += line;
        m_out += '\n';
    }

    void ScriptDebugger::flush()
    {
        while (!m_out.empty() && m_client.state() == TcpState::Open)
        {
            const size_t sent = m_client.send(m_out.data(), m_out.size());
            if (sent == 0)
            {
                break;
            }
            m_out.erase(0, sent);
        }
    }

    void ScriptDebugger::handle(const char* text, size_t size)
    {
        JsonValue ask;
        std::string error;
        if (!ask.parse(text, size, error))
        {
            return;
        }
        const JsonValue* cmd = ask.find("cmd");
        const JsonValue* idValue = ask.find("id");
        u32 requestId=0;
        if(idValue&&(!debuggerUnsigned(idValue,requestId)||requestId>2147483647u))return;
        const int id=static_cast<int>(requestId);
        const std::string name = cmd ? cmd->text() : std::string();
        if (name == "clearBreakpoints")
        {
            m_breakpoints.clear();m_files.clear();
            JsonValue reply;putNumber(reply,"id",id);send(reply.write(true));
        }
        else if (name == "breakpoints")
        {
            const JsonValue* file = ask.find("file");
            const JsonValue* lines = ask.find("lines");
            if (file == nullptr)
            {
                return;
            }
            Breakpoints* these = nullptr;
            for (Breakpoints& b : m_breakpoints)
            {
                these = samePath(b.file, file->text().c_str()) ? &b : these;
            }
            if (these == nullptr)
            {
                m_breakpoints.push_back({ file->text(), {}, {}, {} });
                these = &m_breakpoints.back();
            }
            these->lines.clear();these->conditions.clear();
            const auto* fingerprint=ask.find("fingerprint");
            these->fingerprint=fingerprint?fingerprint->text():"";
            // Legacy debugger clients omit this field. A guarded request
            // must have a nonempty fingerprint to avoid unverified stops.
            if(fingerprint&&these->fingerprint.empty())these->fingerprint="invalid";
            for (u32 i = 0; lines && i < lines->count(); i++)
            {
                u32 line=0;
                if(debuggerUnsigned(lines->at(i),line)&&line>0&&line<=2147483647u)
                    these->lines.push_back(static_cast<int>(line));
            }
            const auto* conditions=ask.find("conditions");
            for(u32 i=0;conditions&&i<conditions->count();++i)
            {
                const auto* condition=conditions->at(i);u32 line=0;
                if(!condition||!debuggerUnsigned(condition->find("line"),line)||line==0||line>2147483647u)continue;
                const auto* value=condition->find("when");
                const std::string predicate=value&&value->type()==JsonValue::Type::String?value->text():"?";
                if(std::find(these->lines.begin(),these->lines.end(),static_cast<int>(line))!=these->lines.end())
                    these->conditions.emplace_back(static_cast<int>(line),validBreakpointCondition(predicate)&&!predicate.empty()?predicate:"?");
            }
            m_files.clear();
            JsonValue reply;
            putNumber(reply, "id", id);
            send(reply.write(true));
        }
        else if (name == "ready")
        {
            m_ready = true;
            JsonValue reply;
            putNumber(reply, "id", id);
            send(reply.write(true));
        }
        else if (name == "hello")
        {
            JsonValue reply;
            putText(reply, "event", "hello");
            putNumber(reply, "version", 1);
            reply.put("sourceIdentity")->setFlag(true);
            reply.put("remoteTrace")->setFlag(true);
            reply.put("studioActors")->setFlag(true);
            reply.put("studioPins")->setFlag(true);
            reply.put("conditionalBreakpoints")->setFlag(true);
            reply.put("studioWatches")->setFlag(true);
            reply.put("telemetryHeartbeat")->setFlag(true);
            const auto* svc=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
            reply.put("studioPreview")->setFlag(svc&&svc->development&&svc->device&&svc->device->previewCaptureSupported());
            if(!m_launchId.empty()){putText(reply,"launchId",m_launchId);reply.put("liveSource")->setFlag(true);reply.put("studioInput")->setFlag(svc&&svc->development); }
            putText(reply, "angelscript", ScriptHost::version());
            putNumber(reply, "id", id);
            send(reply.write(true));
        }
        else if(name=="liveInfo"||name=="liveBegin"||name=="liveChunk"||name=="liveCommit"||name=="liveCancel")
        {
            liveCommand(id,name,ask);
        }
        else if (name == "launchTitle")
        {
            const auto* token=ask.find("token");const auto* folder=ask.find("folder");
            const std::string supplied=token?token->text():"";
            const auto image=folder?studioLaunchPath(folder->text()):std::string();
            unsigned difference=0;
            if(supplied.size()==32&&m_launchKey.size()==32)
                for(size_t i=0;i<32;++i)difference|=static_cast<unsigned>(supplied[i]^m_launchKey[i]);
            else difference=1;
            const bool allowed=!difference&&!m_paused&&m_pendingLaunch.empty()&&!image.empty()&&consoleAvailable();
            if(allowed)m_pendingLaunch=image;
            JsonValue reply;putNumber(reply,"id",id);reply.put("launchAccepted")->setFlag(allowed);
            putText(reply,"message",allowed?"Launch accepted; reconnect after the title starts.":"Launch refused: running title, valid key and Games directory required.");
            send(reply.write(true));
        }
        else if (name == "trace")
        {
            resetRemoteTrace();
            const auto* on=ask.find("on");m_remoteTrace=on&&on->flag();
            JsonValue reply;putNumber(reply,"id",id);reply.put("tracing")->setFlag(m_remoteTrace);send(reply.write(true));
        }
        else if(name=="traceDrain")
        {
            drainRemoteTrace(id);
        }
        else if (name == "telemetry")
        {
            // Read-only snapshot, requested at a low cadence by a connected
            // development client. No script execution or GPU readback.
            JsonValue reply;putNumber(reply,"id",id);
            auto* t=reply.put("telemetry");
            const auto* s=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
            if(s&&s->platform)
            {
                putText(*t,"platform",s->platform->name());
                putNumber(*t,"physicalBytes",static_cast<double>(s->platform->totalMemory()));
                putNumber(*t,"freeBytes",static_cast<double>(s->platform->freeMemory()));
                putNumber(*t,"fps",s->fps);putNumber(*t,"draws",s->draws);
                if(s->frameTimingValid){putNumber(*t,"updateMs",s->updateMs);putNumber(*t,"submitMs",s->submitMs);}
                if(s->audio)putNumber(*t,"voices",s->audio->voicesInUse());
            }
            const auto total=memStatsTotal();
            putNumber(*t,"trackedBytes",static_cast<double>(total.bytes));
            putNumber(*t,"peakBytes",static_cast<double>(total.peak));
            putNumber(*t,"textureBytes",static_cast<double>(memStats(MemTag::Texture).bytes));
            putNumber(*t,"bufferBytes",static_cast<double>(memStats(MemTag::Buffer).bytes));
            putNumber(*t,"sceneBytes",static_cast<double>(memStats(MemTag::Scene).bytes));
            putNumber(*t,"scriptBytes",static_cast<double>(memStats(MemTag::Script).bytes));
            putNumber(*t,"audioBytes",static_cast<double>(memStats(MemTag::Audio).bytes));
            putNumber(*t,"uiBytes",static_cast<double>(memStats(MemTag::UI).bytes));
            send(reply.write(true));
        }
        else if (name == "pause")
        {
            // Also just after being told to go on, in the same breath.
            if (!m_paused || m_resume)
            {
                m_mode = Mode::Pause;
            }
        }
        else if (name == "continue" || name == "next" || name == "stepIn" || name == "stepOut")
        {
            if (m_paused)
            {
                resume(name == "continue" ? Mode::Run : name == "next" ? Mode::StepOver : name == "stepIn" ? Mode::StepIn : Mode::StepOut);
            }
        }
        else if (name == "stack")
        {
            stack(id);
        }
        else if (name == "variables")
        {
            variables(id, ask);
        }
        else if (name == "evaluate")
        {
            evaluate(id, ask);
        }
        else if (name == "profile")
        {
            const JsonValue* on = ask.find("on");
            profile(id, on && on->flag());
        }
        else if(name=="studioInput")
        {
            JsonValue reply;putNumber(reply,"id",id);
            const auto* svc=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
            const auto* token=ask.find("token");const std::string supplied=token?token->text():"";
            unsigned difference=0;
            if(supplied.size()==32&&m_launchKey.size()==32)
                for(size_t i=0;i<32;++i)difference|=static_cast<unsigned>(supplied[i]^m_launchKey[i]);
            else difference=1;
            u32 control=0;bool ok=false;const char* message="Remote input requires the installed development key and a running title.";
            if(!difference&&svc&&svc->development&&svc->platform&&m_actorFrame&&!m_paused&&!m_liveApplying&&!m_liveUpload.ready&&m_pendingLaunch.empty())
            {
                bool free=false;for(const auto& pad:svc->platform->input().pads)if(!pad.connected){free=true;break;}
                if(!free)message="All controller slots are occupied; physical pads take priority.";
                else if(!debuggerUnsigned(ask.find("control"),control)||control>=studioPadCount)message="Unsupported controller input.";
                else if(!m_remotePad.start(control,svc->platform->seconds()))message="Previous pulse is still active.";
                else {ok=true;message="Controller pulse accepted (150 ms).";}
            }
            reply.put("inputOk")->setFlag(ok);putText(reply,"message",message);send(reply.write(true));
        }
        else if(name=="studioWatches")
        {
            JsonValue reply;putNumber(reply,"id",id);auto* list=reply.put("watches");list->setArray();
            const auto* names=ask.find("names");
            for(u32 i=0;m_paused&&m_stopped&&names&&i<names->count()&&i<12;++i)
            {
                const auto* item=names->at(i);if(!item||item->type()!=JsonValue::Type::String)continue;
                const auto& label=item->text();if(label.empty()||label[0]=='!'||!validBreakpointCondition(label))continue;
                auto* row=list->add();int type=0;void* address=nullptr;
                if(!readDebuggerValue(m_stopped,label,type,address)||!address)
                {putText(*row,"name",label);putText(*row,"value","out of scope");continue;}
                if(type==m_stringType||(type>=asTYPEID_BOOL&&type<=asTYPEID_DOUBLE))
                    describe(*row,label.c_str(),type,address);
                else
                {putText(*row,"name",label);putText(*row,"value","object (not evaluated)");}
            }
            send(reply.write(true));
        }
        else if(name=="studioPins"){studioPins(id);}
        else if(name=="studioPreview"){studioPreview(id);}
        else if(name=="studioActors"||name=="studioActor"||name=="studioActorSet")
        {
            studioActors(id,name,ask);
        }
        else if (name == "entities")
        {
            entities(id);
        }
        else if (name == "entity")
        {
            entity(id, ask);
        }
        else if (name == "setEntity")
        {
            setEntity(id, ask);
        }
    }

    int ScriptDebugger::fileOf(const char* section)
    {
        for (const auto& known : m_files)
        {
            if (known.first == section)
            {
                return known.second;
            }
        }
        const char* path = m_host->sourceOf(section);
        int found = -1;
        for (size_t i = 0; i < m_breakpoints.size(); i++)
        {
            found = samePath(m_breakpoints[i].file, path) ? static_cast<int>(i) : found;
        }
        m_files.emplace_back(section, found);
        return found;
    }

#ifdef NX_DESKTOP_BLUEPRINT
    bool ScriptDebugger::startLocalTrace(ScriptHost& host,TraceCallback callback,void* user)
    {
        if(m_host||m_localHost||!callback)return false;
        m_localHost=&host;m_trace=callback;m_traceUser=user;
        m_stringType=host.engine()->GetTypeIdByDecl("string");
        host.setWatcher(this);return true;
    }

    void ScriptDebugger::stopLocalTrace()
    {
        if(!m_localHost)return;
        m_localHost->setWatcher(nullptr);
        m_localHost=nullptr;m_trace=nullptr;m_traceUser=nullptr;
    }

    void ScriptDebugger::traceValues(asIScriptContext* context,std::vector<std::string>& values)
    {
        values.clear();if(!m_localHost||!context)return;
        // Reuse the native debugger's scalar formatter. Never run property getters,
        // retain VM pointers, traverse object graphs or evaluate user expressions here.
        auto add=[&](const char* name,int type,void* address)
        {
            if(!name||!*name||!address||values.size()>=24)return;
            if(type!=m_stringType&&(type<asTYPEID_BOOL||type>asTYPEID_DOUBLE))return;
            JsonValue item;describe(item,name,type,address);
            const auto* value=item.find("value");if(!value)return;
            std::string display=name;
            if(display.rfind("bp_var_",0)==0)display=display.substr(7);
            else if(display.rfind("bp_arg_",0)==0)display=display.substr(7);
            else if(display.rfind("bp_global_",0)==0)display="global "+display.substr(10);
            display+=" = "+value->text();
            for(char& c:display)if(static_cast<unsigned char>(c)<32)c=' ';
            if(display.size()>512)display.resize(512);
            values.push_back(display);
        };
        auto* function=context->GetFunction(0);auto* module=function?function->GetModule():nullptr;
        for(asUINT i=0;module&&i<module->GetGlobalVarCount()&&values.size()<8;++i)
        {
            const char* name=nullptr;int type=0;
            if(module->GetGlobalVar(i,&name,nullptr,&type)>=0)add(name,type,module->GetAddressOfGlobalVar(i));
        }
        if(void* self=context->GetThisPointer(0))
        {
            if(context->GetThisTypeId(0)&asTYPEID_SCRIPTOBJECT)
            {
                auto* object=static_cast<asIScriptObject*>(self);
                for(asUINT i=0;i<object->GetPropertyCount()&&values.size()<16;++i)
                    add(object->GetPropertyName(i),object->GetPropertyTypeId(i),object->GetAddressOfProperty(i));
            }
        }
        for(int i=0;i<context->GetVarCount(0)&&values.size()<24;++i)
        {
            const char* name=nullptr;int type=0;
            if(context->IsVarInScope(i,0)&&context->GetVar(i,0,&name,&type)>=0)
                add(name,type,context->GetAddressOfVar(i,0));
        }
    }
    void ScriptDebugger::traceStack(asIScriptContext* context,std::vector<std::string>& frames)
    {
        frames.clear();if(!m_localHost||!context)return;
        for(asUINT i=0;i<context->GetCallstackSize()&&frames.size()<12;++i)
        {
            auto* function=context->GetFunction(i);if(!function)continue;
            const char* section=nullptr;const int line=context->GetLineNumber(i,nullptr,&section);
            std::string frame=function->GetDeclaration(true,true,false);
            frame+=" : "+std::to_string(line);
            if(frame.size()>256)frame.resize(256);
            frames.push_back(std::move(frame));
        }
    }
#endif

    void ScriptDebugger::resetRemoteTrace()
    {
        m_remoteTrace=false;m_remoteTraceGap=false;m_remoteTraceHits.clear();
    }
    void ScriptDebugger::liveCommand(int id,const std::string& name,const JsonValue& ask)
    {
        JsonValue reply;putNumber(reply,"id",id);
        const auto* s=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
        const auto text=[&](const char* key){const auto* item=ask.find(key);return item?item->text():std::string();};
        const auto number=[&](const char* key){const auto* item=ask.find(key);return item?item->number():-1.0;};
        bool ok=false;std::string message;
        if(m_launchKey.empty()||!s||!s->development||!s->screens||m_liveApplying)
        {message="Live Link is not enabled on this title.";}
        else if(name=="liveInfo")
        {
            putText(reply,"screen",s->screens->current());putText(reply,"fingerprint",s->screens->liveFingerprint());
            putNumber(reply,"limit",static_cast<double>(StudioLiveSource::limit));ok=true;
        }
        else if(m_paused||m_mode!=Mode::Run||!m_pendingLaunch.empty())
        {m_liveUpload.clear();message="Resume the title before updating.";}
        else if(name=="liveBegin")
        {
            const auto token=text("token");const auto total=number("size");
            unsigned diff=0;
            if(token.size()==32&&m_launchKey.size()==32)for(size_t i=0;i<32;++i)diff|=static_cast<unsigned>(token[i]^m_launchKey[i]);else diff=1;
            const auto expected=text("expected");const auto screen=text("screen");
            if(diff||screen!=s->screens->current()||expected!=s->screens->liveFingerprint()||
                total<1||total>StudioLiveSource::limit||total!=static_cast<size_t>(total))
            {m_liveUpload.clear();message="Installation identity, active screen, or source did not match.";}
            else {ok=m_liveUpload.begin(screen,expected,text("fingerprint"),static_cast<size_t>(total));message=ok?"Upload started.":"Invalid upload metadata.";}
        }
        else if(name=="liveChunk")
        {
            const double offset=number("offset");
            ok=offset>=0&&offset<=StudioLiveSource::limit&&offset==static_cast<size_t>(offset)&&m_liveUpload.append(static_cast<size_t>(offset),text("hex"));
            if(!ok)m_liveUpload.clear();
            putNumber(reply,"received",static_cast<double>(m_liveUpload.source.size()));message=ok?"Chunk received.":"Invalid upload chunk.";
        }
        else if(name=="liveCommit")
        {
            if(m_liveUpload.commit()){m_liveCommitId=id;return;}
            message="Incomplete or mismatched upload.";
        }
        else if(name=="liveCancel"){m_liveUpload.clear();ok=true;message="Upload cancelled.";}
        reply.put("liveOk")->setFlag(ok);putText(reply,"message",message);send(reply.write(true));
    }
    void ScriptDebugger::applyLiveUpload()
    {
        auto* s=m_host&&m_host->engine()?static_cast<Services*>(m_host->engine()->GetUserData()):nullptr;
        m_liveApplying=true;
        // Reload may call onExit/onEnter; no suspended VM inside the transaction.
        m_remotePad.clear();m_breakpoints.clear();m_files.clear();m_last=Where();resetRemoteTrace();
        std::string error;
        const bool ok=s&&s->screens&&s->screens->applyLiveSource(m_liveUpload.screen,m_liveUpload.expected,m_liveUpload.source,error);
        m_liveApplying=false;m_liveUpload.clear();
        JsonValue reply;putNumber(reply,"id",m_liveCommitId);reply.put("liveOk")->setFlag(ok);
        putText(reply,"message",ok?"Active screen reloaded from memory; installed files unchanged.":error);
        if(s&&s->screens){putText(reply,"screen",s->screens->current());putText(reply,"fingerprint",s->screens->liveFingerprint());}
        send(reply.write(true));flush();
    }
    void ScriptDebugger::recordRemoteTrace(const Where& here)
    {
        // Drop the remainder of a busy interval after 32 transitions.
        // No JSON generation or network sends in the script line callback.
        if(m_remoteTraceHits.size()>=kRemoteTraceLimit){m_remoteTraceGap=true;return;}
        const auto* function=here.context->GetFunction(0);
        const char* identity=function?m_host->compiledFingerprint(function->GetModuleName(),here.section):"";
        const char* file=m_host->sourceOf(here.section);
        // Unmapped helper calls are explicit gaps, not inferred graph edges.
        if(!*identity||!file||strlen(file)>192||here.line<=0)
        {m_remoteTraceGap=true;return;}
        m_remoteTraceHits.push_back({file,identity,here.run,here.depth,here.line});
    }
    void ScriptDebugger::drainRemoteTrace(int id)
    {
        JsonValue reply;putNumber(reply,"id",id);
        reply.put("gap")->setFlag(m_remoteTraceGap);
        auto* hits=reply.put("trace");hits->setArray();
        for(const auto& hit:m_remoteTraceHits)
        {
            auto* item=hits->add();putText(*item,"file",hit.file);putText(*item,"fingerprint",hit.fingerprint);
            putNumber(*item,"run",hit.run);putNumber(*item,"depth",hit.depth);putNumber(*item,"line",hit.line);
        }
        m_remoteTraceHits.clear();m_remoteTraceGap=false;send(reply.write(true));
    }
    void ScriptDebugger::line(asIScriptContext* context)
    {
        if(m_liveApplying)return;
#ifdef NX_DESKTOP_BLUEPRINT
        if(m_localHost&&m_trace)
        {
            const char* section=nullptr;
            const int line=context->GetLineNumber(0,nullptr,&section);
            if(section&&line>0)m_trace(m_traceUser,section,line,m_localHost->runs(),context);
            return;
        }
#endif
        if (m_paused || m_client.state() != TcpState::Open)
        {
            return;
        }
        // Now and then, so that "pause" reaches a script that never ends its frame.
        if ((++m_tick & 4095) == 0)
        {
            pump();
        }
        // A few statements at a time, so reading the clock costs little.
        if (m_profiling && (++m_profileTick & 15) == 0)
        {
            sample(context);
        }
        if (m_mode == Mode::Run && m_breakpoints.empty() && !m_remoteTrace)
        {
            return;
        }
        Where here;
        here.context = context;
        here.depth = context->GetCallstackSize();
        here.line = context->GetLineNumber(0, nullptr, &here.section);
        here.run = m_host->runs();
        // A line with several statements is stopped at once; so is one that is come back to in a loop.
        const bool moved = here.line != m_last.line || here.section != m_last.section || here.depth != m_last.depth || here.context != m_last.context || here.run != m_last.run;
        if(m_remoteTrace&&(moved||here.run!=m_last.run))recordRemoteTrace(here);
        m_last = here;
        const bool fresh = here.line != m_from.line || here.section != m_from.section || here.depth != m_from.depth || here.context != m_from.context || here.run != m_from.run;
        // Past the end of what was stopped in, into the next call the engine makes.
        const bool later = here.context != m_from.context || here.run != m_from.run;
        switch (m_mode)
        {
        case Mode::Pause:
            stopHere(context, "pause", nullptr);
            return;
        case Mode::StepIn:
            if (fresh)
            {
                stopHere(context, "step", nullptr);
                return;
            }
            break;
        case Mode::StepOver:
            if (fresh && (later || here.depth <= m_from.depth))
            {
                stopHere(context, "step", nullptr);
                return;
            }
            break;
        case Mode::StepOut:
            if (here.depth < m_from.depth || (later && here.depth <= m_from.depth))
            {
                stopHere(context, "step", nullptr);
                return;
            }
            break;
        case Mode::Run:
            break;
        }
        if (moved && !m_breakpoints.empty())
        {
            const int file = fileOf(here.section);
            if (file >= 0)
            {
                for (int at : m_breakpoints[file].lines)
                {
                    if (at == here.line)
                    {
                        const auto& expected=m_breakpoints[file].fingerprint;
                        if(!expected.empty())
                        {
                            const auto* function=context->GetFunction(0);
                            if(!function||expected!=m_host->compiledFingerprint(function->GetModuleName(),here.section))continue;
                        }
                        const auto& conditions=m_breakpoints[file].conditions;
                        const auto condition=std::find_if(conditions.begin(),conditions.end(),[&](const auto& value){return value.first==at;});
                        if(condition!=conditions.end()&&!breakpointConditionMatches(context,condition->second))continue;
                        stopHere(context, "breakpoint", nullptr);
                        return;
                    }
                }
            }
        }
    }

    void ScriptDebugger::exception(asIScriptContext* context)
    {
        if (!m_liveApplying && !m_paused && m_client.state() == TcpState::Open)
        {
            stopHere(context, "exception", context->GetExceptionString());
        }
    }

    void ScriptDebugger::stopHere(asIScriptContext* context, const char* reason, const char* text)
    {
        m_remotePad.clear();
        m_paused = true;
        m_resume = false;
        m_stopped = context;
        forget();
        m_from.context = context;
        m_from.depth = context->GetCallstackSize();
        m_from.line = context->GetLineNumber(0, nullptr, &m_from.section);
        m_from.run = m_host->runs();
        JsonValue event;
        putText(event, "event", "stopped");
        putText(event, "reason", reason);
        if (text)
        {
            putText(event, "text", text);
        }
        send(event.write(true));
        while (!m_resume && m_host)
        {
            pump();
            if (m_idle)
            {
                m_idle(m_idleUser);
            }
            sleepMilliseconds(5);
        }
        m_paused = false;
        m_stopped = nullptr;
        forget();
    }

    void ScriptDebugger::forget()
    {
        m_refs.clear();
        for (asIScriptContext* context : m_temporaries)
        {
            context->Release();
        }
        m_temporaries.clear();
    }

    bool ScriptDebugger::callGetter(void* object, asIScriptFunction* getter, Ref& out)
    {
        asIScriptContext* context = m_host->engine()->CreateContext();
        if (context == nullptr)
        {
            return false;
        }
        if (context->Prepare(getter) < 0 || context->SetObject(object) < 0 || context->Execute() != asEXECUTION_FINISHED)
        {
            context->Release();
            return false;
        }
        asDWORD flags = 0;
        const int typeId = getter->GetReturnTypeId(&flags);
        void* at = (flags & asTM_INOUTREF) ? context->GetReturnAddress() : context->GetAddressOfReturnValue();
        m_temporaries.push_back(context);
        out = { at, typeId };
        return true;
    }

    void ScriptDebugger::resume(Mode mode)
    {
        m_mode = mode;
        m_resume = true;
        m_last = m_from;
        JsonValue event;
        putText(event, "event", "continued");
        send(event.write(true));
    }

    void ScriptDebugger::stack(int id)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        JsonValue* frames = reply.put("frames");
        frames->setArray();
        for (u32 i = 0; m_stopped && i < m_stopped->GetCallstackSize(); i++)
        {
            asIScriptFunction* function = m_stopped->GetFunction(i);
            if (function == nullptr)
            {
                continue;  // where one call waits for another the engine made
            }
            const char* section = nullptr;
            int column = 0;
            const int line = m_stopped->GetLineNumber(i, &column, &section);
            JsonValue* frame = frames->add();
            putNumber(*frame, "frame", i);
            putText(*frame, "name", function->GetDeclaration(true, true, false));
            putText(*frame, "file", m_host->sourceOf(section));
            putText(*frame,"fingerprint",m_host->compiledFingerprint(function->GetModuleName(),section));
            putNumber(*frame, "line", line);
            putNumber(*frame, "column", column);
        }
        send(reply.write(true));
    }

    bool ScriptDebugger::describe(JsonValue& out, const char* name, int typeId, void* address)
    {
        asIScriptEngine* engine = m_host ? m_host->engine()
#ifdef NX_DESKTOP_BLUEPRINT
            : m_localHost->engine()
#else
            : nullptr
#endif
            ;
        putText(out, "name", name);
        asITypeInfo* type = engine->GetTypeInfoById(typeId);
        const char* declaration = engine->GetTypeDeclaration(typeId, true);
        putText(out, "type", declaration ? declaration : "?");
        std::string value;
        bool expandable = false;
        if (address == nullptr)
        {
            value = "(not made yet)";
        }
        else if (typeId & asTYPEID_OBJHANDLE)
        {
            void* object = *static_cast<void**>(address);
            if (object == nullptr)
            {
                value = "null";
            }
            else
            {
                return describe(out, name, typeId & ~asTYPEID_OBJHANDLE, object);
            }
        }
        else
        {
            switch (typeId)
            {
            case asTYPEID_BOOL: value = *static_cast<bool*>(address) ? "true" : "false"; break;
            case asTYPEID_INT8: value = formatNumber(*static_cast<int8_t*>(address)); break;
            case asTYPEID_INT16: value = formatNumber(*static_cast<i16*>(address)); break;
            case asTYPEID_INT32: value = formatNumber(*static_cast<i32*>(address)); break;
            case asTYPEID_INT64: value = formatNumber(static_cast<double>(*static_cast<i64*>(address))); break;
            case asTYPEID_UINT8: value = formatNumber(*static_cast<u8*>(address)); break;
            case asTYPEID_UINT16: value = formatNumber(*static_cast<u16*>(address)); break;
            case asTYPEID_UINT32: value = formatNumber(*static_cast<u32*>(address)); break;
            case asTYPEID_UINT64: value = formatNumber(static_cast<double>(*static_cast<u64*>(address))); break;
            case asTYPEID_FLOAT: value = formatNumber(*static_cast<float*>(address)); break;
            case asTYPEID_DOUBLE: value = formatNumber(*static_cast<double*>(address)); break;
            default:
                if (typeId == m_stringType)
                {
                    const std::string& text = *static_cast<std::string*>(address);
                    value = "\"" + (text.size() > kLongestText ? text.substr(0, kLongestText) + "..." : text) + "\"";
                }
                else if (type && (type->GetFlags() & asOBJ_ENUM))
                {
                    const i32 number = *static_cast<i32*>(address);
                    value = formatNumber(number);
                    for (asUINT i = 0; i < type->GetEnumValueCount(); i++)
                    {
                        asINT64 v = 0;
                        const char* label = type->GetEnumValueByIndex(i, &v);
                        if (v == number)
                        {
                            value = std::string(type->GetName()) + "::" + label;
                            break;
                        }
                    }
                }
                else if (type && strcmp(type->GetName(), "array") == 0)
                {
                    const CScriptArray* array = static_cast<const CScriptArray*>(address);
                    value = std::string(declaration ? declaration : "array") + " (" + formatNumber(array->GetSize()) + ")";
                    expandable = array->GetSize() > 0;
                }
                else if (type && m_summaries > 0)
                {
                    // Inside a summary, only what it is: objects may hold each other.
                    value = type->GetName();
                    expandable = true;
                }
                else if (type)
                {
                    // A few of what it holds, as a summary: { x: 1, y: 2 }. Nothing in it is kept to be opened.
                    Ref self { address, typeId };
                    JsonValue parts;
                    const size_t refs = m_refs.size();
                    m_summaries++;
                    children(parts, self);
                    m_summaries--;
                    m_refs.resize(refs);
                    value = "{";
                    for (u32 i = 0; i < parts.count() && i < 4; i++)
                    {
                        const JsonValue* part = parts.at(i);
                        value += (i ? ", " : " ") + part->find("name")->text() + ": " + part->find("value")->text();
                    }
                    value += parts.count() > 4 ? ", ... }" : parts.count() ? " }" : "}";
                    if (parts.count() == 0)
                    {
                        value = type->GetName();
                    }
                    expandable = parts.count() > 0;
                }
                else
                {
                    value = "?";
                }
            }
        }
        putText(out, "value", value);
        if (expandable)
        {
            m_refs.push_back({ address, typeId });
            putNumber(out, "ref", static_cast<double>(m_refs.size()));
        }
        return true;
    }

    void ScriptDebugger::children(JsonValue& out, const Ref& of)
    {
        out.setArray();
        asIScriptEngine* engine = m_host->engine();
        asITypeInfo* type = engine->GetTypeInfoById(of.typeId);
        if (type == nullptr || of.address == nullptr)
        {
            return;
        }
        if (strcmp(type->GetName(), "array") == 0)
        {
            CScriptArray* array = static_cast<CScriptArray*>(of.address);
            for (asUINT i = 0; i < array->GetSize() && i < kMostChildren; i++)
            {
                char label[16];
                snprintf(label, sizeof(label), "[%u]", static_cast<unsigned>(i));
                describe(*out.add(), label, array->GetElementTypeId(), array->At(i));
            }
        }
        else if (of.typeId & asTYPEID_SCRIPTOBJECT)
        {
            asIScriptObject* object = static_cast<asIScriptObject*>(of.address);
            for (asUINT i = 0; i < object->GetPropertyCount() && i < kMostChildren; i++)
            {
                describe(*out.add(), object->GetPropertyName(i), object->GetPropertyTypeId(i), object->GetAddressOfProperty(i));
            }
        }
        else
        {
            for (asUINT i = 0; i < type->GetPropertyCount() && i < kMostChildren; i++)
            {
                const char* label = nullptr;
                int typeId = 0, offset = 0;
                bool isReference = false;
                type->GetProperty(i, &label, &typeId, nullptr, nullptr, &offset, &isReference);
                void* at = static_cast<char*>(of.address) + offset;
                describe(*out.add(), label, typeId, isReference ? *static_cast<void**>(at) : at);
            }
            for (asUINT i = 0; i < type->GetMethodCount() && out.count() < kMostChildren; i++)
            {
                asIScriptFunction* method = type->GetMethodByIndex(i);
                Ref got {};
                if (isGetter(method) && callGetter(of.address, method, got))
                {
                    describe(*out.add(), method->GetName() + 4, got.typeId, got.address);
                }
            }
        }
    }

    void ScriptDebugger::studioPreview(int id)
    {
        JsonValue reply;putNumber(reply,"id",id);
        auto* svc=m_host&&m_host->engine()?static_cast<Services*>(m_host->engine()->GetUserData()):nullptr;
        const auto reject=[&](const char* message){reply.put("previewOk")->setFlag(false);putText(reply,"message",message);send(reply.write(true));};
        if(!svc||!svc->development||!svc->device||!svc->platform||!svc->device->previewCaptureSupported())
        {reject("Preview capture unavailable on this runtime.");return;}
        if(!m_actorFrame||m_paused||m_liveApplying||!m_pendingLaunch.empty()){reject("Preview only at a running frame boundary.");return;}
        const double now=svc->platform->seconds();
        if(now-m_previewAt<0.9||m_out.size()>8192){reject("Preview rate limited.");return;}
        m_previewAt=now;
        u32 sw=0,sh=0;svc->device->size(sw,sh);
        if(!sw||!sh||sw>2048||sh>2048){reject("Invalid framebuffer size.");return;}
        u32 w=kStudioPreviewWidth,h=sh*w/sw;
        if(h>kStudioPreviewHeight){h=kStudioPreviewHeight;w=sw*h/sh;}
        if(!studioPreviewSize(w,h)){reject("Invalid preview dimensions.");return;}
        // 24 KiB maximum pixel buffer, never a full framebuffer allocation.
        const size_t bytes=static_cast<size_t>(w)*h*sizeof(u16);
        auto* pixels=static_cast<u16*>(memAlloc(bytes,MemTag::General));
        if(!pixels){reject("Preview allocation failed.");return;}
        const bool ok=svc->device->capturePreview(pixels,w,h);
        const auto encoded=ok?studioPreviewEncode(pixels,w,h):std::string();
        memFree(pixels);
        if(encoded.empty()){reject("Front buffer unavailable.");return;}
        reply.put("previewOk")->setFlag(true);putNumber(reply,"width",w);putNumber(reply,"height",h);
        putText(reply,"pixels",encoded);send(reply.write(true));
    }

    void ScriptDebugger::studioPins(int id)
    {
        JsonValue reply;putNumber(reply,"id",id);
        auto* list=reply.put("pins");list->setArray();
        if(m_paused&&m_stopped&&m_stopped->GetCallstackSize())
        {
            const char* section=nullptr;
            const int line=m_stopped->GetLineNumber(0,nullptr,&section);
            auto* function=m_stopped->GetFunction(0);
            if(function&&section)
            {
                putText(reply,"file",m_host->sourceOf(section));
                putText(reply,"fingerprint",m_host->compiledFingerprint(function->GetModuleName(),section));
                putNumber(reply,"line",line);
                const int count=m_stopped->GetVarCount(0);
                bool capped=false;
                // The nearest in-scope local wins. Never evaluate a function or
                // call a getter just to populate a graph pin.
                std::vector<std::string> seen;
                for(int i=count-1;i>=0;--i)
                {
                    const char* label=nullptr;int typeId=0;
                    if(!m_stopped->IsVarInScope(i,0)||m_stopped->GetVar(i,0,&label,&typeId)<0||!label||strncmp(label,"bp_",3)!=0)continue;
                    const std::string name=label;
                    if(name.size()>96||std::find(seen.begin(),seen.end(),name)!=seen.end())continue;
                    if(list->count()>=64){capped=true;break;}
                    seen.push_back(name);
                    auto* address=m_stopped->GetAddressOfVar(i,0);
                    if(!address)continue;
                    const auto* type=m_host->engine()->GetTypeInfoById(typeId);
                    const auto* declaration=m_host->engine()->GetTypeDeclaration(typeId,true);
                    if(typeId&asTYPEID_OBJHANDLE)
                    {
                        auto* item=list->add();putText(*item,"name",name);putText(*item,"type",declaration?declaration:"?");
                        putText(*item,"value",*static_cast<void**>(address)?"(object)":"null");
                    }
                    else if(!type||typeId==m_stringType||(type->GetFlags()&asOBJ_ENUM))
                    {
                        if(typeId==asTYPEID_VOID)continue;
                        describe(*list->add(),label,typeId,address);
                    }
                    else
                    {
                        auto* item=list->add();putText(*item,"name",name);putText(*item,"type",declaration?declaration:"?");
                        putText(*item,"value","(value object)");
                    }
                }
                reply.put("capped")->setFlag(capped);
            }
        }
        send(reply.write(true));
    }

    void ScriptDebugger::variables(int id, const JsonValue& ask)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        JsonValue* list = reply.put("variables");
        list->setArray();
        const JsonValue* ref = ask.find("ref");
        const JsonValue* scope = ask.find("scope");
        const JsonValue* frameValue = ask.find("frame");
        u32 frame=0;
        if(frameValue&&!debuggerUnsigned(frameValue,frame))frame=0xffffffffu;
        const auto* requestedLimit=ask.find("limit");
        const double limitValue=requestedLimit?requestedLimit->number():0;
        const u32 limit=limitValue>=1&&limitValue<=100?static_cast<u32>(limitValue):0;
        if (m_stopped == nullptr)
        {
        }
        else if (ref)
        {
            u32 at=0;
            if (debuggerUnsigned(ref,at) && at >= 1 && at <= m_refs.size())
            {
                const Ref of = m_refs[at - 1];
                children(*list, of);
            }
        }
        else if (frame < m_stopped->GetCallstackSize() && scope && scope->text() == "globals")
        {
            asIScriptFunction* function = m_stopped->GetFunction(frame);
            asIScriptModule* module = function ? function->GetModule() : nullptr;
            for (asUINT i = 0; module && i < module->GetGlobalVarCount(); i++)
            {
                const char* name = nullptr;
                const char* space = nullptr;
                int typeId = 0;
                module->GetGlobalVar(i, &name, &space, &typeId);
                const std::string full = space && *space ? std::string(space) + "::" + name : std::string(name);
                describe(*list->add(), full.c_str(), typeId, module->GetAddressOfGlobalVar(i));
            }
        }
        else if (frame < m_stopped->GetCallstackSize())
        {
            if (void* self = m_stopped->GetThisPointer(frame))
            {
                describe(*list->add(), "this", m_stopped->GetThisTypeId(frame), self);
            }
            const int count = m_stopped->GetVarCount(frame);
            for (int i = 0; i < count && (!limit || list->count()<limit); i++)
            {
                const char* name = nullptr;
                int typeId = 0;
                if (!m_stopped->IsVarInScope(i, frame) || m_stopped->GetVar(i, frame, &name, &typeId) < 0 || name == nullptr || !*name)
                {
                    continue;
                }
                describe(*list->add(), name, typeId, m_stopped->GetAddressOfVar(i, frame));
            }
        }
        send(reply.write(true));
    }

    bool ScriptDebugger::child(const Ref& of, const std::string& name, Ref& out)
    {
        asIScriptEngine* engine = m_host->engine();
        Ref from = of;
        if (from.typeId & asTYPEID_OBJHANDLE)
        {
            from.address = from.address ? *static_cast<void**>(from.address) : nullptr;
            from.typeId &= ~asTYPEID_OBJHANDLE;
        }
        asITypeInfo* type = engine->GetTypeInfoById(from.typeId);
        if (type == nullptr || from.address == nullptr)
        {
            return false;
        }
        if (strcmp(type->GetName(), "array") == 0)
        {
            CScriptArray* array = static_cast<CScriptArray*>(from.address);
            char* end = nullptr;
            const unsigned long index = strtoul(name.c_str(), &end, 10);
            if (end == name.c_str() || *end || index >= array->GetSize())
            {
                return false;
            }
            out = { array->At(static_cast<asUINT>(index)), array->GetElementTypeId() };
            return true;
        }
        if (from.typeId & asTYPEID_SCRIPTOBJECT)
        {
            asIScriptObject* object = static_cast<asIScriptObject*>(from.address);
            for (asUINT i = 0; i < object->GetPropertyCount(); i++)
            {
                if (name == object->GetPropertyName(i))
                {
                    out = { object->GetAddressOfProperty(i), object->GetPropertyTypeId(i) };
                    return true;
                }
            }
            return false;
        }
        for (asUINT i = 0; i < type->GetPropertyCount(); i++)
        {
            const char* label = nullptr;
            int typeId = 0, offset = 0;
            bool isReference = false;
            type->GetProperty(i, &label, &typeId, nullptr, nullptr, &offset, &isReference);
            if (name == label)
            {
                void* at = static_cast<char*>(from.address) + offset;
                out = { isReference ? *static_cast<void**>(at) : at, typeId };
                return true;
            }
        }
        const std::string getter = "get_" + name;
        for (asUINT i = 0; i < type->GetMethodCount(); i++)
        {
            asIScriptFunction* method = type->GetMethodByIndex(i);
            if (isGetter(method) && getter == method->GetName())
            {
                return callGetter(from.address, method, out);
            }
        }
        return false;
    }

    bool ScriptDebugger::lookUp(u32 frame, const std::string& name, Ref& out)
    {
        // What is nearest first: a local, then the object's own, then the module's.
        const int count = m_stopped->GetVarCount(frame);
        for (int i = count - 1; i >= 0; i--)
        {
            const char* label = nullptr;
            int typeId = 0;
            if (m_stopped->IsVarInScope(i, frame) && m_stopped->GetVar(i, frame, &label, &typeId) >= 0 && label && name == label)
            {
                out = { m_stopped->GetAddressOfVar(i, frame), typeId };
                return true;
            }
        }
        if (void* self = m_stopped->GetThisPointer(frame))
        {
            const Ref me { self, m_stopped->GetThisTypeId(frame) };
            if (name == "this")
            {
                out = me;
                return true;
            }
            if (child(me, name, out))
            {
                return true;
            }
        }
        asIScriptFunction* function = m_stopped->GetFunction(frame);
        asIScriptModule* module = function ? function->GetModule() : nullptr;
        for (asUINT i = 0; module && i < module->GetGlobalVarCount(); i++)
        {
            const char* label = nullptr;
            int typeId = 0;
            module->GetGlobalVar(i, &label, nullptr, &typeId);
            if (label && name == label)
            {
                out = { module->GetAddressOfGlobalVar(i), typeId };
                return true;
            }
        }
        return false;
    }

    void ScriptDebugger::evaluate(int id, const JsonValue& ask)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        const JsonValue* expr = ask.find("expr");
        const JsonValue* frameValue = ask.find("frame");
        u32 frame=0;
        if(frameValue&&!debuggerUnsigned(frameValue,frame))frame=0xffffffffu;
        // Names joined by dots, and [n] into arrays: no more than that is worked out.
        std::string text = expr ? expr->text() : std::string();
        std::vector<std::string> names;
        std::string part;
        for (char c : text)
        {
            if (c == '.' || c == '[' || c == ']')
            {
                if (!part.empty())
                {
                    names.push_back(part);
                }
                part.clear();
            }
            else if (c != ' ')
            {
                part += c;
            }
        }
        if (!part.empty())
        {
            names.push_back(part);
        }
        Ref at {};
        bool found = m_stopped && frame < m_stopped->GetCallstackSize() && !names.empty() && lookUp(frame, names[0], at);
        for (size_t i = 1; found && i < names.size(); i++)
        {
            Ref next {};
            found = child(at, names[i], next);
            at = next;
        }
        if (found)
        {
            describe(*reply.put("result"), text.c_str(), at.typeId, at.address);
        }
        else
        {
            putText(reply, "error", "not known here");
        }
        send(reply.write(true));
    }
    // --- Entities ---

    namespace
    {
        std::string joined(const float* values, u32 count)
        {
            std::string out;
            char part[32];
            for (u32 i = 0; i < count; i++)
            {
                snprintf(part, sizeof(part), i ? ", %.3g" : "%.3g", static_cast<double>(values[i]));
                out += part;
            }
            return out;
        }

        // Numbers apart by commas or spaces; how many there were.
        u32 numbers(const std::string& text, float* out, u32 most)
        {
            u32 count = 0;
            const char* at = text.c_str();
            while (*at && count < most)
            {
                char* end = nullptr;
                const float v = strtof(at, &end);
                if (end == at)
                {
                    at++;
                    continue;
                }
                out[count++] = v;
                at = end;
            }
            return count;
        }

        // The world of the screen on top, through the scripts' engine; nullptr before there is one.
        World* worldOf(ScriptHost* host)
        {
            const Services* s = host && host->engine() ? static_cast<const Services*>(host->engine()->GetUserData()) : nullptr;
            return s ? s->world : nullptr;
        }

        void property(JsonValue& list, const char* name, const std::string& value, bool changes)
        {
            JsonValue& p = *list.add();
            p.put("name")->setText(name);
            p.put("value")->setText(value);
            p.put("changes")->setFlag(changes);
        }
    }

    void ScriptDebugger::entities(int id)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        JsonValue* list = reply.put("entities");
        list->setArray();
        World* world = worldOf(m_host);
        u32 count = 0;
        if (world)
        {
            world->eachEntity([&](u32 entity, const World::Entity& e)
            {
                if (count++ >= 4000)
                {
                    return;
                }
                JsonValue& item = *list->add();
                item.put("entity")->setNumber(entity);
                item.put("name")->setText(e.name);
                item.put("parent")->setNumber(e.parent != 0xFFFFFFFFu ? world->parentOf(entity) : 0);
                const Spatial* s = world->space().getSpatial(entity);
                item.put("space")->setFlag(s != nullptr);
                const float at[3] = { s ? s->place.x : e.position.x, s ? s->place.y : e.position.y, s ? s->place.z : 0 };
                item.put("place")->setText(joined(at, s ? 3 : 2));
            });
        }
        putNumber(reply, "count", count);
        send(reply.write(true));
    }

    void ScriptDebugger::studioActors(int id,const std::string& command,const JsonValue& ask)
    {
        JsonValue reply;putNumber(reply,"id",id);
        const auto* services=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
        auto* screens=services?services->screens:nullptr;
        auto* world=screens?screens->liveWorld():nullptr;
        const auto reject=[&](const char* reason){reply.put("actorOk")->setFlag(false);putText(reply,"message",reason);send(reply.write(true));};
        if(!world||!screens||screens->loading()||screens->failed()||m_liveApplying||(!m_actorFrame&&!m_paused))
        {reject("World unavailable at this script boundary; retry.");return;}
        if(m_paused&&services->world!=world){reject("Paused outside active screen.");return;}
        const auto epoch=screens->liveWorldEpoch();
        const auto integer=[&](const char* key,u32& out)
        {
            return debuggerUnsigned(ask.find(key),out);
        };
        if(command=="studioActors")
        {
            u32 offset=0;if(!integer("offset",offset)||offset>65536){reject("Invalid actor page.");return;}
            u32 count=0;auto* list=reply.put("entities");list->setArray();
            world->eachEntity([&](u32 entity,const World::Entity& e){
                const auto at=count++;
                if(at<offset||at-offset>=48)return;
                auto* item=list->add();putNumber(*item,"entity",entity);putText(*item,"name",e.name);
                putNumber(*item,"parent",world->parentOf(entity));item->put("space")->setFlag(world->space().getSpatial(entity)!=nullptr);
            });
            reply.put("actorOk")->setFlag(true);putNumber(reply,"epoch",epoch);putNumber(reply,"offset",offset);
            putNumber(reply,"count",count);putText(reply,"screen",screens->current());send(reply.write(true));return;
        }
        u32 expected=0,entityId=0;
        if(!integer("epoch",expected)||expected!=epoch||!integer("entity",entityId)||!world->find(entityId))
        {reject("Actor is stale or was destroyed; refresh the scene.");return;}
        if(command=="studioActor"){entity(id,ask,true);return;}
        const auto* name=ask.find("property");const auto* value=ask.find("value");
        if(!name||!value||!studioActorValue(name->text(),value->text(),world->space().getSpatial(entityId)!=nullptr))
        {reject("Invalid or out-of-range property value.");return;}
        // Reuse the established component setters. No script evaluation,
        // creation/deletion, persistent edits or arbitrary memory writes.
        setEntity(id,ask,true);
    }
    void ScriptDebugger::entity(int id, const JsonValue& ask,bool studio)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        JsonValue* list = reply.put("properties");
        list->setArray();
        const auto* svc=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
        World* world = studio&&svc&&svc->screens?svc->screens->liveWorld():worldOf(m_host);
        const JsonValue* which = ask.find("entity");
        u32 entity=0;
        const World::Entity* e = world && debuggerUnsigned(which,entity) ? world->find(entity) : nullptr;
        if(studio){reply.put("actorOk")->setFlag(e!=nullptr);putNumber(reply,"epoch",svc->screens->liveWorldEpoch());putNumber(reply,"entity",entity);}
        if (e)
        {
            Space& space = world->space();
            property(*list, "name", e->name, false);
            property(*list, "shown", e->visible ? "true" : "false", true);
            if (const Spatial* s = space.getSpatial(entity))
            {
                const Vec3 angles = s->turn.angles() * 57.29578f;
                const float place[3] = { s->place.x, s->place.y, s->place.z }, turn[3] = { angles.x, angles.y, angles.z }, size[3] = { s->size.x, s->size.y, s->size.z };
                property(*list, "place", joined(place, 3), true);
                property(*list, "angles", joined(turn, 3), true);
                property(*list, "size", joined(size, 3), true);
                if (const Appearance* a = space.getAppearance(entity))
                {
                    const float color[4] = { a->color.r, a->color.g, a->color.b, a->color.a };
                    property(*list, "color", joined(color, 4), true);
                    property(*list, "model", a->model ? a->model->key : a->custom ? "a mesh" : a->sprite ? "a sprite" : "a shape", false);
                    if (a->model && a->model->model && a->animation >= 0)
                    {
                        property(*list, "animation", a->model->model->animations[static_cast<u32>(a->animation)].name, false);
                    }
                    property(*list, "ragdolled", space.ragdolled(entity) ? "true" : "false", false);
                }
                if (const Body3* b = space.getBody(entity))
                {
                    const float velocity[3] = { b->velocity.x, b->velocity.y, b->velocity.z };
                    property(*list, "velocity", joined(velocity, 3), true);
                    property(*list, "mass", joined(&b->mass, 1), true);
                    property(*list, "walker", b->walker ? "true" : "false", false);
                    property(*list, "on the ground", b->onGround ? "true" : "false", false);
                }
                if (const Collider3* c = space.getCollider(entity))
                {
                    static const char* const kShapes[] = { "sphere", "box", "capsule", "shell" };
                    const float half[3] = { c->half.x, c->half.y, c->half.z };
                    property(*list, "collider", std::string(kShapes[static_cast<u32>(c->shape) & 3]) + " " + joined(half, 3), false);
                }
            }
            else
            {
                const float position[2] = { e->position.x, e->position.y }, scale[2] = { e->scale.x, e->scale.y };
                property(*list, "position", joined(position, 2), true);
                property(*list, "rotation", joined(&e->rotation, 1), true);
                property(*list, "scale", joined(scale, 2), true);
                property(*list, "layer", std::to_string(e->layer), false);
                if (const Body* b = world->getBody(entity))
                {
                    const float velocity[2] = { b->velocity.x, b->velocity.y };
                    property(*list, "velocity", joined(velocity, 2), true);
                    property(*list, "on the ground", b->onGround ? "true" : "false", false);
                }
            }
        }
        send(reply.write(true));
    }

    void ScriptDebugger::setEntity(int id, const JsonValue& ask,bool studio)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        const auto* svc=m_host&&m_host->engine()?static_cast<const Services*>(m_host->engine()->GetUserData()):nullptr;
        World* world = studio&&svc&&svc->screens?svc->screens->liveWorld():worldOf(m_host);
        const JsonValue* which = ask.find("entity");
        const JsonValue* name = ask.find("property");
        const JsonValue* value = ask.find("value");
        u32 entity=0;
        World::Entity* e = world && name && value && debuggerUnsigned(which,entity) ? world->find(entity) : nullptr;
        bool done = false;
        if (e)
        {
            const std::string& what = name->text();
            const std::string& text = value->text();
            float v[4] = {};
            const u32 n = numbers(text, v, 4);
            Space& space = world->space();
            Spatial* s = space.getSpatial(entity);
            Appearance* a = space.getAppearance(entity);
            Body3* b3 = space.getBody(entity);
            if (what == "shown")
            {
                e->visible = text == "true" || text == "1";
                done = true;
            }
            else if (s && n == 3 && (what == "place" || what == "angles" || what == "size"))
            {
                const Vec3 x(v[0], v[1], v[2]);
                (what == "place" ? s->place : what == "size" ? s->size : s->place) = what == "angles" ? s->place : x;
                if (what == "angles")
                {
                    s->turn = Quat::fromAngles(x * 0.0174533f);
                }
                space.forget();
                done = true;
            }
            else if (a && what == "color" && n >= 3)
            {
                a->color = { v[0], v[1], v[2], n == 4 ? v[3] : a->color.a };
                done = true;
            }
            else if (b3 && what == "velocity" && n == 3)
            {
                b3->velocity = { v[0], v[1], v[2] };
                b3->still = 0;
                space.forget();
                done = true;
            }
            else if (b3 && what == "mass" && n == 1)
            {
                b3->mass = v[0] > 0 ? v[0] : 0;
                done = true;
            }
            else if (!s && what == "position" && n == 2)
            {
                e->position = { v[0], v[1] };
                done = true;
            }
            else if (!s && what == "rotation" && n == 1)
            {
                e->rotation = v[0];
                done = true;
            }
            else if (!s && what == "scale" && n == 2)
            {
                e->scale = { v[0], v[1] };
                done = true;
            }
            else if (!s && what == "velocity" && n == 2)
            {
                if (Body* b = world->getBody(entity))
                {
                    b->velocity = { v[0], v[1] };
                    done = true;
                }
            }
        }
        reply.put("done")->setFlag(done);
        if(studio){reply.put("actorOk")->setFlag(done);putText(reply,"message",done?"Runtime property updated.":"Component does not support this property.");}
        send(reply.write(true));
    }

    // --- The profiler ---

    void ScriptDebugger::sample(asIScriptContext* context)
    {
        if (m_clock == nullptr)
        {
            return;
        }
        const double now = m_clock(m_clockUser);
        if (now < m_nextSample)
        {
            return;
        }
        // About a thousand a second while scripts run; what they wait for between frames isn't theirs.
        m_nextSample = now + 0.001;
        m_samples++;
        const auto count = [](std::vector<Hit>& hits, const void* key, int line, bool self)
        {
            for (Hit& h : hits)
            {
                if (h.key == key && h.line == line)
                {
                    h.total++;
                    h.self += self ? 1 : 0;
                    return;
                }
            }
            hits.push_back({ key, line, self ? 1u : 0u, 1u });
        };
        const u32 depth = context->GetCallstackSize();
        for (u32 i = 0; i < depth; i++)
        {
            asIScriptFunction* f = context->GetFunction(i);
            if (f == nullptr)
            {
                continue;
            }
            // A function that calls itself is counted once in each sample.
            bool seen = false;
            for (u32 k = 0; k < i && !seen; k++)
            {
                seen = context->GetFunction(k) == f;
            }
            if (!seen)
            {
                count(m_functionHits, f, 0, i == 0);
            }
        }
        const char* section = nullptr;
        const int line = context->GetLineNumber(0, nullptr, &section);
        count(m_lineHits, section, line, true);
    }

    void ScriptDebugger::profile(int id, bool on)
    {
        JsonValue reply;
        putNumber(reply, "id", id);
        const double now = m_clock ? m_clock(m_clockUser) : 0;
        if (on)
        {
            m_functionHits.clear();
            m_lineHits.clear();
            m_samples = 0;
            m_profileStart = now;
            m_nextSample = 0;
            m_profiling = true;
            send(reply.write(true));
            return;
        }
        m_profiling = false;
        putNumber(reply, "seconds", now - m_profileStart);
        putNumber(reply, "samples", m_samples);
        // The most first, and no more than are worth reading.
        std::sort(m_functionHits.begin(), m_functionHits.end(), [](const Hit& a, const Hit& b) { return a.total > b.total; });
        std::sort(m_lineHits.begin(), m_lineHits.end(), [](const Hit& a, const Hit& b) { return a.self > b.self; });
        JsonValue* functions = reply.put("functions");
        functions->setArray();
        for (size_t i = 0; i < m_functionHits.size() && i < 60; i++)
        {
            const asIScriptFunction* f = static_cast<const asIScriptFunction*>(m_functionHits[i].key);
            JsonValue* out = functions->add();
            const char* section = nullptr;
            int line = 0;
            f->GetDeclaredAt(&section, &line, nullptr);
            putText(*out, "name", f->GetDeclaration(true, true, false));
            putText(*out, "file", m_host->sourceOf(section));
            putNumber(*out, "line", line);
            putNumber(*out, "self", m_functionHits[i].self);
            putNumber(*out, "total", m_functionHits[i].total);
        }
        JsonValue* lines = reply.put("lines");
        lines->setArray();
        for (size_t i = 0; i < m_lineHits.size() && i < 40; i++)
        {
            JsonValue* out = lines->add();
            putText(*out, "file", m_host->sourceOf(static_cast<const char*>(m_lineHits[i].key)));
            putNumber(*out, "line", m_lineHits[i].line);
            putNumber(*out, "self", m_lineHits[i].self);
        }
        send(reply.write(true));
    }
}
