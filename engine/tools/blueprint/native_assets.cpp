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
#include <memory>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>
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
        std::unique_ptr<ReverbMemory,decltype(&destroyReverbMemory)> reverb(fx.reverb>0?createReverbMemory():nullptr,destroyReverbMemory);
        std::unique_ptr<EchoMemory,decltype(&destroyEchoMemory)> echo(fx.echo>0?createEchoMemory():nullptr,destroyEchoMemory);
        std::unique_ptr<WaverMemory,decltype(&destroyWaverMemory)> waver((fx.chorus>0||fx.flanger>0)?createWaverMemory():nullptr,destroyWaverMemory);
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
        const auto number=[](float v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(9)<<v;return s.str();};
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
        std::unique_ptr<SoundData,decltype(&freeSound)> sound(loadSound(file.data,file.size),freeSound);
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
        std::ostringstream json;
        json.imbue(std::locale::classic());
        json<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"{\"version\":2,\"wave\":"<<tone.wave<<",\"rate\":"<<tone.rate<<",\"channels\":"<<tone.channels
            <<",\"independentStereo\":"<<(tone.independentStereo?"true":"false")<<",\"rightWave\":"<<tone.rightWave;
        for(const auto& f:rightFields)json<<",\""<<f.name<<"\":"<<(tone.*f.field);
        for(const auto& f:toneFields)json<<",\""<<f.name<<"\":"<<(tone.*f.field);
        json<<",\"effects\":{";
        bool first=true;
        for(const auto& f:blueprintEffectControls)
        {
            if(!first)json<<',';
            first=false;json<<'\"'<<f.name<<"\":"<<(tone.effects.*f.field);
        }
        json<<"}}\n";
        return json.str();
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
        std::ostringstream json;json.imbue(std::locale::classic());
        json<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"{\"version\":1,\"source\":\""<<source<<"\",\"fingerprint\":"<<clipFingerprint(clip)
            <<",\"reverse\":"<<(edit.reverse?"true":"false")<<",\"normalize\":"<<(edit.normalize?"true":"false");
        for(const auto& f:clipFields)json<<",\""<<f.name<<"\":"<<(edit.*f.field);
        json<<",\"output\":"<<tonePresetJson(output)<<"}\n";
        return newToneAsset(p,root,screen,".bpclip.json",json.str(),relative,error);
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
