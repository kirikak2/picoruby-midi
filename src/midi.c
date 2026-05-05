/*
 * PicoRuby MIDI - VM selection wrapper
 */

#include "../include/midi.h"

#if defined(PICORB_VM_MRUBY)
  #include "mruby/midi.c"
#elif defined(PICORB_VM_MRUBYC)
  #include "mrubyc/midi.c"
#endif
