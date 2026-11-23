// ============================================================================
//  GamePanel.cpp - the Game view. See GamePanel.h for how focus works.
// ============================================================================

#include "panels/GamePanel.h"

#include "EditorApp.h"
#include "ProjectSettings.h"

#include <engine/input/InputMap.h>   // HasBindings, for the overlay
#include <engine/render/Presentation.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace editor {

// ---------------------------------------------------------------------------
//  THE DROPDOWN IS THE GAME'S OUTPUT, NOT A PREVIEW.
//
//  This is the fix for the thing that made the whole feature useless: the
//  chosen shape used to live in .editor/session.json, which is one person's
//  view of one project, so pinning 16:9 and exporting produced a game that
//  stretched to fill whatever window it opened in. The setting had never left
//  the editor.
//
//  It now goes to config/engine.json, which the engine reads at start-up and
//  the export copies into the game. The SCALE SLIDER stays in the session,
//  because how large the preview is drawn on one monitor is nobody else's
//  business and must not change what ships.
// ---------------------------------------------------------------------------
void GamePanel::ApplyChosenModeToProject() {
    if (m_chosen >= m_modes.size()) {
        return;
    }

    const GameViewMode& mode = m_modes[m_chosen];

    // Spelled the way the settings file spells it, through the one parser
    // both ends share - see Presentation::ParseSpec.
    std::string spec = "free";
    if (mode.kind == GameViewMode::Kind::Aspect) {
        spec = std::to_string(mode.x) + ":" + std::to_string(mode.y);
    } else if (mode.kind == GameViewMode::Kind::Resolution) {
        spec = std::to_string(mode.x) + "x" + std::to_string(mode.y);
    }

    std::string error;
    if (!SetPresentationMode(spec, error)) {
        // SAID IN THE PANEL, not only in the Console. A read-only checkout is
        // a real case, and a dropdown that silently does not stick is worse
        // than one that refuses out loud.
        std::snprintf(m_settingError, sizeof(m_settingError), "%s", error.c_str());
        return;
    }
    m_settingError[0] = '\0';
}

void GamePanel::AddCustomMode(const GameViewMode& mode) {
    // ALREADY THERE WINS. Typing 16:9 into the box when 16:9 is a built-in
    // should select it rather than growing a second identical row that the
    // dropdown then shows twice.
    for (std::size_t i = 0; i < m_modes.size(); ++i) {
        if (m_modes[i] == mode) {
            m_chosen = i;
            return;
        }
    }
    m_modes.push_back(mode);
    m_chosen = m_modes.size() - 1;
}

// ---------------------------------------------------------------------------
//  THE PROJECT IS THE SOURCE OF TRUTH FOR THE SHAPE, so the dropdown starts
//  on whatever config/engine.json says rather than on whatever this person
//  last looked at.
//
//  WHY LAZILY, ON THE FIRST DRAW, rather than in a constructor: the panel is
//  built before the engine has read the settings file, so there would be
//  nothing to read.
//
//  A SETTING THE DROPDOWN HAS NEVER HEARD OF BECOMES A CUSTOM ENTRY. Somebody
//  may have hand-edited "1366:768" into the file, and a dropdown that
//  silently showed Free Aspect instead would be lying about what the game
//  does.
// ---------------------------------------------------------------------------
void GamePanel::SyncChosenFromProject() {
    if (m_syncedFromProject) {
        return;
    }
    m_syncedFromProject = true;

    const std::string spec = PresentationMode();

    eng::Presentation::Mode mode = eng::Presentation::Mode::Free;
    int                     x    = 0;
    int                     y    = 0;
    std::string             why;
    if (!eng::Presentation::ParseSpec(spec, mode, x, y, why)) {
        return;   // reported by the engine at start-up; Free Aspect stands
    }

    if (mode == eng::Presentation::Mode::Free) {
        m_chosen = 0;   // Free Aspect is always first - see BuiltInGameViewModes
        return;
    }

    const GameViewMode wanted{mode == eng::Presentation::Mode::Aspect
                                  ? GameViewMode::Kind::Aspect
                                  : GameViewMode::Kind::Resolution,
                              x, y, {}};

    for (std::size_t i = 0; i < m_modes.size(); ++i) {
        if (m_modes[i] == wanted) {
            m_chosen = i;
            return;
        }
    }

    // Not one of ours: keep it, labelled the way the file spells it.
    GameViewMode added = wanted;
    added.label = mode == eng::Presentation::Mode::Aspect
                      ? std::to_string(x) + ":" + std::to_string(y)
                      : std::to_string(x) + " x " + std::to_string(y);
    m_modes.push_back(added);
    m_chosen = m_modes.size() - 1;
}

void GamePanel::DrawControls() {
    SyncChosenFromProject();

    // ---- the game camera, editable ---------------------------------------
    //
    // This is the camera a player sees through, and it is what gets written
    // into the scene file when you save. It is edited here rather than in the
    // Scene view because this is the panel showing what it points at.
    eng::Camera& camera = eng::Engine::Get().GetCamera();

    float position[2] = {camera.Position().x, camera.Position().y};
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::DragFloat2("Camera", position, 1.0f)) {
        camera.SetPosition(eng::Vec2{position[0], position[1]});
        EditorState::Get().MarkDirty();
    }

    // ======================================================================
    //  ZOOM OR FRAME HEIGHT, and which one is shown IS the camera's mode.
    //
    //  Two widgets in the same place rather than both at once, because they
    //  are two ways of saying one thing and showing both invites editing the
    //  one that is about to be overwritten. The small button beside it is the
    //  switch, and switching keeps the current shot: the number it fills in
    //  is the one that means the same framing at this viewport.
    //
    //  WHY A GAME WANTS THE HEIGHT. A zoom lets a bigger window show MORE
    //  WORLD, so a player at 2560x1440 saw further than one at 1280x720 -
    //  up, down and sideways. Pinning the aspect fixed the shape of that
    //  mistake; pinning the height fixes the mistake. See render/Camera.h.
    // ======================================================================
    ImGui::SameLine();
    const bool byHeight = camera.CurrentFraming() == eng::Camera::Framing::Height;

    if (byHeight) {
        float height = camera.FrameHeight();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("##height", &height, 1.0f, 1.0f, 100000.0f,
                             "%.0f tall")) {
            camera.SetFrameHeight(height);
            EditorState::Get().MarkDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How many world units are visible from the top of the "
                              "picture to the bottom, WHATEVER its pixel size.\n\n"
                              "The width follows the aspect ratio, so a 21:9 player "
                              "sees further sideways and the same amount "
                              "vertically - which is what every ultrawide player "
                              "expects.\n\n"
                              "This is the one that makes what you frame here what "
                              "ships.");
        }
    } else {
        float zoom = camera.Zoom();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("##zoom", &zoom, 0.01f, 0.05f, 20.0f, "%.2fx")) {
            camera.SetZoom(zoom);
            EditorState::Get().MarkDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How many screen pixels one world unit covers.\n\n"
                              "A BIGGER WINDOW THEN SHOWS MORE WORLD, which is why "
                              "this is not the one to ship with: every player sees a "
                              "different amount of the level. Press 'tall' to pin "
                              "the vertical slice instead.");
        }
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(byHeight ? "zoom" : "tall")) {
        // THE SHOT IS KEPT. Switching framing should change which number is
        // held still, not what is on screen - so each direction hands the
        // other the value that means the same thing at this viewport, which
        // is exactly what Camera's own two accessors already compute.
        if (byHeight) {
            camera.SetZoom(camera.Zoom());
        } else {
            camera.SetFrameHeight(camera.FrameHeight());
        }
        EditorState::Get().MarkDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(byHeight ? "Frame by zoom instead - a bigger window will "
                                     "show more world"
                                   : "Frame by height instead - every player sees the "
                                     "same vertical slice");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        camera.Reset();
        EditorState::Get().MarkDirty();
    }

    // ======================================================================
    //  WHAT SHAPE THE GAME IS RENDERED AT.
    //
    //  NOT MarkDirty, for any of this row. The aspect you are looking through
    //  and how big it is drawn are facts about the person at the keyboard,
    //  not about the level - the same reasoning that keeps the Scene view's
    //  own camera out of the scene file. Marking the scene unsaved because
    //  somebody checked what 4:3 looks like would be a lie, and Ctrl+S would
    //  then write a scene nobody had edited.
    // ======================================================================
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##aspect", m_modes[m_chosen].label.c_str())) {
        for (std::size_t i = 0; i < m_modes.size(); ++i) {
            // A LINE BETWEEN THE GROUPS, because "16:9" and "1920 x 1080" are
            // answers to different questions and a flat list of fourteen
            // makes you read every one to find that out.
            if (i > 0 && m_modes[i].kind != m_modes[i - 1].kind) {
                ImGui::Separator();
            }

            const bool chosen = i == m_chosen;
            if (ImGui::Selectable(m_modes[i].label.c_str(), chosen)) {
                m_chosen = i;
                ApplyChosenModeToProject();
            }
            if (chosen) {
                ImGui::SetItemDefaultFocus();
            }

            // Removing a custom entry, on the row it belongs to. Built-ins
            // have no x: offering one that refuses would be worse than not
            // offering it.
            if (i >= m_builtInCount) {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("x")) {
                    m_modes.erase(m_modes.begin() + static_cast<long>(i));
                    if (m_chosen >= m_modes.size()) {
                        m_chosen = m_modes.size() - 1;
                    } else if (m_chosen > i) {
                        --m_chosen;
                    }
                    ImGui::PopID();
                    break;   // the list just moved under this loop
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        // ---- adding one -------------------------------------------------
        ImGui::TextDisabled("add a ratio or a size");
        ImGui::SetNextItemWidth(110.0f);
        const bool entered =
            ImGui::InputTextWithHint("##custom", "16:9 or 1920x1080", m_customText,
                                     sizeof(m_customText),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::SmallButton("Add") || entered) {
            GameViewMode  made;
            std::string   why;
            if (ParseGameViewMode(m_customText, made, why)) {
                AddCustomMode(made);
                ApplyChosenModeToProject();
                m_customText[0]  = '\0';
                m_customError[0] = '\0';
            } else {
                std::snprintf(m_customError, sizeof(m_customError), "%s", why.c_str());
            }
        }
        if (m_customError[0] != '\0') {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_customError);
        }

        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What the camera renders at, HERE AND IN THE BUILT GAME.\n\n"
                          "Free Aspect follows this panel, so dragging it wider "
                          "shows MORE of the level.\n"
                          "An aspect pins the shape; a resolution pins the exact "
                          "pixels, which is the only one that answers 'what will a "
                          "player see'.\n\n"
                          "Saved in config/engine.json, which the export copies into "
                          "the game - so this is not a preview.");
    }

    // ---- and whether the player may resize the window --------------------
    //
    // BESIDE THE SHAPE, because the two together are what "forcing a look"
    // means: letterboxing keeps a pinned shape correct at any size, so
    // resizing is not broken - but a 480x270 game presented at exactly 3x has
    // one right answer and a hundred wrong ones, and leaving a player to find
    // it by dragging is not a style decision anybody wants to delegate.
    ImGui::SameLine();
    bool resizable = WindowResizable();
    if (ImGui::Checkbox("Resizable", &resizable)) {
        std::string error;
        if (!SetWindowResizable(resizable, error)) {
            std::snprintf(m_settingError, sizeof(m_settingError), "%s", error.c_str());
        } else {
            m_settingError[0] = '\0';
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Whether the BUILT GAME's window can be dragged to any "
                          "size. The editor's own window is never affected.\n\n"
                          "Off pins it to window.width x window.height from "
                          "config/engine.json - set those to match a pinned "
                          "resolution, or the game runs letterboxed for ever.");
    }

    // ---- and how big it is drawn -----------------------------------------
    //
    // SEPARATE FROM THE RESOLUTION, deliberately - see GameViewLayout.h.
    // Scaling the render would change what the camera sees, which is the
    // thing the dropdown beside this exists to pin down.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    float scale = m_scale;
    if (ImGui::DragFloat("##scale", &scale, 0.01f, 0.0f, 8.0f,
                         m_scale <= kFitScale ? "Fit" : "%.2fx")) {
        m_scale = scale <= 0.02f ? kFitScale : scale;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How big the finished picture is drawn in this panel.\n\n"
                          "Fit is as large as the panel allows, never past 1:1.\n"
                          "1.00x is one render pixel per screen pixel, which is the "
                          "only honest way to look at pixel art - a big target is "
                          "then mostly off the edge, on purpose.");
    }
}

void GamePanel::Draw() {
    DrawControls();

    // ======================================================================
    //  THE TWO SIZES, AND THEY ARE DIFFERENT NUMBERS.
    //
    //  `layout.target*` is what the camera renders at. `layout.draw*` is how
    //  big that picture appears here. They were the same number for as long
    //  as the render simply followed the panel, and every piece of arithmetic
    //  below used to be able to ignore the difference. None of it can now -
    //  the pointer translation least of all.
    // ======================================================================
    const ImVec2         avail  = ImGui::GetContentRegionAvail();
    const GameViewLayout layout =
        LayoutGameView(m_modes[m_chosen], avail.x, avail.y, m_scale);

    m_target.Resize(layout.targetWidth, layout.targetHeight);

    // ======================================================================
    //  CENTRED, WITH BLACK AROUND IT.
    //
    //  A 16:9 render in a square panel leaves room on two sides, and what
    //  goes there has to be BLACK rather than the panel's own grey: the whole
    //  point of pinning an aspect is to see what a player sees, and a player
    //  sees black bars. Grey ones would make the framing look like it
    //  continues past the edge of the picture.
    //
    //  The fill covers the WHOLE content region, not just the margins, so
    //  there is no seam to line up and nothing to get wrong when the picture
    //  is larger than the panel and the margins are negative.
    // ======================================================================
    const ImVec2 area = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        area, ImVec2(area.x + avail.x, area.y + avail.y), IM_COL32(0, 0, 0, 255));

    const ImVec2 size(layout.drawWidth, layout.drawHeight);
    const ImVec2 origin(area.x + std::max(0.0f, (avail.x - size.x) * 0.5f),
                        area.y + std::max(0.0f, (avail.y - size.y) * 0.5f));

    if (!m_target.IsValid()) {
        ImGui::TextDisabled("this view could not be created");

        // NOTHING TO CLICK INTO. Returning without clearing would leave last
        // frame's rectangle live over a panel that is now drawing an error
        // message.
        EditorGui::ClearGameViewArea();
        return;
    }

    // ImGui::Image displays a picture. The picture itself is filled in later
    // this frame by RenderView.
    //
    // THE CURSOR IS MOVED TO THE CENTRED ORIGIN FIRST, so that the item
    // ImGui creates is the PICTURE's rectangle and not the panel's. Everything
    // below reads IsItemHovered and IsItemClicked off it, and those have to
    // mean "over the game" rather than "over the black beside it" - a click
    // in the letterbox is not a click in the game.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Image(reinterpret_cast<ImTextureID>(m_target.NativeTexture()), size);

    // Clicking the view takes focus. That is how you get the keyboard back
    // after clicking on something else mid-play.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::SetWindowFocus();
    }

    // ---- where the pointer is, as the GAME sees it ------------------------
    //
    // The game is drawn into a picture inside this panel, so a mouse
    // position in window coordinates means nothing to it: the panel can be
    // anywhere, docked or floating. Subtracting this picture's own corner is
    // the whole translation, and doing it HERE is what keeps the engine from
    // needing to know that panels exist at all. See UISystem::SetPointer.
    //
    // Only while hovered AND in play mode. A button that lit up because the
    // mouse passed over a paused Game view would be reacting to something
    // that is not running.
    const ImGuiIO& io       = ImGui::GetIO();
    const bool     overView = ImGui::IsItemHovered();
    const bool     live     = overView && eng::Engine::Get().IsInPlayMode();

    // ==================================================================
    //  SCALED INTO THE RENDER, and this is the line the whole two-sizes
    //  distinction exists for.
    //
    //  The UI is laid out against the size the game RENDERS at, so the
    //  pointer has to arrive in those coordinates - not in the panel's. A
    //  1920x1080 target drawn 600 pixels wide is being shown at under a
    //  third scale, so a click 300 pixels in from the picture's left edge is
    //  960 pixels into the game.
    //
    //  Getting this wrong does not look like a bug in the mouse. It looks
    //  like buttons that only work near one corner, and it gets blamed on
    //  the UI layout for an afternoon.
    const eng::Vec2 inRender{(io.MousePos.x - origin.x) * layout.TargetPerDrawX(),
                             (io.MousePos.y - origin.y) * layout.TargetPerDrawY()};

    eng::UISystem::SetPointer(
        inRender,
        eng::Vec2{static_cast<float>(layout.targetWidth),
                  static_cast<float>(layout.targetHeight)},
        live && ImGui::IsMouseDown(ImGuiMouseButton_Left), live);

    // ----------------------------------------------------------------------
    //  AND THE SAME RECTANGLE TO THE EVENT PUMP, so that a click reaches the
    //  game's INPUT MAP and not only its UI.
    //
    //  BOTH HALVES OF THE MOUSE ARE PUBLISHED FROM THIS ONE PLACE, out of the
    //  one origin, size and live, which is what stops them ever disagreeing
    //  about where the game is. A UI button that lights up while
    //  Input::IsPressed("Fire") stays false is a very hard afternoon.
    //
    //  WHY IT IS NEEDED AT ALL: ImGui claims the mouse whenever the cursor is
    //  over any of its windows, and the Game view IS one - so without this,
    //  every click over the running game was marked consumed and InputMap
    //  skipped it. Keyboard actions worked and mouse actions did not, while a
    //  standalone build, which installs no GUI hooks, was fine. See
    //  EditorGui::SetGameViewArea.
    // ----------------------------------------------------------------------
    // ======================================================================
    //  CLIPPED TO THE PANEL, and leaving this out was a real bug.
    //
    //  At a scale of 1.00x a 1920x1080 target is DRAWN 1920x1080 - mostly
    //  outside a 600-pixel panel, deliberately. Publishing that rectangle
    //  told EditorGui the game occupied a 1920x1080 region anchored at this
    //  panel's corner, which on a docked layout covers the Inspector and the
    //  Hierarchy.
    //
    //  And the consequence is not cosmetic. EditorGui::HookWantsMouse tests
    //  this LATCHED rectangle against each event's own position - see the
    //  note there about why it is latched rather than asked fresh - so a
    //  click on the Inspector was reported as not wanted by ImGui and routed
    //  to the game's InputMap instead. The Inspector would simply stop
    //  responding, while the Scene view beside it worked.
    //
    //  The intersection with the content region is what the game actually
    //  occupies on screen, which is what this field is for.
    // ======================================================================
    const float clipMinX = std::max(origin.x, area.x);
    const float clipMinY = std::max(origin.y, area.y);
    const float clipMaxX = std::min(origin.x + size.x, area.x + avail.x);
    const float clipMaxY = std::min(origin.y + size.y, area.y + avail.y);

    EditorGui::SetGameViewArea(clipMinX, clipMinY,
                               std::max(0.0f, clipMaxX - clipMinX),
                               std::max(0.0f, clipMaxY - clipMinY), live);

    // FOCUS is what routes the keyboard, and it is deliberately focus rather
    // than merely hovering: hovering would mean the player stopped responding
    // every time the mouse strayed over the Hierarchy.
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    eng::Engine& engine = eng::Engine::Get();

    // A green border while the game has the keyboard. It is the only reliable
    // way to answer "why is nothing responding?" at a glance.
    if (m_focused && engine.IsInPlayMode()) {
        ImGui::GetWindowDrawList()->AddRect(
            origin, ImVec2(origin.x + size.x, origin.y + size.y),
            IM_COL32(90, 200, 110, 220), 0.0f, 0, 2.0f);
    }

    // A short message over the top-left corner saying what to do next.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 6.0f));
    if (!engine.IsInPlayMode()) {
        ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.78f, 0.85f),
                           "not playing - press Play on the Toolbar");
    } else if (!m_focused) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 0.95f),
                           "click here to give the game the keyboard");
    }

    // ======================================================================
    //  THE TWO WAYS A RUNNING GAME CAN LOOK BROKEN WHILE WORKING PERFECTLY.
    //
    //  Both were reported as "the game is in slow motion and my scripts do
    //  not work", and neither leaves a mark anywhere somebody looks while
    //  playing: one is a slider two panels away, the other is a warning
    //  printed during start-up and scrolled away long before Play.
    //
    //  SAID HERE, OVER THE GAME, because that is where the person who is
    //  confused is looking. Only when they are true, so an ordinary session
    //  shows nothing.
    // ======================================================================
    const float scale = engine.Clock().TimeScale();
    if (scale < 0.999f || scale > 1.001f) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + size.y - 46.0f));
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 0.95f),
                           "game time is running at %.2fx - Speed, on the Toolbar",
                           static_cast<double>(scale));
    }

    if (!eng::InputMap::HasBindings()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + size.y - 26.0f));
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 0.95f),
                           "no controls are bound: config/engine.json has no "
                           "\"input\" section, so nothing answers the keyboard");
    }
}

void GamePanel::RenderView() {
    if (!m_target.IsValid()) {
        return;
    }

    // Send drawing into this panel's own picture instead of onto the window.
    eng::Renderer::SetRenderTarget(&m_target);

    // Through the GAME camera, and with gizmos switched OFF - no grid, no
    // origin arrows, no collider outlines. What ships.
    eng::Engine::Get().RenderWorld(eng::Engine::Get().GetCamera(),
                                   /*includeGizmos=*/false);

    // Back to the window, or the editor's own interface would be drawn inside
    // this panel's picture.
    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
