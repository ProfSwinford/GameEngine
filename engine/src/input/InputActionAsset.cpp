#include <engine/input/InputActionAsset.h>

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/platform/EventPump.h>

#include <algorithm>
#include <map>
#include <memory>

namespace eng {

// ---------------------------------------------------------------------------
//  Text to control, through the SAME two functions config/engine.json's
//  bindings go through. There is one translation from a key's name to a key's
//  number in this engine, and this is not a second one.
// ---------------------------------------------------------------------------
Control ParseControl(std::string_view text, std::string& outWarning) {
    Control control;

    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        outWarning = "binding '" + std::string(text) +
                     "' is missing its device prefix (expected Key. or Mouse.)";
        return control;
    }

    const std::string_view device = text.substr(0, dot);
    const std::string      name(text.substr(dot + 1));

    if (device == "Key") {
        const int code = EventPump::KeyCodeFromName(name.c_str());
        if (code < 0) {
            outWarning = "there is no key called '" + name + "'";
            return control;
        }
        control.device = ControlDevice::Key;
        control.code   = code;
        return control;
    }

    if (device == "Mouse") {
        const int code = EventPump::MouseButtonFromName(name.c_str());
        if (code < 0) {
            outWarning = "there is no mouse button called '" + name +
                         "' (try Left, Right or Middle)";
            return control;
        }
        control.device = ControlDevice::MouseButton;
        control.code   = code;
        return control;
    }

    outWarning = "unknown device '" + std::string(device) + "' in binding '" +
                 std::string(text) + "'";
    return control;
}

// Control back to text, for writing the file out again.
//
// THE MOUSE BUTTON'S NAME IS ASKED FOR RATHER THAN TABULATED. There is no
// MouseButtonName to pair with KeyName, and hard-coding 1/2/3 here would be a
// second opinion about what SDL_BUTTON_LEFT is - so this asks the SAME
// function the parser uses, in reverse, and cannot drift from it.
//
// (InputMap::Snapshot writes "Mouse.1" for want of this, which is why the
// editor's binding list shows a number where the config file says "Left".
// That is its business and not this file's; it is noted so the next reader
// does not copy it.)
std::string SpellControl(const Control& control) {
    switch (control.device) {
        case ControlDevice::Key:
            return std::string("Key.") + EventPump::KeyName(control.code);
        case ControlDevice::MouseButton:
            for (const char* name : {"Left", "Right", "Middle"}) {
                if (EventPump::MouseButtonFromName(name) == control.code) {
                    return std::string("Mouse.") + name;
                }
            }
            return "Mouse.<unknown>";
        case ControlDevice::None:
            break;
    }
    return "";
}

namespace {

// Reads one named part of a composite. A part that is absent is silently a
// None control - "a 2DVector with no left" is a one-way stick, which is odd
// but not wrong - while a part that is PRESENT AND WRONG is reported, because
// that one is a typo somebody wants to hear about.
//
// ioAnyWritten is set when the key was THERE, whatever it turned out to say.
// That is the one fact distinguishing a composite nobody has finished typing
// from one whose every part is a typo, and ReadBinding treats those two
// differently - see the note there.
Control ReadPart(const Json& node, const char* key, const std::string& where,
                 std::string& outWarnings, bool& ioAnyWritten) {
    const auto it = node.find(key);
    if (it == node.end()) {
        return Control{};
    }

    ioAnyWritten = true;

    if (!it->is_string()) {
        outWarnings += where + ": '" + key + "' should be a binding like \"Key.W\"\n";
        return Control{};
    }

    std::string   warning;
    const Control control = ParseControl(it->get<std::string>(), warning);
    if (!warning.empty()) {
        outWarnings += where + ": " + warning + "\n";
    }
    return control;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? char(a[i] - 'A' + 'a') : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? char(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
//  THE DEFAULT IS ALWAYS THE ONE THAT KEEPS WORKING. An unknown action type is
//  a Button, an unknown composite mode is DigitalNormalized - and in both
//  cases a sentence goes into the warnings naming what was written and what is
//  accepted. A file with a typo in it should still run the other forty
//  actions; the editor's console is where the typo gets fixed from.
// ---------------------------------------------------------------------------
ActionType ReadActionType(const Json& node, const std::string& where,
                          std::string& outWarnings) {
    const auto it = node.find("type");
    if (it == node.end()) {
        return ActionType::Button;
    }
    if (it->is_string()) {
        const std::string text = it->get<std::string>();
        if (EqualsNoCase(text, "Button")) { return ActionType::Button; }
        if (EqualsNoCase(text, "Value"))  { return ActionType::Value; }
        if (EqualsNoCase(text, "PassThrough") || EqualsNoCase(text, "Pass Through")) {
            return ActionType::PassThrough;
        }
    }
    outWarnings += where +
                   ": unknown action \"type\" - expected Button, Value or "
                   "PassThrough; using Button\n";
    return ActionType::Button;
}

CompositeMode ReadMode(const Json& node, const std::string& where,
                       std::string& outWarnings) {
    const auto it = node.find("mode");
    if (it == node.end()) {
        return CompositeMode::DigitalNormalized;
    }

    // THE NUMBERS ARE UNITY'S, and are accepted because a file exported from
    // there, or written by somebody reading their docs, spells the mode as
    // 0, 1 or 2. The words are what this engine writes back.
    if (it->is_number_integer()) {
        switch (it->get<int>()) {
            case 0: return CompositeMode::DigitalNormalized;
            case 1: return CompositeMode::Digital;
            case 2: return CompositeMode::Analog;
            default: break;
        }
    } else if (it->is_string()) {
        const std::string text = it->get<std::string>();
        if (EqualsNoCase(text, "digitalNormalized")) { return CompositeMode::DigitalNormalized; }
        if (EqualsNoCase(text, "digital"))           { return CompositeMode::Digital; }
        if (EqualsNoCase(text, "analog"))            { return CompositeMode::Analog; }
    }

    outWarnings += where +
                   ": unknown composite \"mode\" - expected analog, digital or "
                   "digitalNormalized; using digitalNormalized\n";
    return CompositeMode::DigitalNormalized;
}

// One entry of an action's "bindings" array. Returns false when there is
// nothing usable to keep, having already said why.
bool ReadBinding(const Json& node, const std::string& where,
                 std::string& outWarnings, InputBinding& out) {
    // A STRING IS ONE CONTROL, AN OBJECT IS A COMPOSITE. The shape is the
    // kind; there is no field saying which, so the two cannot disagree.
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        out.shape              = InputBinding::Shape::Control;

        // ==================================================================
        //  NOTHING WRITTEN DOWN YET IS KEPT. NOTHING USABLE IS NOT.
        //
        //  An empty string is a binding with no control CHOSEN, which is
        //  exactly what the Actions window's "+ binding" adds and leaves for
        //  the control field to fill in. Somebody who adds a binding and
        //  presses Save before typing into it has to get that binding back,
        //  or the window silently eats a row of its own list - and Unity
        //  keeps the same state, as <No Binding>.
        //
        //  It is harmless to keep. A None control is never in the held set,
        //  so the binding reads zero forever and no action can fire from it.
        //
        //  AND IT IS NOT WORTH A WARNING. A warning says "what you wrote
        //  here is wrong"; nothing was written here. The empty string is the
        //  one spelling that cannot be a typo, because there is no text in
        //  it to have mistyped.
        // ==================================================================
        if (text.empty()) {
            return true;
        }

        std::string warning;
        out.control = ParseControl(text, warning);
        if (!warning.empty()) {
            outWarnings += where + ": " + warning + "\n";
            return false;
        }
        return true;
    }

    if (!node.is_object()) {
        outWarnings += where +
                       ": a binding is either \"Key.Space\" or a composite "
                       "object, and this is neither\n";
        return false;
    }

    const auto composite = node.find("composite");
    if (composite == node.end() || !composite->is_string()) {
        outWarnings += where +
                       ": a binding object needs a \"composite\" of \"2DVector\" "
                       "or \"1DAxis\"\n";
        return false;
    }

    const std::string kind = composite->get<std::string>();
    out.mode               = ReadMode(node, where, outWarnings);

    // SET WHEN ANY PART KEY WAS PRESENT, however bad its contents. A
    // composite with no usable parts means two quite different things
    // depending on this, and they get different answers below.
    bool anyWritten = false;

    if (EqualsNoCase(kind, "2DVector") || EqualsNoCase(kind, "Dpad")) {
        out.shape = InputBinding::Shape::Vector2Composite;
        out.up    = ReadPart(node, "up", where, outWarnings, anyWritten);
        out.down  = ReadPart(node, "down", where, outWarnings, anyWritten);
        out.left  = ReadPart(node, "left", where, outWarnings, anyWritten);
        out.right = ReadPart(node, "right", where, outWarnings, anyWritten);
        if (!out.up && !out.down && !out.left && !out.right) {
            // AN EMPTY ONE IS KEPT, A BROKEN ONE IS NOT - the same split the
            // string case above makes, for the same reason. Switching the
            // Binding dropdown to 2DVector gives four empty part fields, and
            // that composite has to survive a Save. A composite whose parts
            // were all WRITTEN and all unreadable is a different thing: the
            // typos have already been reported by name, and keeping a 2DVector
            // that cannot say what any of its directions were would turn four
            // fixable typos into four blank fields.
            if (!anyWritten) {
                return true;
            }
            outWarnings += where + ": this 2DVector has no usable parts\n";
            return false;
        }
        return true;
    }

    if (EqualsNoCase(kind, "1DAxis") || EqualsNoCase(kind, "Axis")) {
        out.shape    = InputBinding::Shape::AxisComposite;
        out.positive = ReadPart(node, "positive", where, outWarnings, anyWritten);
        out.negative = ReadPart(node, "negative", where, outWarnings, anyWritten);
        if (!out.positive && !out.negative) {
            if (!anyWritten) {
                return true;
            }
            outWarnings += where + ": this 1DAxis has no usable parts\n";
            return false;
        }
        return true;
    }

    outWarnings += where + ": unknown composite '" + kind +
                   "' - this engine has 2DVector and 1DAxis\n";
    return false;
}

// ---------------------------------------------------------------------------
//  WHAT SHAPE THE VALUE IS, WORKED OUT FROM THE BINDINGS when the file does
//  not say - the same way a tile's size is derived from its image rather than
//  written down twice and allowed to drift.
// ---------------------------------------------------------------------------
ControlType DeriveControlType(const std::vector<InputBinding>& bindings) {
    bool axis = false;
    for (const InputBinding& binding : bindings) {
        if (binding.shape == InputBinding::Shape::Vector2Composite) {
            return ControlType::Vector2;
        }
        if (binding.shape == InputBinding::Shape::AxisComposite) {
            axis = true;
        }
    }
    return axis ? ControlType::Axis : ControlType::Button;
}

ControlType ReadControlType(const Json& node, const std::vector<InputBinding>& bindings,
                            const std::string& where, std::string& outWarnings) {
    const auto it = node.find("controlType");
    if (it == node.end()) {
        return DeriveControlType(bindings);
    }
    if (it->is_string()) {
        const std::string text = it->get<std::string>();
        if (EqualsNoCase(text, "Button"))  { return ControlType::Button; }
        if (EqualsNoCase(text, "Axis"))    { return ControlType::Axis; }
        if (EqualsNoCase(text, "Vector2")) { return ControlType::Vector2; }
    }
    outWarnings += where +
                   ": unknown \"controlType\" - expected Button, Axis or "
                   "Vector2; working it out from the bindings instead\n";
    return DeriveControlType(bindings);
}

// The cache. In a function and not at file scope, because the engine DLL
// exports functions and not data - the same note TileAsset.cpp carries.
//
// std::map rather than unordered_map so All() has a stable order for the
// editor's list, and unique_ptr because the asset is non-copyable and the
// pointers handed out must not move when the map grows.
std::map<std::string, std::unique_ptr<InputActionAsset>, std::less<>>& Assets() {
    static std::map<std::string, std::unique_ptr<InputActionAsset>, std::less<>> assets;
    return assets;
}

bool ParseAssetFile(std::string_view virtualPath, Json& outNode, std::string& outError) {
    std::string text;
    if (!FileSystem::ReadTextFile(virtualPath, text, outError)) {
        return false;
    }
    outNode = ParseJson(text, outError);
    return outError.empty();
}

void ReportWarnings(std::string_view virtualPath, const std::string& warnings) {
    if (warnings.empty()) {
        return;
    }
    // One line per problem, each already naming the map and the action it is
    // in, so the message says which line of the file to go and look at.
    std::size_t start = 0;
    while (start < warnings.size()) {
        const std::size_t end  = warnings.find('\n', start);
        const std::size_t stop = (end == std::string::npos) ? warnings.size() : end;
        if (stop > start) {
            ENGINE_LOG_WARN(Channels::kInput, "{}: {}", virtualPath,
                            std::string_view(warnings).substr(start, stop - start));
        }
        start = (end == std::string::npos) ? warnings.size() : end + 1;
    }
}

} // namespace

// ---------------------------------------------------------------------------

const char* ToString(ActionType type) {
    switch (type) {
        case ActionType::Button:      return "Button";
        case ActionType::Value:       return "Value";
        case ActionType::PassThrough: return "PassThrough";
    }
    return "?";
}

const char* ToString(ControlType type) {
    switch (type) {
        case ControlType::Button:  return "Button";
        case ControlType::Axis:    return "Axis";
        case ControlType::Vector2: return "Vector2";
    }
    return "?";
}

const char* ToString(CompositeMode mode) {
    switch (mode) {
        case CompositeMode::Analog:            return "analog";
        case CompositeMode::Digital:           return "digital";
        case CompositeMode::DigitalNormalized: return "digitalNormalized";
    }
    return "?";
}

// The reverse of the three ToString above, for the Actions window's
// dropdowns. EqualsNoCase rather than exact, so a file written by hand with
// "button" is read the same as one the window wrote with "Button".
ActionType ActionTypeFromName(std::string_view text, bool& outKnown) {
    outKnown = true;
    if (EqualsNoCase(text, "Button")) { return ActionType::Button; }
    if (EqualsNoCase(text, "Value")) { return ActionType::Value; }
    if (EqualsNoCase(text, "PassThrough") || EqualsNoCase(text, "Pass Through")) {
        return ActionType::PassThrough;
    }
    outKnown = false;
    return ActionType::Button;
}

ControlType ControlTypeFromName(std::string_view text, bool& outKnown) {
    outKnown = true;
    if (EqualsNoCase(text, "Button")) { return ControlType::Button; }
    if (EqualsNoCase(text, "Axis")) { return ControlType::Axis; }
    if (EqualsNoCase(text, "Vector2")) { return ControlType::Vector2; }
    outKnown = false;
    return ControlType::Button;
}

CompositeMode CompositeModeFromName(std::string_view text, bool& outKnown) {
    outKnown = true;
    if (EqualsNoCase(text, "digitalNormalized")) {
        return CompositeMode::DigitalNormalized;
    }
    if (EqualsNoCase(text, "digital")) { return CompositeMode::Digital; }
    if (EqualsNoCase(text, "analog")) { return CompositeMode::Analog; }
    outKnown = false;
    return CompositeMode::DigitalNormalized;
}

std::string DefaultActionsJson() {
    // The "_" keys are prose for whoever opens this in a text editor. nlohmann
    // drops // comments on a rewrite, so an explanation has to be a KEY to
    // survive the Actions window saving over it - the same reason a .otile and
    // scenes/build.json carry one.
    return R"({
  "_comment": "An Input Actions asset: ONE file holding SEVERAL action maps, with one of them current at a time - the same shape as Unity's .inputactions. Put a Player Input component on an entity, point it at this file, and name the map it starts on. Switching maps while the game runs is a pointer change inside this already-loaded file, with no disk access.",

  "_not_the_config_file": "This is NOT config/engine.json's \"input\" section. That one is the POLLING system - Input::IsPressed(\"Fire\") - it is project-wide, and nothing in here affects it. A project may use either or both, and the two never have to agree.",

  "_bindings": "A binding is \"Device.Name\", spelled exactly as config/engine.json spells it: \"Key.\" followed by any key name SDL knows, or \"Mouse.\" followed by Left, Right or Middle. SDL's modifier keys are TWO WORDS - \"Key.Left Ctrl\", not \"Key.LCtrl\". A STRING is one control; an OBJECT is a composite built out of several.",

  "_handlers": "An action called \"Jump\" calls a method named void OnJump() on any script on the entity - no registration, nothing to subscribe to. Add a parameter to be handed the value: void OnMove(eng::Vec2 direction) or void OnLean(float amount). The method must be PUBLIC, because the engine calls it from outside the class.",

  "inputActions": 1,
  "defaultMap": "Gameplay",

  "maps": [
    {
      "name": "Gameplay",
      "actions": [
        {
          "_note": "ONE action where config/engine.json needs four. A script writes void OnMove(eng::Vec2 direction) and is handed the direction worked out, instead of asking four questions and combining them. Two whole composites, so WASD and the arrow keys both work and holding one of each does not add up - the louder one wins. Diagonals are cut back to length 1, the same correction GetAxis2D makes.",
          "name": "Move",
          "type": "Value",
          "controlType": "Vector2",
          "bindings": [
            { "composite": "2DVector",
              "up": "Key.W",  "down": "Key.S",    "left": "Key.A",    "right": "Key.D" },
            { "composite": "2DVector",
              "up": "Key.Up", "down": "Key.Down", "left": "Key.Left", "right": "Key.Right" }
          ]
        },
        {
          "_note": "A Button calls its method ONCE, on the press. There is no second call on the release - poll Input::IsReleased for that, or use a Value action.",
          "name": "Jump",
          "type": "Button",
          "bindings": ["Key.Space"]
        },
        {
          "_note": "Two bindings, one action. Either raises OnFire() once, not twice, and letting go of only one of them is not a release.",
          "name": "Fire",
          "type": "Button",
          "bindings": ["Mouse.Left", "Key.Return"]
        },
        {
          "_note": "config/engine.json also binds Mouse.Left here. That is left off on purpose: in THIS system both actions would fire from one click, where the polling layer silently resolved the click to Fire alone. Add it back if you want both.",
          "name": "Interact",
          "type": "Button",
          "bindings": ["Key.E"]
        },
        {
          "_note": "Escape is Pause here and Cancel in the Menu map below - the reason this file holds both. A map REPLACES rather than layering, so while Menu is current nothing in Gameplay is consulted and there is nothing to arbitrate between the two meanings.",
          "name": "Pause",
          "type": "Button",
          "bindings": ["Key.Escape"]
        },
        {
          "name": "Quit",
          "type": "Button",
          "bindings": ["Key.Q"]
        }
      ]
    },

    {
      "name": "Menu",
      "actions": [
        {
          "_note": "The arrow keys first, then WASD, which is the order config/engine.json lists them in for the menu.",
          "name": "Navigate",
          "type": "Value",
          "controlType": "Vector2",
          "bindings": [
            { "composite": "2DVector",
              "up": "Key.Up", "down": "Key.Down", "left": "Key.Left", "right": "Key.Right" },
            { "composite": "2DVector",
              "up": "Key.W",  "down": "Key.S",    "left": "Key.A",    "right": "Key.D" }
          ]
        },
        {
          "name": "Confirm",
          "type": "Button",
          "bindings": ["Key.Return", "Key.Space", "Mouse.Left"]
        },
        {
          "_note": "Switching to this map while Escape is still held does NOT fire Cancel: a button held from before the switch waits for a fresh press, or opening the menu would instantly close it. A Value action like Navigate behaves the opposite way and adopts whatever is already held.",
          "name": "Back",
          "type": "Button",
          "bindings": ["Key.Escape"]
        }
      ]
    }
  ]
}
)";
}

const InputAction* ActionMap::Find(std::string_view action) const {
    for (const InputAction& candidate : m_actions) {
        if (candidate.name == action) {
            return &candidate;
        }
    }
    return nullptr;
}

const ActionMap* InputActionAsset::FindMap(std::string_view name) const {
    for (const ActionMap& map : m_maps) {
        if (map.Name() == name) {
            return &map;
        }
    }
    return nullptr;
}

const ActionMap* InputActionAsset::DefaultMap() const {
    if (!m_defaultMap.empty()) {
        if (const ActionMap* named = FindMap(m_defaultMap)) {
            return named;
        }
    }
    // A file with one map in it should not have to name that map twice.
    return m_maps.empty() ? nullptr : &m_maps.front();
}

bool InputActionAsset::Deserialize(const Json& node, std::string& outError,
                                   std::string& outWarnings) {
    m_maps.clear();
    m_defaultMap.clear();

    if (!node.is_object()) {
        outError = "an input actions file is a JSON object";
        return false;
    }

    const auto maps = node.find("maps");
    if (maps == node.end() || !maps->is_array()) {
        outError =
            "an input actions file needs a \"maps\" array - that is the list of "
            "action maps the file holds, and one of them is current at a time";
        return false;
    }
    if (maps->empty()) {
        outError = "\"maps\" is empty, so this file binds nothing";
        return false;
    }

    if (const auto def = node.find("defaultMap");
        def != node.end() && def->is_string()) {
        m_defaultMap = def->get<std::string>();
    }

    for (const Json& mapNode : *maps) {
        if (!mapNode.is_object()) {
            outError = "every entry in \"maps\" is an object with a \"name\"";
            return false;
        }

        const auto nameIt = mapNode.find("name");
        if (nameIt == mapNode.end() || !nameIt->is_string() ||
            nameIt->get<std::string>().empty()) {
            outError =
                "every action map needs a non-empty \"name\" - that name is what "
                "SwitchCurrentActionMap asks for";
            return false;
        }

        ActionMap map;
        map.m_name = nameIt->get<std::string>();

        // A SECOND MAP BY THE SAME NAME IS SKIPPED, not refused. FindMap would
        // have to pick one of them and nothing in the file says which, so the
        // later one is dropped and said so - the file still loads and every
        // other map in it still works.
        if (FindMap(map.m_name) != nullptr) {
            outWarnings += "there are two action maps called '" + map.m_name +
                           "'; the second one is ignored\n";
            continue;
        }

        const auto actions = mapNode.find("actions");
        if (actions == mapNode.end() || !actions->is_array()) {
            outWarnings += "action map '" + map.m_name +
                           "' has no \"actions\" array, so it binds nothing\n";
            m_maps.push_back(std::move(map));
            continue;
        }

        for (const Json& actionNode : *actions) {
            if (!actionNode.is_object()) {
                outWarnings += "'" + map.m_name +
                               "': every entry in \"actions\" is an object\n";
                continue;
            }

            const auto actionName = actionNode.find("name");
            if (actionName == actionNode.end() || !actionName->is_string() ||
                actionName->get<std::string>().empty()) {
                outWarnings += "'" + map.m_name +
                               "': an action with no \"name\" is skipped - the "
                               "name is what a script's handler is named after\n";
                continue;
            }

            InputAction action;
            action.name = actionName->get<std::string>();

            const std::string where = "'" + map.m_name + "." + action.name + "'";

            if (map.Find(action.name) != nullptr) {
                outWarnings += where +
                               ": two actions in one map share a name; the "
                               "second is ignored\n";
                continue;
            }

            action.type = ReadActionType(actionNode, where, outWarnings);

            if (const auto bindings = actionNode.find("bindings");
                bindings != actionNode.end() && bindings->is_array()) {
                for (const Json& bindingNode : *bindings) {
                    InputBinding binding;
                    if (ReadBinding(bindingNode, where, outWarnings, binding)) {
                        action.bindings.push_back(binding);
                    }
                }
            } else {
                outWarnings += where +
                               ": no \"bindings\", so nothing can ever trigger "
                               "it\n";
            }

            action.controlType =
                ReadControlType(actionNode, action.bindings, where, outWarnings);

            map.m_actions.push_back(std::move(action));
        }

        m_maps.push_back(std::move(map));
    }

    if (m_maps.empty()) {
        outError = "every action map in this file was refused";
        return false;
    }

    if (!m_defaultMap.empty() && FindMap(m_defaultMap) == nullptr) {
        outWarnings += "\"defaultMap\" names '" + m_defaultMap +
                       "', which is not a map in this file; the first map is "
                       "used instead\n";
    }

    return true;
}

bool InputActionAsset::Serialize(Json& out) const {
    out = Json::object();
    out["inputActions"] = 1;
    if (!m_defaultMap.empty()) {
        out["defaultMap"] = m_defaultMap;
    }

    Json maps = Json::array();
    for (const ActionMap& map : m_maps) {
        Json mapNode;
        mapNode["name"] = map.Name();

        Json actions = Json::array();
        for (const InputAction& action : map.Actions()) {
            Json actionNode;
            actionNode["name"]        = action.name;
            actionNode["type"]        = ToString(action.type);
            actionNode["controlType"] = ToString(action.controlType);

            Json bindings = Json::array();
            for (const InputBinding& binding : action.bindings) {
                switch (binding.shape) {
                    case InputBinding::Shape::Control:
                        bindings.push_back(SpellControl(binding.control));
                        break;
                    case InputBinding::Shape::Vector2Composite: {
                        // A PART THAT IS NOT SET IS LEFT OUT rather than
                        // written as an empty string, because reading this
                        // file back has to produce the same asset and an
                        // absent part is what ReadPart answers None to.
                        //
                        // (The single-control arm above DOES write "", which
                        // is not an inconsistency: there the empty string is
                        // the whole binding and ReadBinding reads it back as
                        // one with no control chosen yet. Here there is no
                        // such thing as an empty PART - a key that is simply
                        // not there already means the same, and writing both
                        // spellings would be two ways to say it.)
                        Json composite;
                        composite["composite"] = "2DVector";
                        composite["mode"]      = ToString(binding.mode);
                        if (binding.up)    { composite["up"]    = SpellControl(binding.up); }
                        if (binding.down)  { composite["down"]  = SpellControl(binding.down); }
                        if (binding.left)  { composite["left"]  = SpellControl(binding.left); }
                        if (binding.right) { composite["right"] = SpellControl(binding.right); }
                        bindings.push_back(composite);
                        break;
                    }
                    case InputBinding::Shape::AxisComposite: {
                        Json composite;
                        composite["composite"] = "1DAxis";
                        if (binding.positive) {
                            composite["positive"] = SpellControl(binding.positive);
                        }
                        if (binding.negative) {
                            composite["negative"] = SpellControl(binding.negative);
                        }
                        bindings.push_back(composite);
                        break;
                    }
                }
            }
            actionNode["bindings"] = bindings;
            actions.push_back(actionNode);
        }
        mapNode["actions"] = actions;
        maps.push_back(mapNode);
    }
    out["maps"] = maps;
    return true;
}

// Every problem, not the first one: somebody who added two maps and two
// actions wants to be told about all four names at once rather than fixing
// one, pressing Save, and being told about the next.
//
// The quadratic name scans are over a handful of entries each, in a function
// that runs when a window is drawn. A set would be slower to build than the
// comparisons it saved.
bool InputActionAsset::Validate(std::string& outProblems) const {
    outProblems.clear();

    if (m_maps.empty()) {
        outProblems +=
            "there are no action maps in this file, and a file that binds "
            "nothing is refused when it is read back\n";
    }

    for (std::size_t m = 0; m < m_maps.size(); ++m) {
        const ActionMap& map = m_maps[m];

        // The map's own name is the locator for everything inside it - except
        // when the missing name IS the problem, and then it has to be the
        // position instead.
        const std::string where = map.Name().empty()
                                      ? ("action map " + std::to_string(m + 1))
                                      : ("'" + map.Name() + "'");

        if (map.Name().empty()) {
            outProblems += where +
                           " has no name, and a nameless map costs the WHOLE "
                           "file: it is refused outright rather than skipped, "
                           "so nothing in here would load\n";
        } else {
            for (std::size_t other = 0; other < m; ++other) {
                if (m_maps[other].Name() == map.Name()) {
                    outProblems += "there are two action maps called '" +
                                   map.Name() +
                                   "'; reading this back keeps the first and "
                                   "drops this one\n";
                    break;
                }
            }
        }

        const std::vector<InputAction>& actions = map.Actions();
        for (std::size_t a = 0; a < actions.size(); ++a) {
            if (actions[a].name.empty()) {
                outProblems += where + ": action " + std::to_string(a + 1) +
                               " has no name, and is dropped when it is read "
                               "back - the name is what a script's handler is "
                               "named after, so there is nothing to call\n";
                continue;
            }
            for (std::size_t other = 0; other < a; ++other) {
                if (actions[other].name == actions[a].name) {
                    outProblems += where + " has two actions called '" +
                                   actions[a].name +
                                   "'; reading this back keeps the first and "
                                   "drops this one\n";
                    break;
                }
            }
        }
    }

    return outProblems.empty();
}

// ---------------------------------------------------------------------------

const InputActionAsset* InputActionLibrary::Load(std::string_view virtualPath,
                                                 std::string&     outError) {
    if (virtualPath.empty()) {
        outError = "no input actions file was named";
        return nullptr;
    }

    auto& assets = Assets();
    if (const auto it = assets.find(virtualPath); it != assets.end()) {
        return it->second.get();
    }

    Json node;
    if (!ParseAssetFile(virtualPath, node, outError)) {
        outError = std::string(virtualPath) + ": " + outError;
        return nullptr;
    }

    auto        asset = std::make_unique<InputActionAsset>();
    std::string warnings;
    asset->SetPath(virtualPath);
    if (!asset->Deserialize(node, outError, warnings)) {
        // NOT CACHED. A file that was broken and has been fixed must start
        // working without restarting the editor.
        outError = std::string(virtualPath) + ": " + outError;
        return nullptr;
    }
    ReportWarnings(virtualPath, warnings);

    InputActionAsset* raw = asset.get();
    assets.emplace(std::string(virtualPath), std::move(asset));

    ENGINE_LOG_INFO(Channels::kInput, "loaded input actions '{}': {} map(s)",
                    virtualPath, raw->Maps().size());
    return raw;
}

const InputActionAsset* InputActionLibrary::Find(std::string_view virtualPath) {
    auto&      assets = Assets();
    const auto it     = assets.find(virtualPath);
    return (it == assets.end()) ? nullptr : it->second.get();
}

bool InputActionLibrary::Reload(std::string_view virtualPath, std::string& outError) {
    auto&      assets = Assets();
    const auto it     = assets.find(virtualPath);
    if (it == assets.end()) {
        outError = "input actions '" + std::string(virtualPath) + "' are not loaded";
        return false;
    }

    Json node;
    if (!ParseAssetFile(virtualPath, node, outError)) {
        return false;
    }

    // PROBED FIRST. Deserialize gives up part-way through on a structural
    // problem, and the live asset is what every PlayerInput in the scene is
    // pointing at - so applying a file somebody is in the middle of typing
    // would leave the game holding half a control scheme. While one of these
    // is being hand-edited, "has an error in it" is its usual state.
    InputActionAsset probe;
    std::string      warnings;
    probe.SetPath(virtualPath);
    if (!probe.Deserialize(node, outError, warnings)) {
        return false;
    }

    warnings.clear();
    const bool ok = it->second->Deserialize(node, outError, warnings);
    ReportWarnings(virtualPath, warnings);
    return ok;
}

void InputActionLibrary::Forget(std::string_view virtualPath) {
    auto&      assets = Assets();
    const auto it     = assets.find(virtualPath);
    if (it != assets.end()) {
        assets.erase(it);
    }
}

void InputActionLibrary::Clear() {
    auto& assets = Assets();
    if (!assets.empty()) {
        ENGINE_LOG_INFO(Channels::kInput, "unloading {} input actions asset(s)",
                        assets.size());
    }
    assets.clear();
}

std::size_t InputActionLibrary::Count() { return Assets().size(); }

std::vector<const InputActionAsset*> InputActionLibrary::All() {
    std::vector<const InputActionAsset*> out;
    out.reserve(Assets().size());
    for (const auto& [path, asset] : Assets()) {
        out.push_back(asset.get());
    }
    return out;
}

} // namespace eng
