#pragma once
#include "tools/blueprint/native_document.h"

namespace nx
{
    struct Services;
    class Canvas;
    struct Asset;

    // A desktop-only workspace in the existing Redux window.
    class BlueprintWorkspace
    {
    public:
        bool active() const { return m_font != nullptr; }
        void open(Services& services);
        void close(Services& services);
        void update(Services& services);
        void draw(Canvas& canvas);
        void recover(Services& services);
    private:
        void selectGraph(size_t index);
        void fitGraph();
        bool saveCopy(Services& services);
        void cancelDrag() { m_drag = false; }
        BlueprintDocument m_document;
        Asset* m_font = nullptr;
        std::string m_status = "Drop a .bp.json or .bp.project.json file here.";
        size_t m_graph = 0, m_selection = static_cast<size_t>(-1), m_first = 0;
        float m_x = 0, m_y = 0, m_zoom = 1, m_lastX = 0, m_lastY = 0;
        bool m_left = false, m_pan = false, m_wantedPointer = false;
        bool m_drag = false;
        float m_dragX = 0, m_dragY = 0, m_grabX = 0, m_grabY = 0;
        unsigned m_shortcuts = 0;
    };
}
