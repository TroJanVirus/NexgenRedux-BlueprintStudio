#pragma once
#include "tools/blueprint/native_document.h"
#include "audio/effects.h"
namespace nx
{
    class Platform;
    // Desktop authoring only: bounded PCM, played by the original mixer.
    struct BlueprintTone
    {
        unsigned wave=0,rate=22050;
        float seconds=0.6f,frequency=440,endFrequency=440,amplitude=0.65f;
        float attack=0.04f,decay=0.15f,sustain=0.65f,release=0.25f;
        unsigned channels=1;
        bool independentStereo=false;
        unsigned rightWave=0;
        float rightFrequency=440,rightEndFrequency=440,rightAmplitude=0.65f;
        float rightAttack=0.04f,rightDecay=0.15f,rightSustain=0.65f,rightRelease=0.25f;
        float tail=1.f;
        BusEffectSettings effects;
    };
    struct BlueprintEffectControl {const char* name;float BusEffectSettings::*field;float lo,hi;};
    inline constexpr BlueprintEffectControl blueprintEffectControls[]={
        {"Low pass",&BusEffectSettings::lowPass,20,20000},{"High pass",&BusEffectSettings::highPass,0,20000},
        {"Distortion",&BusEffectSettings::distortion,0,1},{"Threshold",&BusEffectSettings::threshold,-60,0},
        {"Ratio",&BusEffectSettings::ratio,1,20},{"Reverb",&BusEffectSettings::reverb,0,1},
        {"Room",&BusEffectSettings::room,0,1},{"Echo",&BusEffectSettings::echo,0,1},
        {"Delay",&BusEffectSettings::echoDelay,0.02f,0.75f},{"Feedback",&BusEffectSettings::echoFeedback,0,0.9f},
        {"Chorus",&BusEffectSettings::chorus,0,1},{"Chorus Hz",&BusEffectSettings::chorusRate,0.05f,10},
        {"Flanger",&BusEffectSettings::flanger,0,1},{"Flanger Hz",&BusEffectSettings::flangerRate,0.02f,5},
        {"Flanger fb",&BusEffectSettings::flangerFeedback,0,0.9f},{"Low EQ",&BusEffectSettings::low,-24,24},
        {"Mid EQ",&BusEffectSettings::mid,-24,24},{"High EQ",&BusEffectSettings::high,-24,24}
    };
    bool blueprintSaveTonePreset(Platform& platform,const std::string& root,const std::string& screen,
        const BlueprintTone& tone,std::string& relative,std::string& error);
    bool blueprintLoadTonePreset(Platform& platform,const std::string& root,const std::string& screen,
        const std::string& relative,BlueprintTone& tone,std::string& error);
    struct BlueprintAudioClip
    {
        unsigned rate=0,channels=0;
        std::vector<short> pcm;
    };
    struct BlueprintClipEdit
    {
        float start=0,end=1,gain=1,fadeIn=0,fadeOut=0;
        bool reverse=false,normalize=false;
    };
    bool blueprintSaveClipPreset(Platform& platform,const std::string& root,const std::string& screen,
        const std::string& source,const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,
        const BlueprintTone& output,std::string& relative,std::string& error);
    bool blueprintLoadClipPreset(Platform& platform,const std::string& root,const std::string& screen,
        const std::string& relative,std::string& source,BlueprintAudioClip& clip,BlueprintClipEdit& edit,
        BlueprintTone& output,std::string& error);
    bool blueprintAudioAsset(const std::string& relative);
    bool blueprintLoadAudioClip(Platform& platform,const std::string& root,const std::string& screen,
        const std::string& relative,BlueprintAudioClip& clip,std::string& error);
    bool blueprintEditAudioClip(const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,
        const BlueprintTone& output,std::string& wav,std::string& error);
    bool blueprintSaveEditedAudio(Platform& platform,const std::string& root,const std::string& screen,
        const BlueprintAudioClip& clip,const BlueprintClipEdit& edit,const BlueprintTone& output,
        std::string& relative,std::string& error);
    bool blueprintToneWave(const BlueprintTone& tone,std::string& wav,std::string& error);
    bool blueprintAudioGraph(const BlueprintTone& tone,bool stream,BlueprintDocument& document,std::string& error);
    bool blueprintSaveTone(Platform& platform,const std::string& root,const std::string& screen,
        const BlueprintTone& tone,std::string& relative,std::string& error);
    bool blueprintPackageName(const std::string& file);
    bool blueprintExportPackage(Platform& platform,const BlueprintDocument& document,const std::string& file,std::string& error);
    bool blueprintImportPackage(Platform& platform,const std::string& file,BlueprintDocument& document,std::string& error);
    bool blueprintCreateAssetRoot(Platform& platform,std::string& root,std::string& error);
    bool blueprintEnsureAssetFolder(Platform& platform,const std::string& root,const std::string& screen,std::string& error);
    bool blueprintListAssets(Platform& platform,const std::string& root,const std::string& screen,std::vector<std::string>& files,std::string& error);
    bool blueprintCopySharedAssets(Platform& platform,const std::string& source,const std::string& destination,std::string& error);
    bool blueprintCopyAssets(Platform& platform,const std::string& source,const std::string& destination,const std::string& screen,std::string& error);
    bool blueprintPrepareSceneEditor(Platform& platform,const std::string& root,const std::string& screen,std::string& error);
    // Read only the last selected native scene's renderable entities. Never write or execute it.
    bool blueprintScenePreview(Platform& platform,const std::string& root,const std::string& screen,std::string& json,unsigned& omitted,std::string& error);
    std::string blueprintAssetImportPath(const std::string& file);
    // Recognize explicitly staged projects and native export folders.
    bool blueprintImportProjectFolderName(const std::string& folder);
    bool blueprintImportFolderAssets(Platform& platform,const std::string& folder,const std::vector<std::string>& screens,std::string& root,std::string& error);
    // Copy a standalone source's optional companion own/<stem>/asset tree into a fresh project.
    bool blueprintImportSourceAssets(Platform& platform,const std::string& sourceFile,std::string& root,std::string& error);
    bool blueprintImportAsset(Platform& platform,const std::string& root,const std::string& screen,const std::string& file,std::string& error);
}
