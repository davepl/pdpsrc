/* Full host header and declaration-pruned native header share TMUX_H. */
#if defined(PDP11_NATIVE) || defined(TMUX_NATIVE_211BSD)
#include "tmux-native.h"
#else
#include "tmux-full.h"
#endif
