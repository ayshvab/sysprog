GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: libcoro.c solution.c
	gcc $(GCC_FLAGS) libcoro.c solution.c

sort: sort.c
	gcc $(GCC_FLAGS) sort.c -o run_sort

clean:
	rm a.out run_sort
