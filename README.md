# Echo REPL with History

This is a simple REPL in C that reads a line of input from the user and prints it back to the user. It also saves the history of inputs entered by the user.

## Why a stupid REPL?

Well, creating a basic REPL that reads input until the user hits `ENTER` is straightforward. However, allowing the user to navigate through input history with the UP and DOWN arrow keys is more challenging because:

1. **Immediate Input Handling**: You don't get what the user typed until they hit `ENTER`. This is problematic because users expect the previous command to be displayed immediately upon pressing the UP arrow key.

2. **Raw Mode and Escape Sequences**: To handle user input on each key press, the terminal input must be in raw mode. And once you're in raw mode, say bye bye to the default behavior of the terminal like `CTRL+C` to exit, `CTRL+D` to send EOF, or something as simple as backspace handling. You have to handle all of these manually. In fact, you have to responsible for writing back the character that the user types to the terminal.

Read more about raw mode and escape sequences in my notes

- [Raw Mode](/notes/raw-mode.md)
- [Escape Sequences](/notes/escape-sequences.md)

## Motivation

I created this REPL because I was reading [Build your own Lisp](http://www.buildyourownlisp.com/), where the author used the `editline` library to handle input history. I wanted to build this functionality from scratch but initially opted to use `editline`. But well, I couldn't build it on my Mac, so I decided to implement a naive version that suited my needs. I was already familiar with raw mode and escape sequences from my previous project, [Texterm, a simple text editor in C](https://github.com/biraj21/texterm) so I knew what I was getting into.

## Behavior of history in this REPL

- You can navigate through the history of inputs using the UP and DOWN arrow keys.
- If you navigate to an old input in the history & edit it, then it will remain in the history, even after you hit `ENTER`. This is different from the behavior of most shells where the edited input is not saved in the history after you hit `ENTER`, but I'm too lazy to implement that behavior.
- Your last input is always the latest in the history (duh!).

## How to run

1. Clone the repo

2. Build the project

   ```bash
   make
   ```

3. Run the project

   ```bash
   ./bin/repl
   ```

## Tested on
- MacOS 14.4.1, Apple M1 Chip