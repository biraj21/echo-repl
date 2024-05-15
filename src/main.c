#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "readline.h"

#define REPL_INPUT_BUFFER_SIZE 1024

int main(void) {
  puts("welcome to Biraj's echo repl\n"
       "- press arrow UP/DOWN to navigate in history\n"
       "- type 'exit' or press Ctrl+C to exit\n");

  atexit(rl_cleanup);

  char input_line[REPL_INPUT_BUFFER_SIZE];
  while (true) {
    enum ReadLineResult r =
        rl_read_line(input_line, REPL_INPUT_BUFFER_SIZE, "> ");

    if (r == RL_SIGINT) {
      puts("\npressed Ctrl+C (SIGINT), exiting...");
      break;
    }

    if (r == RL_EOF) {
      puts("\npressed Ctrl+D (EOF), exiting...");
      break;
    }

    if (strcmp(input_line, "exit") == 0) {
      break;
    }

    printf("you said: %s\n", input_line);
  }

  return 0;
}
