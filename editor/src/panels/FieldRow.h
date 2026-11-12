#pragma once

// ============================================================================
//  FieldRow.h - the Inspector's rows: NAME ON THE LEFT, value on the right.
//
//  Dear ImGui puts a widget's label AFTER it, which is the right default for
//  a tool window full of one-off controls and the wrong one for a column of
//  properties. Every other editor - Unity's Inspector, Unreal's Details
//  panel, Blender's sidebar - puts the name in a column down the left and the
//  value beside it, and the reason is scanning: a name you read at a fixed x
//  is a list, and a name you read at wherever the last drag box happened to
//  end is a paragraph.
//
//  ==========================================================================
//  WHY A WRAPPER AND NOT EIGHTY EDITED CALL SITES
//
//  The layout is four lines long and it has to be the SAME four lines in
//  every row, or the column is not a column. Writing them out at each call
//  would be eighty chances for one row to use a different width, and the
//  first time somebody adds a component they would copy whichever row they
//  happened to look at.
//
//  So these functions take exactly what ImGui's take, in the same order, with
//  the same defaults:
//
//      row::DragFloat("Rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg")
//
//  and the only edit a call site needs is the namespace. Anything ImGui
//  accepts, these accept.
//
//  ==========================================================================
//  A HIDDEN LABEL IS LEFT COMPLETELY ALONE, and that is what makes the
//  conversion safe to do in bulk.
//
//  "##point", "##tag", "##filter" - a label starting with ## draws no text,
//  so there is no name to move and nothing to lay out. Those rows are
//  forwarded straight to ImGui with no group, no label and no width changed,
//  so a row that puts a drag box and a small button side by side keeps
//  working exactly as it did. There are several of those and every one of
//  them would have been broken by a wrapper that assumed a full-width value.
//
//  AN EXPLICIT WIDTH IS ALSO KEPT. SetNextItemWidth(130.0f) before one of
//  these still means 130 pixels; the name still moves to the left. Only a row
//  that asked for nothing in particular is stretched to the panel's edge.
//
//  ==========================================================================
//  THE TOOLTIP STILL WORKS ON THE NAME, which is the one thing that would
//  have been quietly lost.
//
//  ImGui counts a label as part of its widget's item, so hovering "Gravity
//  scale" used to show that field's tooltip. Drawing the name as separate
//  text would have ended that - the tooltip would appear over the number and
//  not over the word, which is the half nobody points at.
//
//  BeginGroup/EndGroup is ImGui's own answer: EndGroup emits the whole row as
//  one item and forwards the hovered, active and deactivated state of
//  whatever is inside it, so IsItemHovered(), IsItemActive() and
//  BeginDragDropTarget() after one of these functions all behave as they did
//  when the widget was the item. See the comment in ImGui's EndGroup, which
//  names this case.
// ============================================================================

#include <imgui.h>
#include <imgui_internal.h>   // GImGui, for "did somebody already set a width?"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <string>

namespace editor::row {

// ---------------------------------------------------------------------------
//  HOW WIDE THE NAME COLUMN IS.
//
//  A FRACTION WITH ENDS, rather than a constant: the Inspector is a dockable
//  panel and people make it narrow. A fixed 150 pixels leaves a 200-pixel
//  panel with 50 for the value, and a fraction alone gives a very wide panel
//  a column of whitespace with six words in it.
//
//  MEASURED FROM WHAT IS LEFT, not from the window, so a row inside a tree
//  node or an indent gets a column that starts where the row starts.
// ---------------------------------------------------------------------------
inline float LabelWidth() {
    const float available = ImGui::GetContentRegionAvail().x;
    return std::clamp(available * 0.42f, 72.0f, 180.0f);
}

// ---------------------------------------------------------------------------
//  One row: the name, then the cursor parked where the value goes.
//
//  Held by value for the length of one widget call. The destructor closes the
//  group, and it runs BEFORE the wrapper returns - so the caller's
//  IsItemHovered() sees the finished row.
// ---------------------------------------------------------------------------
class Row {
public:
    explicit Row(const char* label) {
        // NOTHING TO SHOW, NOTHING TO DO. See the note above: this is the
        // case that keeps the bulk conversion honest.
        if (label == nullptr || (label[0] == '#' && label[1] == '#')) {
            m_id = label != nullptr ? label : "";
            return;
        }

        m_active = true;
        m_id     = "##";
        m_id += label;

        // WHETHER THE VALUE FILLS THE ROW IS DECIDED BEFORE THE GROUP OPENS,
        // because ImGui clears the next-item data as soon as an item is
        // submitted - and the name is an item.
        const ImGuiContext& g = *GImGui;
        const bool widthWasAsked =
            (g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasWidth) != 0;
        float wanted = 0.0f;
        if (widthWasAsked) {
            wanted = g.NextItemData.Width;
        }

        ImGui::BeginGroup();

        const float startX = ImGui::GetCursorPosX();

        // The name sits on the value's text baseline rather than at the top
        // of the row, which is what makes a column of names and a column of
        // drag boxes read as lines.
        ImGui::AlignTextToFramePadding();

        // UP TO THE ##, because "Name##collideswith" shows as "Name". Hidden
        // text in the middle of a label is how two rows with the same name
        // stay separate widgets, and printing it would undo that.
        const char* hidden = std::strstr(label, "##");
        if (hidden != nullptr) {
            ImGui::TextUnformatted(label, hidden);
        } else {
            ImGui::TextUnformatted(label);
        }

        ImGui::SameLine();

        // A NAME WIDER THAN THE COLUMN PUSHES THE VALUE RIGHT rather than
        // being drawn underneath it. SetCursorPosX alone would move the
        // cursor BACKWARDS for a long name and ImGui would then overlap the
        // two, which looks like a rendering fault rather than a long word.
        ImGui::SetCursorPosX(
            std::max(startX + LabelWidth(), ImGui::GetCursorPosX()));

        // -FLT_MIN is ImGui's "out to the right edge". Restored rather than
        // invented when the caller asked for a width of its own.
        ImGui::SetNextItemWidth(widthWasAsked ? wanted : -FLT_MIN);
    }

    ~Row() {
        if (m_active) {
            ImGui::EndGroup();
        }
    }

    Row(const Row&)            = delete;
    Row& operator=(const Row&) = delete;

    // What to pass the ImGui function. The ID is unchanged in the hidden-label
    // case and is "##" plus the label otherwise, which keeps every row's
    // identity distinct for exactly as long as its name is.
    const char* Id() const { return m_id.c_str(); }

private:
    std::string m_id;
    bool        m_active = false;
};

// ---------------------------------------------------------------------------
//  The widgets. Signatures and defaults copied from imgui.h, deliberately -
//  a wrapper that quietly changed a default would be worse than no wrapper.
// ---------------------------------------------------------------------------

inline bool DragFloat(const char* label, float* v, float speed = 1.0f,
                      float min = 0.0f, float max = 0.0f,
                      const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::DragFloat(row.Id(), v, speed, min, max, format, flags);
}

inline bool DragFloat2(const char* label, float v[2], float speed = 1.0f,
                       float min = 0.0f, float max = 0.0f,
                       const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::DragFloat2(row.Id(), v, speed, min, max, format, flags);
}

inline bool DragInt(const char* label, int* v, float speed = 1.0f, int min = 0,
                    int max = 0, const char* format = "%d",
                    ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::DragInt(row.Id(), v, speed, min, max, format, flags);
}

inline bool DragInt2(const char* label, int v[2], float speed = 1.0f, int min = 0,
                     int max = 0, const char* format = "%d",
                     ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::DragInt2(row.Id(), v, speed, min, max, format, flags);
}

inline bool SliderFloat(const char* label, float* v, float min, float max,
                        const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::SliderFloat(row.Id(), v, min, max, format, flags);
}

inline bool SliderInt(const char* label, int* v, int min, int max,
                      const char* format = "%d", ImGuiSliderFlags flags = 0) {
    Row row(label);
    return ImGui::SliderInt(row.Id(), v, min, max, format, flags);
}

inline bool InputFloat(const char* label, float* v, float step = 0.0f,
                       float stepFast = 0.0f, const char* format = "%.3f",
                       ImGuiInputTextFlags flags = 0) {
    Row row(label);
    return ImGui::InputFloat(row.Id(), v, step, stepFast, format, flags);
}

// A CHECKBOX GETS THE SAME TREATMENT, which is worth saying because it is the
// one widget whose ImGui label looks right where it is. It is not: a tick
// floating in the name column with its word to the right of it breaks the one
// thing this file is for, which is that every value starts at the same x.
inline bool Checkbox(const char* label, bool* v) {
    Row row(label);
    return ImGui::Checkbox(row.Id(), v);
}

inline bool ColorEdit4(const char* label, float col[4],
                       ImGuiColorEditFlags flags = 0) {
    Row row(label);
    return ImGui::ColorEdit4(row.Id(), col, flags);
}

inline bool InputText(const char* label, char* buf, std::size_t bufSize,
                      ImGuiInputTextFlags flags = 0,
                      ImGuiInputTextCallback callback = nullptr,
                      void* userData = nullptr) {
    Row row(label);
    return ImGui::InputText(row.Id(), buf, bufSize, flags, callback, userData);
}

inline bool InputTextWithHint(const char* label, const char* hint, char* buf,
                              std::size_t bufSize, ImGuiInputTextFlags flags = 0,
                              ImGuiInputTextCallback callback = nullptr,
                              void* userData = nullptr) {
    Row row(label);
    return ImGui::InputTextWithHint(row.Id(), hint, buf, bufSize, flags, callback,
                                    userData);
}

inline bool InputTextMultiline(const char* label, char* buf, std::size_t bufSize,
                               const ImVec2& size = ImVec2(0, 0),
                               ImGuiInputTextFlags flags = 0,
                               ImGuiInputTextCallback callback = nullptr,
                               void* userData = nullptr) {
    Row row(label);
    return ImGui::InputTextMultiline(row.Id(), buf, bufSize, size, flags, callback,
                                     userData);
}

inline bool Combo(const char* label, int* current, const char* itemsSeparatedByZeros,
                  int popupMaxHeightInItems = -1) {
    Row row(label);
    return ImGui::Combo(row.Id(), current, itemsSeparatedByZeros,
                        popupMaxHeightInItems);
}

inline bool Combo(const char* label, int* current, const char* const items[],
                  int itemsCount, int popupMaxHeightInItems = -1) {
    Row row(label);
    return ImGui::Combo(row.Id(), current, items, itemsCount, popupMaxHeightInItems);
}

// ---------------------------------------------------------------------------
//  NOT BeginCombo, AND NOT A CUSTOM WIDGET, and both omissions are on purpose.
//
//  BeginCombo opens a popup that stays open across the call, so the group
//  would have to close after EndCombo - which no amount of RAII around one
//  call can arrange. There is one of those in the Inspector and it lays its
//  own name out with the two functions below.
//
//  The same two serve a widget drawn by hand: the entity, prefab, animation
//  and image slots are buttons with their own colours, pickers and drop
//  targets, so they place the name themselves and then draw whatever they
//  like beside it.
// ---------------------------------------------------------------------------

// Moves the cursor from wherever a row's name finished to where its value
// starts, and sets the next item to fill the rest of the row.
//
// BOTH NUMBERS ARE TAKEN BEFORE THE NAME IS DRAWN, which is the only reason
// this is two arguments instead of none: LabelWidth measures what is LEFT of
// the row, so asking it after the name has eaten part of the line gives a
// narrower column for every row whose name is longer - and a column that
// moves is not a column.
inline void ValueColumn(float startX, float labelWidth) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(startX + labelWidth, ImGui::GetCursorPosX()));
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Draws `name` in the label column and leaves the cursor at the value, with
// the next item set to fill the rest of the row. No group: the caller owns
// however many items it draws.
inline void Label(const char* name) {
    const float startX     = ImGui::GetCursorPosX();
    const float labelWidth = LabelWidth();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ValueColumn(startX, labelWidth);
}

// How wide the value is once the name has taken its column - for a widget
// sized by hand, like the slot buttons, which need a number rather than
// ImGui's fill-to-the-edge.
inline float ValueWidth() {
    const float available = ImGui::GetContentRegionAvail().x;
    return available > 1.0f ? available : 1.0f;
}

} // namespace editor::row
