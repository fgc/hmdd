#include <stdio.h>
#include <string.h>    //strlen
#include <sys/types.h> /* For open() */
#include <sys/stat.h>  /* open, stat */
#include <fcntl.h>     /* For open() */
#include <stdlib.h>    /* For exit() */
#include <assert.h>
#include <unistd.h>    //sleep, close
#include <X11/Xlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <dwarf.h>
#include <libdwarf.h>


#define MAX_ERR_MSG_LEN 255
#define MAX_LINE_LENGTH 512
//TODO: better ID
#define GEN_ID __LINE__

struct Breakpoint {
  unsigned long line_number;
  unsigned int original_data;
};

struct Rectangle {
  int x;
  int y;
  int w;
  int h;
};

struct Line {
  unsigned long offset;
  unsigned long length;
  Dwarf_Addr address;
  XCharStruct extents;
};

struct UIState {
  int mouse_x;
  int mouse_y;
  int mouse_down;

  int hot_item;
  int active_item;
};

struct State {
  Display* display;
  int screen;
  Window window;
  GC gc;
  pid_t pid;
  XFontStruct* font_info;
  XColor hot_color;
  XColor active_color;
  char* program_name;
  char* source_buffer;
  Line* lines;
  unsigned long line_count;
  int code_at_y;
  int console_at_y;
  Dwarf_Debug dbg;
  Dwarf_Error err;
  int result;
  UIState ui_state;
};

inline bool in_rectangle(int x, int y, Rectangle rect) {
  bool result = (x > rect.x 
		 && y > rect.y 
		 && x < (rect.x + rect.w)
		 && y < (rect.y + rect.h));
  return(result);
}

inline bool is_hot(State* state, int id) {
  bool result = state->ui_state.hot_item == id;
  return(result);
}

inline bool is_active(State* state, int id) {
  bool result = state->ui_state.active_item == id;
  return(result);
}

inline void std_check_hot_and_active(State* state, Rectangle rect, int id) {
  if (in_rectangle(state->ui_state.mouse_x, state->ui_state.mouse_y, rect)) {
    state->ui_state.hot_item = id;
    if(state->ui_state.mouse_down) {
      state->ui_state.active_item = id;
    }
  }
}

bool button(State* state, int id, int x, int y, const char* label) {
  int direction, font_ascent, font_descent;
  XCharStruct extents;
  int length = strlen(label);
  XTextExtents(state->font_info
               , label, length
               , &font_ascent, &font_descent, &direction
               , &extents);
  Rectangle rect = {x, y, extents.width + 20, font_ascent + font_descent + 20};

  std_check_hot_and_active(state, rect, id);

  if(is_hot(state, id)) {
    XSetForeground(state->display, state->gc, state->hot_color.pixel);
  }

  XDrawRectangle(state->display, state->window, state->gc
		 , rect.x, rect.y, rect.w, rect.h);
		 
  XDrawString(state->display, state->window, state->gc
	      , x + 10, y + 20 + font_ascent
	      , label, length);
  XSetForeground(state->display, state->gc, WhitePixel(state->display, state->screen));
  XFlush(state->display);
  return(is_active(state, id));
}

void console_log(State* state, const char* buffer, int length) {
  XDrawString(state->display, state->window, state->gc, 500, state->console_at_y, buffer, length);
  state->console_at_y += 15;
  XFlush(state->display);
}

void print_error(State* state, const char * msg, bool print_ok = false) {
  int msg_len = 0;
  char buffer[MAX_ERR_MSG_LEN];
  if (state->result == DW_DLV_OK) {
    if (!print_ok) {
      return;
    }
    msg_len = snprintf(buffer, MAX_ERR_MSG_LEN, "%s: OK %s", state->program_name, msg);
  } else if (state->result == DW_DLV_ERROR) {
    char* errmsg = dwarf_errmsg(state->err);
    Dwarf_Unsigned errno = dwarf_errno(state->err);
    msg_len = snprintf(buffer, MAX_ERR_MSG_LEN, "%s ERROR %s: %s (%lu)",
                       state->program_name, msg, errmsg, (unsigned long)errno);
  } else if (state->result == DW_DLV_NO_ENTRY) {
    msg_len = snprintf(buffer, MAX_ERR_MSG_LEN, "%s NO ENTRY %s", state->program_name, msg);
  } else {
    msg_len = snprintf(buffer, MAX_ERR_MSG_LEN, "%s InternalError:  %s: code %d",
                       state->program_name, msg, state->result);
  }
  console_log(state, buffer, msg_len);
}


bool source_line(int id, State* state, unsigned long line_number) {

  Line line = state->lines[line_number];
  char* offset = state->source_buffer + line.offset;
  Dwarf_Addr address = line.address;

  char line_str[MAX_LINE_LENGTH];
  snprintf(line_str, line.length, "%s", offset);
  char complete_line[MAX_LINE_LENGTH];
  int total_length = snprintf(complete_line, MAX_LINE_LENGTH,
                                "0x%08llx : %s", address, line_str);

  int direction, font_ascent, font_descent;
  XTextExtents(state->font_info
               , complete_line, total_length
               , &direction
               , &font_ascent, &font_descent
               , &line.extents);

  
  Rectangle rect = {10, state->code_at_y - line.extents.ascent, line.extents.width, line.extents.ascent + line.extents.descent};
  
  std_check_hot_and_active(state, rect, id);

  if (is_hot(state,id)) {
    XSetForeground(state->display, state->gc, state->hot_color.pixel);
  }
  XDrawString(state->display, state->window, state->gc, 10, state->code_at_y, 
	      complete_line, total_length);
  state->code_at_y += 15;
  XFlush(state->display);
  if (is_hot(state,id)) {
    XSetForeground(state->display, state->gc, WhitePixel(state->display, state->screen));
  }

  return is_active(state, id); 
}

void get_line_info(State* state, Dwarf_Die cu_die) {
#if 0
  char header[] = ".debug_line: line number info for a single cu";
  console_log(state, header, sizeof(header) - 1);
  console_log(state, "Hello2", 6);
#endif
  Dwarf_Line* line_buffer;
  Dwarf_Signed line_count;
  Dwarf_Addr line_addr;
  Dwarf_Unsigned dwarf_line_number;

  state->result = dwarf_srclines(cu_die, &line_buffer, &line_count, &state->err);
  if (state->result == DW_DLV_ERROR) {
    print_error(state, "dwarf_srclines");
  } else if (state->result == DW_DLV_NO_ENTRY) {
    printf("\nNOOOOOOOOO\n");
  }


  char lines_sg[MAX_ERR_MSG_LEN];
  int msg_size = snprintf(lines_sg, MAX_ERR_MSG_LEN, "Got line info for %lli lines", line_count);
  console_log(state, lines_sg, msg_size);

  char* filename = 0;

  for (Dwarf_Signed i = 0;
       i < line_count;
       ++i) {

    Dwarf_Line line = line_buffer[i];

    if (filename == 0) {
      state->result = dwarf_linesrc(line, &filename, &state->err);
      print_error(state, "dwarf_linesrc");
      struct stat st;
      stat(filename, &st);
      unsigned long length = st.st_size;
      state->source_buffer = (char *) malloc(length * sizeof(char));
      // TODO: this is obviously too big
      state->lines = (Line *)malloc(length * sizeof(Line));
      int handle = open(filename, O_RDONLY);
      read(handle, state->source_buffer, length);
      unsigned long line_number = 0;
      state->lines[0].offset = 0;
      for (unsigned long i = 0;
           i < length;) {
        char firstchar = state->source_buffer[i++];
        char secondchar = state->source_buffer[i];
        if (firstchar == '\r' && secondchar == '\n') {
          //TODO: \r\n handling is untested
          state->lines[line_number].address = 0;
          state->lines[line_number].length = i - state->lines[line_number].offset - 1;
          state->lines[line_number++].offset = ++i;
          continue;
        }
        if (firstchar == '\n') {
          state->lines[line_number].address = 0;
          state->lines[line_number].length = i - state->lines[line_number].offset;
          state->lines[++line_number].offset = i;
        }
      }
      state->lines[line_number].address = 0;
      state->lines[line_number].length = length - state->lines[line_number].offset;
      state->line_count = line_number + 1;
    }

    state->result = dwarf_lineaddr(line, &line_addr, &state->err);
    print_error(state, "dwarf_lineaddr");

    state->result = dwarf_lineno(line, &dwarf_line_number, &state->err);
    print_error(state, "dwarf_lineno");

    if (state->lines[dwarf_line_number - 1].address == 0) {
      state->lines[dwarf_line_number - 1].address = line_addr;
    }

    char line_info[MAX_ERR_MSG_LEN];
    int info_size = snprintf(line_info, MAX_ERR_MSG_LEN,  "0x%" DW_PR_XZEROS DW_PR_DUx " [%4" DW_PR_DUu "]",
                             line_addr, dwarf_line_number);
    console_log(state, line_info, info_size);

  }

  dwarf_srclines_dealloc(state->dbg, line_buffer, line_count);
}

void run_debugger(State* state) {

  int handle = open(state->program_name, O_RDONLY);
  if (handle < 0) {
    fprintf(stderr, "Could not open: %s\n", state->program_name);
    exit(-1);
  }

  Dwarf_Error dwarf_error;
  Dwarf_Handler error_handler = 0;
  Dwarf_Ptr error_argument = 0;
  int init_result = dwarf_init(handle, DW_DLC_READ, error_handler, error_argument, &state->dbg, &dwarf_error);
  if (init_result != DW_DLV_OK) {
    fprintf(stderr, "Could not init DWARF\n");
    exit(-1);
  }

  Dwarf_Unsigned cu_header_length = 0;
  Dwarf_Half version_stamp = 0;
  Dwarf_Unsigned abbrev_offset = 0;
  Dwarf_Half address_size = 0;
  Dwarf_Unsigned next_cu_header = 0;

  int result = dwarf_next_cu_header(state->dbg
                                    , &cu_header_length
                                    , &version_stamp
                                    , &abbrev_offset
                                    , &address_size
                                    , &next_cu_header
                                    , &dwarf_error);

  printf("1st CU header: length(%llu), version(%d), next(%llu)\n", cu_header_length, version_stamp, next_cu_header);

  Dwarf_Die prev_die, die;
  if (dwarf_siblingof(state->dbg, NULL, &die, &dwarf_error) != DW_DLV_OK) {
    fprintf(stderr, "Can't get sibling\n");
    return;
  }
  char* name = 0;
  dwarf_diename(die, &name, &dwarf_error);
  Dwarf_Half tag = 0;
  dwarf_tag(die, &tag, &dwarf_error);
  printf("CU Die: name (%s), tag (%d)\n", name, tag);
  get_line_info(state, die);

  XEvent an_event;
  while (1) {
    XNextEvent(state->display, &an_event);
    switch (an_event.type) {
    case MotionNotify:
      state->ui_state.mouse_x = an_event.xmotion.x;
      state->ui_state.mouse_y = an_event.xmotion.y;
      break;
    case ButtonPress:
      state->ui_state.mouse_down = true;
      break;
    case ButtonRelease:
      state->ui_state.mouse_down = false;
      break;
    default:
      printf("Event\n");
      break;
    }
    state->code_at_y = 15;
    for (unsigned long i = 0; i < state->line_count; ++i) {
      if (source_line(10000 + i, state, i)) {
        char buffer[100];
        int length = snprintf(buffer, 100, "Setting breakpoint at line: %lu", i);
        console_log(state, buffer, length);
        Dwarf_Addr address = state->lines[i].address;
        unsigned int data = ptrace(PTRACE_PEEKTEXT, state->pid, (void *) address, 0);
        length = snprintf(buffer, 100, "Original data at 0x%08llx: 0x%08ux", address, data);
        console_log(state, buffer, length);
        unsigned int trapped_data = (data & 0xFFFFFF00) | 0xCC;
        ptrace(PTRACE_POKETEXT, state->pid, (void *) address, (void *)trapped_data); 
      }
    }
    
    if (button(state, GEN_ID, 350, 0, "Quit")) {
      break;
    }
    
    if (button(state, GEN_ID, 350, 40, "Run")) {
      ptrace(PTRACE_CONT, state->pid, 0, 0);
      int wait_status;
      wait(&wait_status);
      user_regs_struct regs;
      ptrace(PTRACE_GETREGS, state->pid, 0, &regs);
      char buffer[100];
      int length = snprintf(buffer, 100, "Child stopped at EIP = 0x%08lx\n", regs.eip);
      console_log(state, buffer, length);
    }

    state->ui_state.hot_item = 0;
    state->ui_state.active_item = 0;
  }

#if 0
  int wait_status;

  wait(&wait_status);

  unsigned int icounter = 0;
  while(WIFSTOPPED(wait_status)) {
    ++icounter;
    if(ptrace(PTRACE_SINGLESTEP, state->pid, 0, 0) < 0) {
      fprintf(stderr, "Error: ptrace step\n");
      return;
    }
    wait(&wait_status);
  }

  //XFillRectangle(state->display, state->window, state->gc, 100, 100, 150, 150);

  char buffer[100];
  int length = snprintf(buffer, 100, "Executed: %d instructions", icounter);
  console_log(state, buffer, length);
  XFlush(state->display);
  sleep(100);
#endif

  XCloseDisplay(state->display);
  init_result = dwarf_finish(state->dbg, &dwarf_error);
  if (init_result != DW_DLV_OK) {
    fprintf(stderr, "Could not finish DWARF\n");
    exit(-1);
  }
  close(handle);
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

  state.code_at_y = 15;
  state.console_at_y = 15;

  if (argc < 2) {
    fprintf(stderr, "Missing program name to debug\n");
    return(-1);
  }

  state.program_name = argv[1];

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

  XSelectInput(state.display, state.window
               , StructureNotifyMask
               | ButtonPressMask
               | ButtonReleaseMask
               | PointerMotionMask);

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

  Colormap colormap = DefaultColormap(state.display, 0);
  const char* yellow="#FFFF00";
  XParseColor(state.display, colormap, yellow, &state.hot_color);
  XAllocColor(state.display, colormap, &state.hot_color);
  const char* green="#00FF00";
  XParseColor(state.display, colormap, green, &state.active_color);
  XAllocColor(state.display, colormap, &state.active_color);

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
    state.pid = pid;
    run_debugger(&state);
  } else {
    fprintf(stderr, "Error: fork\n");
    return -1;
  }

  return(0);
}
