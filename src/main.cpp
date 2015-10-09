#include <stdio.h>
#include <assert.h>
#include <unistd.h> //sleep
#include <X11/Xlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

struct State {
  Display* display;
  int screen;
  Window window;
  GC gc;
  XFontStruct* font_info;
};

void run_debugger(State* state, pid_t pid) {
  int wait_status;

  wait(&wait_status);

  unsigned int icounter = 0;
  while(WIFSTOPPED(wait_status)) {
    ++icounter;
    if(ptrace(PTRACE_SINGLESTEP, pid, 0, 0) < 0) {
      fprintf(stderr, "Error: ptrace step\n");
      return;
    }
    wait(&wait_status);
  }

  XFillRectangle(state->display, state->window, state->gc, 100, 100, 150, 150);

  char buffer[100];
  int length = snprintf(buffer, 100, "Executed: %d instructions", icounter);
  XDrawString(state->display, state->window, state->gc, 300, 300, buffer, length);

  XFlush(state->display);
  sleep(3);
  XCloseDisplay(state->display);
}


void run_debug_target(char* program_name) {
  if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
    fprintf(stderr, "Error: ptrace");
    return;
  }
  execl(program_name, program_name, (char*) 0);
}

int main (int argc, char** argv) {

  State state = {};

  if (argc < 2) {
    fprintf(stderr, "Missing program name to debug\n");
    return(-1);
  }

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
    fprintf(stderr, "XCreateGC\n");
    return -1;
  }

  const char* font_name = "fixed";
  state.font_info = XLoadQueryFont(state.display, font_name);
  assert(state.font_info);
  XSetFont(state.display, state.gc, state.font_info->fid);

  XSetForeground(state.display, state.gc, WhitePixel(state.display, state.screen));

  for(;;) {
    XEvent e;
    XNextEvent(state.display, &e);
    if (e.type == MapNotify)
      break;
  }

  pid_t pid = fork();

  if (pid == 0) {
    run_debug_target(argv[1]);
  } else if (pid > 0) {
    run_debugger(&state, pid);
  } else {
    fprintf(stderr, "Error: fork\n");
    return -1;
  }

  return(0);
}
