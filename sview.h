#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#include <stdint.h>

typedef struct sview sview_t;

typedef enum sview_pixfmt {
  SVIEW_PIXFMT_RGBA,
  SVIEW_PIXFMT_BGRA,
  SVIEW_PIXFMT_RGB,
  SVIEW_PIXFMT_I,
} sview_pixfmt_t;

typedef struct sview_picture {
  unsigned int width;
  unsigned int height;
  sview_pixfmt_t pixfmt;

  unsigned char *planes[4];
  int strides[4];

  void (*release)(struct sview_picture *sp);
  void *opaque;

} sview_picture_t;


typedef enum {
  SVIEW_WIDGET_INT,
} sview_widget_type_t;


typedef struct sview_widget {
  const char *name;
  sview_widget_type_t type;
  int min;
  int *value;
  int max;
  void (*updated)(struct sview_widget *widget);
  struct widget_state *state; // Internal state
} sview_widget_t;

// Open a window
sview_t *sview_create(const char *title, int width, int height,
                      sview_widget_t *widgets);

void sview_put_picture(sview_t *sv, int col, int row,
                       sview_picture_t *picture,
                       const char *text);

sview_picture_t *sview_picture_alloc(unsigned int width, unsigned int height,
                                     sview_pixfmt_t pixfmt, int clear);


#ifdef __cplusplus
}
#endif
