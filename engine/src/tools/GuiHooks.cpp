// =============================================================================
//  GuiHooks.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. GuiHooks.h is the specification; read it first.
//
//  Three function pointers are the engine's ENTIRE connection to a tool layer.
//  The engine never learns what the editor is; it only asks whether something
//  else wanted an event first.
// =============================================================================

#include <engine/tools/GuiHooks.h>

namespace eng {
namespace {

GuiHooks g_hooks;

} // namespace

 // Records the three functions a tool wants the engine to call. The editor sets
// these when it starts and clears them when it stops.
void SetGuiHooks(const GuiHooks& hooks) { g_hooks = hooks; }

// The currently installed hooks. Every one is null in a plain game, which is
// what makes the engine work with no tool attached.
const GuiHooks& GetGuiHooks() { return g_hooks; }

} // namespace eng
