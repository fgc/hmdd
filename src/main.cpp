#include <stdio.h>
#include <assert.h>
#include <unistd.h> //sleep
#include <X11/Xlib.h>

struct State {
  Display* display;
  int screen;
  Window window;
  GC gc;
  XFontStruct* font_info;
};

int main (int argc, char** argv) {

  State state = {};

  state.display = XOpenDisplay(":0");

  if(!state.display) {
    fprintf(stderr, "Cannot open display :0\n");
    return(-1);
  }

  state.screen = DefaultScreen(state.display);


  state.window = XCreateSimpleWindow(state.display
                                     , RootWindow(state.display, state.screen)
                                     , 0, 0
                                     , 1024, 768
                                     , 0
                                     , BlackPixel(state.display, state.screen)
                                     , BlackPixel(state.display, state.screen)
                                     );

  XSelectInput(state.display, state.window, StructureNotifyMask);

  XMapWindow(state.display, state.window);

  XGCValues values;
  values.cap_style = CapButt;
  values.join_style = JoinBevel;
  unsigned long valuemask = GCCapStyle | GCJoinStyle;
  state.gc = XCreateGC(state.display
                       , state.window
                       , valuemask, &values);
  if (state.gc < 0) {
    fprintf(stderr, "XCreateGC: %d\n", (int)state.gc);
    return -1;
  }


  XSetForeground(state.display, state.gc, WhitePixel(state.display, state.screen));

  for(;;) {
    XEvent e;
    XNextEvent(state.display, &e);
    if (e.type == MapNotify)
      break;
  }

  XFillRectangle(state.display, state.window, state.gc, 100, 100, 150, 150);


  const char* font_name = "fixed";
  state.font_info = XLoadQueryFont(state.display, font_name);
  assert(state.font_info);
  XSetFont(state.display, state.gc, state.font_info->fid);

  XDrawString(state.display, state.window, state.gc, 300, 300, "Hello", 5);

  XFlush(state.display);
  sleep(3);
  XCloseDisplay(state.display);
  return(0);
}
