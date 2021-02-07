#include<stdio.h>
#include<stdlib.h>
#include <sys/mman.h>
#include"slab.h"

typedef struct {
    uint32_t size;      /* size of chunk */

    void *chunksList;           /* list of item ptrs */
    uint32_t freeChunks;   /* total free items in list */
    uint32_t chunksCount;   /* how many items per slab */


    void **slabList;       /* array of slab pointers */
    uint32_t slabsCount;     /* how many slabs were allocated for this class */
    unsigned int slabListSize; /* size of prev array */
} slabclass_t;

typedef struct {
	slabclass_t *slabs;
	settings *stn;
	void *base; // huge memory chunk
	void *memCurPos; // current position in huge memory chunk
	size_t memLimit; //Total size of allocated mem using mmap
	size_t memAvail; //Available size of allocated mem using mmap
} kmemCashe;

typedef struct _stritem {
    /* Protected by LRU locks */
    struct _stritem *next;
    struct _stritem *prev;
    /* Rest are protected by an item lock */
    struct _stritem *h_next;    /* hash chain next */
    //rel_time_t      time;       /* least recent access */
    //rel_time_t      exptime;    /* expire time */
    int             nbytes;     /* size of data */
    unsigned short  refcount;
    uint16_t        it_flags;   /* ITEM_* above */
    uint8_t         slabs_clsid;/* which slab class we're in */
    uint8_t         nkey;       /* key length, w/terminating null and padding */
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
	uint32_t size = sizeof(item) + s->minChunkSize;
	memset(slabs, 0, sizeof *slabs);
	for (uint32_t i = 1; i<MAX_NUMBER_OF_SLAB_CLASSES-1; i++, size *= s->factor){
		printf("%d size: %d; chunksCount: %ld;\n", i, size, s->slabPageSize / size);
		slabs[i].size = size;
		slabs[i].chunksCount = s->slabPageSize / size;
	}
	//printf("Size: %d\n", slabs[MAX_NUMBER_OF_SLAB_CLASSES-2].size/1024);
	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].size = s->maxChunkSize;
	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].chunksCount = s->slabPageSize / s->maxChunkSize;
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

static uint8_t growList(uint32_t id){
	slabclass_t *sl = &(cache->slabs[id]);
	if (sl->slabsCount == sl->slabListSize) {
		size_t size =  (sl->slabListSize != 0) ? sl->slabListSize * 2 : 16;
		void *list = realloc(sl->slabList, size * sizeof(void *));
		if (list == 0) return 1;
		sl->slabListSize = size;
		sl->slabList = list;
	}
	return 0;
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

void* slubAlloc(size_t size) {
	uint8_t ret = 0;
	if (!cache && (ret = slabInit())) {
		return 0;
	};
	uint32_t id = 0;
	while (cache->slabs[++id].size < size){};
	slabclass_t *p = &(cache->slabs[id]);
	item *it = 0;
	if (p->freeChunks == 0){
		// create linked list of slab
		growList(id);
		void *ptr = 0;
		if ((ptr = getSlubPage(cache->stn->slabPageSize)) == 0) {
			return 0;
		}
		memset(ptr, 0, cache->stn->slabPageSize);
		for (uint32_t j = 0; j < p->chunksCount; j++, ptr += p->size) {
			it = (item *)ptr;
			it->it_flags = ITEM_SLABBED;
			it->slabs_clsid = id;
			it->prev = 0;
			it->next = p->chunksList;
			if (it->next) it->next->prev = it;
			p->chunksList = it;
			p->freeChunks++;
		}
		p->slabList[p->slabsCount++] = ptr;
	}

	it = (item *)p->chunksList;
	p->chunksList = it->next;
	if (it->next) it->next->prev = 0;
	it->it_flags &= ~ITEM_SLABBED;
	it->refcount = 1;
	p->freeChunks--;
	
    return (void *)it;
}

uint8_t slubFree(void *object) {
	if (!cache) {
		return 0;
	};
}
