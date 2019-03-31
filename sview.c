#include <sys/param.h>
#include <sys/queue.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include "sview.h"
#include "font8x8_basic.h"

TAILQ_HEAD(img_cell_queue, img_cell);


typedef struct tex {
  uint32_t t_texture;
  unsigned int t_width;
  unsigned int t_height;
  sview_picture_t *t_source;
} tex_t;


typedef struct img_cell {

  TAILQ_ENTRY(img_cell) ic_link;

  unsigned int ic_col;
  unsigned int ic_row;

  tex_t ic_content;
  tex_t ic_overlay;

} img_cell_t;




struct sview {
  char *sv_title;
  int sv_width;
  int sv_height;

  pthread_mutex_t sv_cell_mutex;
  struct img_cell_queue sv_cells;
  struct img_cell_queue sv_pending_cells;
};

typedef struct rectf {
  int left, top, right, bottom;
} rect_t;


static sview_picture_t *
text_draw_simple(uint32_t max_width, uint32_t max_height,
                 uint32_t size, const char *msg)
{
  float spacing = 1.1f;
  const int horiz_adv    = size * spacing;
  const int vert_adv     = size * spacing;
  const int horiz_border = size / 8;
  const int vert_border  = size / 8;

  int height = size;
  int width = 0;
  int xp = 0;

  // Trim trailing LF and don't draw anything if empty
  size_t len = strlen(msg);
  if(len == 0)
    return NULL;
  if(msg[len - 1] == '\n')
    len--;
  if(len == 0)
    return NULL;

  for(int i = 0; i < len; i++) {
    if(msg[i] == '\n') {
      height += vert_adv;
      xp = 0;
    } else {
      xp += horiz_adv;
      width = MAX(width, xp);
    }
  }

  height += 2 * horiz_border;
  width  += 2 * vert_border;

  width  = MIN(max_width,  width);
  height = MIN(max_height, height);


  sview_picture_t *sv = sview_picture_alloc(width, height,
                                            SVIEW_PIXFMT_BGRA, 0);

  uint32_t *d = (uint32_t *)sv->planes[0];
  const size_t s = sv->height * sv->strides[0];
  for(int i = 0; i < s; i += 4) {
    *d++ = 0x80000000;
  }

  uint32_t top = vert_border;
  uint32_t left = horiz_border * 2;

  for(int i = 0; i < len; i++) {
    int c = msg[i];
    if(c > 127)
      continue;

    if(c == '\n') {
      top += vert_adv;
      left = horiz_border;
      continue;
    }

    const uint8_t *data = font8x8_basic[c];

    for(unsigned int y = top; y < top + size && y < height; y++) {
      const unsigned int ly = y - top;
      const unsigned int fy = MIN(ly * 8 / size, 7);
      const uint8_t bits = data[fy];
      uint32_t *row = (uint32_t *)(sv->planes[0] + y * sv->strides[0]);
      for(unsigned int x = left; x < left + size && x < width; x++) {
        const unsigned int lx = x - left;
        const unsigned int fx = MIN(lx * 8 / size, 7);
        if(bits & (1 << fx))
          row[x] = 0xffffffff;
      }
    }
    left += horiz_adv;
  }
  return sv;
}


static void
tex_source_swap(tex_t *a, tex_t *b)
{
  sview_picture_t *old = a->t_source;
  a->t_source = b->t_source;
  b->t_source = old;
}

static void
tex_source_free(tex_t *t)
{
  if(t->t_source != NULL)
    t->t_source->release(t->t_source);
  t->t_source = NULL;
}


static void
copy_pending_cells(sview_t *sv)
{
  struct img_cell_queue flush;
  img_cell_t *p, *ic;
  TAILQ_INIT(&flush);
  pthread_mutex_lock(&sv->sv_cell_mutex);

  while((p = TAILQ_FIRST(&sv->sv_pending_cells)) != NULL) {
    TAILQ_REMOVE(&sv->sv_pending_cells, p, ic_link);

    TAILQ_FOREACH(ic, &sv->sv_cells, ic_link) {
      if(ic->ic_col == p->ic_col && ic->ic_row == p->ic_row)
        break;
    }

    if(ic == NULL) {
      ic = calloc(1, sizeof(img_cell_t));
      ic->ic_col = p->ic_col;
      ic->ic_row = p->ic_row;
      TAILQ_INSERT_HEAD(&sv->sv_cells, ic, ic_link);
    }

    tex_source_swap(&ic->ic_content, &p->ic_content);
    tex_source_swap(&ic->ic_overlay, &p->ic_overlay);

    TAILQ_INSERT_TAIL(&flush, p, ic_link);
  }

  pthread_mutex_unlock(&sv->sv_cell_mutex);

  while((ic = TAILQ_FIRST(&flush)) != NULL) {
    TAILQ_REMOVE(&flush, ic, ic_link);
    tex_source_free(&ic->ic_content);
    tex_source_free(&ic->ic_overlay);
    free(ic);
  }
}

static void
upload_tex(tex_t *t)
{
  sview_picture_t *sp = t->t_source;
  if(sp == NULL)
    return;

  if(t->t_texture == 0) {
    glGenTextures(1, &t->t_texture);
    glBindTexture(GL_TEXTURE_2D, t->t_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  } else {
    glBindTexture(GL_TEXTURE_2D, t->t_texture);
  }

  switch(sp->pixfmt) {
  case SVIEW_PIXFMT_BGRA:
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 sp->width, sp->height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, sp->planes[0]);
    break;
  case SVIEW_PIXFMT_I:
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 sp->width, sp->height,
                 0, GL_RED, GL_UNSIGNED_BYTE, sp->planes[0]);
    break;
  }

  t->t_width  = sp->width;
  t->t_height = sp->height;
  tex_source_free(t);
}



static void
upload_textures(sview_t *sv)
{
  img_cell_t *ic;
  TAILQ_FOREACH(ic, &sv->sv_cells, ic_link) {
    upload_tex(&ic->ic_content);
    upload_tex(&ic->ic_overlay);
  }
}


static void
draw_tex(const tex_t *t, const rect_t r)
{
  if(t->t_texture == 0)
    return;

  glBindTexture(GL_TEXTURE_2D, t->t_texture);

  glDisable(GL_BLEND);
  glColor4f(1,1,1,1);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);   glVertex3f(r.left,  r.top,    0);
  glTexCoord2f(1, 0);   glVertex3f(r.right, r.top,    0);
  glTexCoord2f(1, 1);   glVertex3f(r.right, r.bottom, 0);
  glTexCoord2f(0, 1);   glVertex3f(r.left,  r.bottom, 0);
  glEnd();
  glEnable(GL_BLEND);
}

static rect_t
rect_inset(rect_t src, int x, int y)
{
  return (rect_t){src.left + x, src.top + y, src.right - x, src.bottom - y};
}


static rect_t
rect_fit(const tex_t *t, const rect_t rect)
{
  int r_width  =  rect.right  - rect.left;
  int r_cx     = (rect.right  + rect.left) / 2;
  int r_height =  rect.bottom - rect.top;
  int r_cy     = (rect.bottom + rect.top) / 2;

  const float img_a = (float)t->t_width / (float)t->t_height;
  const float r_a   = (float)r_width / r_height;

  if(r_a > img_a) {
    r_width = r_height * img_a;
  } else if(r_a < img_a) {
    r_height = r_width / img_a;
  }

  r_width  -= 4;
  r_height -= 4;

  if(r_width < 1 || r_height < 1)
    return (rect_t){0,0,0,0};

  const rect_t r = {
    r_cx - r_width  / 2,
    r_cy - r_height / 2,
    r_cx + r_width  / 2,
    r_cy + r_height / 2,
  };
  return r;
}


static rect_t
rect_align(const tex_t *t, const rect_t rect)
{
  const rect_t r = {
    .left = rect.left,
    .top = rect.top,
    .right = rect.left + t->t_width,
    .bottom = rect.top + t->t_height
  };
  return r;

}




static void
draw_cells(sview_t *sv, const rect_t r0)
{
  int num_cols = 1;
  int num_rows = 1;

  const img_cell_t *ic;
  TAILQ_FOREACH(ic, &sv->sv_cells, ic_link) {
    num_cols = MAX(num_cols, ic->ic_col + 1);
    num_rows = MAX(num_rows, ic->ic_row + 1);
  }

  const int tot_width  = r0.right  - r0.left;
  const int tot_height = r0.bottom - r0.top;

  TAILQ_FOREACH(ic, &sv->sv_cells, ic_link) {
    const rect_t r = {
      .left   = r0.left + (tot_width  * (ic->ic_col + 0) / num_cols),
      .top    = r0.top  + (tot_height * (ic->ic_row + 0) / num_rows),
      .right  = r0.left + (tot_width  * (ic->ic_col + 1) / num_cols),
      .bottom = r0.top +  (tot_height * (ic->ic_row + 1) / num_rows),
    };

    const rect_t inner = rect_fit(&ic->ic_content, r);
    draw_tex(&ic->ic_content, inner);

    draw_tex(&ic->ic_overlay, rect_align(&ic->ic_overlay,
                                         rect_inset(inner, 10,10)));
  }
}



static void
draw_scene(sview_t *sv, int win_width, int win_height)
{
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, win_width, win_height, 0, 0, 1);

  copy_pending_cells(sv);
  upload_textures(sv);

  const rect_t r = {0, 0, win_width, win_height};
  draw_cells(sv, r);
}



static void *
sview_thread(void *aux)
{
  sview_t *sv = aux;
  Display *dpy = XOpenDisplay(NULL);
  if(dpy == NULL) {
    fprintf(stderr, "Unable to connect to X server\n");
    exit(1);
  }

  Window root = DefaultRootWindow(dpy);
  GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
  XVisualInfo *vi = glXChooseVisual(dpy, 0, att);
  if(vi == NULL) {
    fprintf(stderr, "No visual found\n");
    exit(1);
  }

  XSetWindowAttributes swa = {
    .colormap = XCreateColormap(dpy, root, vi->visual, AllocNone),
    .event_mask = ExposureMask | KeyPressMask
  };

  int win_width  = sv->sv_width;
  int win_height = sv->sv_height;

  Window win = XCreateWindow(dpy, root, 0, 0, win_width, win_height,
                             0, vi->depth, InputOutput, vi->visual,
                             CWColormap | CWEventMask, &swa);
  XMapWindow(dpy, win);
  XStoreName(dpy, win, sv->sv_title);

  GLXContext glc = glXCreateContext(dpy, vi, NULL, GL_TRUE);
  glXMakeCurrent(dpy, win, glc);

  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  while(1) {
    XWindowAttributes gwa;
    XEvent xev;

    while(XPending(dpy)) {
      XNextEvent(dpy, &xev);

      if(xev.type == Expose) {
        XGetWindowAttributes(dpy, win, &gwa);

        win_width  = gwa.width;
        win_height = gwa.height;

        glViewport(0, 0, win_width, win_height);
      } else if(xev.type == KeyPress) {

        printf("keypress\n");
#if 0
        handle_keypress(&xev);

        glXMakeCurrent(dpy, None, NULL);
        glXDestroyContext(dpy, glc);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        exit(0);
#endif
      }
    }

    draw_scene(sv, win_width, win_height);
    glXSwapBuffers(dpy, win);

  }

  return NULL;

}


sview_t *
sview_create(const char *title, int width, int height)
{
  sview_t *sv = calloc(1, sizeof(sview_t));
  sv->sv_title = strdup(title);
  sv->sv_width = width;
  sv->sv_height = height;
  TAILQ_INIT(&sv->sv_pending_cells);
  TAILQ_INIT(&sv->sv_cells);
  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&tid, &attr, sview_thread, sv);
  pthread_attr_destroy(&attr);
  return sv;
}




void
sview_put_picture(sview_t *sv, int col, int row,
                  sview_picture_t *picture,
                  const char *text)
{
  img_cell_t *ic = calloc(1, sizeof(img_cell_t));
  ic->ic_content.t_source = picture;
  if(text)
    ic->ic_overlay.t_source = text_draw_simple(640, 480, 8, text);

  ic->ic_col = col;
  ic->ic_row = row;

  pthread_mutex_lock(&sv->sv_cell_mutex);
  TAILQ_INSERT_TAIL(&sv->sv_pending_cells, ic, ic_link);
  pthread_mutex_unlock(&sv->sv_cell_mutex);
}


static void
sview_picture_default_free(sview_picture_t *sp)
{
  free(sp->planes[0]);
  free(sp);
}


sview_picture_t *
sview_picture_alloc(unsigned int width, unsigned int height,
                    sview_pixfmt_t pixfmt, int clear)
{
  sview_picture_t *sp = calloc(1, sizeof(sview_picture_t));
  sp->width = width;
  sp->height = height;
  sp->pixfmt = pixfmt;
  sp->release = sview_picture_default_free;

  int bpp;
  switch(pixfmt) {
  case SVIEW_PIXFMT_BGRA:
    bpp = 4;
    break;
  case SVIEW_PIXFMT_I:
    bpp = 1;
    break;
  default:
    free(sp);
    return NULL;
  }

  sp->strides[0] = bpp * width;
  const size_t siz = sp->strides[0] * height;
  if(clear)
    sp->planes[0] = calloc(1, siz);
  else
    sp->planes[0] = malloc(siz);
  return sp;
}



