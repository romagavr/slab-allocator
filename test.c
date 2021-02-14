#include<stdio.h>
#include<stdint.h>
#include<assert.h>
#include"src/slab.h"

#define runTest(test) do {char *message = test(); test_run++; \
			if (message) return (char*)message; } while(0)

int test_run = 0;

/*
* Alloc and Free objects of same size
* Second alloc of the same size must be from cache
*/
static char* test_cache_create(){
	uint16_t size = 100;
	uint8_t testValue = 21;
	uint8_t *ptr = slubAlloc(size);
	printf("First alloc %p\n", ptr);
	for (uint8_t *t = ptr; t<ptr+size; t++)
		*t = testValue;
	slubFree(ptr);

	uint8_t *ptr2 = slubAlloc(size);
	printf("Second alloc %p\n", ptr2);
	assert(ptr2 == ptr);
	for (uint8_t *t = ptr2; t<ptr2+size; t++)
		assert(*t == testValue);
	printf("test_cache_create passed\n");
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
