#include <stdio.h>
#include <sys/types.h> /* For open() */
#include <sys/stat.h>  /* For open() */
#include <fcntl.h>     /* For open() */
#include <stdlib.h>     /* For exit() */
#include <assert.h>
#include <unistd.h> //sleep, close
#include <X11/Xlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dwarf.h>
#include <libdwarf.h>


#define MAX_ERR_MSG_LEN 255

struct State {
  Display* display;
  int screen;
  Window window;
  GC gc;
  XFontStruct* font_info;
  char* program_name;
  int at_y;
  Dwarf_Debug dbg;
  Dwarf_Error err;
  int result;
};


void put_string(State* state, const char* buffer, int length) {
  XDrawString(state->display, state->window, state->gc, 10, state->at_y, buffer, length);
  state->at_y += 15;
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
  put_string(state, buffer, msg_len);
}

void print_line_info(State* state, Dwarf_Die cu_die) {
  char header[] = ".debug_line: line number info for a single cu";
  put_string(state, header, sizeof(header) - 1);
  put_string(state, "Hello2", 6);

  Dwarf_Line* line_buffer;
  Dwarf_Signed line_count;
  Dwarf_Addr line_addr;
  Dwarf_Unsigned line_number;

  state->result = dwarf_srclines(cu_die, &line_buffer, &line_count, &state->err);
  if (state->result == DW_DLV_ERROR) {
    print_error(state, "dwarf_srclines");
  } else if (state->result == DW_DLV_NO_ENTRY) {
    printf("\nNOOOOOOOOO\n");
  }
  char lines_sg[MAX_ERR_MSG_LEN]; 
  int msg_size = snprintf(lines_sg, MAX_ERR_MSG_LEN, "Got line info for %lli lines", line_count);
  put_string(state, lines_sg, msg_size);

  for (Dwarf_Signed i = 0;
       i < line_count;
       ++i) {
    Dwarf_Line line = line_buffer[i];

    char* filename = 0;
    state->result = dwarf_linesrc(line, &filename, &state->err);
    print_error(state, "dwarf_linesrc");

    state->result = dwarf_lineaddr(line, &line_addr, &state->err);
    print_error(state, "dwarf_lineaddr");

    state->result = dwarf_lineno(line, &line_number, &state->err);
    print_error(state, "dwarf_lineno");

    char line_info[MAX_ERR_MSG_LEN];
    int info_size = snprintf(line_info, MAX_ERR_MSG_LEN,  "0x%" DW_PR_XZEROS DW_PR_DUx " [%4" DW_PR_DUu "]", line_addr, line_number);
    put_string(state, line_info, info_size);

  }

  dwarf_srclines_dealloc(state->dbg, line_buffer, line_count);
}

void run_debugger(State* state, pid_t pid) {

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
  print_line_info(state, die);
#if 0
  Dwarf_Line* linebuf;
  Dwarf_Signed linecount;
  result = dwarf_srclines(die, &linebuf, &linecount, &dwarf_error);
printf("Got line info for %lli lines\n", linecount);


  for (Dwarf_Signed i = 0;
       i < linecount;
       ++i) {
    Dwarf_Line* line = linebuf + i * sizeof(Dwarf_Line);
    Dwarf_Bool has_addr = 0;
    dwarf_line_is_addr_set(*line, &has_addr, &dwarf_error);
    if (has_addr) {
      Dwarf_Addr addr = 0;
      dwarf_lineaddr(*line, &addr, &dwarf_error);
      printf("(%lli) -> 0x%llx\n", i, addr);
    }
  }

  dwarf_srclines_dealloc(state->dbg, linebuf, linecount);

  Dwarf_Attribute line_info_ref = 0;

  result = dwarf_attr(die, DW_AT_stmt_list, &line_info_ref, &dwarf_error);

  if (result != DW_DLV_OK) {
    printf("Error: No line info attr\n");
  } else {
    Dwarf_Off offset = 0;
    dwarf_global_formref(line_info_ref, &offset, &dwarf_error);
    printf("Got the attr: %llu\n", offset);
  }

  Dwarf_Addr line_info_addr = 0;
  Dwarf_Unsigned line_info_size = 0;
  result = dwarf_get_section_info_by_name(state->dbg, ".debug_line", &line_info_addr, &line_info_size, &dwarf_error);
  if (result != DW_DLV_OK) {
    printf("Error: No line section info\n");
  } else {
    printf("Line section found: %llu (%llu)\n", line_info_addr, line_info_size);
  }
#endif

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

  //XFillRectangle(state->display, state->window, state->gc, 100, 100, 150, 150);

  char buffer[100];
  int length = snprintf(buffer, 100, "Executed: %d instructions", icounter);
  XDrawString(state->display, state->window, state->gc, 300, 300, buffer, length);

  XFlush(state->display);
  sleep(5);
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

  state.at_y = 15;

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
