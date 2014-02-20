/*
** mirb-hostbased - Hostbased Embeddable Interactive mruby Shell
**
** This program takes code from the user in
** an interactive way and executes it
** immediately. It's a REPL...
*/

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>

#include <mruby.h>
#include "mruby/array.h"
#include <mruby/proc.h>
#include <mruby/data.h>
#include <mruby/compile.h>
#include "mruby/dump.h"
#ifdef ENABLE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include <mruby/string.h>

//DEBUG PRINT
//http://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html
#define DPRINTF(...) if (args.verbose) printf(__VA_ARGS__)

#ifdef ENABLE_READLINE
static const char *history_file = ".mirb-hostbased_history";
#endif
char history_path[1024];

int g_continue_view;

/* Guess if the user might want to enter more
 * or if he wants an evaluation of his code now */
int
is_code_block_open(struct mrb_parser_state *parser)
{
  int code_block_open = FALSE;

  /* check for heredoc */
  if (parser->parsing_heredoc != NULL) return TRUE;
  if (parser->heredoc_end_now) {
    parser->heredoc_end_now = FALSE;
    return FALSE;
  }

  /* check if parser error are available */
  if (0 < parser->nerr) {
    const char *unexpected_end = "syntax error, unexpected $end";
    const char *message = parser->error_buffer[0].message;

    /* a parser error occur, we have to check if */
    /* we need to read one more line or if there is */
    /* a different issue which we have to show to */
    /* the user */

    if (strncmp(message, unexpected_end, strlen(unexpected_end)) == 0) {
      code_block_open = TRUE;
    }
    else if (strcmp(message, "syntax error, unexpected keyword_end") == 0) {
      code_block_open = FALSE;
    }
    else if (strcmp(message, "syntax error, unexpected tREGEXP_BEG") == 0) {
      code_block_open = FALSE;
    }
    return code_block_open;
  }

  /* check for unterminated string */
  if (parser->lex_strterm) return TRUE;

  switch (parser->lstate) {

  /* all states which need more code */

  case EXPR_BEG:
    /* an expression was just started, */
    /* we can't end it like this */
    code_block_open = TRUE;
    break;
  case EXPR_DOT:
    /* a message dot was the last token, */
    /* there has to come more */
    code_block_open = TRUE;
    break;
  case EXPR_CLASS:
    /* a class keyword is not enough! */
    /* we need also a name of the class */
    code_block_open = TRUE;
    break;
  case EXPR_FNAME:
    /* a method name is necessary */
    code_block_open = TRUE;
    break;
  case EXPR_VALUE:
    /* if, elsif, etc. without condition */
    code_block_open = TRUE;
    break;

  /* now all the states which are closed */

  case EXPR_ARG:
    /* an argument is the last token */
    code_block_open = FALSE;
    break;

  /* all states which are unsure */

  case EXPR_CMDARG:
    break;
  case EXPR_END:
    /* an expression was ended */
    break;
  case EXPR_ENDARG:
    /* closing parenthese */
    break;
  case EXPR_ENDFN:
    /* definition end */
    break;
  case EXPR_MID:
    /* jump keyword like break, return, ... */
    break;
  case EXPR_MAX_STATE:
    /* don't know what to do with this token */
    break;
  default:
    /* this state is unexpected! */
    break;
  }

  return code_block_open;
}

void mrb_show_version(mrb_state *);
void mrb_show_copyright(mrb_state *);

struct _args {
  mrb_bool verbose      : 1;
  int argc;
  char** argv;
  const char *port;
  int noreset;
};

static void
usage(const char *name)
{
  static const char *const usage_msg[] = {
  "switches:",
  "-v           print version number, then run in verbose mode",
  "--noreset    continue without wait HELLO. Local variables will not be shared",
  "--verbose    run in verbose mode",
  "--version    print the version",
  "--copyright  print the copyright",
  NULL
  };
  const char *const *p = usage_msg;

  printf("Usage: %s [switches] -p <port>\n", name);
  while (*p)
    printf("  %s\n", *p++);
}

static int
parse_args(mrb_state *mrb, int argc, char **argv, struct _args *args)
{
  static const struct _args args_zero = { 0 };

  *args = args_zero;

  for (argc--,argv++; argc > 0; argc--,argv++) {
    char *item;
    if (argv[0][0] != '-') break;

    item = argv[0] + 1;
    switch (*item++) {
    case 'v':
      if (!args->verbose) mrb_show_version(mrb);
      args->verbose = 1;
      break;
    case 'p':
      args->port = argv[1];
      break;
    case '-':
      if (strcmp((*argv) + 2, "version") == 0) {
        mrb_show_version(mrb);
        exit(EXIT_SUCCESS);
      }else if (strcmp((*argv)+2, "noreset") == 0){
        args->noreset = 1;
        break;
      }
      else if (strcmp((*argv) + 2, "verbose") == 0) {
        args->verbose = 1;
        break;
      }/*
      else if (strcmp((*argv) + 2, "copyright") == 0) {
        mrb_show_copyright(mrb);
        exit(EXIT_SUCCESS);
      }*/
    default:
      return EXIT_FAILURE;
    }
  }
  if (!args->port){
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static void
cleanup(mrb_state *mrb, struct _args *args)
{
  mrb_close(mrb);
}

/* Print a short remark for the user */
static void
print_hint(struct _args *args)
{
  printf("mirb-hostbased - Hostbased Interactive mruby Shell\n");
}

/* Print the command line prompt of the REPL */
void
print_cmdline(int code_block_open)
{
  if (code_block_open) {
    printf("*    ");
  }
  else {
    printf("> ");
  }
}

ssize_t
read_target(int fd, char *buffer, int timeout){

  // non-blocking 1 byte read with timeout
  // timeout in roughly 10 mSec ticks (0 == infinite)
  ssize_t result = 0;
  while(0==result){
    result = read(fd, buffer, 1);
    if(0>=result){
      // normal with O_NONBLOCK
      result = 0;
      if ( 0 != timeout ){
        if ( 0 >= --timeout ) break;
      }
      usleep(10000);
    }
  }
  return result;
}

void
read_flush(int fd){

  // flush all serial input
  char c;
  usleep(1000);
  while( 0 < read(fd, &c, 1) )
    ;
}
int reconnect(const char *port, int *fd);

int
wait_hello(const char *port , int *fd){

  // use ENQ/ACK polling to sync with the target
  const char ACK = 0x06;
  char c=0;
  const char ENQ = 0x05;
  int retry = 100;

  int send_enq = 1;

  while(0<retry--){
    c = ENQ;
    int ret = 0;
    if (send_enq){
      ret = write(*fd, &c, 1);
      
      //special handling for chipKIT Max32
      //we dont send ENQ for chipKIT Max32 because bootloader enter update mode once some data received..
      if ((ret == -1) || (errno == EAGAIN || errno == EWOULDBLOCK)){
        printf("  chipKIT detected. reopening port..\n");
        reconnect(port , fd);     //force board to reset
        send_enq = 0;             //don't send ENQ anymore.
      }
    }
    while ( 1 == read_target(*fd, &c, 20) ) {
      if (c == ACK) {
        break;
      }else{
        putc(c, stdout);
      }
    }
    if (c == ACK) break;
  }
  if (c != ACK){
    printf("sync error\n");
    return -1;
  }
  
  return 0;
}

int read_result(int fd, char *result_str, int *is_exeption){

  const char SOH = 0x01;          //Header for normal result
  const char SOH_EXCEPTION = 0x02;//Header for exception
  ssize_t read_size;
  char c;
  while(1){    
    read_size = read_target(fd, &c, 0);
    if (read_size != 1) goto read_error;
    if (c == SOH || c == SOH_EXCEPTION) break;

    //normal output from target
    putc(c, stdout);
  }

  *is_exeption = (c == SOH)? 0 : 1;

  unsigned char len_h;
  unsigned char len_l;
  read_size = read_target(fd, (char *)&len_h, 20);
  if (read_size != 1) goto read_error;
  read_size = read_target(fd, (char *)&len_l, 20);
  if (read_size != 1) goto read_error;

  char ack = '!';
  ssize_t written_size = write(fd, &ack, 1);
  if (written_size != 1) goto write_error;

  unsigned short len_to_read = ((unsigned short)len_h << 8) | len_l;

  unsigned short len_readed = 0;
  int i;
  while(len_readed < len_to_read){
    for (i = 0 ; i < 100; i++){
      read_size = read_target(fd, result_str+len_readed, 20);
      if (read_size != 1) goto read_error;
      len_readed++;
      if (len_readed == len_to_read){
        break;
      }
    }
    //send ack;
    ack = '#';
    written_size = write(fd, &ack, 1);
    if (written_size != 1) goto write_error;

  }

  return 0;

read_error:
  perror("read error\n");
  return -1;
write_error:
  perror("write error\n");
  return -1;
}

int write_bytecode(int fd, const void *buffer, int len, int verbose){

  ssize_t read_size;
  ssize_t written_size;

  unsigned char header[3];
  header[0] = verbose ? 0x02 : 0x01; //1:SOH 2:SOH with verbose
  header[1] = (unsigned char)(len >> 8);
  header[2] = (unsigned char)(len & 0xFF);

  char ack='?';
  int retry = 5;
  while ((ack != '!') && (0 < retry--)){
    read_flush(fd);
    (void)write(fd, header, 3);
    (void)read_target(fd, &ack, 20);
  }
  if ( '!' != ack ){
    printf("protocol error(first ack:%c)\n",ack);
    return -1;
  }

  unsigned short len_written = 0;
  while(len_written < len){
    int i=0;
    while(i < 100){
      written_size = write(fd, buffer + len_written,1);
      if((-1==written_size) && (EAGAIN==errno)) continue;// no i++
      else if (written_size != 1) {
        perror("write error\n");
        return -1;
      }
      i++;
      len_written++;
      if (len_written == len){
        break;
      }
    }
    ack = '?';
    read_size = read_target(fd, &ack, 20);
    if ( (read_size != 1) || ack != '#'){
      printf("protocol error(normal ack:%c)\n", ack);
      return -1;
    }
  }

  //OK all data sent.
  return 0;

}

int 
reconnect(const char *port, int *fd)
{
  /*ok here open serial port*/
  int fd_port = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_port < 0){
    printf("failed to open port %s\n",port);
    perror("error:");
    return -1;
  }
  //http://irobot.csse.muroran-it.ac.jp/html-corner/robotMaker/elements/outlineSerialCommProgramming/
  struct termios oldtio, newtio;
  tcgetattr(fd_port, &oldtio);
  newtio = oldtio;
  cfsetspeed(&newtio, B9600);
  tcflush(fd_port, TCIFLUSH);
  tcsetattr(fd_port,TCSANOW, &newtio);

  *fd = fd_port;
  return 0;
}

void
sigint_handler(int sig)
{
  g_continue_view = 0;
}

int
main(int argc, char **argv)
{
  char ruby_code[1024] = { 0 };
  char last_code_line[1024] = { 0 };
#ifndef ENABLE_READLINE
  int last_char;
  int char_index;
#endif
  mrbc_context *cxt;
  struct mrb_parser_state *parser;
  mrb_state *mrb;
  // mrb_value result;
  struct _args args;
  int n;
  struct RProc *proc;
  int code_block_open = FALSE;
  int ai;

  /* new interpreter instance */
  mrb = mrb_open();
  if (mrb == NULL) {
    fputs("Invalid mrb interpreter, exiting mirb\n", stderr);
    return EXIT_FAILURE;
  }
  mrb_define_global_const(mrb, "ARGV", mrb_ary_new_capa(mrb, 0));

  n = parse_args(mrb, argc, argv, &args);
  if (n == EXIT_FAILURE) {
    cleanup(mrb, &args);
    usage(argv[0]);
    return n;
  }

  print_hint(&args);

  /*ok here open serial port*/
  int fd_port = 0;
  if (0 != reconnect(args.port, &fd_port)){
    cleanup(mrb, &args);
    return EXIT_FAILURE;
  }

  if (!args.noreset){
    printf("  waiting for target on %s...\n", args.port);
    fflush(stdout);
    int ret = wait_hello(args.port, &fd_port);
    if (ret != 0){
      printf("\nfailed to open communication with target.\n");
      cleanup(mrb, &args);
      return EXIT_FAILURE;
    }
  }else{
    printf("continue without reset. Note:local variables are not restored.\n");
  }
  printf("target is ready.\n");

  cxt = mrbc_context_new(mrb);
  cxt->capture_errors = 1;
  if (args.verbose) cxt->dump_result = 1;

  ai = mrb_gc_arena_save(mrb);

#ifdef ENABLE_READLINE
  using_history();
  strcpy(history_path, getenv("HOME"));
  strcat(history_path, "/");
  strcat(history_path,history_file);
  read_history(history_path);
#endif

  while (TRUE) {
#ifndef ENABLE_READLINE
    print_cmdline(code_block_open);

    char_index = 0;
    while ((last_char = getchar()) != '\n') {
      if (last_char == EOF) break;
      last_code_line[char_index++] = last_char;
    }
    if (last_char == EOF) {
      fputs("\n", stdout);
      break;
    }

    last_code_line[char_index] = '\0';
#else
    char* line = readline(code_block_open ? "* " : "> ");
    if (line == NULL) {
      printf("\n");
      break;
    }
    strncpy(last_code_line, line, sizeof(last_code_line)-1);
    add_history(line);

    free(line);
#endif

    if ((strcmp(last_code_line, "quit") == 0) || (strcmp(last_code_line, "exit") == 0)) {
      if (!code_block_open) {
        break;
      }
      else{
        /* count the quit/exit commands as strings if in a quote block */
        strcat(ruby_code, "\n");
        strcat(ruby_code, last_code_line);
      }
    }else if ((strncmp(last_code_line,"#file",strlen("#file")) == 0) || 
              (strncmp(last_code_line,"#load",strlen("#load")) == 0)){
      if (!code_block_open) {
        char *filename = last_code_line + strlen("#file");

        //strip space
        while(filename[0] == ' ' || filename[0] == '\t' || filename[0] == '"'){
          filename++;
        }
        while(filename[strlen(filename)-1] == ' ' || filename[strlen(filename)-1] == '\t' ||
              filename[strlen(filename)-1] == '"'){
          filename[strlen(filename)-1] = '\0';
        }

        FILE *f = fopen(filename, "r");
        if (!f){
          printf("cannot open file:%s\n",filename);
          continue;
        }
        char line[1024];
        while(fgets(line, 1024, f) != NULL){
          char c = line[0];
          int is_comment_line = 0;
          //
          while(TRUE){
            if(c == '#') {
              is_comment_line = 1;
              break;
            }else if (c == '\n') {
              break;
            }else if (c == ' ' || c == '\t'){
              c++;
              continue;
            }else{
              break;
            }
          }
          if (!is_comment_line)
            strcat(ruby_code, line);
        }
        fclose(f);
        
        //remove '\n' or spaces from last line to prevent code_block_open
        while(TRUE){
          char last_char = ruby_code[strlen(ruby_code)-1];
          if (last_char == '\n' || last_char == ' ' || last_char == '\t'){
            ruby_code[strlen(ruby_code)-1] = '\0';
            continue;
          }
          break;
        }
      }else{
        /* count the #file/#load commands as strings if in a quote block */
        strcat(ruby_code, "\n");
        strcat(ruby_code, last_code_line);
      }
    }else if (strncmp(last_code_line,"#reconnect",strlen("#reconnect")) == 0){
      if (!code_block_open){
        close(fd_port);
        printf("reconnecting to %s...", args.port);
        if(0 != reconnect(args.port, &fd_port)) {
          printf("\nfailed. Check connectivity.\n");
          continue;
        }else{
          printf("\n");
          continue;
        }
      }
      else{
        strcat(ruby_code, "\n");
        strcat(ruby_code, last_code_line);
      }
    }else if (strncmp(last_code_line, "#view", strlen("#view")) == 0){
      if (!code_block_open){
        //view mode
        printf("...Entering view mode.. press Ctrl-C to back to REPL...\n");
        g_continue_view = 1;

        if (SIG_ERR == signal(SIGINT, sigint_handler)){
          printf("failed to set signal handler");
          continue;
        }

        while(g_continue_view){
          char c;
          ssize_t ret = read(fd_port, &c, 1);
          if (ret == 1){
            putc(c,stdout);
            continue;
          }else if (ret > 0){
            printf("ret = %zd\n", ret);
          }else{    //need strick check. currently assume EAGAIN or EWOULDBLOCK
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)){
              printf("oops, something bad happen");
              continue;
            }
          }
        }
        printf("\n...get back to REPL\n");
        signal(SIGINT, SIG_DFL);  //restore default handler

        ruby_code[0] = '\0';
        last_code_line[0] = '\0';
        continue;

      }else{
        strcat(ruby_code, "\n");
        strcat(ruby_code, last_code_line);
      }
    }
    else {
      if (code_block_open) {
        strcat(ruby_code, "\n");
        strcat(ruby_code, last_code_line);
      }
      else {
        strcpy(ruby_code, last_code_line);
      }
    }

    /* parse code */
    parser = mrb_parser_new(mrb);
    parser->s = ruby_code;
    parser->send = ruby_code + strlen(ruby_code);
    parser->lineno = 1;
    mrb_parser_parse(parser, cxt);
    code_block_open = is_code_block_open(parser);

    if (code_block_open) {
      /* no evaluation of code */
    }
    else {
      if (0 < parser->nerr) {
        /* syntax error */
        printf("line %d: %s\n", parser->error_buffer[0].lineno, parser->error_buffer[0].message);
      }
      else {
        /* generate bytecode */
        DPRINTF("(host:)generating bytecode...\n");
        proc = mrb_generate_code(mrb, parser);
        DPRINTF("(host:)generating bytecode...done.\n");

        char mrbpath[1024];
        strcpy(mrbpath,getenv("HOME"));
        strcat(mrbpath,"/.mirb-hostbased.mrb");
        FILE *f = fopen( mrbpath ,"w+b");
        if (!f){
          perror("failed to dump bytecode(file open error).");
        }

        /* dump bytecode to file */
        DPRINTF("(host:)dumping bytecode to temp file...\n");
        int ret = mrb_dump_irep_binary(mrb, proc->body.irep, 0, f);
        if (ret != MRB_DUMP_OK){
          printf("failed to dump bytecode. err = %d\n", ret);
        }
        DPRINTF("(host:)dumping bytecode to temp file...done.\n");

        /* read dumped bytecode to buffer */
        unsigned char bytecode[2048];
        fseek(f, 0, SEEK_SET);
        size_t bytecode_size = fread(bytecode, 1, 2048,f);
        if (ferror(f)){
          perror("file read error.");
        }
        fclose(f);
        
        DPRINTF("(host:)bytecode size = %zd\n", bytecode_size);

        /* send to target */
        DPRINTF("(host:)sending bytecode to target...\n");
        ret = write_bytecode(fd_port, bytecode, bytecode_size, args.verbose);
        if (ret != 0){
          printf("failed to send bytecode.\n");
          ruby_code[0] = '\0';
          last_code_line[0] = '\0';
          mrb_gc_arena_restore(mrb, ai);
          printf("type #reconnect to reconnect to target without reset.\n");
          continue;
        }
        
        DPRINTF("(host:)sending bytecode to target...done.\n");

        /* receive result from target */
        DPRINTF("(host:)receiving result from target...\n");
        char result[4048];
        int is_exception = 0;
        ret = read_result(fd_port, result, &is_exception);
        if (ret != 0){
          printf("failed to get result.\n");
          ruby_code[0] = '\0';
          last_code_line[0] = '\0';
          mrb_gc_arena_restore(mrb, ai);
          printf("type #reconnect to reconnect to target without reset.\n");
          continue;
        }
        DPRINTF("(host:)receiving result from target...done. len=%zd\n",strlen(result));
        if (is_exception){
          printf("   %s\n",result);
        }else{
          printf(" => %s\n",result);
        }
      }
      ruby_code[0] = '\0';
      last_code_line[0] = '\0';
      mrb_gc_arena_restore(mrb, ai);
    }
    mrb_parser_free(parser);
  }
  mrbc_context_free(mrb, cxt);
  mrb_close(mrb);

#ifdef ENABLE_READLINE
  write_history(history_path);
#endif
  
  return 0;
}
