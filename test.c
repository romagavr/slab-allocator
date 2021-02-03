#include<stdio.h>
#include<stdint.h>
#include"src/slab.h"

#define runTest(test) do {char *message = test(); test_run++; \
			if (message) return (char*)message; } while(0)

int test_run = 0;

static char* test_cache_create(){
	uint8_t *ptr = slubAlloc(10);
	return 0;
}

static char* testAll(){
	runTest(test_cache_create);
	return 0;
}

int main(void){
	char *result = testAll();
	return 0;
}
