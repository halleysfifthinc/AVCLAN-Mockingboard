#include "hal/media.h"

extern "C" void media_init(void) {}

extern "C" void media_action(MediaAction fn) {}

#ifndef NDEBUG
extern "C" bool media_mic_toggle(void) { return false; }
extern "C" bool media_busy(void) { return true; }
#endif
