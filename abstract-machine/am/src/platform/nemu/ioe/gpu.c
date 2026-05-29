#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

static int screen_w = 0;
static int screen_h = 0;

void __am_gpu_init() {
  uint32_t vga_ctl = inl(VGACTL_ADDR);
  screen_w = vga_ctl >> 16;
  screen_h = vga_ctl & 0xffff;
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  *cfg = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = screen_w, .height = screen_h,
    .vmemsz = screen_w * screen_h * sizeof(uint32_t)
  };
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  uint32_t *fb = (uint32_t *)(uintptr_t)FB_ADDR;
  int x = ctl->x, y = ctl->y, w = ctl->w, h = ctl->h;
  uint32_t *pixels = ctl->pixels;

  if (pixels != NULL && w > 0 && h > 0 && x < screen_w && y < screen_h) {
    int copy_w = w;
    if (x + copy_w > screen_w) {
      copy_w = screen_w - x;
    }

    for (int j = 0; j < h && y + j < screen_h; j ++) {
      uint32_t *dst = &fb[(y + j) * screen_w + x];
      uint32_t *src = &pixels[j * w];
      for (int i = 0; i < copy_w; i ++) {
        dst[i] = src[i];
      }
    }
  }

  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
