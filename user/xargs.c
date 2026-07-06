#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXLINE 512

// xargs: read whitespace/newline-separated lines from standard input and,
// for each line, run `argv[1] argv[2..] <line-args>`.
int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: xargs <command> [args...]\n");
    exit(1);
  }

  char *cmd = argv[1];
  char line[MAXLINE];
  char *args[MAXARG];
  int base = 0;

  // Fixed prefix: the command plus any arguments supplied on the xargs
  // command line itself.
  for(int i = 1; i < argc && base < MAXARG - 1; i++)
    args[base++] = argv[i];

  int npos = 0;   // length of the current line buffer
  char c;
  int n;

  // Read stdin one byte at a time, splitting on newlines. Each complete
  // line is appended as a single extra argument to the command.
  while((n = read(0, &c, 1)) > 0){
    if(c == '\n'){
      line[npos] = 0;

      // Build the argv for this invocation: prefix + the line.
      char *v[MAXARG];
      int k = 0;
      for(int i = 0; i < base; i++)
        v[k++] = args[i];
      v[k++] = line;
      v[k] = 0;

      int pid = fork();
      if(pid < 0){
        fprintf(2, "xargs: fork failed\n");
        exit(1);
      }
      if(pid == 0){
        exec(cmd, v);
        fprintf(2, "xargs: exec %s failed\n", cmd);
        exit(1);
      }
      wait(0);

      npos = 0;
    } else {
      if(npos < MAXLINE - 1)
        line[npos++] = c;
    }
  }

  // Handle a trailing line with no final newline.
  if(npos > 0){
    line[npos] = 0;
    char *v[MAXARG];
    int k = 0;
    for(int i = 0; i < base; i++)
      v[k++] = args[i];
    v[k++] = line;
    v[k] = 0;

    int pid = fork();
    if(pid < 0){
      fprintf(2, "xargs: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec(cmd, v);
      fprintf(2, "xargs: exec %s failed\n", cmd);
      exit(1);
    }
    wait(0);
  }

  exit(0);
}
