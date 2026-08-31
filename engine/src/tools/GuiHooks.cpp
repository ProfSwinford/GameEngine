// ============================================================================
//  GuiHooks.cpp - storage for the three function pointers. See GuiHooks.h.
// ============================================================================

#include <engine/tools/GuiHooks.h>

namespace eng {
namespace {

// Starts out full of nullptr, which is exactly what "no tool layer is
// attached" should look like. A game never touches this.
GuiHooks g_hooks;

} // namespace

void SetGuiHooks(const GuiHooks& hooks) { g_hooks = hooks; }

const GuiHooks& GetGuiHooks() { return g_hooks; }

} // namespace eng
