#pragma once
#include "tools/blueprint/native_compile.h"
#include <utility>

class asIScriptContext;

namespace nx
{
    struct Services;
    class Canvas;
    class Input;
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
        bool recoveryProtected() const
        {return !m_document.dirty()||(m_checkpointPresent&&m_checkpointRevision==m_document.revision()&&m_recoveryWarning.empty());}
    private:
        void returnToRedux(Services& services);
        void recoveryWarning(const std::string& message);
        void selectGraph(size_t index);
        void fitGraph();
        void rebuildPins();
        void filterCatalogue();
        void clearWire(){m_pending=static_cast<size_t>(-1);m_pendingPin.clear();m_pinDrag=false;m_pendingOutput=true;}
        void openPinMenu(float x,float y);
        void closePinMenu(Services& services);
        bool saveNamed(Services& services,const std::string& name);
        bool openFile(Services& services,const std::string& file,bool managed);
        bool importSource(Services& services,const std::string& file);
        bool importProjectFolder(Services& services,const std::string& folder);
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
        struct TraceHit { BlueprintLocation location; int line; unsigned run; std::vector<std::string> values{},stack{}; };
        ScriptDebugger* m_traceDebugger=nullptr;
        bool m_traceEnabled=false,m_tracePaused=false,m_tracePending=false,m_traceStep=false,m_traceKey=false;
        bool m_tracePointer=false,m_traceTyping=false;
        unsigned m_traceBudget=0;
        std::vector<TraceHit> m_traceHits;
        size_t m_traceSelected=0,m_traceFirst=0,m_traceDetailFirst=0;
        bool m_traceShowStack=false;
        void focusTraceNode();
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
        std::string m_status = "Drop a .bp.json or supported .as file here.";
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
        std::vector<std::string> m_inputComponents,m_inputChoices;
        size_t m_inputComponent=0,m_inputChoiceFirst=0;
        bool m_inputReplace=false;
        void prepareInputEditor(size_t node);
        struct InputDetail { std::string name,type,value,origin; bool editable=false; };
        std::vector<InputDetail> inputDetails(size_t node) const;
        size_t m_inspectorFirst=0;
        bool beginInputEdit(size_t node,const std::string& pin,Input& input);
        std::vector<std::string> namedSceneTargets() const;
        struct VariableRow { BlueprintDocument::Field field; bool global=false,explicitField=true; };
        std::vector<VariableRow> variableRows() const;
        void resetVariableEditor(bool global=false);
        size_t m_variableFirst=0;
        std::string m_variableOld,m_variableName,m_variableType="int",m_variableValue;
        bool m_variableGlobal=false,m_variableInitialized=false,m_variableTypes=false;
        int m_variableInput=0; // 1 name, 2 initial value
        size_t m_quickVariableFirst=0;
        VariableRow m_quickVariable{};
        bool m_quickDrag=false,m_quickMenu=false;
        bool m_pinDrag=false,m_pinMenu=false,m_pendingOutput=true;
        bool m_layoutMenu=false,m_layoutDragging=false,m_layoutMoved=false;
        size_t m_layoutComment=static_cast<size_t>(-1),m_layoutLink=static_cast<size_t>(-1),m_layoutPoint=0;
        float m_layoutStartX=0,m_layoutStartY=0,m_layoutX=0,m_layoutY=0;
        std::vector<size_t> m_selectedNodes,m_layoutMembers,m_extractNodes;
        std::vector<std::pair<std::string,std::string>> m_graphBack;
        void openSelectedFunction();
        void returnToCaller();
        BlueprintDocument::Point nodeLayoutPosition(size_t node) const;
        BlueprintDocument::Point routeLayoutPosition(size_t wire,size_t point) const;
        std::vector<size_t> commentMembers(size_t comment) const;
        bool commentBounds(size_t comment,float& x,float& y,float& w,float& h) const;
        std::vector<BlueprintDocument::Point> wireRoute(size_t link) const;
        bool m_rightDown=false,m_rightMoved=false;
        float m_rightX=0,m_rightY=0;
        struct ContextResult { std::string kind,reference,title,pin; bool conversion=false; };
        std::vector<ContextResult> m_contextResults;
        float m_pinStartX=0,m_pinStartY=0,m_pinX=0,m_pinY=0,m_pinMenuX=0,m_pinMenuY=0;
        std::string m_pinQuery,m_pinType;
        float m_quickX=0,m_quickY=0,m_quickMenuX=0,m_quickMenuY=0;
        std::string m_filePath,m_fileBaseline;
        std::vector<std::string> m_files;
        size_t m_fileFirst=0;
        bool m_browsing=false;
        bool m_previousTyping=false;
        bool m_playing=false;
        bool m_checkpointPresent=false;
        bool m_protectDamagedRecovery=false;
        std::string m_recoveryWarning;
        std::string m_checkpointBytes; // last recovery bytes read or verified by this workspace
        size_t m_checkpointRevision=static_cast<size_t>(-1);
        double m_nextCheckpoint=0;
    };
}
