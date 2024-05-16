#include <assert.h>  // for assert()
#include <ctype.h>   // for isprint()
#include <errno.h>   // for errno
#include <stdbool.h> // for bool, duh
#include <stdio.h>   // for fputs(), putchar(), perror()
#include <stdlib.h>  // for exit(), EXIT_FAILURE
#include <string.h>  // for strlen(), memmove()
#include <termios.h> // for struct termios, tcgetattr(), tcsetattr()
#include <unistd.h>  // for STDIN_FILENO, STDOUT_FILENO, read(), write()

#include "readline.h" // for enum ReadLineResult
#include "vector.h"   // for struct Vector & related functions

/*
 * This macro is used to check if the key pressed is Ctrl+<alphabet>
 *
 * Ctrl key combined with the alphabetic keys maps to 1–26.
 *
 * ASCII of 'b' is 98, which is 01100010 in binary.
 * 0x1f is 00011111 in binary.
 * 00011111 & 01100010 = 00000010 = 2, which is Ctrl+b.
 *
 * ASCII of 'B' is 66, which is 01000010 in binary.
 * 00011111 & 01000010 = 00000010 = 2,  which is Ctrl+B.
 *
 * So it works regardless of the case.
 *
 * This mirrors what the Ctrl key does in the terminal: it strips bits 5 and 6
 * from whatever key you press in combination with Ctrl, and sends that.
 * (By convention, bit numbering starts from 0.) The ASCII character set seems
 * to be designed this way on purpose. (It is also similarly designed so that
 * you can set and clear bit 5 to switch between lowercase and uppercase.)
 */
#define CTRL_KEY(k) (k & 0x1f)

enum TermKey {
  KEY_ENTER = 13, // '\r',
  KEY_ESC = 27,   // '\x1b',
  KEY_BACKSPACE = 127,
  KEY_ARROW_UP = 1000,
  KEY_ARROW_DOWN,
  KEY_ARROW_RIGHT,
  KEY_ARROW_LEFT,
  KEY_DELETE,
  KEY_HOME,
  KEY_END,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
};

static void enable_raw_mode(void);
static void disable_raw_mode(void);

static bool add_to_history(const char *line, size_t line_len);
static int read_key(void);

static bool get_cursor_position(unsigned short *row, unsigned short *col);
static bool move_cursor_left(void);
static bool move_cursor_right(void);
static bool move_cursor_to(unsigned short row, unsigned short col);

static bool repaint_line(unsigned short orig_cy, unsigned short orig_cx,
                         const char *line, size_t len);

static void die(const char *msg);

// original settings of the terminal
static struct termios original_state;

// whether raw mode is enabled or not
static bool raw_mode_enabled = false;

// to store the history of inputs (each element is a `char *`)
static struct Vector *history = NULL;
static size_t history_index = 0;

enum ReadLineResult rl_read_line(char *buf, size_t buf_size, char *prompt) {
  assert(buf != NULL);
  assert(buf_size > 0);

  // print the prompt if provided
  if (prompt != NULL) {
    size_t prompt_len = strlen(prompt);
    if (write(STDOUT_FILENO, prompt, prompt_len) != prompt_len) {
      die("failed to write to terminal (prompt)");
    }
  }

  // add the current buffer as a node in the history
  if (!add_to_history(buf, buf_size)) {
    die("failed to add line to history");
  }

  size_t history_len = vector_length(history);

  // set the history index to the latest element
  history_index = history_len - 1;

  // get the current buffer from the history
  char *current_buf = *((char **)vector_get(history, history_index));

  // clear the buffer
  current_buf[0] = '\0';

  // enable raw mode for the terminal
  enable_raw_mode();

  // get current cursor position
  unsigned short cx, cy;
  if (!get_cursor_position(&cy, &cx)) {
    die("failed to get cursor position");
  }

  int cursor_pos = 0; // the x offset of the cursor from the original position

  // handle each key press
  size_t num_chars = 0;
  while (num_chars < buf_size - 1) {
    int key = read_key();

    // handle printable characters, i.e., the actual characters that user types
    if (isprint(key)) {
      // write the typed character to the terminal (won't happen automatically
      // in raw mode)
      if (write(STDOUT_FILENO, &key, 1) != 1) {
        die("failed to write to terminal (key press)");
      }

      // move characters after the cursor to the right by one if required
      if (cursor_pos < num_chars) {
        memmove(&current_buf[cursor_pos + 1], &current_buf[cursor_pos],
                num_chars - cursor_pos + 1);

        // repaint the line after the cursor
        size_t len = num_chars - cursor_pos;
        if (write(STDOUT_FILENO, &current_buf[cursor_pos + 1], len) != len) {
          die("failed to write to terminal (key press, repaint)");
        }

        // move the cursor back to the original position plus one because
        // cursor will be moved because of the write() above
        if (!move_cursor_to(cy, cx + cursor_pos + 1)) {
          die("failed to move cursor (key press)");
        }
      }

      // insert the character at the cursor position
      current_buf[cursor_pos] = key;

      // move the cursor to the right
      ++cursor_pos;

      // move to the next character
      ++num_chars;

      // null-terminate the buffer
      current_buf[num_chars] = '\0';

      continue;
    }

    // handle other keys
    switch (key) {
    case KEY_ENTER:
      // if hit enter, then get out of the loop
      if (write(STDOUT_FILENO, "\r\n", 2) != 2) {
        die("failed to write to terminal (key press, enter)");
      }

      goto end_of_loop;
      break;

    // handle Ctrl+C (SIGINT)
    case CTRL_KEY('c'):
      disable_raw_mode();
      return RL_SIGINT;
      break;

    // handle Ctrl+D (EOF)
    case CTRL_KEY('d'):
      // if the buffer is empty, then return EOF
      if (num_chars == 0) {
        disable_raw_mode();
        return RL_EOF;
      }

      goto end_of_loop;
      break;

    // handle BACKSPACE key
    case KEY_BACKSPACE:
      // if the cursor is at the beginning of the line, do nothing
      if (cursor_pos == 0) {
        continue;
      }

      // move the cursor to the left
      --cursor_pos;

      // move characters after the cursor to the left by one, essentially
      // deleting the character
      memmove(&current_buf[cursor_pos], &current_buf[cursor_pos + 1],
              num_chars - cursor_pos);

      // reduce the length of current buffer
      --num_chars;

      // null-terminate the buffer
      current_buf[num_chars] = '\0';

      // repaint the line
      if (!repaint_line(cy, cx, current_buf, num_chars)) {
        die("failed to repaint line (BACKSPACE)");
      }
      break;

    // handle arrow up & down to navigate through history
    case KEY_ARROW_UP:
    case KEY_ARROW_DOWN:
      // validate the history index before moving
      if ((key == KEY_ARROW_UP && history_index == 0) ||
          (key == KEY_ARROW_DOWN && history_index == history_len - 1)) {
        continue;
      }

      // go backward if arrow up, else go forward
      history_index += key == KEY_ARROW_UP ? -1 : 1;

      // go forward in history get the data
      current_buf = *((char **)vector_get(history, history_index));

      size_t current_buf_len = strlen(current_buf);

      // move cursor back to the original position
      if (!repaint_line(cy, cx, current_buf, current_buf_len)) {
        die("failed to repaint line (ARROW_UP/DOWN)");
      }

      // cursor position is at the end of the line now
      cursor_pos = current_buf_len;
      num_chars = current_buf_len;
      break;

    // backward / arrow left
    case CTRL_KEY('b'):
    case KEY_ARROW_LEFT:
      // if the cursor is at the beginning of the line, do nothing
      if (cursor_pos == 0) {
        continue;
      }

      // move the cursor to the left
      move_cursor_left();
      --cursor_pos;
      break;

    // forward / arrow right
    case CTRL_KEY('f'):
    case KEY_ARROW_RIGHT:
      // if the cursor is at the end of the line, do nothing
      if (cursor_pos == num_chars) {
        continue;
      }

      // move the cursor to the right
      move_cursor_right();
      ++cursor_pos;
      break;

    default:
      // just ignore other keys
      break;
    }
  }

end_of_loop:
  current_buf[num_chars] = '\0';

  // remember that i is the length of current buffer
  size_t num_chars_to_copy = num_chars < buf_size ? num_chars + 1 : buf_size;

  // copy the current buffer to the buffer arg
  memcpy(buf, current_buf, num_chars_to_copy);

  // if we're not at the end of the history, then copy the current buffer to
  // the history
  if (history_index < history_len - 1) {
    char *last_node = *((char **)vector_get(history, history_len - 1));
    memcpy(last_node, current_buf, num_chars_to_copy);
  }

  // disable the raw mode so that the terminal behaves normally again
  disable_raw_mode();

  return RL_SUCCESS;
}

/**
 * enables raw mode for the terminal.
 * for more information on raw mode, read /notes/raw-mode.md
 *
 * it will exit the program using `die` function if it fails to enable raw mode.
 */
static void enable_raw_mode(void) {
  // save original terminal settings
  if (tcgetattr(STDIN_FILENO, &original_state) == -1) {
    die("failed to enable raw mode (tcgetattr)");
  }

  struct termios term = original_state;

  // from linux man pages (man cfmakeraw -> Raw Mode -> cfmakeraw())
  // read /notes/raw-mode.md
  term.c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  term.c_oflag &= ~OPOST;
  term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  term.c_cflag &= ~(CSIZE | PARENB);
  term.c_cflag |= CS8;

  // to read within 100ms
  term.c_cc[VMIN] = 0;
  term.c_cc[VTIME] = 1; // 1 * 1/10th seconds = 100ms timeout

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) == -1) {
    die("failed to enable raw mode (tcsetattr)");
  }

  raw_mode_enabled = true;
}

static void disable_raw_mode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_state) == -1) {
    die("failed to disable raw mode (tcsetattr)");
  }
}

/**
 * raw mode MUST be enabled before calling this function!!!
 *
 * reads a key from the terminal. it handles escape sequences for arrow keys,
 * HOME, END, etc.
 */
static int read_key(void) {
  assert(raw_mode_enabled);

  ssize_t bytes_read;
  char c;
  do {
    bytes_read = read(STDIN_FILENO, &c, 1);
    // in Cygwin, when read() times out it returns -1 and sets errno
    // to EAGAIN, instead of just returning 0
    if (bytes_read == -1 && errno != EAGAIN) {
      die("failed to read input");
    }
  } while (bytes_read != 1);

  if (c != KEY_ESC) {
    return c;
  }

  // for reading escape sequences to check if the user pressed arrow keys,
  // function keys, HOME, END, etc.
  char seq[5];

read_esc_seq:
  // 2nd byte
  if (read(STDIN_FILENO, &seq[0], 1) != 1) {
    return KEY_ESC;
  }

  /*
   * It is possible to get a combination like ESC + Arrow Up, in which case
   * the sequence will be ESC, ESC, [, A. In such cases, I want to ignore the
   * first ESC and just return Arrow Up.
   */

  if (seq[0] == KEY_ESC) {
    goto read_esc_seq;
  }

  if (seq[0] == '[') {
    // 3rd byte
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return KEY_ESC;
    }

    if (seq[1] >= '0' && seq[1] <= '9') {
      // 4th byte
      if (read(STDIN_FILENO, &seq[2], 1) != 1) {
        return KEY_ESC;
      }

      if (seq[2] != '~') {
        // 5th byte
        if (read(STDIN_FILENO, &seq[3], 1) == 1) {
          read(STDIN_FILENO, &seq[4], 1); // 6th byte
        }

        return KEY_ESC;
      }

      switch (seq[1]) {
      case '1':
      case '7':
        return KEY_HOME;
      case '3':
        return KEY_DELETE;
      case '4':
      case '8':
        return KEY_END;
      case '5':
        return KEY_PAGE_UP;
      case '6':
        return KEY_PAGE_DOWN;
      }
    } else {
      switch (seq[1]) {
      case 'A':
        return KEY_ARROW_UP;
      case 'B':
        return KEY_ARROW_DOWN;
      case 'C':
        return KEY_ARROW_RIGHT;
      case 'D':
        return KEY_ARROW_LEFT;
      case 'F':
        return KEY_END;
      case 'H':
        return KEY_HOME;
      }
    }
  } else if (seq[0] == 'O') {
    // 3rd byte
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return KEY_ESC;
    }

    switch (seq[1]) {
    case 'A':
      return KEY_ARROW_UP;
    case 'B':
      return KEY_ARROW_DOWN;
    case 'C':
      return KEY_ARROW_RIGHT;
    case 'D':
      return KEY_ARROW_LEFT;
    case 'F':
      return KEY_END;
    case 'H':
      return KEY_HOME;
    }
  }

  return KEY_ESC;
}

/**
 * to print an error message and exit the program with `EXIT_FAILURE`.
 * it tries to disable raw mode if it was enabled.
 *
 * if `errno` is set, it prints the error message using `perror()`.
 * otherwise, it prints the message directly using `fputs()`.
 *
 * @param msg the error message to print
 */
static void die(const char *msg) {
  if (raw_mode_enabled) {
    // to prevent infinite recursion in case die() is called from
    // disable_raw_mode() function
    raw_mode_enabled = false;

    disable_raw_mode();
  }

  if (errno == 0) {
    fputs(msg, stderr);
    putchar('\n');
  } else {
    perror(msg);
  }

  exit(EXIT_FAILURE);
}

/**
 * uses the CPR (cursor position report) escape sequence to get the cursor
 * position.
 *
 * @param row pointer to store the row where the cursor is
 * @param col pointer to store the column where the cursor is
 *
 * @return `true` if the cursor position was successfully retrieved, else
 * `false`
 */
static bool get_cursor_position(unsigned short *row, unsigned short *col) {
  assert(row != NULL);
  assert(col != NULL);

  // write escape sequence to get the cursor position
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return false;
  }

  char res[16]; // stores response form CPR (cursor position report)
  size_t i;
  for (i = 0; i < sizeof(res); ++i) {
    if (read(STDIN_FILENO, &res[i], 1) != 1 || res[i] == 'R') {
      break;
    }
  }

  res[i] = '\0';

  if (res[0] != '\x1b' || res[1] != '[') {
    return false;
  }

  if (sscanf(&res[2], "%hu;%hu", row, col) != 2) {
    return false;
  }

  return true;
}

/**
 * moves the cursor to the left by one character.
 *
 * @return `true` if the cursor was moved successfully, else `false`
 */
static bool move_cursor_left(void) {
  return write(STDOUT_FILENO, "\x1b[D", 3) == 3;
}

/**
 * moves the cursor to the right by one character.
 *
 * @return `true` if the cursor was moved successfully, else `false`
 */
static bool move_cursor_right(void) {
  return write(STDOUT_FILENO, "\x1b[C", 3) == 3;
}

/**
 * moves the cursor to the specified row and column.
 *
 * @param row the row to move the cursor to
 * @param col the column to move the cursor to
 *
 * @return `true` if the cursor was moved successfully, else `false`
 */
static bool move_cursor_to(unsigned short row, unsigned short col) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
  return write(STDOUT_FILENO, buf, strlen(buf)) == strlen(buf);
}

static bool repaint_line(unsigned short orig_cy, unsigned short orig_cx,
                         const char *line, size_t len) {
  // move cursor back to the original position
  if (!move_cursor_to(orig_cy, orig_cx)) {
    die("repaint_line: failed to move cursor");
  }

  // clear the line
  if (write(STDOUT_FILENO, "\x1b[K", 3) != 3) {
    die("repaint_line: failed to clear line");
  }

  // repaint the line
  return write(STDOUT_FILENO, line, len) == len;
}

/**
 * adds the buffer to the history.
 *
 * @param buf the line to add to the history. must be null-terminated.
 * @param buf_size the size of the buffer. memory will be allocated based on
 * this size.
 *
 * @return `true` if the line was added to the history successfully, else
 * `false`
 */
static bool add_to_history(const char *buf, size_t buf_size) {
  assert(buf != NULL);

  // initialize the history vector if it's not already initialized
  if (history == NULL) {
    history = vector_init(sizeof(char *), 0);
  }

  // allocate memory for the line copy
  char *line_copy = malloc(buf_size);
  if (line_copy == NULL) {
    return false;
  }

  // copy the line to the allocated memory
  memcpy(line_copy, buf, buf_size);

  // add the pointer to the line copy to the history vector
  vector_push(history, &line_copy);

  return true;
}

void rl_cleanup(void) {
  if (history != NULL) {
    // free all the lines in the history
    size_t history_len = vector_length(history);

    for (size_t i = 0; i < history_len; ++i) {
      char *alloced_str = *((char **)vector_get(history, i));
      free(alloced_str);
    }

    // free the history vector
    vector_free(history);
  }

  // in case the raw mode was left enabled, disable it
  // should not happen ideally, but just in case
  if (raw_mode_enabled) {
    disable_raw_mode();
  }
}
