run: test
	./test

test: hm.c
	gcc hm.c -fsanitize=address,null -ggdb -std=c99 -O0 -o test
