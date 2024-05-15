cc=gcc
flags=-Wall -Werror
src=src
bin=bin
bin_name=repl

src_files = $(wildcard $(src)/*.c)

all: setup clean $(bin)/$(bin_name)

setup:
	mkdir -p $(bin)

clean:
	rm -f $(bin)/*

$(bin)/$(bin_name): $(src_files)
	$(cc) $(flags) -o $@ $^
