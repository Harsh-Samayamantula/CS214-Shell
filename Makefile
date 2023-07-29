shell: shell.c
	gcc -g -o shell shell.c -fsanitize=address -Wall -Werror -Wvla
	
clean:
	rm -f shell