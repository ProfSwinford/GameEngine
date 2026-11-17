#include "InputActionsPanel.h"

#include "../EditorApp.h"
#include "FieldRow.h"

#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/input/InputActions.h>
#include <engine/input/PlayerInputComponent.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace editor {

namespace {

// Copies a std::string into a fixed buffer for ImGui, which edits char arrays.
void ToBuffer(const std::string& from, char* into, std::size_t size) {
    std::snprintf(into, size, "%s", from.c_str());
}

// Is there already one of these by this name?
bool NameTaken(const std::vector<eng::ActionMap>& maps, const std::string& name) {
    for (const eng::ActionMap& map : maps) {
        if (map.Name() == name) {
            return true;
        }
    }
    return false;
}

bool NameTaken(const std::vector<eng::InputAction>& actions, const std::string& name) {
    for (const eng::InputAction& action : actions) {
        if (action.name == name) {
            return true;
        }
    }
    return false;
}

// A NAME NOTHING IN THE LIST ALREADY HAS: "New Map", then "New Map_1", then
// "New Map_2".
//
// Two maps - or two actions in one map - sharing a name is not untidiness.
// The parser keeps the first and drops the second, so pressing "+" twice and
// saving would cost a whole map. Making the name unique where it is CREATED
// removes the only easy way to do that, and leaves the Save check behind it
// to catch the rare one: renaming something onto a name already in use.
//
// "_1" AND NOT UNITY'S "1", which is the one place this deliberately does not
// follow Unity. eng::Scene::MakeUniqueName spells it with the underscore and
// every other "+" in this editor goes through it - the Hierarchy's entities,
// the Object menu's, the Assets panel's - so a second spelling here would be
// the only one in the editor. It cannot share that function itself: that one
// uniquifies against a SCENE's entity names, and there is no scene in sight.
template <typename T>
std::string UniqueName(std::string_view stem, const std::vector<T>& existing) {
    std::string name(stem);
    for (int n = 1; NameTaken(existing, name); ++n) {
        name = std::string(stem) + "_" + std::to_string(n);
    }
    return name;
}

const char* kActionTypeNames[]  = {"Button", "Value", "PassThrough"};
const char* kControlTypeNames[] = {"Button", "Axis", "Vector2"};
const char* kCompositeModes[]   = {"analog", "digital", "digitalNormalized"};

// A control, or the empty slot where one has not been chosen yet.
//
// UNITY'S WORDS, and it has to say SOMETHING: SpellControl answers "" for a
// control with nothing in it, and a row with no text in it is a row nobody
// can see, click with any confidence, or tell from the one below it. These
// now survive a Save, so an empty one is something that will still be sitting
// in the list the next time the file is opened.
std::string SpellSlot(const eng::Control& control) {
    return control ? eng::SpellControl(control) : std::string("<no binding>");
}

// What a binding reads as in the middle pane: the control, or the composite
// and the parts it is built from.
std::string DescribeBinding(const eng::InputBinding& binding) {
    switch (binding.shape) {
        case eng::InputBinding::Shape::Control:
            return SpellSlot(binding.control);
        case eng::InputBinding::Shape::Vector2Composite: {
            std::string out = "2DVector: ";
            out += SpellSlot(binding.up) + " ";
            out += SpellSlot(binding.down) + " ";
            out += SpellSlot(binding.left) + " ";
            out += SpellSlot(binding.right);
            return out;
        }
        case eng::InputBinding::Shape::AxisComposite:
            return "1DAxis: " + SpellSlot(binding.positive) + " / " +
                   SpellSlot(binding.negative);
    }
    return "<nothing>";
}

} // namespace

// ---------------------------------------------------------------------------

eng::ActionMap* InputActionsPanel::SelectedMap() {
    if (!m_loaded || m_map < 0 ||
        static_cast<std::size_t>(m_map) >= m_asset.Maps().size()) {
        return nullptr;
    }
    return &m_asset.Maps()[static_cast<std::size_t>(m_map)];
}

eng::InputAction* InputActionsPanel::SelectedAction() {
    eng::ActionMap* map = SelectedMap();
    if (map == nullptr || m_action < 0 ||
        static_cast<std::size_t>(m_action) >= map->Actions().size()) {
        return nullptr;
    }
    return &map->Actions()[static_cast<std::size_t>(m_action)];
}

eng::InputBinding* InputActionsPanel::SelectedBinding() {
    eng::InputAction* action = SelectedAction();
    if (action == nullptr || m_binding < 0 ||
        static_cast<std::size_t>(m_binding) >= action->bindings.size()) {
        return nullptr;
    }
    return &action->bindings[static_cast<std::size_t>(m_binding)];
}

// ---------------------------------------------------------------------------

bool InputActionsPanel::Load(std::string_view virtualPath) {
    m_error.clear();
    m_loaded = false;
    m_dirty  = false;
    m_map    = 0;
    m_action = -1;
    m_binding = -1;

    std::string text;
    if (!eng::FileSystem::ReadTextFile(virtualPath, text, m_error)) {
        return false;
    }

    std::string     parseError;
    const eng::Json node = eng::ParseJson(text, parseError);
    if (!parseError.empty()) {
        m_error = parseError;
        return false;
    }

    std::string warnings;
    if (!m_asset.Deserialize(node, m_error, warnings)) {
        return false;
    }

    // THE WARNINGS ARE SHOWN, not swallowed. A binding the parser could not
    // read is simply absent from the list, and somebody looking at a file
    // with four bindings when they wrote five needs to be told which one and
    // why - otherwise the window appears to have eaten it.
    if (!warnings.empty()) {
        m_error = warnings;
    }

    m_asset.SetPath(virtualPath);
    m_path   = virtualPath;
    m_loaded = true;
    return true;
}

void InputActionsPanel::Save() {
    if (!m_loaded) {
        return;
    }

    // NOT WRITTEN IF IT WOULD NOT COME BACK. Serialize cannot refuse any of
    // this - it writes down whatever the asset holds - and the parser then
    // either throws the WHOLE FILE out, for a map with no name, or quietly
    // keeps one of each pair of duplicates. Either way the loss lands on
    // disk, where Revert cannot undo it.
    //
    // THE SECOND LINE OF DEFENCE AND NOT THE FIRST: the button is already
    // disabled while this is true, and says why. It is here as well because
    // that one depends on a frame having been drawn, and a guard that only
    // holds while somebody is looking at the window is not a guard.
    //
    // m_dirty IS LEFT SET. Nothing was written, so the file is still unsaved
    // and the window must not claim otherwise.
    std::string problems;
    if (!m_asset.Validate(problems)) {
        m_problems = problems;
        return;
    }

    eng::Json out;
    if (!m_asset.Serialize(out)) {
        m_error = "this asset could not be written out";
        return;
    }

    std::string error;
    if (!eng::FileSystem::WriteTextFile(m_path, out.dump(2) + "\n", error)) {
        m_error = error;
        return;
    }

    m_dirty = false;
    m_error.clear();

    // RELOADED THROUGH THE LIBRARY, so anything already pointing at this file
    // sees the new contents - and through the library's own probe-then-apply,
    // which is the one path that knows how to do that without leaving a
    // half-applied asset behind. Only if it was loaded in the first place:
    // saving a file nothing has opened should not load it.
    if (eng::InputActionLibrary::Find(m_path) != nullptr) {
        std::string reloadError;
        if (!eng::InputActionLibrary::Reload(m_path, reloadError)) {
            m_error = reloadError;
        } else {
            // AND THE COMPONENTS HOLDING POINTERS INTO IT ARE TOLD. Reload
            // refills the asset's map vector in place, which leaves every
            // ActionMap* taken from it before now pointing at freed or
            // renumbered storage - and a PlayerInput caches one of those,
            // resolved once. Reloading the asset without this second half is
            // how "edit a binding, Save, press Play" ends up driving the
            // wrong map while the Inspector still names the right one.
            //
            // ONLY ON SUCCESS. A failed Reload left the live asset exactly as
            // it was, by the library's probe-then-apply, so nothing has been
            // invalidated and nothing needs re-resolving.
            eng::PlayerInputSystem::ReloadAll(m_path);
        }
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "saved input actions '{}'", m_path);
}

void InputActionsPanel::Open(std::string_view virtualPath) {
    SetOpen(true);
    Load(virtualPath);
}

// A DOUBLE-CLICK IN THE ASSETS PANEL ARRIVES HERE, as a request left for this
// frame rather than as a call made during the other panel's draw - the same
// bargain EditorState::requestedScene makes.
//
// CLEARED WHETHER OR NOT THE LOAD WORKED, so a file with a syntax error does
// not reopen itself every frame with the same message.
void InputActionsPanel::TakeOpenRequest() {
    std::string& requested = EditorState::Get().requestedInputActions;
    if (requested.empty()) {
        return;
    }
    const std::string path = requested;
    requested.clear();
    Open(path);
}

void InputActionsPanel::OnHidden() { TakeOpenRequest(); }

// ---------------------------------------------------------------------------

bool InputActionsPanel::CreateDefault(std::string_view virtualPath,
                                      std::string&     outError) {
    if (virtualPath.empty()) {
        outError = "an input actions file needs a name";
        return false;
    }
    if (eng::FileSystem::Exists(virtualPath)) {
        outError = "'" + std::string(virtualPath) + "' already exists";
        return false;
    }

    // PARSED BEFORE IT IS WRITTEN. The default is prose with escapes and
    // nested composites in it, and a template that does not load would leave
    // somebody staring at a brand-new file the window refuses to open - with
    // every reason to believe they had done something wrong. There is a test
    // over the same string, and this is the belt to its braces.
    std::string     parseError;
    const eng::Json node = eng::ParseJson(eng::DefaultActionsJson(), parseError);
    if (!parseError.empty()) {
        outError = "the built-in default is broken: " + parseError;
        return false;
    }

    if (!eng::FileSystem::WriteTextFile(virtualPath, eng::DefaultActionsJson(),
                                        outError)) {
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created the input actions '{}'",
                    virtualPath);

    // Opened straight away, wherever the button was pressed. A new file you
    // then have to go and find is a new file you have to go and find.
    EditorState::Get().requestedInputActions = std::string(virtualPath);
    return true;
}

void InputActionsPanel::DrawNewPopup() {
    if (m_openNew) {
        ImGui::OpenPopup("New Input Actions");
        m_openNew = false;
        m_newError.clear();
    }
    if (!ImGui::BeginPopupModal("New Input Actions", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("assets/input/");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetNextItemWidth(220.0f);

    // FOCUSED AND SELECTED on the frame it opens, so the prefilled name can be
    // typed straight over without reaching for the mouse.
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered = ImGui::InputText("##newname", m_newName, sizeof(m_newName),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextDisabled("%s", eng::kInputActionsExtension);

    ImGui::Spacing();
    ImGui::TextWrapped("Starts with the same controls config/engine.json binds - WASD "
                       "and the arrows as one Move action, Jump, Fire, Interact, "
                       "Pause, Quit, and a Menu map. Ready to drop on a Player Input.");

    if (!m_newError.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_newError.c_str());
    }

    ImGui::Spacing();
    const bool create = ImGui::Button("Create") || entered;
    if (create) {
        const std::string path =
            "input/" + std::string(m_newName) + eng::kInputActionsExtension;
        if (CreateDefault(path, m_newError)) {
            m_filesListed = false;   // the dropdown has a new entry
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------

void InputActionsPanel::DrawToolbar() {
    if (!m_filesListed) {
        eng::FileSystem::ListFiles("input", eng::kInputActionsExtension, m_files);
        m_filesListed = true;
    }

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##file", m_loaded ? m_path.c_str() : "(choose a file)")) {
        if (m_files.empty()) {
            ImGui::TextDisabled("no .oinput files in assets/input/");
        }
        for (const std::string& file : m_files) {
            if (ImGui::Selectable(file.c_str(), file == m_path)) {
                Load(file);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("+ New")) {
        m_openNew = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Makes a new .oinput with the same controls\n"
                          "config/engine.json binds, ready to use.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        m_filesListed = false;
        if (m_loaded) {
            Load(m_path);
        }
    }

    if (m_loaded) {
        ImGui::SameLine();
        // DISABLED RATHER THAN HIDDEN when there is nothing to save, so the
        // button does not move about under the cursor.
        ImGui::BeginDisabled(!m_dirty || !m_problems.empty());
        if (ImGui::SmallButton("Save")) {
            Save();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::SmallButton("Revert")) {
            Load(m_path);
        }
        ImGui::EndDisabled();

        if (m_dirty) {
            ImGui::SameLine();
            ImGui::TextDisabled(m_problems.empty() ? "unsaved" : "cannot be saved yet");
        }
    }

    if (!m_error.empty()) {
        ImGui::TextWrapped("%s", m_error.c_str());
    }

    // WHY SAVE IS GREYED OUT, directly under the greyed-out button. A
    // disabled button with no reason beside it is the worst of both worlds:
    // it stops the save that would have lost something and leaves nobody any
    // way to work out what to fix. It cannot be a tooltip - ImGui's disabled
    // items do not answer a hover.
    if (!m_problems.empty()) {
        ImGui::TextWrapped("%s", m_problems.c_str());
    }
}

void InputActionsPanel::DrawMaps(float width) {
    ImGui::BeginChild("##maps", ImVec2(width, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Action Maps");
    ImGui::Separator();

    std::vector<eng::ActionMap>& maps = m_asset.Maps();
    for (std::size_t i = 0; i < maps.size(); ++i) {
        const std::string label = maps[i].Name() + "##map" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), m_map == static_cast<int>(i))) {
            m_map     = static_cast<int>(i);
            m_action  = -1;
            m_binding = -1;
        }
    }

    ImGui::Separator();
    if (ImGui::SmallButton("+##addmap")) {
        eng::ActionMap added;
        added.SetName(UniqueName("New Map", maps));
        maps.push_back(std::move(added));
        m_map     = static_cast<int>(maps.size()) - 1;
        m_action  = -1;
        m_binding = -1;
        m_dirty   = true;
    }
    if (SelectedMap() != nullptr && maps.size() > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("-##delmap")) {
            maps.erase(maps.begin() + m_map);
            m_map     = 0;
            m_action  = -1;
            m_binding = -1;
            m_dirty   = true;
        }
    }
    ImGui::EndChild();
}

void InputActionsPanel::DrawActions(float width) {
    ImGui::BeginChild("##actions", ImVec2(width, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Actions");
    ImGui::Separator();

    eng::ActionMap* map = SelectedMap();
    if (map == nullptr) {
        ImGui::TextDisabled("no action map selected");
        ImGui::EndChild();
        return;
    }

    std::vector<eng::InputAction>& actions = map->Actions();
    for (std::size_t a = 0; a < actions.size(); ++a) {
        eng::InputAction& action = actions[a];

        const std::string label = action.name + "##act" + std::to_string(a);
        if (ImGui::Selectable(label.c_str(),
                              m_action == static_cast<int>(a) && m_binding < 0)) {
            m_action  = static_cast<int>(a);
            m_binding = -1;
        }

        // THE METHOD IT WILL CALL, under the name. This is the one thing
        // Unity's window does not show and the one thing somebody always
        // ends up guessing at.
        ImGui::Indent();
        const std::string method = eng::ActionHandlerMethodName(action.name);
        if (method.empty()) {
            ImGui::TextDisabled("no method can be named after this");
        } else {
            ImGui::TextDisabled("%s()", method.c_str());
        }

        for (std::size_t b = 0; b < action.bindings.size(); ++b) {
            const std::string text = DescribeBinding(action.bindings[b]) + "##b" +
                                     std::to_string(a) + "_" + std::to_string(b);
            if (ImGui::Selectable(text.c_str(), m_action == static_cast<int>(a) &&
                                                    m_binding == static_cast<int>(b))) {
                m_action  = static_cast<int>(a);
                m_binding = static_cast<int>(b);
            }
        }

        if (ImGui::SmallButton(("+ binding##" + std::to_string(a)).c_str())) {
            action.bindings.push_back(eng::InputBinding{});
            m_action  = static_cast<int>(a);
            m_binding = static_cast<int>(action.bindings.size()) - 1;
            m_dirty   = true;
        }
        ImGui::Unindent();
    }

    ImGui::Separator();
    if (ImGui::SmallButton("+##addaction")) {
        eng::InputAction added;
        added.name = UniqueName("NewAction", actions);
        actions.push_back(std::move(added));
        m_action  = static_cast<int>(actions.size()) - 1;
        m_binding = -1;
        m_dirty   = true;
    }
    if (SelectedAction() != nullptr) {
        ImGui::SameLine();
        if (ImGui::SmallButton("-##delaction")) {
            actions.erase(actions.begin() + m_action);
            m_action  = -1;
            m_binding = -1;
            m_dirty   = true;
        }
    }
    ImGui::EndChild();
}

void InputActionsPanel::DrawControlField(const char* label, eng::Control& control,
                                         const char* id) {
    char buffer[128];
    ToBuffer(eng::SpellControl(control), buffer, sizeof(buffer));

    ImGui::SetNextItemWidth(200.0f);
    const std::string widget = std::string("##") + id;
    if (ImGui::InputText(widget.c_str(), buffer, sizeof(buffer))) {
        std::string   warning;
        const eng::Control parsed = eng::ParseControl(buffer, warning);
        // EMPTY CLEARS IT, which is how a composite part is removed - and is
        // not an error worth a message. Anything else that will not parse
        // leaves the old value and says why, rather than silently becoming
        // nothing while the text box still shows what was typed.
        if (buffer[0] == '\0') {
            control = eng::Control{};
            m_dirty = true;
        } else if (warning.empty()) {
            control = parsed;
            m_dirty = true;
        } else {
            m_error = warning;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", label);
}

void InputActionsPanel::DrawActionProperties(eng::InputAction& action) {
    char name[128];
    ToBuffer(action.name, name, sizeof(name));
    if (row::InputText("Name", name, sizeof(name))) {
        action.name = name;
        m_dirty     = true;
    }

    int type = static_cast<int>(action.type);
    if (row::Combo("Action Type", &type, kActionTypeNames, 3)) {
        action.type = static_cast<eng::ActionType>(type);
        m_dirty     = true;
    }

    int control = static_cast<int>(action.controlType);
    if (row::Combo("Control Type", &control, kControlTypeNames, 3)) {
        action.controlType = static_cast<eng::ControlType>(control);
        m_dirty            = true;
    }

    ImGui::Separator();
    switch (action.type) {
        case eng::ActionType::Button:
            ImGui::TextWrapped("A Button calls its method ONCE, on the press. There is "
                               "no second call on the release - poll IsDown for that.");
            break;
        case eng::ActionType::Value:
            ImGui::TextWrapped("A Value calls its method on every change AND once more "
                               "with zero when it stops, so a script that started "
                               "moving is told when to stop.");
            break;
        case eng::ActionType::PassThrough:
            ImGui::TextWrapped("Pass Through has no phases and no conflict resolution: "
                               "every change on any bound control gets through.");
            break;
    }
}

void InputActionsPanel::DrawBindingProperties(eng::InputBinding& binding) {
    const char* shapes[] = {"Control", "2DVector", "1DAxis"};
    int         shape    = static_cast<int>(binding.shape);
    if (row::Combo("Binding", &shape, shapes, 3)) {
        binding.shape = static_cast<eng::InputBinding::Shape>(shape);
        m_dirty       = true;
    }

    ImGui::Separator();
    switch (binding.shape) {
        case eng::InputBinding::Shape::Control:
            DrawControlField("the control", binding.control, "ctl");
            break;

        case eng::InputBinding::Shape::Vector2Composite: {
            DrawControlField("up", binding.up, "up");
            DrawControlField("down", binding.down, "down");
            DrawControlField("left", binding.left, "left");
            DrawControlField("right", binding.right, "right");

            int mode = static_cast<int>(binding.mode);
            if (row::Combo("Mode", &mode, kCompositeModes, 3)) {
                binding.mode = static_cast<eng::CompositeMode>(mode);
                m_dirty      = true;
            }
            ImGui::TextWrapped("digitalNormalized is the default and cuts a diagonal "
                               "back to length 1, so holding two keys is not 41% "
                               "faster than holding one.");
            break;
        }

        case eng::InputBinding::Shape::AxisComposite:
            DrawControlField("positive", binding.positive, "pos");
            DrawControlField("negative", binding.negative, "neg");
            break;
    }

    ImGui::Separator();
    ImGui::TextDisabled("Key.Space, Mouse.Left. Modifiers are two words: Key.Left Ctrl");

    if (ImGui::SmallButton("Remove this binding")) {
        eng::InputAction* action = SelectedAction();
        if (action != nullptr) {
            action->bindings.erase(action->bindings.begin() + m_binding);
            m_binding = -1;
            m_dirty   = true;
        }
    }
}

void InputActionsPanel::DrawProperties() {
    ImGui::BeginChild("##props", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    // THE TITLE SAYS WHICH, because the pane shows two quite different things
    // and an unlabelled one leaves you guessing what you selected.
    if (eng::InputBinding* binding = SelectedBinding()) {
        ImGui::TextDisabled("Binding Properties");
        ImGui::Separator();
        DrawBindingProperties(*binding);
    } else if (eng::InputAction* action = SelectedAction()) {
        ImGui::TextDisabled("Action Properties");
        ImGui::Separator();
        DrawActionProperties(*action);
    } else if (eng::ActionMap* map = SelectedMap()) {
        ImGui::TextDisabled("Action Map Properties");
        ImGui::Separator();

        char name[128];
        ToBuffer(map->Name(), name, sizeof(name));
        if (row::InputText("Name", name, sizeof(name))) {
            map->SetName(name);
            m_dirty = true;
        }

        const bool isDefault = m_asset.DefaultMapName() == map->Name();
        ImGui::BeginDisabled(isDefault);
        if (ImGui::SmallButton("Make this the default map")) {
            m_asset.SetDefaultMapName(map->Name());
            m_dirty = true;
        }
        ImGui::EndDisabled();
        if (isDefault) {
            ImGui::SameLine();
            ImGui::TextDisabled("already the default");
        }

        ImGui::Separator();
        ImGui::TextWrapped("A map REPLACES rather than layering: while this one is "
                           "current, nothing in any other map is consulted. That is "
                           "what lets one key mean two things in two maps.");
    } else {
        ImGui::TextDisabled("select an action map");
    }

    ImGui::EndChild();
}

void InputActionsPanel::Draw() {
    TakeOpenRequest();

    // ASKED EVERY FRAME, not when Save is pressed. The answer changes as
    // somebody types, and a window that only tells you the second map is
    // called "New Map" too at the moment you try to save it has waited until
    // the least useful moment to say so.
    //
    // It is a handful of string compares over two short vectors. A dirty flag
    // to avoid them would be another piece of state to get wrong, and this
    // runs only while the window is open and being looked at.
    m_problems.clear();
    if (m_loaded) {
        m_asset.Validate(m_problems);
    }

    DrawToolbar();
    DrawNewPopup();
    ImGui::Separator();

    if (!m_loaded) {
        // THE EMPTY STATE OFFERS THE WAY OUT OF ITSELF. A project with no
        // .oinput in it reaches this window from View -> Input Actions and
        // would otherwise find a dropdown with nothing in it and no hint that
        // making one is a button away.
        ImGui::TextWrapped("An action maps file binds keys to named actions, and a "
                           "script answers them by writing void OnJump().");
        ImGui::Spacing();
        if (ImGui::Button("Create a new one")) {
            m_openNew = true;
        }
        ImGui::Spacing();
        // POINTED AT THE SCRIPTING API REFERENCE AND NOT AT A HEADER. Almost
        // nobody using this editor has the engine's source - the reference
        // under docs/api/ is the documentation that ships, and "Input" is the
        // page that covers these. Naming an internal path here sends most
        // readers to a file they cannot open.
        ImGui::TextDisabled("It starts with the controls config/engine.json already");
        ImGui::TextDisabled("binds, so it is playable straight away.");
        ImGui::Spacing();
        ImGui::TextDisabled("To write one by hand, or to call any of this from a");
        ImGui::TextDisabled("script, see Input in the scripting API reference.");
        return;
    }

    const float total = ImGui::GetContentRegionAvail().x;
    const float mapsWidth    = total * 0.22f;
    const float actionsWidth = total * 0.40f;

    DrawMaps(mapsWidth);
    ImGui::SameLine();
    DrawActions(actionsWidth);
    ImGui::SameLine();
    DrawProperties();
}

} // namespace editor
