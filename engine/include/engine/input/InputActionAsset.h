#pragma once

// ============================================================================
//  InputActionAsset.h - a file full of action maps. Unity's .inputactions.
//
//  ONE FILE holds SEVERAL action maps, and one of them is current at a time:
//
//      assets/input/player.oinput
//        +-- "Gameplay"   Move, Jump, Fire, Pause
//        +-- "UI"         Navigate, Submit, Cancel
//
//  A PlayerInputComponent points at the FILE and names which map starts
//  current. Switching to "UI" when a menu opens is a pointer change inside an
//  asset already in memory - no disk, no parsing, no frame cost. Loading a
//  different FILE is the other, rarer move, for a game that ships a whole
//  separate control scheme.
//
//  ==========================================================================
//  THIS IS NOT config/engine.json, AND THE TWO DO NOT MEET
//
//  The settings file has an "input" section with contexts in it, and that is
//  a DIFFERENT SYSTEM - the polling one, InputMap, reached from a script as
//  Input::IsPressed("Fire"). It is project-wide, loaded once at start-up, and
//  fixed for the run.
//
//  This is the event-driven half. There are many of these files, they are
//  ordinary assets that the Asset Browser lists and the exporter packs, and a
//  game swaps between them while it runs. Nothing here reads, writes or
//  perturbs InputMap, and nothing in InputMap knows this file exists. A
//  project may use either, or both, and the two never have to agree.
//
//  WHAT IS DELIBERATELY SHARED IS THE SPELLING. A binding is "Key.Space" or
//  "Mouse.Left", parsed with the same EventPump::KeyCodeFromName and
//  MouseButtonFromName that config/engine.json's bindings go through. There
//  is one translation from text to key number in this engine, and somebody
//  who has written one file already knows how to write the other.
//
//  ==========================================================================
//  A STRING IS ONE CONTROL, AN OBJECT IS A COMPOSITE
//
//      "bindings": ["Mouse.Left", "Key.Return"]        <- two ways to Fire
//
//      "bindings": [                                   <- one way to Move
//        { "composite": "2DVector",
//          "up": "Key.W", "down": "Key.S", "left": "Key.A", "right": "Key.D" }
//      ]
//
//  There is no "kind" field saying which it is, for the reason TileAsset.h
//  gives about "sheet" against "image": the SHAPE is the kind, so the two
//  cannot disagree with each other and there is no third state to handle.
//
//  ==========================================================================
//  NO '#' IN A REFERENCE TO A MAP, WHICH IS NOT A STYLE CHOICE
//
//  "input/player.oinput#Gameplay" would have been the obvious spelling, and
//  it silently breaks shipping. GameExport walks every string in a scene and
//  keeps the ones that name a real file, passing each through eng::SheetPathOf
//  first - and eng::IsSubAsset, which decides whether there is a sub-asset to
//  split off, is hard-gated on the left-hand side being an IMAGE (.png, .bmp,
//  .jpg, .jpeg). So the '#' form comes back unsplit, names no file on disk,
//  and the asset is left out of the build with nothing wrong in the editor.
//
//  So a reference is TWO fields - the asset's plain path, and the map's name:
//
//      { "actions": "input/player.oinput", "defaultMap": "Gameplay" }
//
//  The path is packed by the same scan that packs every other asset, and the
//  map name is a string that names no file and is harmlessly ignored. This is
//  also what Unity does: PlayerInput.actions is the ASSET, and
//  SwitchCurrentActionMap("UI") takes a NAME.
//
//  ==========================================================================
//  ARRAYS, NOT OBJECTS KEYED BY NAME
//
//  Maps and actions are both JSON arrays carrying their own "name". Keying by
//  name would have matched config/engine.json, and it loses the author's
//  order: nlohmann's object is a std::map, so every save would re-alphabetise
//  a file somebody arranged to read well, and the editor's list would reorder
//  itself under the cursor. Order is content here in a way it is not there.
//
//  ==========================================================================
//  WHAT A POINTER FROM HERE IS GOOD FOR
//
//  An InputActionAsset* stays valid until Clear(), like a TileAsset*, and
//  Reload() mutates the live object in place so holders see the new contents.
//
//  A POINTER INSIDE ONE DOES NOT. Reload re-fills the map vector, so every
//  ActionMap* and InputAction* handed out before it dangles afterwards. Hold
//  the asset and the map's NAME and resolve on use - the same bargain
//  ComponentRef.h makes, for the same reason, and the house rule it states:
//  do not keep the pointer, ask each time.
//
//  ==========================================================================
//  THIS FILE IS HAND-WRITTEN TODAY AND WILL NOT ALWAYS BE
//
//  An Actions editor window is planned, modelled on Unity's, which is three
//  panels side by side:
//
//      A. ACTION MAPS   the list of maps in this file.   "+" adds a map.
//      B. ACTIONS       the selected map's actions, each
//                       expanding to its bindings.       "+" adds an action,
//                                                        and a "+" on an
//                                                        action adds a binding.
//      C. PROPERTIES    whatever is selected in B - an action's type and
//                       control type, or a binding's control and composite
//                       parts. Its title changes between the two.
//
//  It is deliberately LAST. Everything below has to work from a hand-written
//  file first, because a window that writes a format nothing can yet read is
//  a window with no way to tell whether it is writing it correctly - and the
//  format is the part that is expensive to change afterwards.
//
//  TWO THINGS HERE EXIST FOR IT. Serialize is the write path, and the
//  warn-and-skip split means a file half-edited by hand still loads, which is
//  what lets the window and a text editor be used on the same file. A panel
//  will want to keep the parsed Json as its edit buffer so that "_comment"
//  and "_note" keys survive a round trip, the way TilePalettePanel does.
// ============================================================================

#include <engine/core/Json.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  What kind of thing an action is, in Unity's vocabulary and with Unity's
//  meanings. Kept verbatim because a reader arriving from there should not
//  have to learn a second set of words for the same three ideas.
// ---------------------------------------------------------------------------
enum class ActionType {
    Button,        // on or off. Raises on the press; see the note in InputActions.h
    Value,         // a changing amount, with phases and conflict resolution
    PassThrough,   // a changing amount with NEITHER - every change gets through
};

// What an action's value looks like. Decides which handler shapes a script may
// write for it, and is derived from the bindings when the file does not say.
enum class ControlType {
    Button,
    Axis,       // one number, -1 to 1
    Vector2,
};

// How a 2D composite combines its four parts. Unity's default is
// DigitalNormalized, and so is this one: holding up and right at once would
// otherwise travel about 41% faster than holding right, which players notice
// immediately. It is the same correction InputMap::GetAxis2D already applies,
// so the two halves of the engine cannot disagree about what a diagonal is.
enum class CompositeMode {
    Analog,
    Digital,
    DigitalNormalized,
};

const char* ToString(ActionType type);
const char* ToString(ControlType type);
const char* ToString(CompositeMode mode);

// The reverse of each, for the Actions window's dropdowns and for reading a
// file. `outKnown` is false when the text matched nothing, in which case the
// first value is returned - the caller decides whether that is worth saying.
ActionType    ActionTypeFromName(std::string_view text, bool& outKnown);
ControlType   ControlTypeFromName(std::string_view text, bool& outKnown);
CompositeMode CompositeModeFromName(std::string_view text, bool& outKnown);

// ---------------------------------------------------------------------------
//  One physical control. "Key.Space" or "Mouse.Left", already translated.
//
//  Its own enum rather than InputMap's, which is private to InputMap.cpp and
//  is going to stay there. The two systems agreeing on a spelling is the
//  point; sharing a type would be the coupling this file exists to avoid.
// ---------------------------------------------------------------------------
enum class ControlDevice {
    None,
    Key,
    MouseButton,
};

struct Control {
    ControlDevice device = ControlDevice::None;
    int           code   = 0;

    // False for a control that failed to parse, which is how a binding with
    // one bad part still works for its other parts.
    explicit operator bool() const { return device != ControlDevice::None; }

    bool operator==(const Control& other) const {
        return device == other.device && code == other.code;
    }
};

// "Key.Space" or "Mouse.Left", both ways. The SAME two spellings
// config/engine.json uses, translated by the same EventPump statics - so
// there is one text-to-key-number answer in this engine and not two.
//
// ParseControl leaves outWarning empty on success and otherwise says what is
// wrong in a whole sentence - "there is no key called 'Spacebar'" - because
// that sentence is what the Console and the Actions window both put on
// screen. The returned Control is false-y when it failed.
Control     ParseControl(std::string_view text, std::string& outWarning);
std::string SpellControl(const Control& control);

// ---------------------------------------------------------------------------
//  One binding on an action: a control, or a composite built from several.
// ---------------------------------------------------------------------------
struct InputBinding {
    enum class Shape {
        Control,            // "Key.Space"
        Vector2Composite,   // { "composite": "2DVector", "up": ..., ... }
        AxisComposite,      // { "composite": "1DAxis", "positive": ..., ... }
    };

    Shape         shape = Shape::Control;
    CompositeMode mode  = CompositeMode::DigitalNormalized;

    Control control;                           // Shape::Control
    Control up, down, left, right;             // Shape::Vector2Composite
    Control positive, negative;                // Shape::AxisComposite
};

// ---------------------------------------------------------------------------
//  One named action and everything that can trigger it.
// ---------------------------------------------------------------------------
struct InputAction {
    std::string               name;
    ActionType                type        = ActionType::Button;
    ControlType               controlType = ControlType::Button;
    std::vector<InputBinding> bindings;
};

// ---------------------------------------------------------------------------
//  A named group of actions. One is current at a time per PlayerInput.
//
//  NOT A STACK, which is the visible difference from InputMap's contexts and
//  is Unity's behaviour rather than an omission. A context stack layers, so a
//  menu pushed over gameplay hides only the keys it binds and lets the rest
//  through. A map REPLACES: while "UI" is current, "Gameplay" is not consulted
//  at all, even for a key "UI" says nothing about. That is what makes a map a
//  complete statement of what the controls mean right now, and it is why
//  Escape can be Pause in one map and Cancel in the other with nothing to
//  arbitrate between them.
// ---------------------------------------------------------------------------
class ActionMap {
public:
    const std::string&                Name() const { return m_name; }
    const std::vector<InputAction>&   Actions() const { return m_actions; }

    // ---- EDITING, which only the Actions window does -----------------
    //
    //  The window is the authoring tool for this format, so it has to add,
    //  rename and remove. Everything else in the engine takes an asset as
    //  read, which is why the const forms come first and these are the
    //  exception rather than the interface.
    //
    //  ANYTHING CHANGED HERE INVALIDATES POINTERS INTO THE ASSET, exactly
    //  as a Reload does - the vector may move. The window edits a copy of
    //  its own and saves through the file, so nothing live is holding one.
    std::vector<InputAction>& Actions() { return m_actions; }
    void SetName(std::string_view name) { m_name = name; }

    // Null when this map has no action by that name. Names are unique within
    // a map and not across them, exactly as in Unity - two maps may each have
    // a "Fire" and they are different actions.
    const InputAction* Find(std::string_view action) const;

private:
    friend class InputActionAsset;
    std::string              m_name;
    std::vector<InputAction> m_actions;
};

// ---------------------------------------------------------------------------
//  The file.
// ---------------------------------------------------------------------------
class InputActionAsset {
public:
    InputActionAsset() = default;

    // NON-COPYABLE, like TileAsset and for the same reason: the library hands
    // out pointers and identity is compared by pointer, so a copy would be a
    // second asset that claims to be the same one.
    InputActionAsset(const InputActionAsset&)            = delete;
    InputActionAsset& operator=(const InputActionAsset&) = delete;

    // ---- reading and writing --------------------------------------------
    //
    //  TAKES A PARSED NODE, NOT A PATH. Reading the file is the library's job,
    //  which is the split every asset here uses and the reason the whole of
    //  this class is testable with a Json built in the test and no asset root
    //  at all.
    //
    //  TWO KINDS OF PROBLEM, TWO ANSWERS. A structural one - not an object, no
    //  "maps", a map with no name - REFUSES, because there is nothing to make
    //  of the file and a half-loaded action map is worse than none. A local
    //  one - a key that does not exist, an unknown action type, a composite
    //  missing a part - goes into outWarnings and that one binding is skipped.
    //  The same two-tier split TileAsset::Deserialize makes between a missing
    //  texture and a bad field, for the same reason: a typo in one binding
    //  should not cost you the other forty.
    bool Deserialize(const Json& node, std::string& outError, std::string& outWarnings);
    bool Serialize(Json& out) const;

    // ---- WOULD THIS SURVIVE BEING WRITTEN OUT AND READ BACK? -------------
    //
    //  False when writing this asset AS IT STANDS would lose something, with
    //  one line per problem in outProblems. The Actions window asks before it
    //  writes, and does not write when the answer is no - because Serialize
    //  will happily produce a file that Deserialize then refuses or quietly
    //  thins out, and the window is the only thing that can put an asset in
    //  such a state. (A loaded one never is: the parser drops the duplicates
    //  and the nameless on the way in, which is exactly the loss this exists
    //  to prevent a second time.)
    //
    //  THE RULES HERE ARE DESERIALIZE'S RULES, with no independent existence:
    //  each one names something the parser either refuses outright or skips
    //  with a warning. Two sets of rules in two functions can drift apart, so
    //  every rule has a test asserting BOTH halves - that this complains, and
    //  that Deserialize really would have lost it. Add a rule here only
    //  together with its pair.
    //
    //  IT IS ABOUT LOSS, NOT ABOUT STYLE. A binding with no control chosen
    //  yet, a composite with nothing typed into it, an action with no bindings
    //  at all - every one of those comes back exactly as it went in, so none
    //  of them is a problem here. Half-finished is the normal state of a file
    //  somebody is editing, and refusing to save it would be the worse bug.
    bool Validate(std::string& outProblems) const;

    // ---- what is in it ----------------------------------------------------
    const std::string&            Path() const { return m_path; }
    void                          SetPath(std::string_view path) { m_path = path; }

    // The map a PlayerInput starts on when it names none itself. Empty when
    // the file does not say, in which case the first map is used - a file with
    // one map in it should not have to name it twice.
    const std::string&            DefaultMapName() const { return m_defaultMap; }
    const std::vector<ActionMap>& Maps() const { return m_maps; }

    // ---- EDITING. See the note on ActionMap::Actions above. ----------
    std::vector<ActionMap>& Maps() { return m_maps; }
    void SetDefaultMapName(std::string_view name) { m_defaultMap = name; }

    // Null when there is no map by that name. See the header note: hold this
    // asset and the NAME, not the pointer, across a Reload.
    const ActionMap* FindMap(std::string_view name) const;

    // The map a PlayerInput naming nothing should start on: the one
    // DefaultMapName picks, or the first, or null for an empty file.
    const ActionMap* DefaultMap() const;

private:
    std::string            m_path;
    std::string            m_defaultMap;
    std::vector<ActionMap> m_maps;
};

// ---------------------------------------------------------------------------
//  The cache. One asset per path, parsed once.
//
//  Modelled on TileLibrary, which is the closest existing thing: a standalone
//  named asset, loaded by virtual path, handed out as a raw pointer, alive
//  until Clear(). Two deliberate differences, each taken from PrefabLibrary
//  where that one fits better:
//
//    Load TAKES AN outError rather than only logging, because the Inspector
//    and the Asset Browser both want the sentence to put on screen beside the
//    field that is wrong.
//
//    A FAILURE IS NOT CACHED. A file that was broken and has been fixed must
//    start working without restarting the editor, which is the whole of how
//    hand-editing one of these is meant to feel.
// ---------------------------------------------------------------------------
class InputActionLibrary {
public:
    static const InputActionAsset* Load(std::string_view virtualPath, std::string& outError);
    static const InputActionAsset* Find(std::string_view virtualPath);

    // Re-reads the file INTO THE LIVE OBJECT, so every holder of the asset
    // pointer sees the new contents and nothing has to be re-dropped in the
    // Inspector. Parsed into a scratch asset first and only applied if that
    // succeeded, because Deserialize gives up part-way through and the live
    // object is what the running scene is pointing at - the same probe
    // TileLibrary::Reload does.
    //
    // INTERIOR POINTERS DIE HERE. See the header note.
    static bool Reload(std::string_view virtualPath, std::string& outError);

    static void        Forget(std::string_view virtualPath);
    static void        Clear();
    static std::size_t Count();

    static std::vector<const InputActionAsset*> All();
};

// The extension, declared once, beside the type that owns it - as
// kSceneExtension is in Scene.h and kPrefabExtension is in Prefab.h, and
// unlike .otile, whose spelling is scattered across the editor as a literal.
inline constexpr const char* kInputActionsExtension = ".oinput";

// ---------------------------------------------------------------------------
//  WHAT A NEW FILE STARTS AS, ready to use rather than empty.
//
//  It binds what config/engine.json's "input" section binds - the same keys
//  for the same intents - so a project that already works keeps working when
//  it moves to action maps, and so a brand-new one can be played
//  immediately. An empty file would be correct and useless: there is nothing
//  to look at, nothing to run, and no example of the two shapes a binding
//  can take.
//
//  THE FOUR MOVE ACTIONS BECOME ONE. config/engine.json needs MoveLeft,
//  MoveRight, MoveUp and MoveDown as separate actions because that layer has
//  no composites; here they are one "Move" action that hands a script a
//  direction already worked out. That is the whole reason to prefer this
//  system for movement, so the default has to show it.
//
//  HERE AND NOT IN THE EDITOR, deliberately: the format's own header owns the
//  canonical example of it, and keeping it on this side of the line is what
//  lets a test assert the thing actually parses with no warnings. A default
//  template nobody can test is a template that quietly rots.
// ---------------------------------------------------------------------------
std::string DefaultActionsJson();

} // namespace eng
