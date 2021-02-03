#include<stdio.h>
#include<stdint.h>
#define ITERATIONS 300000

#define runTest(test) do {char *message = test(); test_run++; \
			if (message) return (char*)message; } while(0)

int test_run = 0;

static uint8_t* test_cache_create(){
	kmem_cache_t cp = kmem_cache_create("test", 12, 0, 0, 0);
	kmem_cache_destroy(cp);
	return 0;
}

static uint8_t* testAll(){
	runTest(test_cache_create);
	return 0;
}

int main(void){
	uint8_t result = test_all();
	return 0;
}
