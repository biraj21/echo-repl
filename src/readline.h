#ifndef TERM_H
#define TERM_H

#include <stdbool.h>
#include <stddef.h>

/**
 * the result of calling `rl_read_line` function.
 */
enum ReadLineResult {
  // line was read successfully
  RL_SUCCESS,

  // user immediately pressed Ctrl+D (EOF)
  RL_EOF,

  // user pressed Ctrl+C (SIGINT)
  RL_SIGINT,
};

/**
 * reads a line from the terminal.
 *
 * @param buf the buffer to store the line read from the terminal
 * @param buf_size the size of the buffer
 * @param prompt the prompt to display before reading the line
 *
 * @return `RL_SUCCESS` if the line was read successfully, `RL_EOF` if the user
 * immediately pressed Ctrl+D (EOF), `RL_SIGINT` if the user pressed Ctrl+C
 * (SIGINT)
 */
enum ReadLineResult rl_read_line(char *buf, size_t buf_size, char *prompt);

/**
 * performs cleanup tasks. this function MUST be called if you've called the
 * `rl_read_line` function at least once. just register this function to be
 * called at exit.
 */
void rl_cleanup(void);

#endif