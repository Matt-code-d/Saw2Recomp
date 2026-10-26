// saw2 - ReXGlue Recompiled Project

#include "generated/default/saw2_init.h"

#include "game_fixes.h"
#include "saw2_app.h"

#if defined(_MSC_VER)
#include <cstdlib>
struct Saw2AbortQuiet {
  Saw2AbortQuiet() {
    // Avoid blocking "Debug Error! abort() has been called" dialogs during iteration.
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
  }
};
static Saw2AbortQuiet g_saw2_abort_quiet;
#endif

REX_DEFINE_APP(saw2, Saw2App::Create)