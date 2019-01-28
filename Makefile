all: MyProxy

MyProxy: MyProxy.c
	gcc -Werror -g -Wall -o MyProxy MyProxy.c
