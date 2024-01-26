GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: libcoro.c solution.c
	gcc $(GCC_FLAGS) libcoro.c solution.c

sort: sort.c
	gcc $(GCC_FLAGS) sort.c -o run_sort

generate_test_data:
	python3 generator.py -f test1.txt -c 10000 -m 10000 && \
	python3 generator.py -f test2.txt -c 10000 -m 10000 && \
	python3 generator.py -f test3.txt -c 10000 -m 10000 && \
	python3 generator.py -f test4.txt -c 10000 -m 10000 && \
	python3 generator.py -f test5.txt -c 10000 -m 10000 && \
	python3 generator.py -f test6.txt -c 100000 -m 10000

test_sort: sort
	./run_sort test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt
	python3 -m checker -f sorted.txt
clean:
	rm -f a.out run_sort test*.txt sorted*.txt
