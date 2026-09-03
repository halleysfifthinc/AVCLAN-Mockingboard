#include "hal/cd_timer.h"

extern "C" void cdtimer_init(void *ptr, void(clbk)(void *),
                             bool(isplay)(void *)) {}

extern "C" void cdtimer_reset() {}

extern "C" void cdtimer_restore() {}

extern "C" void cdtimer_disable() {}

volatile bool cdtimer_pending_flag;
