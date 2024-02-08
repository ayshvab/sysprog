# GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant
GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -Wno-unused-function -Wno-unused-variable


solution: libcoro.c solution.c
	gcc $(GCC_FLAGS) -g libcoro.c solution.c -o solution

test_solution: solution
	./solution test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt
	python3 -m checker -f sorted_by_solution.txt

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
	rm -f a.out solution run_sort test*.txt sorted*.txt
