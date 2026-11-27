#pragma once
#include "tools/blueprint/native_compile.h"

class asIScriptContext;

namespace nx
{
    struct Services;
    class Canvas;
    struct Asset;
    class ScriptDebugger;

    // A desktop-only workspace in the existing Redux window.
    class BlueprintWorkspace
    {
    public:
        bool active() const { return m_font != nullptr; }
        void open(Services& services);
        void close(Services& services);
        void update(Services& services);
        bool previewing() const { return m_playing; }
        bool tracePaused() const { return m_playing&&m_tracePaused; }
        void updatePreview(Services& services, float dt);
        void stopPreview(Services& services);
        void draw(Canvas& canvas);
        void drawPreviewOverlay(Canvas& canvas);
        void recover(Services& services);
        void checkpoint(Services& services,bool force=false);
    private:
        void selectGraph(size_t index);
        void fitGraph();
        void rebuildPins();
        void filterCatalogue();
        void clearWire(){m_pending=static_cast<size_t>(-1);m_pendingPin.clear();}
        bool saveNamed(Services& services,const std::string& name);
        bool openFile(Services& services,const std::string& file,bool managed);
        bool importSource(Services& services,const std::string& file);
        void browseFiles(Services& services);
        void browseAssets(Services& services);
        void launchSceneEditor(Services& services);
        bool m_sceneEditor=false,m_sceneExit=false;
        void importAsset(Services& services,const std::string& file);
        std::string assetScreen() const;
        bool m_assetBrowser=false;
        std::vector<std::string> m_assetFiles;
        size_t m_assetFirst=0;
        bool saveCurrent(Services& services);
        bool writeVerified(Services& services,const std::string& path,const std::string& bytes);
        bool compileAndExport(Services& services);
        bool validateProject(Services& services,const std::vector<BlueprintScreenOutput>& screens);
        void focusDiagnostic(BlueprintLocation location);
        void focusPreviewError(const std::string& error);
        bool startTrace(Services& services);
        void stopTrace();
        void pauseTrace(Services& services);
        void resumeTrace(Services& services,bool step);
        void traceLine(const char* section,int line,unsigned run,asIScriptContext* context);
        struct TraceHit { BlueprintLocation location; int line; unsigned run; };
        ScriptDebugger* m_traceDebugger=nullptr;
        bool m_traceEnabled=false,m_tracePaused=false,m_tracePending=false,m_traceStep=false,m_traceKey=false;
        bool m_tracePointer=false,m_traceTyping=false;
        unsigned m_traceBudget=0;
        std::vector<TraceHit> m_traceHits;
        BlueprintLocation m_traceLocation;
        std::vector<std::string> m_traceValues;
        std::vector<BlueprintScreenOutput> m_previewOutputs;
        BlueprintLocation m_diagnostic;
        void cancelDrag() { m_drag = false; }
        struct NodePins { std::vector<std::string> inputs, outputs, inputTypes, outputTypes; float height = 72; };
        struct WirePins { size_t output, input; };
        std::vector<NodePins> m_rows;
        std::vector<WirePins> m_wirePins;
        BlueprintDocument m_document;
        Asset* m_font = nullptr;
        std::string m_status = "Drop a .bp.json or .bp.project.json file here.";
        size_t m_graph = 0, m_selection = static_cast<size_t>(-1), m_first = 0;
        float m_x = 0, m_y = 0, m_zoom = 1, m_lastX = 0, m_lastY = 0;
        bool m_left = false, m_pan = false, m_wantedPointer = false;
        bool m_drag = false, m_dragMoved = false;
        float m_dragX = 0, m_dragY = 0, m_grabX = 0, m_grabY = 0;
        float m_pressX = 0, m_pressY = 0;
        unsigned m_shortcuts = 0;
        size_t m_pending=static_cast<size_t>(-1),m_paletteFirst=0;
        std::string m_pendingPin,m_query,m_edit;
        std::vector<const BlueprintNodeSpec*> m_palette;
        int m_focus=0; // 1 catalogue search, 2 literal, 3 reference, 4 new function, 5 save as, 6 input value
        std::string m_editPin,m_editType;
        std::string m_filePath,m_fileBaseline;
        std::vector<std::string> m_files;
        size_t m_fileFirst=0;
        bool m_browsing=false;
        bool m_previousTyping=false;
        bool m_playing=false;
        bool m_checkpointPresent=false;
        size_t m_checkpointRevision=static_cast<size_t>(-1);
        double m_nextCheckpoint=0;
    };
}
