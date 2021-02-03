#ifndef SLAB_H
#define SLAB_H
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<memory.h>
#include<unistd.h>

#define PAGE_SZ (size_t)sysconf(_SC_PAGESIZE)
#define SMALL_OBJ_SZ PAGE_SZ/8

typedef struct kmem_cache kmem_cache;
typedef struct kmem_slab kmem_slab;
typedef struct kmem_bufctl kmem_bufctl;

struct kmem_bufctl {
    void *buf;
    kmem_bufctl *next;
    kmem_slab *slab;
};

struct kmem_slab {
    kmem_slab *next;
    kmem_slab *prev;
    kmem_bufctl *start;
    void *freeList;
    uint32_t bufcount;
};

struct kmem_cache {
    kmem_slab *slabs;
    
    
    char *name;
    size_t size;
    //size_t effsize;
    //uint32_t slabMaxBuf;
    void (*constructor)(void *, size_t);
    void (*destructor)(void *, size_t);
    kmem_slab *slabls_back;
    
    size_t alSize;  // total aligned size of cache
    size_t alBSize; // size of aligned buffer
    uint32_t maxBufCount; // limit of buffers count
};

struct settings {
	uint16_t maxSlabCount; // maximum elements in array of slabs
	uint8_t align;
	uint32_t maxbytes;     // maximum size of mmaped precache memory
	
	
};

typedef struct _stritem {
    /* Protected by LRU locks */
    struct _stritem *next;
    struct _stritem *prev;
    /* Rest are protected by an item lock */
    struct _stritem *h_next;    /* hash chain next */
    rel_time_t      time;       /* least recent access */
    rel_time_t      exptime;    /* expire time */
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

//	    settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */

kmem_cache* kmem_cache_create(char *name, size_t size, uint8_t align,
                              void (*constructor)(void *, size_t),
                              void (*destructor)(void *, size_t));

void kmem_cache_destroy(kmem_cache *cache);
void* kmem_cache_alloc(kmem_cache *cache, uint8_t flags);
void kmem_cache_free(kmem_cache *cache, void *buf);
void kmem_cache_grow(kmem_cache *cache);
void kmem_cache_reap(void);
#endif
