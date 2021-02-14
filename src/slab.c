#include<stdio.h>
#include<stdlib.h>
#include <sys/mman.h>
#include"slab.h"

#define GET_ITEM(p) ((item *)((uint8_t *)p - sizeof(item)))
#define SLAB_CACHE 0

typedef struct _stritem {
    /* Protected by LRU locks */
    struct _stritem *next;
    struct _stritem *prev;
    /* Rest are protected by an item lock */
    //rel_time_t      time;       /* least recent access */
    //rel_time_t      exptime;    /* expire time */
    int             nbytes;     /* size of data */
    unsigned short  refcount;
    uint16_t        it_flags;   /* ITEM_* above */
    uint8_t         slabId;/* which slab class we're in */
    /* this odd type prevents type-punning issues when we do
     * the little shuffle to save space when not using CAS. */
    union {
        uint64_t cas;
        char end;
    } data[];
    /* if it_flags & ITEM_CAS we have 8 bytes CAS */
    /* then null-terminated key */
    /* then " flags length\r\n" (no terminating null) */
    /* then data with terminating \r\n (no terminating null; it's binary!) */
} item;

typedef struct {
    uint32_t size;      /* size of returned memory */
	uint32_t sizeOfChunk;  /* size of chunk */

    void *chunksList;           /* list of item ptrs */
    uint32_t chunksCount;   /* how many items per slab */
    uint32_t freeChunks;   /* total free items in list */

    void **slabList;       /* array of slab pointers */
    uint32_t slabsCount;     /* how many slabs were allocated for this class */
    unsigned int slabListSize; /* size of prev array */

	item *freeList;
	item *usedList;
	item *cachedList;
} slabclass_t;

typedef struct {
	slabclass_t *slabs;
	settings *stn;
	void *base; // huge memory chunk
	void *memCurPos; // current position in huge memory chunk
	size_t memLimit; //Total size of allocated mem using mmap
	size_t memAvail; //Available size of allocated mem using mmap
} kmemCashe;



static kmemCashe *cache = 0;
static settings *preSet = 0;

static uint8_t slabInit();
static settings* settingsInit();

static uint8_t slabInit(){
	cache = calloc(1, sizeof(kmemCashe));
	if (!cache) return 1;
	cache->slabs = calloc(1, sizeof(slabclass_t) * MAX_NUMBER_OF_SLAB_CLASSES);
	if (!cache->slabs) {
		free(cache);
		return 1;
	}
	cache->stn = calloc(1, sizeof(settings));
	if (!cache->stn) {
		free(cache);
		free(cache->slabs);
		return 1;
	}
	cache->stn = settingsInit();
	
	settings *s = cache->stn;
	int ret;
	
	void *ptr = mmap (0, s->maxbytes,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
   	if(ptr == MAP_FAILED){
		printf("Mapping Failed\n");
		exit(1);
    }
	cache->base = cache->memCurPos = ptr;
	cache->memLimit = cache->memAvail = s->maxbytes;
	
	slabclass_t *slabs = cache->slabs;
	uint32_t size = s->minChunkSize;
	uint32_t sizeItem = sizeof(item);
	memset(slabs, 0, sizeof *slabs);
	for (uint32_t i = 1; i<MAX_NUMBER_OF_SLAB_CLASSES-1; i++, size *= s->factor){
		//printf("%d size: %d; sizeOfChunk: %d; chunksCount: %ld;\n", i, size, size+sizeItem, s->slabPageSize / (size+sizeItem));
		slabs[i].size = size;
		slabs[i].sizeOfChunk = size + sizeItem;
		slabs[i].chunksCount = s->slabPageSize / (size + sizeItem);
		slabs[i].slabsCount++;
	}
	//printf("Size: %d\n", slabs[MAX_NUMBER_OF_SLAB_CLASSES-2].size/1024);
	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].size = s->maxChunkSize;
	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].sizeOfChunk = s->maxChunkSize + sizeItem;
	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].chunksCount = s->slabPageSize / (s->maxChunkSize + sizeItem);
	return 0;
}

static settings* settingsInit(){
	if (preSet) return preSet;
	settings *s = calloc(1, sizeof(settings));
	if (!s) return 0;
	s->maxSlabCount = 10; 	// maximum elements in array of slabs
	s->align = 16;
	s->maxbytes = 64 * 1024 * 1024;     	// maximum size of mmaped precache memory
	s->slabPageSize = 1024 * 1024;         // 1024*1024
	s->minChunkSize = 48;		// 48
	s->maxChunkSize = s->slabPageSize / 2;		// settings.slabPageSize / 2;
	s->factor = 1.1;
	return s;
	//TODO: s init by values;
}

uint8_t preSetInit(settings *s){
	if (preSet) return 0;
	preSet = s;
	return 1;
}

static void* getSlubPage(size_t size) {
	if (size > cache->memAvail) return 0;
	if (size % CHUNK_ALIGN_BYTES)
	    size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
	    
	void* ret = cache->memCurPos;
	cache->memCurPos = ((char*)cache->memCurPos) + size;
	cache->memAvail = (size < cache->memAvail) ? cache->memAvail - size : 0;
	return ret;
}

static void addToList(item *it, item **list) {
	if (*list) {
		it->next = *list;
		it->prev = (*list)->prev;
		(*list)->prev = it;
	} else {
		*list = it;
		printf("List - %p\n", *list);
		(*list)->prev = (*list)->next = *list;
		printf("Add to List prev %p\n", it->prev);
		printf("Add to List next %p\n", it->next);
	}
}

void* slubAllocNamed(char *name, size_t size, int align, 
                  	void (*constructor)(void *, size_t),
                  	void (*destructor)(void *, size_t)) {

}

void* slubAlloc(size_t size) {
	uint8_t ret = 0;
	if (!cache && (ret = slabInit())) {
		return 0;
	};
	uint32_t id = 0;
	while (cache->slabs[++id].size < size){};
	slabclass_t *p = &(cache->slabs[id]);
	item *it = 0;

	printf("Slab Id %d\n", id);
	// find in cachedList	
	if (p->cachedList) {
		printf("Searching in cached List\n");
		it = p->cachedList;
		if (it->next == it) {
			p->cachedList = 0;
		} else {
			p->cachedList = p->cachedList->next;
			p->cachedList->prev = it->prev;
		}
	} else {
		printf("Searching in free List\n");
		// cachedList is empty -> search in freeList
		if (!p->freeList) {
			//freeList is empty -> mmap page -> create freeList
			void *ptr = 0;
			if ((ptr = getSlubPage(cache->stn->slabPageSize)) == 0) {
				return 0;
			}
			memset(ptr, 0, cache->stn->slabPageSize);
			for (uint32_t j = 0; j < p->chunksCount; j++, ptr += p->sizeOfChunk) {
				it = (item *)ptr;
				it->it_flags = ITEM_FREE;
				it->slabId = id;
				it->next = p->freeList;
				if (it->next) {
					it->prev = it->next->prev;
					it->next->prev = it;
				} else it->prev = it;
				p->freeList = it;
			}
		}
		// remove from free List
		it = (item *)p->freeList;
		if (it->next == it) {
			p->freeList = 0;
		} else {
			p->freeList = it->next;
			p->freeList->prev = it->prev;
		}
	}

	// add to used List
	addToList(it, &p->usedList);
	it->it_flags &= ~ITEM_USED;
	it->refcount = 1;
/*
	if (uList) {
		it->next = uList;
		it->prev = uList->prev;
		uList->prev = it;
	} else {
		uList = it;
		uList->prev = uList->next = uList;
	}
	*/
	//uint8_t *ret = (void *)((uint8_t*)it + p->size - p->sizeOfChunk);
	/*
	uint8_t *rett = (uint8_t*)it + p->sizeOfChunk - p->size;
	printf("\n%d\n", p->sizeOfChunk - p->size);
	printf("\n%d\n", p->sizeOfChunk);
	printf("\n%p\n", ((uint8_t*)it ));
	printf("\n%p\n", rett);
	printf("\n%hu\n", ((item *)((uint8_t *)rett - sizeof(item)))->refcount);
	*/
	/*
	uint8_t *ret2 = rett;
	uint8_t *rettt = rett;
	for (uint32_t i=0; i<p->size; i++){
		*rett = 21;
		rett++;
	}
	for (uint32_t i=0; i<p->size; i++){
		printf("%hhx ", *ret2);
		ret2++;
	}
	printf("\n%p\n", (uint8_t*)it);
	printf("\n%p\n", rettt);
	*/
	//printf("\n%hu\n", ((item *)((uint8_t *)rettt - sizeof(item)))->refcount);
	printf("\n%p\n", (void *)((uint8_t*)it + sizeof(item)));
    return (void *)((uint8_t*)it + sizeof(item));
}

uint8_t slubFree(void *object) {
	if (!cache) {
		return 0;
	};
	item *it = GET_ITEM(object);

	// remove from usedList
	it->prev->next = it->next;
	it->next->prev = it->prev;

	//move to cachedList
	item *cList = cache->slabs[it->slabId].cachedList;
	printf("Slab Id %d\n", it->slabId);
	addToList(it, &(cache->slabs[it->slabId].cachedList));
	it->it_flags &= ~ITEM_CACHED;
	it->refcount--;
	printf("\nFreed\n");
}
