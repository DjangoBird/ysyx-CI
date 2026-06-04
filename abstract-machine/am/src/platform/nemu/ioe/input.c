#include <am.h>
#include <nemu.h>

#define KEYDOWN_MASK 0x8000

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  uint32_t data = inl(KBD_ADDR);
  if (data == 0) {
    kbd->keydown = false;
    kbd->keycode = AM_KEY_NONE;
    return;
  }

  kbd->keydown = (data & KEYDOWN_MASK) != 0;
  kbd->keycode = data & ~KEYDOWN_MASK;
}
