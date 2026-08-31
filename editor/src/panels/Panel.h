#pragma once

// =============================================================================
//  WEEK 2 - the base every panel inherits. Ten of these exist by Week 10, so
//  the shape was worth getting right once.
//
//  ---------------------------------------------------------------------------
//  IMMEDIATE MODE, because it is genuinely different from WinForms, WPF, or
//  Unity's editor GUI.
//
//  There you CREATE a widget, it persists, and you wire up events. In ImGui
//  you CALL a function every frame and the widget exists for that frame only:
//
//      if (ImGui::Button("Reload")) { DoReload(); }
//
//  There is no button object and there is no click handler. Button() draws a
//  button and returns true on the frame it was clicked.
//
//  Consequences worth internalising:
//
//   - PANELS HOLD ALMOST NO STATE. The state lives in the engine and the panel
//     reads it fresh each frame. That is exactly what a debug tool wants: it
//     cannot show you something stale. The two exceptions in this editor are
//     the Log panel's filters and the Hierarchy's selection, and both are USER
//     PREFERENCES rather than engine data - which is the test for whether a
//     panel is allowed to remember something.
//
//   - WIDGET IDS COME FROM LABELS. Two buttons both labelled "Delete" in one
//     window are the SAME WIDGET to ImGui. Disambiguate with
//     "Delete##entity_7" - text after ## is part of the id and is not drawn.
//
//   - You cannot "update the label later". You pass a different string next
//     frame.
//
//  ---------------------------------------------------------------------------
//  THE Begin/End PAIR LIVES IN THE CALLER (EditorApp::Run), not in Draw().
//  Decided once and applied to every panel, so that visibility, docking and
//  the close button behave identically everywhere. Mixing the two conventions
//  produces mismatched Begin/End calls, and ImGui's assert for that is one
//  worth meeting only once.
// =============================================================================

namespace editor {

class Panel {
public:
    virtual ~Panel() = default;

    // The window title, which is also the ImGui id - so it must be unique.
    virtual const char* Title() const = 0;

    // Draws the CONTENTS. Called once per frame between EditorGui::BeginFrame
    // and EndFrame, with Begin/End already done by the caller.
    virtual void Draw() = 0;

    // Called INSTEAD of Draw on a frame the panel is not visible - closed, or
    // collapsed, or a background tab in a dock node.
    //
    // This exists because of a bug that is easy to write and hard to see. A
    // panel that caches "am I focused?" during Draw keeps the last value it
    // computed forever once it becomes a background tab, because Draw stops
    // being called - ImGui::Begin returns false. The Game view then reported
    // that it still had the keyboard while sitting behind the Scene tab, and
    // every key went to a game nobody could see. Cached per-frame state has to
    // be cleared on the frames the panel does not run, and the only place that
    // knows is the caller that owns Begin/End.
    virtual void OnHidden() {}

    bool IsOpen() const   { return m_open; }
    void SetOpen(bool on) { m_open = on; }
    bool* OpenFlag()      { return &m_open; }

protected:
    bool m_open = true;
};

} // namespace editor
