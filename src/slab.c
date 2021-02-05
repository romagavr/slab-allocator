#include<stdio.h>
#include<stdlib.h>
#include <sys/mman.h>
#include"slab.h"

typedef struct {
    uint32_t size;      /* sizes of items */
    uint32_t perslab;   /* how many items per slab */

    void *slots;           /* list of item ptrs */
    unsigned int sl_curr;   /* total free items in list */

    unsigned int slabs;     /* how many slabs were allocated for this class */

    void **slab_list;       /* array of slab pointers */
    unsigned int list_size; /* size of prev array */
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


static kmemCashe *cashe = 0;
static settings *preSet = 0;

static uint8_t slabInit();
static settings* settingsInit();

static uint8_t slabInit(){
	cashe = calloc(1, sizeof(kmemCashe));
	if (!cashe) return 1;
	cashe->slabs = calloc(1, sizeof(slabclass_t) * MAX_NUMBER_OF_SLAB_CLASSES);
	if (!cashe->slabs) {
		free(cashe);
		return 1;
	}
	cashe->stn = calloc(1, sizeof(settings));
	if (!cashe->stn) {
		free(cashe);
		free(cashe->slabs);
		return 1;
	}
	cashe->stn = settingsInit();
	
	settings *s = cashe->stn;
	int ret;
	
	void *ptr = mmap (0, s->maxbytes,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    	if(ptr == MAP_FAILED){
		printf("Mapping Failed\n");
		exit(1);
    	}
	cashe->base = cashe->memCurPos = ptr;
	cashe->memLimit = cashe->memAvail = s->maxbytes;
	
	slabclass_t *slabs = cashe->slabs;
	uint32_t size = sizeof(item) + s->minChunkSize;
	memset(slabs, 0, sizeof *slabs);
	for (uint32_t i = 1; i<MAX_NUMBER_OF_SLAB_CLASSES-1; i++, size *= s->factor){
		printf("%d size: %d; perslab: %ld;\n", i, size, s->slabPageSize / size);
		slabs[i].size = size;
		slabs[i].perslab = s->slabPageSize / size;
	}
	//printf("Size: %d\n", slabs[MAX_NUMBER_OF_SLAB_CLASSES-2].size/1024);
    	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].size = s->maxChunkSize;
    	slabs[MAX_NUMBER_OF_SLAB_CLASSES-1].perslab = s->slabPageSize / s->maxChunkSize;
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
	slabclass_t *sl = &(cashe->slabs[id]);
	size_t new_size =  (sl->list_size != 0) ? sl->list_size * 2 : 16;
	void *new_list = realloc(sl->slab_list, new_size * sizeof(void *));
	if (new_list == 0) return 1;
	sl->list_size = new_size;
	sl->slab_list = new_list;
	return 0;
}

static void* getSlubPage(size_t size) {
	if (size > cashe->memAvail) return 0;
	if (size % CHUNK_ALIGN_BYTES)
	    size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
	    
	void* ret = cashe->memCurPos;
	cashe->memCurPos = ((char*)cashe->memCurPos) + size;
	cashe->memAvail = (size < cashe->memAvail) ? cashe->memAvail - size : 0;
	return ret;
}

void* slubAlloc(size_t size) {
	uint8_t ret = 0;
	if (!cashe && (ret = slabInit())) {
		return 0;
	};
	uint32_t id = 0;
	while (cashe->slabs[++id].size < size){};

	// create linked list of slab
	growList(id);
	void *ptr = 0;
	if ((ptr = getSlubPage(cashe->stn->slabPageSize)) == 0) {
		return 0;
	}
	memset(ptr, 0, cashe->stn->slabPageSize);
	slabclass_t *p = &(cashe->slabs[id]);
    	for (uint32_t j = 0; j < p->perslab; j++, ptr += p->size) {
    		item *it = (item *)ptr;
        	it->it_flags = ITEM_SLABBED;
        	it->slabs_clsid = id;
        	it->prev = 0;
        	it->next = p->slots;
        	if (it->next) it->next->prev = it;
        	p->slots = it;
        	p->sl_curr++;
    	}
    	p->slab_list[p->slabs++] = ptr;
    	if (p->sl_curr == 0) return 0;
    
        item *it = (item *)p->slots;
	p->slots = it->next;
	if (it->next) it->next->prev = 0;
	it->it_flags &= ~ITEM_SLABBED;
	it->refcount = 1;
	p->sl_curr--;
	
    	return (void *)it;

	/*
	slabclass_t *s = &(cashe->slabs[BANK_OF_FREE_SLABS]);
	if (s->slabs == 0){
		void *ptr =0;
		if ((ptr = getSlubPage(cashe->stn->slabPageSize)) == 0) {
			return 0;
		}
		growList(BANK_OF_FREE_SLABS);
		memset(ptr, 0, size);
		s->slab_list[s->slabs++] = ptr;
	}
        
        // get_page_from_global_pool(void)
    	uint8_t *res = s->slab_list[s->slabs - 1];
    	s->slabs--;
    	*/
}

uint8_t slubFree(void *object) {
	if (!cashe) {
		return 0;
	};
}
