#include "tools/blueprint/native_assets.h"
#include "tools/blueprint/native_compile.h"
#include "platform/platform.h"
#include "base/json_tree.h"
#include "base/json_value.h"
#include "assets/decode.h"
#include "assets/bitmap_font.h"
#include "assets/formats.h"
#include "audio/audio.h"
#include "audio/decoder.h"
#include <limits>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <ctime>

namespace nx
{
    namespace
    {
        constexpr size_t kMaxFiles=4096,kMaxFile=64*1024*1024,kMaxTotal=256*1024*1024;
        // A scoped owner for native engine handles. No additional standard-library
        // dependencies; all early exits still return allocations to the native pool.
        template<class T,void (*Release)(T*)> class AudioOwner
        {
            T* m_value;
        public:
            explicit AudioOwner(T* value):m_value(value){}
            ~AudioOwner(){if(m_value)Release(m_value);}
            AudioOwner(const AudioOwner&)=delete;
            AudioOwner& operator=(const AudioOwner&)=delete;
            T* get()const{return m_value;}
            T* operator->()const{return m_value;}
            T& operator*()const{return *m_value;}
            explicit operator bool()const{return m_value!=nullptr;}
        };
        // Nine significant decimal digits round-trip binary32. Construct the
        // decimal directly, so neither C++ streams nor the C locale are needed.
        std::string audioFloatText(float value)
        {
            if(value==0)return "0";
            const bool negative=value<0;
            double scaled=std::abs(static_cast<double>(value));int exponent=0;
            while(scaled>=10){scaled/=10;++exponent;}
            while(scaled<1){scaled*=10;--exponent;}
            unsigned mantissa=static_cast<unsigned>(std::round(scaled*100000000.0));
            if(mantissa==1000000000u){mantissa=100000000u;++exponent;}
            std::string digits=std::to_string(mantissa);
            while(digits.size()>1&&digits.back()=='0')digits.pop_back();
            std::string result=negative?"-":"";
            const int point=exponent+1;
            if(point>0&&point<=9)
            {
                const auto pos=static_cast<size_t>(point);
                if(pos>=digits.size())result+=digits+std::string(pos-digits.size(),'0');
                else result+=digits.substr(0,pos)+"."+digits.substr(pos);
            }
            else if(point<=0&&point>=-5)result+="0."+std::string(static_cast<size_t>(-point),'0')+digits;
            else
            {
                result+=digits.substr(0,1);
                if(digits.size()>1)result+="."+digits.substr(1);
                result+="e"+std::to_string(exponent);
            }
            return result;
        }
        bool folderId(const std::string& s)
        {return s=="Screen"||blueprintAssetName(s,true);}
        bool directoryRoot(const std::string& s)
        {return blueprintAssetRoot(s)||(s.rfind("bp-",0)==0&&s.size()<=32&&s.size()>3&&s.find_first_not_of("0123456789-",3)==std::string::npos);}
        bool ensure(Platform& p,const std::string& path)
        {return p.createOwnFolder(path.c_str())||p.fileTime(("own/"+path).c_str())!=0;}
        bool children(Platform& p,const std::string& path,bool folders,std::vector<std::string>& names)
        {
            struct Context {std::vector<std::string>& names;bool overflow=false;} ctx{names};
            p.list(path.c_str(),folders,[](const char* name,void* user)
            {
                auto& c=*static_cast<Context*>(user);
                if(c.names.size()>=kMaxFiles)c.overflow=true;
                else c.names.emplace_back(name);
            },&ctx);
            std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
            return !ctx.overflow;
        }
        bool collect(Platform& p,const std::string& base,const std::string& rel,unsigned depth,size_t& directories,std::vector<std::string>& result,std::string& error)
        {
            if(depth>4||rel.size()>80||++directories>1024){error="Asset folder count, nesting or path limit exceeded.";return false;}
            std::vector<std::string> files,folders;
            const auto path=base+(rel.empty()?"":"/"+rel);
            if(!children(p,path,false,files)||!children(p,path,true,folders)){error="Too many assets.";return false;}
            for(const auto& file:files)
            {
                if(!blueprintAssetName(file)||result.size()>=kMaxFiles){error="Invalid or excessive asset names in "+path;return false;}
                const auto relative=rel.empty()?file:rel+"/"+file;
                if(relative.size()>90){error="Asset path is too long: "+relative;return false;}
                result.push_back(relative);
            }
            for(const auto& folder:folders)
            {
                if(!blueprintAssetName(folder,true)){error="Invalid asset folder: "+folder;return false;}
                if(!collect(p,base,rel.empty()?folder:rel+"/"+folder,depth+1,directories,result,error))return false;
            }
            return true;
        }
        bool copy(Platform& p,const std::string& from,const std::string& to,size_t& total,std::string& error)
        {
            if(p.fileTime(to.c_str())){error="Asset already exists: "+to;return false;}
            FileData data;
            if(!p.readFile(from.c_str(),data,MemTag::General)||data.size>kMaxFile||data.size>kMaxTotal-total)
            {error="Cannot read asset or asset size limit exceeded: "+from;return false;}
            total+=data.size;
            if(!p.writeFile(to.c_str(),data.data,data.size)){error="Cannot write "+to;return false;}
            FileData check;
            if(!p.readFile(to.c_str(),check,MemTag::General)||check.size!=data.size||std::memcmp(check.data,data.data,data.size)!=0)
            {error="Asset copy verification failed: "+to;return false;}
            return true;
        }
        bool copyShared(Platform& p,const std::string& source,const std::string& destination,const std::vector<std::string>& files,size_t& total,std::string& error)
        {
            if(files.empty())return true;
            const auto dest=destination+"/_global",base="own/"+source+"/_global/";
            if(!ensure(p,dest)){error="Cannot create shared asset directory.";return false;}
            for(const auto& file:files)
            {
                for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                    if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create shared asset subfolder.";return false;}
                if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
            }
            return true;
        }
    }
    static bool dryToneWave(const BlueprintTone& t,std::string& wav,std::string& error)
    {
        const auto between=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
        if(t.wave>4||(t.rate!=11025&&t.rate!=22050&&t.rate!=44100&&t.rate!=48000)||
            !between(t.seconds,0.05f,4.f)||!between(t.frequency,20,4000)||!between(t.endFrequency,20,4000)||
            !between(t.amplitude,0,1)||!between(t.attack,0,0.33f)||!between(t.decay,0,0.33f)||
            !between(t.sustain,0,1)||!between(t.release,0,0.33f))
        {error="Invalid tone settings.";return false;}
        const unsigned frames=static_cast<unsigned>(std::round(t.seconds*t.rate));
        std::string result;
        result.reserve(44+frames*2);
        const auto u16le=[&](unsigned v){result+=static_cast<char>(v&255);result+=static_cast<char>((v>>8)&255);};
        const auto u32le=[&](unsigned v){u16le(v);u16le(v>>16);};
        result+="RIFF";u32le(36+frames*2);result+="WAVEfmt ";u32le(16);
        u16le(1);u16le(1);u32le(t.rate);u32le(t.rate*2);u16le(2);u16le(16);
        result+="data";u32le(frames*2);
        constexpr double tau=6.2831853071795864769;
        double phase=0;unsigned rng=0x18374651u;
        for(unsigned i=0;i<frames;++i)
        {
            const float time=static_cast<float>(i)/static_cast<float>(frames-1);
            float envelope=t.sustain;
            if(t.attack>0&&time<t.attack)envelope=time/t.attack;
            else if(t.decay>0&&time<t.attack+t.decay)
                envelope=1-(1-t.sustain)*(time-t.attack)/t.decay;
            else if(t.release>0&&time>1-t.release)envelope=t.sustain*(1-time)/t.release;
            // Even zero attack/release settings get a short de-click ramp.
            const float ramp=std::min(1.f,std::min(static_cast<float>(i),static_cast<float>(frames-1-i))/(t.rate*0.003f));
            float sample=0;
            switch(t.wave)
            {
                case 0: sample=static_cast<float>(std::sin(tau*phase));break;
                case 1: sample=static_cast<float>(1-4*std::abs(phase-0.5));break;
                case 2: sample=static_cast<float>(2*phase-1);break;
                case 3: sample=phase<0.5?1.f:-1.f;break;
                case 4: rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                    sample=static_cast<float>(rng>>8)/8388607.5f-1.f;break;
            }
            const int value=static_cast<int>(std::round(std::clamp(sample*envelope*ramp*t.amplitude,-1.f,1.f)*32767));
            u16le(static_cast<unsigned>(value));
            phase+=(t.frequency+(t.endFrequency-t.frequency)*time)/t.rate;
            phase-=std::floor(phase);
        }
        wav.swap(result);error.clear();return true;
    }
    namespace
    {
        struct ToneField {const char* name;float BlueprintTone::*field;float lo,hi;};
        constexpr ToneField toneFields[]={
            {"seconds",&BlueprintTone::seconds,0.05f,4},{"frequency",&BlueprintTone::frequency,20,4000},
            {"endFrequency",&BlueprintTone::endFrequency,20,4000},{"amplitude",&BlueprintTone::amplitude,0,1},
            {"attack",&BlueprintTone::attack,0,0.33f},{"decay",&BlueprintTone::decay,0,0.33f},
            {"sustain",&BlueprintTone::sustain,0,1},{"release",&BlueprintTone::release,0,0.33f},
            {"tail",&BlueprintTone::tail,0,2}
        };
        constexpr ToneField rightFields[]={
            {"rightFrequency",&BlueprintTone::rightFrequency,20,4000},{"rightEndFrequency",&BlueprintTone::rightEndFrequency,20,4000},
            {"rightAmplitude",&BlueprintTone::rightAmplitude,0,1},{"rightAttack",&BlueprintTone::rightAttack,0,0.33f},
            {"rightDecay",&BlueprintTone::rightDecay,0,0.33f},{"rightSustain",&BlueprintTone::rightSustain,0,1},{"rightRelease",&BlueprintTone::rightRelease,0,0.33f}
        };
        bool toneValid(const BlueprintTone& t,std::string& error)
        {
            const auto valid=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
            if(t.wave>4||t.rightWave>4||(t.channels!=1&&t.channels!=2)||
                (t.rate!=11025&&t.rate!=22050&&t.rate!=44100&&t.rate!=48000))
            {error="Invalid waveform, channel count or sample rate.";return false;}
            for(const auto& f:toneFields)if(!valid(t.*f.field,f.lo,f.hi))
            {error="Invalid tone parameter: "+std::string(f.name);return false;}
            for(const auto& f:rightFields)if(!valid(t.*f.field,f.lo,f.hi))
            {error="Invalid right oscillator parameter: "+std::string(f.name);return false;}
            for(const auto& f:blueprintEffectControls)if(!valid(t.effects.*f.field,f.lo,f.hi))
            {error="Invalid effect parameter: "+std::string(f.name);return false;}
            return true;
        }
        bool newToneAsset(Platform& p,const std::string& root,const std::string& screen,const std::string& suffix,
            const std::string& bytes,std::string& relative,std::string& error)
        {
            if(!blueprintAssetRoot(root)){error="Invalid project asset root.";return false;}
            if(!blueprintEnsureAssetFolder(p,root,screen,error))return false;
            const auto folder=root+"/"+screen+"/asset/sound";
            if(!ensure(p,folder)){error="Cannot create sound folder.";return false;}
            for(unsigned i=1;i<=4096;++i)
            {
                const auto name="synth-"+std::to_string(i)+suffix,path="own/"+folder+"/"+name;
                if(p.fileTime(path.c_str()))continue;
                if(!p.writeFile(path.c_str(),bytes.data(),bytes.size())){error="Cannot write audio asset.";return false;}
                FileData check;
                if(!p.readFile(path.c_str(),check,MemTag::General)||check.size!=bytes.size()||
                    std::memcmp(check.data,bytes.data(),bytes.size())!=0)
                {error="Audio asset verification failed.";return false;}
                relative="sound/"+name;error.clear();return true;
            }
            error="No unused audio asset name.";return false;
        }
    }
    // Input is interleaved stereo at the original mix rate, in signed PCM units.
    static bool renderAudio(const BlueprintTone& t,std::vector<float>& stereo,std::string& wav,std::string& error)
    {
        BusEffects processor;processor.set(t.effects,0);
        const auto& fx=t.effects;
        // Isolated copies: never change the live mixer's bus or its effect state.
        AudioOwner<ReverbMemory,destroyReverbMemory> reverb(fx.reverb>0?createReverbMemory():nullptr);
        AudioOwner<EchoMemory,destroyEchoMemory> echo(fx.echo>0?createEchoMemory():nullptr);
        AudioOwner<WaverMemory,destroyWaverMemory> waver((fx.chorus>0||fx.flanger>0)?createWaverMemory():nullptr);
        if((fx.reverb>0&&!reverb)||(fx.echo>0&&!echo)||((fx.chorus>0||fx.flanger>0)&&!waver))
        {error="Not enough memory for audio effects.";return false;}
        processor.giveReverb(reverb.get());processor.giveEcho(echo.get());processor.giveWaver(waver.get());

        const unsigned inputFrames=static_cast<unsigned>(stereo.size()/2);
        const unsigned tailFrames=(fx.reverb>0||fx.echo>0||fx.chorus>0||fx.flanger>0)?
            static_cast<unsigned>(std::round(t.tail*kMixRate)):0;
        const unsigned total=inputFrames+tailFrames;
        stereo.resize(static_cast<size_t>(total)*2,0);
        for(unsigned at=0;at<total;at+=256)
            processor.process(stereo.data()+2*at,std::min(256u,total-at));
        const unsigned frames=static_cast<unsigned>(std::round(static_cast<double>(total)*t.rate/kMixRate));
        const unsigned payload=frames*2*t.channels;
        std::string result;result.reserve(44+payload);
        const auto le16=[&](unsigned v){result+=static_cast<char>(v&255);result+=static_cast<char>((v>>8)&255);};
        const auto le32=[&](unsigned v){le16(v);le16(v>>16);};
        result+="RIFF";le32(36+payload);result+="WAVEfmt ";le32(16);
        le16(1);le16(t.channels);le32(t.rate);le32(t.rate*2*t.channels);le16(2*t.channels);le16(16);
        result+="data";le32(payload);
        // The native effect unit runs at 48 kHz. Average the covered source
        // sample intervals when reducing rate; no claim of studio-grade resampling.
        const double stride=static_cast<double>(kMixRate)/t.rate;
        for(unsigned i=0;i<frames;++i)
        {
            const double start=i*stride,end=std::min(static_cast<double>(total),(i+1)*stride);
            float sums[2]={};
            for(unsigned j=static_cast<unsigned>(start);j<total&&j<end;++j)
            {
                const float weight=static_cast<float>(std::min(end,static_cast<double>(j+1))-std::max(start,static_cast<double>(j)));
                sums[0]+=stereo[2*j]*weight;sums[1]+=stereo[2*j+1]*weight;
            }
            const float scale=static_cast<float>(1.0/std::max(0.001,end-start))*
                std::min(1.f,static_cast<float>(frames-1-i)/(t.rate*0.01f));
            for(unsigned ch=0;ch<t.channels;++ch)
            {
                float v=(t.channels==1?(sums[0]+sums[1])*0.5f:sums[ch])*scale;
                if(!std::isfinite(v)){error="Non-finite audio effect result.";return false;}
                le16(static_cast<unsigned>(static_cast<int>(std::round(std::clamp(v,-32767.f,32767.f)))));
            }
        }
        wav.swap(result);error.clear();return true;

    }
    bool blueprintToneWave(const BlueprintTone& t,std::string& wav,std::string& error)
    {
        if(!toneValid(t,error))return false;
        BusEffects processor;processor.set(t.effects,0);
        if(!processor.busy()&&t.channels==1)return dryToneWave(t,wav,error);
        BlueprintTone dry=t;dry.rate=kMixRate;
        std::string pcm;
        if(!dryToneWave(dry,pcm,error))return false;
        std::string rightPcm;
        if(t.channels==2&&t.independentStereo)
        {
            dry.wave=t.rightWave;dry.frequency=t.rightFrequency;dry.endFrequency=t.rightEndFrequency;
            dry.amplitude=t.rightAmplitude;dry.attack=t.rightAttack;dry.decay=t.rightDecay;
            dry.sustain=t.rightSustain;dry.release=t.rightRelease;
            if(!dryToneWave(dry,rightPcm,error)||rightPcm.size()!=pcm.size())return false;
        }
        const unsigned inputFrames=static_cast<unsigned>((pcm.size()-44)/2);
        std::vector<float> stereo(static_cast<size_t>(inputFrames)*2);
        for(unsigned i=0;i<inputFrames;++i)
        {
            const unsigned v=static_cast<unsigned char>(pcm[44+2*i])|
                (static_cast<unsigned>(static_cast<unsigned char>(pcm[45+2*i]))<<8);
            stereo[2*i]=stereo[2*i+1]=static_cast<float>(v>=32768?static_cast<int>(v)-65536:static_cast<int>(v));
            if(!rightPcm.empty())
            {
                const unsigned rv=static_cast<unsigned char>(rightPcm[44+2*i])|
                    (static_cast<unsigned>(static_cast<unsigned char>(rightPcm[45+2*i]))<<8);
                stereo[2*i+1]=static_cast<float>(rv>=32768?static_cast<int>(rv)-65536:static_cast<int>(rv));
            }
        }
        return renderAudio(t,stereo,wav,error);
    }
    bool blueprintAudioGraph(const BlueprintTone& tone,bool stream,BlueprintDocument& document,std::string& error)
    {
        if(!toneValid(tone,error))return false;
        const bool independent=tone.channels==2&&tone.independentStereo;
        if(tone.wave==4||(independent&&tone.rightWave==4))
        {error="Graph templates support sine, triangle, saw and square; bake noise as WAV.";return false;}
        BlueprintDocument candidate;candidate.create();
        candidate.name=stream?"Streaming oscillator":"Sample synthesis";
        bool ok=true;
        const auto add=[&](size_t g,const char* kind,float x,float y)
        {const auto n=candidate.graphs[g].nodes.size();ok=ok&&candidate.addNode(g,kind,x,y,error);return n;};
        const auto variable=[&](size_t g,const char* name,bool setter,float x,float y)
        {const auto n=candidate.graphs[g].nodes.size();ok=ok&&candidate.addVariableNode(g,name,false,setter,x,y,error);return n;};
        const auto wire=[&](size_t g,size_t from,const char* out,size_t to,const char* in)
        {ok=ok&&candidate.connect(g,from,out,to,in,error);};
        const auto input=[&](size_t g,size_t n,const char* pin,const std::string& value)
        {ok=ok&&candidate.setInputValue(g,n,pin,value,error);};
        const auto number=[](float v){return audioFloatText(v);};
        constexpr unsigned shapes[]={0,3,2,1}; // editor's wave order differs from the script helper.
        if(stream)
        {
            ok=candidate.editVariableDeclaration("Screen","field stream:AudioStream@",error)&&
                candidate.editVariableDeclaration("Screen","field scratch:array<float>",error)&&
                candidate.editVariableDeclaration("Screen","field phase:float=0",error)&&
                candidate.editVariableDeclaration("Screen","field rightPhase:float=0",error);
            const auto create=add(0,"api:AudioStream:AudioStream:uint,uint,float",300,60);
            const auto keep=variable(0,"stream",true,680,60);
            const auto play=add(0,"api:AudioStream:play:AudioStream@,float,float",1080,60);
            input(0,create,"rate",std::to_string(tone.rate));input(0,create,"channels",std::to_string(tone.channels));input(0,create,"seconds","0.12");
            wire(0,0,"then",create,"in");wire(0,create,"then",keep,"in");wire(0,create,"result",keep,"value");
            wire(0,keep,"then",play,"in");wire(0,create,"result",play,"target");
            const auto handle=variable(1,"stream",false,24,200),scratch=variable(1,"scratch",false,24,300);
            const auto phase=variable(1,"phase",false,24,400),right=variable(1,"rightPhase",false,24,500);
            const auto fill=add(1,"audio:stream-stereo",420,60);
            wire(1,0,"then",fill,"in");wire(1,handle,"value",fill,"stream");wire(1,scratch,"value",fill,"scratch");
            wire(1,phase,"value",fill,"phase");wire(1,right,"value",fill,"rightPhase");
            input(1,fill,"shape",std::to_string(shapes[tone.wave]));
            input(1,fill,"rightShape",std::to_string(shapes[independent?tone.rightWave:tone.wave]));
            input(1,fill,"frequency",number(tone.frequency));input(1,fill,"rightFrequency",number(independent?tone.rightFrequency:tone.frequency));
            input(1,fill,"gain",number(tone.amplitude));input(1,fill,"rightGain",number(independent?tone.rightAmplitude:tone.amplitude));
            const auto savePhase=variable(1,"phase",true,960,60),saveRight=variable(1,"rightPhase",true,1270,60);
            wire(1,fill,"then",savePhase,"in");wire(1,fill,"nextPhase",savePhase,"value");
            wire(1,savePhase,"then",saveRight,"in");wire(1,fill,"nextRightPhase",saveRight,"value");
        }
        else
        {
            const unsigned frames=static_cast<unsigned>(std::round(tone.seconds*tone.rate));
            const auto samples=add(0,"api:Samples:Samples:uint,uint,uint",300,60);
            input(0,samples,"frames",std::to_string(frames));input(0,samples,"rate",std::to_string(tone.rate));
            input(0,samples,"channels",std::to_string(tone.channels));wire(0,0,"then",samples,"in");
            const auto repeat=add(0,"repeat",700,60);input(0,repeat,"count",std::to_string((frames+4095)/4096));
            wire(0,samples,"then",repeat,"in");
            const auto cast=add(0,"cast:int:uint",700,260),offset=add(0,"multiply:uint",1000,260);
            wire(0,repeat,"index",cast,"a");wire(0,cast,"result",offset,"a");input(0,offset,"b","4096");
            size_t previous=repeat;
            for(unsigned ch=0;ch<tone.channels;++ch)
            {
                const bool right=ch==1&&independent;
                const auto render=add(0,"audio:render-channel",1400.f+static_cast<float>(ch)*530.f,60);
                wire(0,previous,previous==repeat?"body":"then",render,"in");wire(0,samples,"result",render,"samples");wire(0,offset,"result",render,"start");
                input(0,render,"channel",std::to_string(ch));input(0,render,"shape",std::to_string(shapes[right?tone.rightWave:tone.wave]));
                input(0,render,"frequency",number(right?tone.rightFrequency:tone.frequency));input(0,render,"endFrequency",number(right?tone.rightEndFrequency:tone.endFrequency));
                input(0,render,"gain",number(right?tone.rightAmplitude:tone.amplitude));input(0,render,"attack",number(right?tone.rightAttack:tone.attack));
                input(0,render,"decay",number(right?tone.rightDecay:tone.decay));input(0,render,"sustain",number(right?tone.rightSustain:tone.sustain));input(0,render,"release",number(right?tone.rightRelease:tone.release));
                previous=render;
            }
            const auto play=add(0,"api:Samples:play:Samples@,float,float,float,bool",1400,480);
            wire(0,repeat,"then",play,"in");wire(0,samples,"result",play,"target");
        }
        std::string script;
        if(!ok||!compileBlueprint(candidate,script,error))return false;
        document=std::move(candidate);error.clear();return true;
    }
    bool blueprintProjectTemplate(const std::string& id,BlueprintDocument& document,std::string& error)
    {
        BlueprintDocument candidate;candidate.create();
        if(id=="blank"){document=std::move(candidate);error.clear();return true;}
        if(id=="cinematic")
        {
            candidate.name="Cinematic Showcase";std::string sequence;
            const auto add=[&](size_t g,const char* kind,float x,float y){return candidate.addNode(g,kind,x,y,error);};
            const auto val=[&](size_t g,size_t node,const char* pin,const char* text){return candidate.setInputValue(g,node,pin,text,error);};
            const auto chain=[&](size_t g,size_t from,size_t to){return candidate.connect(g,from,"then",to,"in",error);};
            bool ok=add(0,"spawn:plane",320,60)&&val(0,1,"name","Stage floor")&&val(0,1,"size","30,1,30")&&chain(0,0,1)&&
                add(0,"spawn:box",660,60)&&val(0,2,"name","Boss")&&val(0,2,"place","0,1.5,0")&&val(0,2,"size","2,3,2")&&val(0,2,"color","0.85,0.18,0.12,1")&&chain(0,1,2)&&
                add(0,"api:Space:Space::sky:Color,Color",1020,60)&&val(0,3,"top","0.12,0.22,0.42,1")&&val(0,3,"horizon","0.6,0.65,0.8,1")&&chain(0,2,3)&&
                add(0,"api:Space:Space::sun:Vec3,Color",1380,60)&&val(0,4,"direction","0.5,0.75,0.35")&&val(0,4,"color","1,0.95,0.85,1")&&chain(0,3,4)&&candidate.createVisualAsset("sequence",sequence,error)&&candidate.renameVisualAsset(sequence,"Boss Introduction",error);
            std::vector<BlueprintCameraKey> keys;
            ok=ok&&blueprintCameraKeys("0,0,2,10,0,0,0,65,0;2,5,2,5,0,0,0,55,2@RevealBoss;4,0,1,4,0,0,0,45,1;6,0,2,8,0,0,0,60,2",keys,error)&&candidate.setCameraSequenceKeys(sequence,keys,error);
            size_t graph=0,node=0;
            ok=ok&&candidate.placeCameraSequence("Screen",sequence,graph,node,error)&&val(graph,node,"play","true")&&val(graph,node,"anchor","Boss")&&
                add(2,"render:space",340,60)&&chain(2,0,1)&&candidate.addFunction("RevealBoss",error);
            if(!ok)return false;
            const char* notes[]={"BUILD STAGE | Create the floor, a red boss stand-in, sky and sunlight. Replace the boss box with your own actor.","CINEMATIC | Play the reusable sequence once. The anchor binds camera and look target offsets to Boss. Open Sequencer to edit timing, cuts and easing.","DRAW | Render the native 3D scene using the active cinematic camera.","SUSPEND | The scene stops receiving Update while suspended.","RESUME | Continue the scene lifecycle.","EXIT | Native scene teardown releases its entities.","REVEAL EVENT | Fires at two seconds. Add animation, sound, lighting or HUD logic to this function."};
            for(size_t g=0;g<candidate.graphs.size();++g){std::vector<size_t> members;for(size_t i=0;i<candidate.graphs[g].nodes.size();++i)members.push_back(i);if(!candidate.addComment(g,members,notes[g],error))return false;}
            std::string source,bytes;if(!compileBlueprint(candidate,source,error)||!candidate.serialize(bytes,error))return false;
            BlueprintDocument checked;if(!checked.load(bytes.data(),bytes.size(),error))return false;document=std::move(checked);error.clear();return true;
        }
        if(id=="flight"||id=="rolling-ball")
        {
            const bool flight=id=="flight";
            candidate.name=flight?"Flight":"Rolling Ball";
            bool ok=true;
            const auto field=[&](const char* declaration){if(ok)ok=candidate.editVariableDeclaration("Screen",declaration,error);};
            field("field hero:Entity@");
            field(flight?"field speed:float=8":"field driveStrength:float=18");
            field("field cameraDistance:float=7");
            if(flight){field("field yaw:float=0");field("field pitch:float=0");}
            const auto fn=[&](const char* name,bool delta=false)
            {
                const auto g=candidate.graphs.size();ok=ok&&candidate.addFunction(name,error);
                if(delta)ok=ok&&candidate.setFunctionSignature(g,{{"dt","float"}},"void",error);
                return g;
            };
            const auto arena=fn("BuildCourse"),spawn=fn("SpawnPlayer"),drive=fn(flight?"Fly":"DriveBall",true),camera=fn("FollowCamera");
            const auto add=[&](size_t g,const char* kind,float x,float y,const char* target="")
            {if(!ok)return size_t(0);const auto n=candidate.graphs[g].nodes.size();ok=candidate.addNode(g,kind,x,y,error,target);return n;};
            const auto var=[&](size_t g,const char* name,bool set,float x,float y)
            {if(!ok)return size_t(0);const auto n=candidate.graphs[g].nodes.size();ok=candidate.addVariableNode(g,name,false,set,x,y,error);return n;};
            const auto wire=[&](size_t g,size_t from,const char* out,size_t to,const char* in){if(ok)ok=candidate.connect(g,from,out,to,in,error);};
            const auto val=[&](size_t g,size_t n,const char* pin,const char* value){if(ok)ok=candidate.setInputValue(g,n,pin,value,error);};
            const auto chain=[&](size_t g,size_t from,size_t to){wire(g,from,"then",to,"in");};
            const auto note=[&](size_t g,const char* text)
            {if(!ok)return;std::vector<size_t> members;for(size_t n=0;n<candidate.graphs[g].nodes.size();++n)members.push_back(n);ok=candidate.addComment(g,members,text,error);};
            const auto build=add(0,"call:function",340,60,"BuildCourse"),player=add(0,"call:function",700,60,"SpawnPlayer");
            chain(0,0,build);chain(0,build,player);note(0,"START | Build the course once, then spawn the controlled entity and keep its handle.");
            const auto tick=add(1,"call:function",340,60,flight?"Fly":"DriveBall"),view=add(1,"call:function",700,60,"FollowCamera"),dt=add(1,"event:delta",30,270);
            chain(1,0,tick);chain(1,tick,view);wire(1,dt,"value",tick,"arg_dt");
            note(1,"UPDATE | Read native movement input, apply the controller, then update the camera. Delta time makes controls independent of frame rate.");
            const auto render=add(2,"render:space",340,60);chain(2,0,render);note(2,"DRAW | Render the native scene with the current camera.");
            for(size_t g:{size_t(3),size_t(4),size_t(5)})
            {
                if(flight){const auto lock=add(g,"api:Input:Input::lockMouse:bool",340,60);val(g,lock,"locked",g==4?"true":"false");chain(g,0,lock);}
                note(g,g==3?"SUSPEND | The screen stops receiving updates.":g==4?"RESUME | Continue the screen lifecycle.":"EXIT | Native screen teardown releases the entities.");
            }
            const auto ground=add(arena,"api:Space:Space::ground:float",340,60),floor=add(arena,"spawn:plane",700,60),sky=add(arena,"api:Space:Space::sky:Color,Color",1060,60),sun=add(arena,"api:Space:Space::sun:Vec3,Color",1420,60);
            chain(arena,0,ground);val(arena,ground,"height","0");chain(arena,ground,floor);val(arena,floor,"name","Course floor");val(arena,floor,"size","80,1,80");
            chain(arena,floor,sky);val(arena,sky,"top","0.18,0.35,0.7,1");val(arena,sky,"horizon","0.65,0.8,0.95,1");
            chain(arena,sky,sun);val(arena,sun,"direction","0.5,0.75,0.35");val(arena,sun,"color","1,0.95,0.85,1");
            struct Marker{const char* name;const char* place;const char* size;};
            const Marker markers[]={{"Left gate","-3,2,-8","1,4,1"},{"Right gate","3,2,-8","1,4,1"},{"Slalom left","-2,1,-15","2,2,2"},{"Slalom right","2,1,-22","2,2,2"},{"Finish","0,0.25,-30","7,0.5,2"}};
            size_t last=sun;
            for(size_t i=0;i<5;++i)
            {
                const auto box=add(arena,"spawn:box",340+float(i%3)*850,320+float(i/3)*300);
                chain(arena,last,box);val(arena,box,"name",markers[i].name);val(arena,box,"place",markers[i].place);val(arena,box,"size",markers[i].size);last=box;
                if(!flight){const auto col=add(arena,"api:Entity:cube:Entity@,Vec3,uint,uint",700+float(i%3)*850,320+float(i/3)*300);chain(arena,last,col);wire(arena,box,"result",col,"target");val(arena,col,"size",markers[i].size);last=col;}
            }
            note(arena,flight?"COURSE | Lightweight visual gates and slalom markers. This arcade flight controller does not simulate aircraft collisions.":"COURSE | Native ground plus five fixed collision obstacles. Navigate around the gates and slalom to the finish.");
            const auto entity=add(spawn,"api:Space:Space::spawn:Vec3",340,60),keep=var(spawn,"hero",true,700,60),name=add(spawn,"api:Entity:set_name:Entity@,string",1060,60),size=add(spawn,"api:Entity:set_size:Entity@,Vec3",1420,60),shape=add(spawn,"api:Entity:shape:Entity@,Solid,Color",340,340);
            chain(spawn,0,entity);val(spawn,entity,"place",flight?"0,4,0":"0,1,0");chain(spawn,entity,keep);wire(spawn,entity,"result",keep,"value");
            chain(spawn,keep,name);wire(spawn,entity,"result",name,"target");val(spawn,name,"value",flight?"Aircraft":"Player Ball");
            chain(spawn,name,size);wire(spawn,entity,"result",size,"target");val(spawn,size,"value",flight?"1.8,0.35,1.1":"1,1,1");
            chain(spawn,size,shape);wire(spawn,entity,"result",shape,"target");val(spawn,shape,"solid",flight?"SolidBox":"SolidSphere");val(spawn,shape,"color","0.15,0.65,0.95,1");
            if(flight){const auto lock=add(spawn,"api:Input:Input::lockMouse:bool",700,340);chain(spawn,shape,lock);val(spawn,lock,"locked","true");}
            else
            {
                const auto collider=add(spawn,"api:Entity:sphere:Entity@,float,uint,uint",700,340),mass=add(spawn,"api:Entity:set_mass:Entity@,float",1060,340);
                chain(spawn,shape,collider);wire(spawn,entity,"result",collider,"target");val(spawn,collider,"radius","0.5");chain(spawn,collider,mass);wire(spawn,entity,"result",mass,"target");val(spawn,mass,"value","1");
            }
            note(spawn,flight?"AIRCRAFT | A lightweight box stand-in. No rigid body is created; flight is directly controlled. Replace the visual with your own aircraft model.":"BALL | Native sphere appearance and 0.5 m sphere collider; 1 kg mass enables gravity and collision response.");
            const auto hero=var(drive,"hero",false,30,240),move=add(drive,"api:Input:Input::get_move:",30,420);
            if(flight)
            {
                const auto mouse=add(drive,"api:Input:Input::get_mouseMove:",30,600),pad=add(drive,"api:Input:Input::padRight:uint",30,760),yaw=var(drive,"yaw",false,340,240),pitch=var(drive,"pitch",false,340,420);
                val(drive,pad,"pad","0");
                const auto y=add(drive,"player:yaw",700,240),p=add(drive,"player:pitch",700,580),setY=var(drive,"yaw",true,1100,60),setP=var(drive,"pitch",true,1450,60);
                for(size_t n:{y,p}){wire(drive,mouse,"result",n,"mouse");wire(drive,pad,"result",n,"pad");wire(drive,0,"arg_dt",n,"dt");}
                wire(drive,yaw,"value",y,"yaw");wire(drive,pitch,"value",p,"pitch");
                chain(drive,0,setY);wire(drive,y,"result",setY,"value");chain(drive,setY,setP);wire(drive,p,"result",setP,"value");
                const auto place=add(drive,"api:Entity:get_place:Entity@",340,780),speed=var(drive,"speed",false,340,950),next=add(drive,"player:flight-position",1150,420),set=add(drive,"api:Entity:set_place:Entity@,Vec3",1810,60);
                wire(drive,hero,"value",place,"target");wire(drive,place,"result",next,"place");wire(drive,move,"result",next,"move");wire(drive,yaw,"value",next,"yaw");wire(drive,pitch,"value",next,"pitch");wire(drive,speed,"value",next,"speed");wire(drive,0,"arg_dt",next,"dt");
                chain(drive,setP,set);wire(drive,hero,"value",set,"target");wire(drive,next,"result",set,"value");
                const auto angles=add(drive,"player:angles",1500,420),face=add(drive,"api:Entity:set_angles:Entity@,Vec3",2180,60);wire(drive,yaw,"value",angles,"yaw");chain(drive,set,face);wire(drive,hero,"value",face,"target");wire(drive,angles,"result",face,"value");
            }
            else
            {
                const auto force=var(drive,"driveStrength",false,30,590),impulse=add(drive,"player:ball-impulse",380,240),push=add(drive,"api:Entity:push:Entity@,Vec3",780,60);
                wire(drive,move,"result",impulse,"move");wire(drive,force,"value",impulse,"strength");wire(drive,0,"arg_dt",impulse,"dt");chain(drive,0,push);wire(drive,hero,"value",push,"target");wire(drive,impulse,"result",push,"impulse");
            }
            note(drive,flight?"FLY | WASD / left stick moves along the yaw and pitch heading; mouse / right stick steers. Forward movement climbs or descends with pitch. Altitude is clamped above the floor.":"DRIVE | WASD / left stick applies a horizontal impulse scaled by delta time. Native rigid-body physics handles inertia, gravity and obstacle collisions.");
            const auto ch=var(camera,"hero",false,30,240),target=add(camera,"api:Entity:get_place:Entity@",340,240),dist=var(camera,"cameraDistance",false,340,440),offset=add(camera,"player:chase-camera",700,240),cam=add(camera,"api:Space:Space::camera:Vec3,Vec3,float",1100,60);
            wire(camera,ch,"value",target,"target");wire(camera,target,"result",offset,"pivot");wire(camera,dist,"value",offset,"distance");
            if(flight){const auto yaw=var(camera,"yaw",false,30,420);wire(camera,yaw,"value",offset,"yaw");}else val(camera,offset,"yaw","0");
            val(camera,offset,"pitch","-25");chain(camera,0,cam);wire(camera,offset,"result",cam,"from");wire(camera,target,"result",cam,"to");val(camera,cam,"angle","65");
            note(camera,"FOLLOW CAMERA | Offset above and behind the controlled entity, then look at it. Change cameraDistance to adjust framing. No camera collision avoidance.");
            if(!ok)return false;
            std::string source,bytes;
            if(!compileBlueprint(candidate,source,error)||!candidate.serialize(bytes,error))return false;
            BlueprintDocument checked;if(!checked.load(bytes.data(),bytes.size(),error))return false;
            document=std::move(checked);error.clear();return true;
        }
        const bool third=id=="third-person";
        if(id!="first-person"&&!third){error="Unknown project template.";return false;}
        candidate.name=third?"Third Person":"First Person";
        bool ok=true;
        const auto field=[&](const char* command){if(ok&&!candidate.editVariableDeclaration("Screen",command,error)){ok=false;error=std::string(command)+": "+error;}};
        field("field hero:Entity@");field("field yaw:float=0");field("field pitch:float=0");
        field("field walkSpeed:float=4.6");field("field jumpSpeed:float=6.4");field("field eyeHeight:float=1.55");
        field("field mouseSensitivity:float=0.2");field("field gamepadYawSpeed:float=130");field("field gamepadPitchSpeed:float=100");
        field("field groundGrip:float=14");field("field airGrip:float=4");if(third){field("field cameraDistance:float=5");field("field modelYawOffset:float=180");}
        const auto fn=[&](const char* name,bool delta=false)
        {
            const auto index=candidate.graphs.size();
            ok=ok&&candidate.addFunction(name,error);
            if(delta)ok=ok&&candidate.setFunctionSignature(index,{{"dt","float"}},"void",error);
            return index;
        };
        const auto arena=fn("BuildArena"),spawn=fn("SpawnCharacter"),look=fn("UpdateLook",true),
            movement=fn("UpdateMovement",true),camera=fn("UpdateCamera"),hud=fn("DrawHUD"),obstacle=fn("AddObstacle");
        ok=ok&&candidate.setFunctionSignature(hud,{{"surface","Canvas@"}},"void",error)&&
            candidate.setFunctionSignature(obstacle,{{"name","string"},{"place","Vec3"},{"size","Vec3"},{"color","Color"}},"void",error);
        // Each function owns one compact, documented task. World-space layout leaves
        // room for variable-width titles and both left and right pin columns.
        const auto add=[&](size_t g,const char* kind,float x,float y,const char* target="")
        {if(!ok)return size_t(0);const auto n=candidate.graphs[g].nodes.size();ok=candidate.addNode(g,kind,x,y,error,target);return n;};
        const auto variable=[&](size_t g,const char* name,bool setter,float x,float y)
        {if(!ok)return size_t(0);const auto n=candidate.graphs[g].nodes.size();ok=candidate.addVariableNode(g,name,false,setter,x,y,error);return n;};
        const auto wire=[&](size_t g,size_t from,const char* out,size_t to,const char* in)
        {if(ok)ok=candidate.connect(g,from,out,to,in,error);};
        const auto value=[&](size_t g,size_t n,const char* pin,const char* text)
        {if(ok)ok=candidate.setInputValue(g,n,pin,text,error);};
        const auto chain=[&](size_t g,size_t from,size_t to){wire(g,from,"then",to,"in");};
        const auto note=[&](size_t g,const char* description)
        {
            if(!ok)return;
            std::vector<size_t> members;
            for(size_t i=0;i<candidate.graphs[g].nodes.size();++i)members.push_back(i);
            ok=candidate.addComment(g,members,description,error);
        };
        const auto build=add(0,"call:function",320,60,"BuildArena"),player=add(0,"call:function",650,60,"SpawnCharacter");
        chain(0,0,build);chain(0,build,player);
        note(0,"INITIALIZE | Build a lightweight test arena, then create and store the player character. Runs once when this screen opens.");
        const auto inputLook=add(1,"call:function",340,60,"UpdateLook"),move=add(1,"call:function",680,60,"UpdateMovement"),
            view=add(1,"call:function",1020,60,"UpdateCamera"),dt=add(1,"event:delta",24,250);
        chain(1,0,inputLook);chain(1,inputLook,move);chain(1,move,view);
        wire(1,dt,"value",inputLook,"arg_dt");wire(1,dt,"value",move,"arg_dt");
        note(1,"FRAME UPDATE | Apply mouse/right-stick look, calculate physics motion from left-stick/WASD and jump, then aim the first-person camera. Delta time keeps movement frame-rate independent.");
        const auto canvasPin=add(2,"event:canvas",24,240),design=add(2,"api:Canvas:design:Canvas@,float,float",340,60),
            draw=add(2,"render:space",700,60),overlay=add(2,"call:function",1040,60,"DrawHUD");
        chain(2,0,design);wire(2,canvasPin,"value",design,"target");value(2,design,"width","1280");value(2,design,"height","720");
        chain(2,design,draw);chain(2,draw,overlay);wire(2,canvasPin,"value",overlay,"arg_surface");
        note(2,"RENDER | Establish a 1280 x 720 virtual canvas. Native Space draws the scene first, then DrawHUD draws the crosshair on top. The same graph runs on desktop and Xbox.");
        for(size_t g:{size_t(3),size_t(4),size_t(5)})
        {
            const auto lock=add(g,"api:Input:Input::lockMouse:bool",360,60);
            value(g,lock,"locked",g==4?"true":"false");chain(g,0,lock);
            note(g,g==4?"RESUME | Recapture mouse for first-person controls after the engine menu closes.":"RELEASE MOUSE | Restore normal pointer behavior while the game is suspended or exiting.");
        }
        const auto ground=add(arena,"api:Space:Space::ground:float",340,60);
        value(arena,ground,"height","0");chain(arena,0,ground);
        const auto floor=add(arena,"spawn:plane",680,60);chain(arena,ground,floor);
        value(arena,floor,"name","Arena floor");value(arena,floor,"size","80,1,80");
        value(arena,floor,"color","0.32,0.37,0.43,1");
        const auto sky=add(arena,"api:Space:Space::sky:Color,Color",1080,60);chain(arena,floor,sky);
        value(arena,sky,"top","0.22,0.42,0.75,1");value(arena,sky,"horizon","0.72,0.83,0.93,1");
        const auto sun=add(arena,"api:Space:Space::sun:Vec3,Color",1480,60);chain(arena,sky,sun);
        value(arena,sun,"direction","0.5,0.75,0.35");value(arena,sun,"color","1,0.93,0.8,1");
        size_t last=sun;
        struct Marker{const char* name;const char* place;const char* size;const char* color;};
        constexpr Marker markers[]={
            {"Orange marker","0,1,-7","2,2,2","0.92,0.35,0.12,1"},
            {"Blue marker","-5,0.75,-11","2,1.5,2","0.15,0.45,0.9,1"},
            {"Green marker","5,1.5,-12","2,3,2","0.2,0.75,0.36,1"},
            {"North wall","0,1,-20","40,2,1","0.46,0.49,0.55,1"},
            {"South wall","0,1,20","40,2,1","0.46,0.49,0.55,1"},
            {"West wall","-20,1,0","1,2,40","0.46,0.49,0.55,1"},
            {"East wall","20,1,0","1,2,40","0.46,0.49,0.55,1"}};
        for(unsigned i=0;i<7;++i)
        {
            const auto& marker=markers[i];
            const auto box=add(arena,"call:function",340+static_cast<float>(i%3)*490,310+static_cast<float>(i/3)*250,"AddObstacle");
            chain(arena,last,box);last=box;
            value(arena,box,"arg_name",marker.name);value(arena,box,"arg_place",marker.place);
            value(arena,box,"arg_size",marker.size);value(arena,box,"arg_color",marker.color);
        }
        note(arena,"ARENA | Native ground supplies collision. Floor, sky and sunlight establish the scene; seven calls to AddObstacle build solid markers and perimeter walls. Change each instance's position, size and colour.");
        const auto box=add(obstacle,"spawn:box",380,60),collider=add(obstacle,"api:Entity:cube:Entity@,Vec3,uint,uint",820,60);
        chain(obstacle,0,box);chain(obstacle,box,collider);
        for(const char* pin:{"name","place","size","color"})
        {
            const auto arg=std::string("arg_")+pin;
            wire(obstacle,0,arg.c_str(),box,pin);
        }
        wire(obstacle,box,"result",collider,"target");value(obstacle,collider,"size","1,1,1");
        const auto wall=add(obstacle,"api:Entity:set_wall:Entity@,bool",1260,60);chain(obstacle,collider,wall);wire(obstacle,box,"result",wall,"target");value(obstacle,wall,"value","true");
        note(obstacle,"OBSTACLE | Create a native SolidBox with the supplied visual transform, then attach a unit local cube collider (the entity transform already supplies the dimensions). Enable the native wall flag explicitly; no mass is assigned, so the obstacle stays fixed. Reuse this function for walls and platforms.");
        const auto entity=add(spawn,"api:Space:Space::spawn:Vec3",340,60);
        value(spawn,entity,"place","0,0.2,0");chain(spawn,0,entity);
        const auto keep=variable(spawn,"hero",true,680,60);chain(spawn,entity,keep);wire(spawn,entity,"result",keep,"value");
        const auto name=add(spawn,"api:Entity:set_name:Entity@,string",1020,60);
        chain(spawn,keep,name);wire(spawn,entity,"result",name,"target");value(spawn,name,"value","Player");
        const auto capsule=add(spawn,"api:Entity:capsule:Entity@,float,float,uint,uint",1380,60);
        chain(spawn,name,capsule);wire(spawn,entity,"result",capsule,"target");value(spawn,capsule,"radius","0.35");value(spawn,capsule,"height","1.7");value(spawn,capsule,"group","1");
        const auto offset=add(spawn,"api:Entity:set_colliderAt:Entity@,Vec3",340,330);
        chain(spawn,capsule,offset);wire(spawn,entity,"result",offset,"target");value(spawn,offset,"value","0,0.85,0");
        const auto walker=add(spawn,"api:Entity:set_walker:Entity@,bool",700,330);
        chain(spawn,offset,walker);wire(spawn,entity,"result",walker,"target");value(spawn,walker,"value","true");
        const auto mass=add(spawn,"api:Entity:set_mass:Entity@,float",1060,330);
        chain(spawn,walker,mass);wire(spawn,entity,"result",mass,"target");value(spawn,mass,"value","60");
        const auto model=add(spawn,"api:Assets:Assets::model:string",340,590);
        chain(spawn,mass,model);value(spawn,model,"name","global:model/robot.glb");
        const auto apply=add(spawn,"api:Entity:model:Entity@,Model@",740,590);
        chain(spawn,model,apply);wire(spawn,entity,"result",apply,"target");wire(spawn,model,"result",apply,"model");
        const auto lock=add(spawn,"api:Input:Input::lockMouse:bool",1100,590);chain(spawn,apply,lock);value(spawn,lock,"locked","true");
        note(spawn,"PLAYER | Spawn at feet height, retain Entity handle, configure 0.35 m radius / 1.7 m capsule with its centre 0.85 m up, enable native walker and 60 kg mass. Attach the Redux robot model, then capture mouse.");
        const auto mouse=add(look,"api:Input:Input::get_mouseMove:",24,300),pad=add(look,"api:Input:Input::padRight:uint",24,460);
        value(look,pad,"pad","0");
        const auto yaw=variable(look,"yaw",false,300,300),pitch=variable(look,"pitch",false,300,460);
        const auto sense=variable(look,"mouseSensitivity",false,300,620),ys=variable(look,"gamepadYawSpeed",false,300,780),
            ps=variable(look,"gamepadPitchSpeed",false,300,940);
        const auto turnYaw=add(look,"player:yaw",640,270),turnPitch=add(look,"player:pitch",640,640);
        for(size_t turn:{turnYaw,turnPitch})
        {
            wire(look,mouse,"result",turn,"mouse");wire(look,pad,"result",turn,"pad");
            wire(look,0,"arg_dt",turn,"dt");wire(look,sense,"value",turn,"mouseSensitivity");
        }
        wire(look,yaw,"value",turnYaw,"yaw");wire(look,ys,"value",turnYaw,"padSpeed");
        wire(look,pitch,"value",turnPitch,"pitch");wire(look,ps,"value",turnPitch,"padSpeed");
        const auto setYaw=variable(look,"yaw",true,1120,60),setPitch=variable(look,"pitch",true,1480,60);
        chain(look,0,setYaw);chain(look,setYaw,setPitch);wire(look,turnYaw,"result",setYaw,"value");wire(look,turnPitch,"result",setPitch,"value");
        note(look,"LOOK | Mouse movement is measured per frame, right-stick speed per second. Update yaw in degrees, clamp pitch to +/-85 degrees, and store both. Editable sensitivity fields control each input source.");
        const auto hero=variable(movement,"hero",false,24,240),motion=add(movement,"api:Entity:get_motion:Entity@",340,240),
            onGround=add(movement,"api:Entity:get_onGround:Entity@",340,390),stick=add(movement,"api:Input:Input::get_move:",340,540),
            jump=add(movement,"api:Input:Input::pressed:string",340,690),heading=variable(movement,"yaw",false,24,390);
        wire(movement,hero,"value",motion,"target");wire(movement,hero,"value",onGround,"target");value(movement,jump,"action","accept");
        const auto speed=variable(movement,"walkSpeed",false,24,540),jumpSpeed=variable(movement,"jumpSpeed",false,24,690),
            grip=variable(movement,"groundGrip",false,24,840),air=variable(movement,"airGrip",false,340,840);
        const auto calculate=add(movement,"player:motion",750,250),set=add(movement,"api:Entity:set_motion:Entity@,Vec3",1260,60);
        wire(movement,motion,"result",calculate,"motion");wire(movement,stick,"result",calculate,"move");wire(movement,heading,"value",calculate,"yaw");
        wire(movement,0,"arg_dt",calculate,"dt");wire(movement,onGround,"result",calculate,"grounded");wire(movement,jump,"result",calculate,"jump");
        wire(movement,speed,"value",calculate,"speed");wire(movement,jumpSpeed,"value",calculate,"jumpSpeed");
        wire(movement,grip,"value",calculate,"groundGrip");wire(movement,air,"value",calculate,"airGrip");
        chain(movement,0,set);wire(movement,hero,"value",set,"target");wire(movement,calculate,"result",set,"value");
        note(movement,"WALK / JUMP | Rotate normalized move input by yaw. Blend horizontal velocity using ground or air grip; keep native vertical velocity unless grounded jump is pressed. Space's walker applies gravity and collision; no manual position teleport.");
        const auto ch=variable(camera,"hero",false,24,240),feet=add(camera,"api:Entity:get_place:Entity@",340,240),
            cy=variable(camera,"yaw",false,24,410),cp=variable(camera,"pitch",false,24,570),height=variable(camera,"eyeHeight",false,340,410);
        const auto eye=add(camera,"player:eye",700,240),target=add(camera,"player:target",1080,240),
            angles=add(camera,"player:angles",700,510),face=add(camera,"api:Entity:set_angles:Entity@,Vec3",340,60),
            cam=add(camera,"api:Space:Space::camera:Vec3,Vec3,float",1480,60);
        wire(camera,ch,"value",feet,"target");wire(camera,feet,"result",eye,"feet");wire(camera,height,"value",eye,"height");
        wire(camera,eye,"result",target,"eye");wire(camera,cy,"value",target,"yaw");wire(camera,cp,"value",target,"pitch");
        wire(camera,cy,"value",angles,"yaw");if(third){const auto modelHeading=variable(camera,"modelYawOffset",false,340,730);wire(camera,modelHeading,"value",angles,"modelYawOffset");}wire(camera,angles,"result",face,"value");wire(camera,ch,"value",face,"target");
        chain(camera,0,face);chain(camera,face,cam);if(third){const auto chase=add(camera,"player:chase-camera",1080,550),distance=variable(camera,"cameraDistance",false,700,730);wire(camera,eye,"result",chase,"pivot");wire(camera,cy,"value",chase,"yaw");wire(camera,cp,"value",chase,"pitch");wire(camera,distance,"value",chase,"distance");wire(camera,chase,"result",cam,"from");wire(camera,eye,"result",cam,"to");}else{wire(camera,eye,"result",cam,"from");wire(camera,target,"result",cam,"to");}value(camera,cam,"angle","72");
        note(camera,third?"THIRD PERSON | Pivot is above the character feet. Chase camera offsets backward along yaw/pitch by cameraDistance, looks at the pivot, and follows the robot. Model yaw offset corrects the robot asset forward axis without changing movement or camera heading. Camera collision avoidance is not included.":"FIRST PERSON | Eye is feet position plus eye height. Target is one unit along yaw/pitch direction; field of view is 72 degrees vertically. Rotate the attached robot body with yaw, then set the native Space camera.");
        const auto horizontal=add(hud,"api:Canvas:line:Canvas@,float,float,float,float,float,Color",360,60),
            vertical=add(hud,"api:Canvas:line:Canvas@,float,float,float,float,float,Color",830,60);
        chain(hud,0,horizontal);chain(hud,horizontal,vertical);
        for(size_t line:{horizontal,vertical})
        {
            wire(hud,0,"arg_surface",line,"target");value(hud,line,"thickness","2");
            value(hud,line,"color","1,1,1,0.9");
            value(hud,line,"x1",line==horizontal?"632":"640");value(hud,line,"x2",line==horizontal?"648":"640");
            value(hud,line,"y1",line==horizontal?"360":"352");value(hud,line,"y2",line==horizontal?"360":"368");
        }
        note(hud,"HUD | Two native Canvas lines draw a small central crosshair after the world. The virtual canvas is 1280 x 720, so 640,360 is its centre. Add health, ammo or interaction prompts here without affecting the character.");
        if(!ok)return false;
        std::string source,bytes;
        if(!compileBlueprint(candidate,source,error)||!candidate.serialize(bytes,error))return false;
        BlueprintDocument checked;
        if(!checked.load(bytes.data(),bytes.size(),error))return false;
        document=std::move(checked);error.clear();return true;
    }
    bool blueprintStageTemplateMedia(Platform& p,const std::string& root,const std::string& id,std::string& error)
    {
        if(id=="blank"||id=="cinematic"||id=="flight"||id=="rolling-ball"){error.clear();return true;}
        if((id!="first-person"&&id!="third-person")||!blueprintAssetRoot(root)){error="Invalid template media request.";return false;}
        if(!ensure(p,root+"/_global")||!ensure(p,root+"/_global/model"))
        {error="Cannot create template model folder.";return false;}
        size_t total=0;
        return copy(p,"global/model/robot.glb","own/"+root+"/_global/model/robot.glb",total,error);
    }
    bool blueprintAudioAsset(const std::string& relative)
    {
        if(relative.empty()||relative.size()>90)return false;
        size_t at=0;unsigned depth=0;
        for(;;)
        {
            const auto slash=relative.find('/',at);
            if(!blueprintAssetName(relative.substr(at,slash==std::string::npos?slash:slash-at),slash!=std::string::npos))return false;
            if(slash==std::string::npos)break;
            if(++depth>4)return false;
            at=slash+1;
        }
        return relative.ends_with(".wav")||relative.ends_with(".ogg")||relative.ends_with(".adpcm");
    }
    bool blueprintLoadAudioClip(Platform& p,const std::string& root,const std::string& screen,
        const std::string& relative,BlueprintAudioClip& clip,std::string& error)
    {
        if(!blueprintAssetRoot(root)||!folderId(screen)||!blueprintAudioAsset(relative))
        {error="Invalid audio asset path.";return false;}
        FileData file;
        if(!p.readFile(("own/"+root+"/"+screen+"/asset/"+relative).c_str(),file,MemTag::General)||!file.size||file.size>8*1024*1024)
        {error="Cannot read audio asset (8 MiB file limit).";return false;}
        AudioOwner<SoundData,freeSound> sound(loadSound(file.data,file.size));
        if(!sound||sound->kind==SoundKind::Stream||sound->rate<8000||sound->rate>48000||sound->channels<1||sound->channels>2)
        {error="Use a native WAV, Ogg or cooked ADPCM clip at 8-48 kHz.";return false;}
        u32 frames=0;
        const size_t limit=static_cast<size_t>(sound->rate)*12*sound->channels*sizeof(i16);
        i16* pcm=decodeAll(*sound,frames,limit);
        if(!pcm||!frames){memFree(pcm);error="Cannot decode audio or clip exceeds 12 seconds.";return false;}
        BlueprintAudioClip candidate;
        candidate.rate=sound->rate;candidate.channels=sound->channels;
        candidate.pcm.assign(pcm,pcm+static_cast<size_t>(frames)*sound->channels);memFree(pcm);
        clip=std::move(candidate);error.clear();return true;
    }
    bool blueprintEditAudioClip(const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,
        const BlueprintTone& output,std::string& wav,std::string& error)
    {
        if(!toneValid(output,error))return false;
        const auto valid=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
        if(clip.channels<1||clip.channels>2||clip.rate<8000||clip.rate>48000||
            clip.pcm.empty()||clip.pcm.size()%clip.channels||
            clip.pcm.size()/clip.channels>static_cast<size_t>(clip.rate)*12||
            !valid(edit.start,0,1)||!valid(edit.end,0,1)||edit.start>=edit.end||
            !valid(edit.gain,0,2)||!valid(edit.fadeIn,0,1)||!valid(edit.fadeOut,0,1))
        {error="Invalid clip or edit range.";return false;}
        const auto n=static_cast<unsigned>(clip.pcm.size()/clip.channels);
        const auto start=std::min(n-1,static_cast<unsigned>(std::round(static_cast<double>(edit.start)*n)));
        const auto end=std::min(n,std::max(start+1,static_cast<unsigned>(std::round(static_cast<double>(edit.end)*n))));
        const unsigned count=end-start;
        float peak=0;
        if(edit.normalize)for(unsigned i=start;i<end;++i)for(unsigned ch=0;ch<clip.channels;++ch)
            peak=std::max(peak,std::abs(static_cast<float>(clip.pcm[i*clip.channels+ch])));
        const float gain=edit.gain*(edit.normalize&&peak>0?32767.f/peak:1.f);
        const unsigned frames=std::max(1u,static_cast<unsigned>(std::round(static_cast<double>(count)*kMixRate/clip.rate)));
        std::vector<float> stereo(static_cast<size_t>(frames)*2);
        const auto get=[&](unsigned frame,unsigned channel)
        {
            const unsigned source=edit.reverse?end-1-frame:start+frame;
            return static_cast<float>(clip.pcm[source*clip.channels+std::min(channel,clip.channels-1)]);
        };
        for(unsigned i=0;i<frames;++i)
        {
            const double pos=std::min(static_cast<double>(count-1),static_cast<double>(i)*clip.rate/kMixRate);
            const auto lo=static_cast<unsigned>(pos),hi=std::min(count-1,lo+1);
            const float frac=static_cast<float>(pos-lo);
            const float progress=frames>1?static_cast<float>(i)/(frames-1):0;
            const float envelope=std::min(edit.fadeIn>0?std::min(1.f,progress/edit.fadeIn):1.f,
                edit.fadeOut>0?std::min(1.f,(1-progress)/edit.fadeOut):1.f);
            for(unsigned ch=0;ch<2;++ch)
                stereo[2*i+ch]=(get(lo,ch)+(get(hi,ch)-get(lo,ch))*frac)*envelope*gain;
        }
        return renderAudio(output,stereo,wav,error);
    }
    bool blueprintSaveEditedAudio(Platform& p,const std::string& root,const std::string& screen,
        const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,const BlueprintTone& output,
        std::string& relative,std::string& error)
    {
        std::string wav;
        return blueprintEditAudioClip(clip,edit,output,wav,error)&&newToneAsset(p,root,screen,"-edited.wav",wav,relative,error);
    }
    static std::string tonePresetJson(const BlueprintTone& tone)
    {
        // All keys are fixed literals. Write enough significant digits to
        // recover the identical float, even under a non-English desktop locale.
        std::string json="{\"version\":2,\"wave\":"+std::to_string(tone.wave)+",\"rate\":"+std::to_string(tone.rate)+
            ",\"channels\":"+std::to_string(tone.channels)+",\"independentStereo\":"+(tone.independentStereo?"true":"false")+
            ",\"rightWave\":"+std::to_string(tone.rightWave);
        for(const auto& f:rightFields)json+=",\""+std::string(f.name)+"\":"+audioFloatText(tone.*f.field);
        for(const auto& f:toneFields)json+=",\""+std::string(f.name)+"\":"+audioFloatText(tone.*f.field);
        json+=",\"effects\":{";
        bool first=true;
        for(const auto& f:blueprintEffectControls)
        {
            if(!first)json+=',';
            first=false;json+="\""+std::string(f.name)+"\":"+audioFloatText(tone.effects.*f.field);
        }
        json+="}}\n";
        return json;
    }

    bool blueprintSaveTonePreset(Platform& p,const std::string& root,const std::string& screen,
        const BlueprintTone& tone,std::string& relative,std::string& error)
    {
        return toneValid(tone,error)&&newToneAsset(p,root,screen,".bptone.json",tonePresetJson(tone),relative,error);
    }
    static bool readTonePreset(JsonTree::Value obj,BlueprintTone& tone,std::string& error)
    {
        if(!obj["version"].isNumber()||(obj["version"].exact()!=1&&obj["version"].exact()!=2))
        {error="Unsupported audio preset version.";return false;}
        BlueprintTone candidate;
        const auto number=[](JsonTree::Value v,double lo,double hi,double& out)
        {if(!v.isNumber()||!std::isfinite(v.exact())||v.exact()<lo||v.exact()>hi)return false;out=v.exact();return true;};
        double value=0;
        const auto integer=[&](const char* name,unsigned lo,unsigned hi,unsigned& out)
        {if(!number(obj[name],lo,hi,value)||std::floor(value)!=value)return false;out=static_cast<unsigned>(value);return true;};
        if(!integer("wave",0,4,candidate.wave)||!integer("rate",11025,48000,candidate.rate)||
            !integer("channels",1,2,candidate.channels))
        {error="Invalid audio preset format.";return false;}
        for(const auto& f:toneFields)
        {
            // A stored decimal can differ by the last float bit at the bound.
            if(!number(obj[f.name],f.lo-0.000001,f.hi+0.000001,value))
            {error="Invalid preset parameter: "+std::string(f.name);return false;}
            candidate.*f.field=std::clamp(static_cast<float>(value),f.lo,f.hi);
        }
        if(obj["version"].exact()==2)
        {
            if(obj["independentStereo"].type()!=JsonTree::Type::Bool||!integer("rightWave",0,4,candidate.rightWave))
            {error="Invalid stereo preset.";return false;}
            candidate.independentStereo=obj["independentStereo"].flag();
            for(const auto& f:rightFields)
            {
                if(!number(obj[f.name],f.lo-0.000001,f.hi+0.000001,value))
                {error="Invalid right oscillator parameter: "+std::string(f.name);return false;}
                candidate.*f.field=std::clamp(static_cast<float>(value),f.lo,f.hi);
            }
        }
        for(const auto& f:blueprintEffectControls)
        {
            if(!number(obj["effects"][f.name],f.lo-0.000001,f.hi+0.000001,value))
            {error="Invalid preset effect: "+std::string(f.name);return false;}
            candidate.effects.*f.field=std::clamp(static_cast<float>(value),f.lo,f.hi);
        }
        if(!toneValid(candidate,error))return false;
        tone=candidate;error.clear();return true;
    }
    static bool readPresetFile(Platform& p,const std::string& root,const std::string& screen,
        const std::string& relative,const char* suffix,JsonTree& json,std::string& error)
    {
        if(!blueprintAssetRoot(root)||!folderId(screen)||relative.rfind("sound/",0)!=0||
            !relative.ends_with(suffix)||!blueprintAssetName(relative.substr(6)))
        {error="Invalid audio preset path.";return false;}
        FileData file;
        if(!p.readFile(("own/"+root+"/"+screen+"/asset/"+relative).c_str(),file,MemTag::General)||
            !file.size||file.size>16384||!json.parse(reinterpret_cast<const char*>(file.data),file.size)||!json.root().isObject())
        {error="Cannot read audio preset.";return false;}
        return true;
    }
    bool blueprintLoadTonePreset(Platform& p,const std::string& root,const std::string& screen,
        const std::string& relative,BlueprintTone& tone,std::string& error)
    {
        JsonTree json;
        return readPresetFile(p,root,screen,relative,".bptone.json",json,error)&&readTonePreset(json.root(),tone,error);
    }
    namespace
    {
        struct ClipField{const char* name;float BlueprintClipEdit::*field;float lo,hi;};
        constexpr ClipField clipFields[]={{"start",&BlueprintClipEdit::start,0,1},{"end",&BlueprintClipEdit::end,0,1},
            {"gain",&BlueprintClipEdit::gain,0,2},{"fadeIn",&BlueprintClipEdit::fadeIn,0,1},{"fadeOut",&BlueprintClipEdit::fadeOut,0,1}};
        unsigned clipFingerprint(const BlueprintAudioClip& clip)
        {
            unsigned hash=2166136261u;
            const auto byte=[&](unsigned v){hash=(hash^(v&255))*16777619u;};
            for(unsigned v:{clip.rate,clip.channels})for(unsigned i=0;i<4;++i)byte(v>>(8*i));
            for(auto sample:clip.pcm){const unsigned v=static_cast<unsigned>(sample);byte(v);byte(v>>8);}
            return hash;
        }
    }
    bool blueprintSaveClipPreset(Platform& p,const std::string& root,const std::string& screen,
        const std::string& source,const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,
        const BlueprintTone& output,std::string& relative,std::string& error)
    {
        BlueprintAudioClip current;
        if(!blueprintLoadAudioClip(p,root,screen,source,current,error))return false;
        if(current.rate!=clip.rate||current.channels!=clip.channels||current.pcm!=clip.pcm)
        {error="The source audio changed; reopen it before saving edits.";return false;}
        std::string validated;
        if(!blueprintEditAudioClip(clip,edit,output,validated,error))return false;
        std::string json="{\"version\":1,\"source\":\""+source+"\",\"fingerprint\":"+std::to_string(clipFingerprint(clip))+
            ",\"reverse\":"+(edit.reverse?"true":"false")+",\"normalize\":"+(edit.normalize?"true":"false");
        for(const auto& f:clipFields)json+=",\""+std::string(f.name)+"\":"+audioFloatText(edit.*f.field);
        json+=",\"output\":"+tonePresetJson(output)+"}\n";
        return newToneAsset(p,root,screen,".bpclip.json",json,relative,error);
    }
    bool blueprintLoadClipPreset(Platform& p,const std::string& root,const std::string& screen,
        const std::string& relative,std::string& source,BlueprintAudioClip& clip,BlueprintClipEdit& edit,
        BlueprintTone& output,std::string& error)
    {
        JsonTree json;
        if(!readPresetFile(p,root,screen,relative,".bpclip.json",json,error))return false;
        const auto obj=json.root();
        if(!obj["version"].isNumber()||obj["version"].exact()!=1||!obj["source"].isString()||
            !obj["fingerprint"].isNumber()||obj["reverse"].type()!=JsonTree::Type::Bool||obj["normalize"].type()!=JsonTree::Type::Bool)
        {error="Invalid clip preset format.";return false;}
        BlueprintClipEdit candidate;BlueprintTone settings;BlueprintAudioClip decoded;
        candidate.reverse=obj["reverse"].flag();candidate.normalize=obj["normalize"].flag();
        for(const auto& f:clipFields)
        {
            const auto v=obj[f.name];
            if(!v.isNumber()||!std::isfinite(v.exact())||v.exact()<f.lo||v.exact()>f.hi)
            {error="Invalid clip preset parameter.";return false;}
            candidate.*f.field=static_cast<float>(v.exact());
        }
        const std::string path=obj["source"].text();
        if(!readTonePreset(obj["output"],settings,error)||!blueprintLoadAudioClip(p,root,screen,path,decoded,error))return false;
        if(obj["fingerprint"].exact()!=clipFingerprint(decoded))
        {error="The clip preset's source audio changed.";return false;}
        std::string validated;
        if(!blueprintEditAudioClip(decoded,candidate,settings,validated,error))return false;
        source=path;clip=std::move(decoded);edit=candidate;output=settings;error.clear();return true;
    }
    bool blueprintSaveTone(Platform& p,const std::string& root,const std::string& screen,
        const BlueprintTone& tone,std::string& relative,std::string& error)
    {
        std::string wav;
        return blueprintToneWave(tone,wav,error)&&newToneAsset(p,root,screen,".wav",wav,relative,error);
    }
    bool blueprintCreateAssetRoot(Platform& p,std::string& root,std::string& error)
    {
        static unsigned sequence=0;
        for(unsigned i=0;i<100;++i)
        {
            const auto candidate="bpa-"+std::to_string(static_cast<long long>(std::time(nullptr)))+"-"+std::to_string(++sequence);
            if(blueprintAssetRoot(candidate)&&p.createOwnFolder(candidate.c_str())){root=candidate;error.clear();return true;}
        }
        error="Cannot create project asset directory.";return false;
    }
    bool blueprintEnsureAssetFolder(Platform& p,const std::string& root,const std::string& screen,std::string& error)
    {
        if(!directoryRoot(root)||!folderId(screen)||!p.fileTime(("own/"+root).c_str()))
        {error="Invalid or missing project asset root.";return false;}
        if(!ensure(p,root+"/"+screen)||!ensure(p,root+"/"+screen+"/asset"))
        {error="Cannot create screen asset directory.";return false;}
        error.clear();return true;
    }
    bool blueprintListAssets(Platform& p,const std::string& root,const std::string& screen,std::vector<std::string>& files,std::string& error)
    {
        if(root.empty()){files.clear();error.clear();return true;}
        if(!blueprintAssetRoot(root)||!folderId(screen)||!p.fileTime(("own/"+root).c_str()))
        {error="Missing or invalid project asset root.";return false;}
        std::vector<std::string> result;size_t directories=0;
        if(!collect(p,"own/"+root+"/"+screen+"/asset","",0,directories,result,error))return false;
        std::sort(result.begin(),result.end());files.swap(result);error.clear();return true;
    }
    bool blueprintCopyAssets(Platform& p,const std::string& source,const std::string& destination,const std::string& screen,std::string& error)
    {
        std::vector<std::string> files;
        if(!blueprintListAssets(p,source,screen,files,error))return false;
        if(files.empty())return true;
        if(source==destination){error="Source and destination asset folders must differ.";return false;}
        if(!blueprintEnsureAssetFolder(p,destination,screen,error))return false;
        const auto dest=destination+"/"+screen+"/asset",base="own/"+source+"/"+screen+"/asset/";
        size_t total=0;
        for(const auto& file:files)
        {
            for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create asset subfolder.";return false;}
            if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
        }
        error.clear();return true;
    }
    bool blueprintPrepareSceneEditor(Platform& p,const std::string& root,const std::string& screen,std::string& error)
    {
        if(!blueprintAssetRoot(root)||!blueprintEnsureAssetFolder(p,root,screen,error))return false;
        const auto base="own/"+root+"/"+screen+"/asset/";
        const auto manifest=base+"scenes.json";
        const auto readScene=[&](const std::string& file)
        {
            FileData data;JsonTree tree;
            if(!p.readFile((base+file).c_str(),data,MemTag::General)||data.size>1024*1024||
                !tree.parse(reinterpret_cast<const char*>(data.data),data.size)||!tree.root().isObject())
            {error="Scene JSON is missing, too large or invalid: "+file;return false;}
            return true;
        };
        if(p.fileTime(manifest.c_str()))
        {
            FileData data;JsonTree tree;
            if(!p.readFile(manifest.c_str(),data,MemTag::General)||data.size>1024*1024||
                !tree.parse(reinterpret_cast<const char*>(data.data),data.size)||!tree.root().isObject())
            {error="Invalid native scenes.json. Existing assets were not changed.";return false;}
            auto scenes=tree.root()["scenes"];
            if(!scenes.isArray()||!scenes.count()||scenes.count()>12){error="Invalid native scene list.";return false;}
            std::vector<std::string> seen;
            for(auto s=scenes.first();s.exists();s=s.next())
            {
                const std::string name=s["file"].text();
                const bool valid=name=="scene"||(name.size()>5&&name.size()<=11&&name.rfind("scene",0)==0&&name.find_first_not_of("0123456789",5)==std::string::npos);
                if(!valid||std::find(seen.begin(),seen.end(),name)!=seen.end()||!s["name"].isString())
                {error="Invalid or duplicate native scene entry.";return false;}
                seen.push_back(name);
                if(!readScene(name+".json"))return false;
            }
            error.clear();return true;
        }
        // Preserve an imported scene.json. A new screen starts empty, not with the showcase village.
        if(p.fileTime((base+"scene.json").c_str()))
        {
            if(!readScene("scene.json"))return false;
        }
        else
        {
            const std::string blank="{\"ground\":1,\"hour\":0,\"entities\":[]}";
            if(!p.writeFile((base+"scene.json").c_str(),blank.data(),blank.size())||!readScene("scene.json"))
            {error="Cannot create initial scene.";return false;}
        }
        const std::string list="{\"last\":0,\"scenes\":[{\"name\":\"Scene\",\"file\":\"scene\"}]}";
        if(!p.writeFile(manifest.c_str(),list.data(),list.size()))
        {error="Cannot create native scene list.";return false;}
        FileData check;
        if(!p.readFile(manifest.c_str(),check,MemTag::General)||check.size!=list.size()||std::memcmp(check.data,list.data(),list.size())!=0)
        {error="Could not verify native scene list.";return false;}
        error.clear();return true;
    }
    bool blueprintScenePreview(Platform& p,const std::string& root,const std::string& screen,std::string& json,unsigned& omitted,std::string& error)
    {
        json="[]";omitted=0;error.clear();
        if(!blueprintAssetRoot(root)||!folderId(screen)){error="Invalid scene preview path.";return false;}
        const auto base="own/"+root+"/"+screen+"/asset/";
        std::string name="scene";
        FileData manifest;
        if(p.readFile((base+"scenes.json").c_str(),manifest,MemTag::General))
        {
            JsonTree tree;
            if(manifest.size>1024*1024||!tree.parse(reinterpret_cast<const char*>(manifest.data),manifest.size))
            {error="Invalid scene preview manifest.";return false;}
            const auto scenes=tree.root()["scenes"],last=tree.root()["last"];
            const double index=last.isNumber()?last.exact():0;
            if(!scenes.isArray()||!scenes.count()||scenes.count()>12||!std::isfinite(index)||index<0||index>=scenes.count()||std::floor(index)!=index)
            {error="Invalid selected native scene.";return false;}
            name=scenes[static_cast<u32>(index)]["file"].text();
            if(name!="scene"&&!(name.size()>5&&name.size()<=11&&name.rfind("scene",0)==0&&name.find_first_not_of("0123456789",5)==std::string::npos))
            {error="Unsafe native scene filename.";return false;}
        }
        FileData file;
        if(!p.readFile((base+name+".json").c_str(),file,MemTag::General))return true;
        JsonTree tree;
        if(file.size>1024*1024||!tree.parse(reinterpret_cast<const char*>(file.data),file.size)||!tree.root().isObject())
        {error="Invalid native scene preview.";return false;}
        const auto entities=tree.root()["entities"];
        if(!entities.exists())return true;
        if(!entities.isArray()){error="Native scene entities are invalid.";return false;}
        JsonValue out;out.setArray();
        for(u32 i=0;i<entities.count();++i)
        {
            const auto entity=entities[i];
            if(i>=512||!entity.isObject()||(!entity["model"].isString()&&!entity["shape"].isString()&&!entity["sprite"].isString())){++omitted;continue;}
            // Explicit allowlist: no behaviour, cameras, sounds, physics, scripts,
            // triggers, emitters or arbitrary asset-loading callbacks.
            JsonValue safe;
            for(auto field=entity.first();field.exists();field=field.next())
            {
                const std::string key=field.key();
                if(key!="place"&&key!="angles"&&key!="size"&&key!="model"&&key!="skin"&&key!="sprite"&&key!="shape"&&key!="color"&&key!="shine"&&key!="glowing"&&key!="twoSided"&&key!="shadow"&&key!="views"&&key!="frames")continue;
                if(auto* value=safe.put(key))value->read(field);
            }
            if(auto* entry=out.add())entry->setCopy(safe);
        }
        json=out.write(true);return true;
    }
    std::string blueprintAssetImportPath(const std::string& file)
    {
        if(!blueprintAssetName(file))return "";
        const auto ext=file.substr(file.find_last_of('.')==std::string::npos?file.size():file.find_last_of('.'));
        if(ext==".fnt")return "font/"+file;
        for(const char* type:{".obj",".glb",".gltf",".stl",".fbx",".dae"})
            if(ext==type)return "model/"+file;
        for(const char* type:{".png",".jpg",".jpeg",".tga",".bmp",".dds"})
            if(ext==type)return "picture/"+file;
        for(const char* type:{".wav",".ogg",".adpcm",".mp3",".flac"})
            if(ext==type)return "sound/"+file;
        return file;
    }
    bool blueprintImportProjectFolderName(const std::string& folder)
    {
        if(!blueprintAssetName(folder,true))return false;
        if(folder.size()>8&&folder.rfind("project-",0)==0)return true;
        if(folder.rfind("bp-",0)!=0||folder.size()>32)return false;
        const auto stamp=folder.substr(3);
        const auto separator=stamp.find('-');
        return separator!=std::string::npos&&separator>0&&separator+1<stamp.size()&&
            stamp.substr(0,separator).find_first_not_of("0123456789")==std::string::npos&&
            stamp.substr(separator+1).find_first_not_of("0123456789")==std::string::npos;
    }
    bool blueprintListSharedAssets(Platform& p,const std::string& root,std::vector<std::string>& files,std::string& error)
    {
        if(!blueprintAssetRoot(root)||!p.fileTime(("own/"+root).c_str()))
        {error="Missing or invalid project asset root.";return false;}
        std::vector<std::string> result;size_t directories=0;
        const auto base="own/"+root+"/_global";
        if(p.fileTime(base.c_str())&&!collect(p,base,"",0,directories,result,error))return false;
        std::sort(result.begin(),result.end());files.swap(result);error.clear();return true;
    }
    bool blueprintCopySharedAssets(Platform& p,const std::string& source,const std::string& destination,std::string& error)
    {
        if(source.empty()){error.clear();return true;}
        if(!blueprintAssetRoot(source)||!directoryRoot(destination)||source==destination||
            !p.fileTime(("own/"+source).c_str())||!p.fileTime(("own/"+destination).c_str()))
        {error="Invalid shared asset copy roots.";return false;}
        const auto base="own/"+source+"/_global";
        if(!p.fileTime(base.c_str())){error.clear();return true;}
        std::vector<std::string> files;size_t directories=0,total=0;
        if(!collect(p,base,"",0,directories,files,error)||!copyShared(p,source,destination,files,total,error))return false;
        error.clear();return true;
    }
    bool blueprintImportFolderAssets(Platform& p,const std::string& folder,const std::vector<std::string>& screens,std::string& root,std::string& error)
    {
        if(!blueprintImportProjectFolderName(folder)||screens.empty()||screens.size()>10)
        {error="Invalid native project folder.";return false;}
        std::vector<std::vector<std::string>> collected(screens.size());
        size_t directories=0,totalFiles=0;
        for(size_t i=0;i<screens.size();++i)
        {
            if(!folderId(screens[i])){error="Invalid screen asset folder.";return false;}
            const auto base="own/"+folder+"/"+screens[i]+"/asset";
            if(!p.fileTime(base.c_str()))continue;
            if(!collect(p,base,"",0,directories,collected[i],error))return false;
            totalFiles+=collected[i].size();
            if(totalFiles>kMaxFiles){error="Project asset count limit exceeded.";return false;}
        }
        std::vector<std::string> shared;
        const auto sharedBase="own/"+folder+"/_global";
        if(p.fileTime(sharedBase.c_str())&&!collect(p,sharedBase,"",0,directories,shared,error))return false;
        if(shared.size()>kMaxFiles-totalFiles){error="Project asset count limit exceeded.";return false;}
        std::string fresh;
        if(!blueprintCreateAssetRoot(p,fresh,error))return false;
        size_t total=0;
        if(!copyShared(p,folder,fresh,shared,total,error))return false;
        for(size_t i=0;i<screens.size();++i)
        {
            if(!blueprintEnsureAssetFolder(p,fresh,screens[i],error))return false;
            const auto dest=fresh+"/"+screens[i]+"/asset",base="own/"+folder+"/"+screens[i]+"/asset/";
            for(const auto& file:collected[i])
            {
                for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                    if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create imported asset subfolder.";return false;}
                if(!copy(p,base+file,"own/"+dest+"/"+file,total,error))return false;
            }
        }
        root=std::move(fresh);error.clear();return true;
    }

    bool blueprintImportSourceAssets(Platform& p,const std::string& sourceFile,std::string& root,std::string& error)
    {
        if(!sourceFile.ends_with(".as")||sourceFile.find_first_of("/\\:")!=std::string::npos)
        {error="Invalid standalone source filename.";return false;}
        const auto stem=sourceFile.substr(0,sourceFile.size()-3);
        if(!blueprintAssetName(stem,true)){root.clear();error.clear();return true;}
        const auto base="own/"+stem+"/asset";
        if(!p.fileTime(base.c_str())){root.clear();error.clear();return true;}
        std::vector<std::string> files;size_t directories=0;
        if(!collect(p,base,"",0,directories,files,error))return false;
        if(files.empty()){root.clear();error.clear();return true;}
        std::string fresh;
        if(!blueprintCreateAssetRoot(p,fresh,error)||!blueprintEnsureAssetFolder(p,fresh,"Screen",error))return false;
        const auto dest=fresh+"/Screen/asset";
        size_t total=0;
        for(const auto& file:files)
        {
            for(size_t at=file.find('/');at!=std::string::npos;at=file.find('/',at+1))
                if(!ensure(p,dest+"/"+file.substr(0,at))){error="Cannot create imported asset folder.";return false;}
            if(!copy(p,base+"/"+file,"own/"+dest+"/"+file,total,error))return false;
        }
        root=std::move(fresh);error.clear();return true;
    }
    bool blueprintImportAsset(Platform& p,const std::string& root,const std::string& screen,const std::string& file,std::string& error,bool shared)
    {
        if(!blueprintAssetName(file)){error="Use a lowercase portable asset filename (letters, digits, _, -, .).";return false;}
        if(!blueprintAssetRoot(root)){error="Invalid project asset root.";return false;}
        if(shared)
        {
            if(!p.fileTime(("own/"+root).c_str())||!ensure(p,root+"/_global"))
            {error="Cannot create shared project asset directory.";return false;}
        }
        else if(!blueprintEnsureAssetFolder(p,root,screen,error))return false;
        const auto relative=blueprintAssetImportPath(file),dest=shared?root+"/_global/":root+"/"+screen+"/asset/";
        const auto slash=relative.find('/');
        if(slash!=std::string::npos&&!ensure(p,dest+relative.substr(0,slash)))
        {error="Cannot create native asset type folder.";return false;}
        size_t total=0;
        if(file.ends_with(".fnt"))
        {
            // Use the same descriptor parser and texture formats as the runtime.
            // Keep the atlas beside its descriptor, not in the generic picture folder.
            const auto target="own/"+dest+relative;
            if(p.fileTime(target.c_str())){error="Asset already exists: "+target;return false;}
            FileData descriptor;
            if(!p.readFile(("own/"+file).c_str(),descriptor,MemTag::General)||!descriptor.size||descriptor.size>1024*1024)
            {error="Cannot read bitmap font descriptor (maximum 1 MiB).";return false;}
            const std::string text(reinterpret_cast<const char*>(descriptor.data),descriptor.size);
            FontData font{};char page[96]={};u32 w=0,h=0;
            if(!parseBitmapFont(text.c_str(),text.size(),font,page,sizeof(page),w,h)||
                !blueprintAssetName(page)||std::string(page)==file||!w||!h||w>2048||h>2048)
            {error="Unsupported bitmap font: use a single-page text BMFont with a portable atlas name and size up to 2048.";return false;}
            const auto sourcePage="own/"+std::string(page),targetPage="own/"+dest+"font/"+page;
            FileData staged,existing;
            const bool hasStaged=p.readFile(sourcePage.c_str(),staged,MemTag::General);
            const bool hasExisting=p.readFile(targetPage.c_str(),existing,MemTag::General);
            if(!hasStaged&&!hasExisting)
            {error="Missing bitmap font atlas: "+std::string(page)+". Drop the atlas and descriptor together, or stage the atlas first.";return false;}
            if(hasStaged&&hasExisting&&(staged.size!=existing.size||std::memcmp(staged.data,existing.data,staged.size)!=0))
            {error="A different bitmap font atlas already exists: "+std::string(page);return false;}
            const auto& image=hasStaged?staged:existing;
            if(!image.size||image.size>kMaxFile)
            {error="Invalid bitmap font atlas size.";return false;}
            u32 iw=0,ih=0;gpu::TextureData cooked;
            if(readCookedTexture(image.data,image.size,cooked)){iw=cooked.width;ih=cooked.height;}
            else if(imageSize(image.data,image.size,iw,ih)&&iw==w&&ih==h)
            {
                auto* pixels=decodeImage(image.data,image.size,iw,ih);
                if(!pixels){error="Cannot decode bitmap font atlas.";return false;}
                freeImage(pixels);
            }
            if(iw!=w||ih!=h)
            {error="Bitmap font atlas dimensions do not match the descriptor.";return false;}
            // Publish the descriptor last so a failed copy never advertises a missing atlas.
            if(!hasExisting&&!copy(p,sourcePage,targetPage,total,error))return false;
            return copy(p,"own/"+file,target,total,error);
        }
        return copy(p,"own/"+file,"own/"+dest+relative,total,error);
    }

    namespace
    {
        // Standard stored ZIP entries. No encryption, data descriptors, ZIP64,
        // compressed data, links or directory records are accepted by this package reader.
        constexpr size_t packageLimit=kMaxTotal+4*1024*1024;
        struct PackageEntry {std::string name;size_t offset=0,size=0;u32 crc=0;};
        void put16(std::string& s,u32 v){s+=static_cast<char>(v&255);s+=static_cast<char>((v>>8)&255);}
        void put32(std::string& s,u32 v){put16(s,v);put16(s,v>>16);}
        u32 get16(const u8* p){return static_cast<u32>(p[0])|(static_cast<u32>(p[1])<<8);}
        u32 get32(const u8* p){return get16(p)|(get16(p+2)<<16);}
        bool range(size_t at,size_t count,size_t limit){return at<=limit&&count<=limit-at;}
        bool relativeAsset(const std::string& path)
        {
            if(path.empty()||path.size()>90)return false;
            size_t start=0;unsigned depth=0;
            for(;;)
            {
                const auto slash=path.find('/',start);
                if(!blueprintAssetName(path.substr(start,slash==std::string::npos?slash:slash-start),slash!=std::string::npos))return false;
                if(slash==std::string::npos)return true;
                if(++depth>4)return false;
                start=slash+1;
            }
        }
        bool packageAsset(const std::string& name,const std::vector<std::string>& screens)
        {
            if(name.rfind("assets/_global/",0)==0)return relativeAsset(name.substr(15));
            for(const auto& screen:screens)
            {
                const auto prefix="assets/"+screen+"/asset/";
                if(name.rfind(prefix,0)==0)return relativeAsset(name.substr(prefix.size()));
            }
            return false;
        }
        bool appendPackage(std::string& zip,std::vector<PackageEntry>& entries,const std::string& name,const void* bytes,size_t size,std::string& error)
        {
            if(size>kMaxFile||entries.size()>=kMaxFiles+1||!range(zip.size(),size+30+name.size(),packageLimit))
            {error="Project package size or file limit exceeded.";return false;}
            const auto crc=crc32Of(static_cast<const u8*>(bytes),size);
            entries.push_back({name,zip.size(),size,crc});
            put32(zip,0x04034b50);put16(zip,20);put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,33);
            put32(zip,crc);put32(zip,static_cast<u32>(size));put32(zip,static_cast<u32>(size));
            put16(zip,static_cast<u32>(name.size()));put16(zip,0);zip+=name;
            if(size)zip.append(static_cast<const char*>(bytes),size);
            return true;
        }
        bool finishPackage(std::string& zip,const std::vector<PackageEntry>& entries)
        {
            const size_t central=zip.size();
            for(const auto& e:entries)
            {
                if(!range(zip.size(),46+e.name.size(),packageLimit))return false;
                put32(zip,0x02014b50);put16(zip,20);put16(zip,20);put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,33);
                put32(zip,e.crc);put32(zip,static_cast<u32>(e.size));put32(zip,static_cast<u32>(e.size));
                put16(zip,static_cast<u32>(e.name.size()));put16(zip,0);put16(zip,0);put16(zip,0);put16(zip,0);put32(zip,0);
                put32(zip,static_cast<u32>(e.offset));zip+=e.name;
            }
            if(!range(zip.size(),22,packageLimit))return false;
            const auto directorySize=static_cast<u32>(zip.size()-central);
            put32(zip,0x06054b50);put16(zip,0);put16(zip,0);
            put16(zip,static_cast<u32>(entries.size()));put16(zip,static_cast<u32>(entries.size()));
            put32(zip,directorySize);put32(zip,static_cast<u32>(central));put16(zip,0);return true;
        }
        bool readPackage(const FileData& data,std::vector<PackageEntry>& entries,std::string& error)
        {
            const auto fail=[&](){error="Invalid, unsupported or damaged project ZIP (stored entries only).";return false;};
            const auto* bytes=static_cast<const u8*>(data.data);const size_t n=data.size;
            if(n<22||n>packageLimit)return fail();
            const size_t end=n-22;const auto* footer=bytes+end;
            if(get32(footer)!=0x06054b50||get16(footer+4)||get16(footer+6)||get16(footer+20))return fail();
            const size_t count=get16(footer+10),central=get32(footer+16);
            if(count==0||count>kMaxFiles+1||get16(footer+8)!=count||!range(central,get32(footer+12),end)||central+get32(footer+12)!=end)return fail();
            size_t at=central,nextLocal=0,total=0;std::unordered_set<std::string> names;
            for(size_t i=0;i<count;++i)
            {
                if(!range(at,46,end))return fail();
                const auto* e=bytes+at;
                const size_t length=get16(e+28),size=get32(e+24),local=get32(e+42);
                const u32 crc=get32(e+16);
                if(get32(e)!=0x02014b50||get16(e+8)||get16(e+10)||get16(e+30)||get16(e+32)||get16(e+34)||
                    get32(e+38)||get32(e+20)!=size||size>kMaxFile||length==0||length>128||!range(at+46,length,end)||local!=nextLocal||
                    !range(local,30,central)||!range(local+30,length,central))return fail();
                const auto* h=bytes+local;
                if(get32(h)!=0x04034b50||get16(h+6)||get16(h+8)||get16(h+28)||get16(h+26)!=length||
                    get32(h+14)!=crc||get32(h+18)!=size||get32(h+22)!=size||std::memcmp(h+30,e+46,length)!=0)return fail();
                const size_t payload=local+30+length;
                if(!range(payload,size,central)||size>kMaxTotal-total||crc32Of(bytes+payload,size)!=crc)return fail();
                std::string name(reinterpret_cast<const char*>(e+46),length);
                if(!names.insert(name).second)return fail();
                entries.push_back({std::move(name),payload,size,crc});
                total+=size;nextLocal=payload+size;at+=46+length;
            }
            if(at!=end||nextLocal!=central)return fail();
            return true;
        }
    }
    bool blueprintPackageName(const std::string& file)
    {
        if(!file.ends_with(".bp.zip")||file.size()>100||file.size()<=7)return false;
        const auto stem=file.substr(0,file.size()-7);
        std::string ignored;
        return blueprintManagedName(stem,ignored);
    }
    bool blueprintExportPackage(Platform& p,const BlueprintDocument& doc,const std::string& file,std::string& error)
    {
        const auto target="own/"+file;
        if(!blueprintPackageName(file)||p.fileTime(target.c_str())){error="Invalid package filename or package already exists.";return false;}
        std::string graph;
        if(!doc.serialize(graph,error))return false;
        BlueprintDocument portable;
        if(!portable.load(graph.data(),graph.size(),error))return false;
        portable.assetRoot.clear();
        if(!portable.serialize(graph,error))return false;
        std::string zip;std::vector<PackageEntry> entries;
        if(!appendPackage(zip,entries,"project.bp.json",graph.data(),graph.size(),error))return false;
        size_t total=graph.size(),directories=0;
        if(!doc.assetRoot.empty())
        {
            if(!blueprintAssetRoot(doc.assetRoot)||!p.fileTime(("own/"+doc.assetRoot).c_str()))
            {error="Project asset directory is missing.";return false;}
            std::vector<std::string> scopes={"_global"};
            for(const auto& screen:doc.screens())scopes.push_back(screen+"/asset");
            for(const auto& scope:scopes)
            {
                const auto base="own/"+doc.assetRoot+"/"+scope;
                if(!p.fileTime(base.c_str()))continue;
                std::vector<std::string> files;
                if(!collect(p,base,"",0,directories,files,error))return false;
                for(const auto& name:files)
                {
                    FileData data;
                    if(!p.readFile((base+"/"+name).c_str(),data,MemTag::General)||data.size>kMaxFile||data.size>kMaxTotal-total)
                    {error="Cannot read package asset or size limit exceeded: "+name;return false;}
                    total+=data.size;
                    if(!appendPackage(zip,entries,"assets/"+scope+"/"+name,data.data,data.size,error))return false;
                }
            }
        }
        if(!finishPackage(zip,entries)){error="Project ZIP exceeds package limit.";return false;}
        if(!p.writeFile(target.c_str(),zip.data(),zip.size())){error="Cannot write project package.";return false;}
        FileData check;
        if(!p.readFile(target.c_str(),check,MemTag::General)||check.size!=zip.size()||std::memcmp(check.data,zip.data(),zip.size())!=0)
        {error="Package write verification failed.";return false;}
        error.clear();return true;
    }
    bool blueprintImportPackage(Platform& p,const std::string& file,BlueprintDocument& document,std::string& error)
    {
        if(!blueprintPackageName(file)){error="Choose a .bp.zip project package.";return false;}
        FileData data;
        if(!p.readFile(("own/"+file).c_str(),data,MemTag::General)){error="Cannot read project package.";return false;}
        std::vector<PackageEntry> entries;
        if(!readPackage(data,entries,error))return false;
        const auto graph=std::find_if(entries.begin(),entries.end(),[](const PackageEntry& e){return e.name=="project.bp.json";});
        if(graph==entries.end()){error="Package has no project.bp.json.";return false;}
        const auto* bytes=reinterpret_cast<const char*>(data.data);
        BlueprintDocument candidate;
        if(!candidate.load(bytes+graph->offset,graph->size,error))return false;
        if(!candidate.assetRoot.empty()){error="Package graph must not refer to an external asset root.";return false;}
        const auto screens=candidate.screens();
        // Validate every member before creating a directory or writing files.
        for(const auto& e:entries)if(e.name!="project.bp.json"&&!packageAsset(e.name,screens))
        {error="Invalid package asset path: "+e.name;return false;}
        if(entries.size()>1)
        {
            std::string fresh;
            if(!blueprintCreateAssetRoot(p,fresh,error))return false;
            for(const auto& screen:screens)if(!blueprintEnsureAssetFolder(p,fresh,screen,error))return false;
            for(const auto& e:entries)
            {
                if(e.name=="project.bp.json")continue;
                const auto rel=e.name.substr(7),dest=fresh+"/"+rel;
                for(size_t at=rel.find('/');at!=std::string::npos;at=rel.find('/',at+1))
                    if(!ensure(p,fresh+"/"+rel.substr(0,at))){error="Cannot create package folder.";return false;}
                const auto target="own/"+dest;
                if(p.fileTime(target.c_str())||!p.writeFile(target.c_str(),bytes+e.offset,e.size))
                {error="Cannot restore package asset: "+rel;return false;}
                FileData check;
                if(!p.readFile(target.c_str(),check,MemTag::General)||check.size!=e.size||(e.size&&std::memcmp(check.data,bytes+e.offset,e.size)!=0))
                {error="Package asset verification failed: "+rel;return false;}
            }
            candidate.assetRoot=std::move(fresh);
        }
        candidate.markRecovered();document=std::move(candidate);error.clear();return true;
    }
}
