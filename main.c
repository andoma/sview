#include <unistd.h>
#include <stdlib.h>
#include "sview.h"


int
main(void)
{
  sview_t *sv = sview_create("test", 640, 480);

  sview_picture_t *sp = sview_picture_alloc(640, 480, SVIEW_PIXFMT_BGRA, 1);
  uint8_t *x = sp->planes[0];
  for(int i = 0; i < sp->width * sp->height; i++) {
    x[i * 4 + 0] = i;
    x[i * 4 + 1] = i * 3;
    x[i * 4 + 2] = i * 5;
    x[i * 4 + 3] = i * 7;
  }

  sview_put_picture(sv, 0, 0, sp, "This is a test");
  pause();
}
