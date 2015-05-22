/*
Copyright (c) 2011, Yahoo! Inc.  All rights reserved.
Copyrights licensed under the New BSD License. See the accompanying LICENSE file for terms
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "set.h"
#include <apr_strings.h>

#define NUM_PRIMES 29
static const unsigned long prime_list[NUM_PRIMES] =
{
    19ul, 53ul,   97ul,         193ul,       389ul,       769ul,
    1543ul,       3079ul,       6151ul,      12289ul,     24593ul,
    49157ul,      98317ul,      196613ul,    393241ul,    786433ul,
    1572869ul,    3145739ul,    6291469ul,   12582917ul,  25165843ul,
    50331653ul,   100663319ul,  201326611ul, 402653189ul, 805306457ul,
    1610612741ul, 3221225473ul, 4294967291ul
};


unsigned long next_prime(unsigned long n)
{
    const unsigned long* prime = prime_list;
    assert(n < prime_list[NUM_PRIMES - 1]);
    while (*prime < n) ++prime;
    return *prime;
}

static size_t _count_members(const set* s)
{
    size_t count = 0;
    int i;
    set_element* n;

    for (i = 0; i < s->hashsize; i++) 
        for (n = s->table[i]; n; n = n->next)
            count++;
    return count;
}

set* set_new(apr_pool_t* parent_pool, int hashsize)
{
    set* s;
    int i;
    apr_pool_t* pool;
    apr_pool_create(&pool, parent_pool);
    s = apr_palloc(pool, sizeof(set));

    s->pool = pool;
    s->hashsize = next_prime(hashsize);
    s->table = (set_element**) apr_palloc(pool, sizeof(set_element *) *
                                       s->hashsize);

    for (i = 0; i < s->hashsize; i++)
        s->table[i] = NULL;

    s->members = 0;
    return s;
}

void set_destroy(set* s)
{
    apr_pool_destroy(s->pool);
}

static set_element* 
set_element_new(apr_pool_t* pool, const char* name, void* data)
{
    set_element* n;
    n = apr_palloc(pool, sizeof(set_element));
    n->name = apr_pstrdup(pool, name);
    n->data = data;
    n->next = NULL;
    return n;
}

#define HSIEH_HASH 1

#if defined(HSIEH_HASH)
static uint32_t string_hash(const char* data)
{
#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
                       +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif
	uint32_t len = strlen(data);
	uint32_t hash = len, tmp;
	int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (uint16_t);
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= data[sizeof (uint16_t)] << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}

#elif defined(PJW_HASH)
/* PJW hash function (optmized for 32bit ints) */
static unsigned string_hash(const char* str)
{
    unsigned hash = 0;
    assert(str);
    while (*str) {
        unsigned x;
        hash = (hash << 4) + *str;
        if ((x = hash & 0xf0000000) != 0) 
            hash ^= x >> 24;
        hash &= ~x;
        ++str;
    }
    return hash;
}
#elif defined(RS_HASH)
/* Robert Sedgewick hash function */
static unsigned string_hash(const char* str)
{
   unsigned b    = 378551;
   unsigned a    = 63689;
   unsigned hash = 0;
   unsigned i    = 0;

   while (*str) {
      hash = hash * a + (*str);
      a    = a * b;
      ++str;
   }

   return hash;
}
#elif defined(DJB_HASH)
/* DJB's extremely efficient hash function */
static unsigned string_hash(const char* str)
{
    unsigned int hash = 5381;
    while (*str)
        hash = ((hash << 5) + hash) + (*str++);

    return hash;

}
#elif defined(DEK_HASH)
static unsigned string_hash(const char* str)
{
    unsigned len = strlen(str);
    unsigned hash = len;
    unsigned i    = 0;

    for (i = 0; i < len; str++, i++)
    {  
        hash = ((hash << 5) ^ (hash >> 27)) ^ (*str);
    }
    return hash;
}
#endif

#define set_hash(s, str) (string_hash(str) % s->hashsize)

static void resize(set* s, size_t num_elements_hint)
{
    size_t old_n = s->hashsize;
    if (old_n < num_elements_hint) {
        size_t n = next_prime(num_elements_hint);
        if (n > old_n) {
            int i;
            set_element* *new_table = apr_pcalloc(s->pool, sizeof(set_element* ) * n);
            for (i=0; i<old_n; ++i) {
                set_element* bucket, *next_bucket;
                for (bucket = s->table[i]; bucket; bucket = next_bucket) {
                    int new_idx = string_hash(bucket->name) % n;
                    next_bucket = bucket->next;
                    bucket->next = new_table[new_idx];
                    new_table[new_idx] = bucket;

                }
            }
            s->table = new_table;
            s->hashsize = n;
        }
    }
}

static set_element* set_add_noresize(set* s, const char* name, void* data)
{
    int i;
    set_element* n;
    i = set_hash(s, name);
    for (n = s->table[i]; n; n = n->next)
        if (!strcmp(n->name, name)) {
            n->data = data;
            return n;
        }

    n = set_element_new(s->pool, name, data);
    n->next = s->table[i];
    s->table[i] = n;
    s->members++;

    return n;
}

set_element* set_add(set* s, const char* name, void* data)
{
    resize(s, s->members + 1);
    return set_add_noresize(s, name, data);
}

set_element* set_get(const set* s, const char* name)
{
    int i;
    set_element* n;
    i = set_hash(s, name);

    for (n = s->table[i]; n; n = n->next)
        if (!strcmp(n->name, name))
            return n;

    return NULL;
}
void* set_get_data(const set* s, const char* name)
{
    set_element* e = set_get(s, name);

    if (e) return e->data;
    return NULL;
}

set_element** set_members(const set* s)
{
    int i, j;
    set_element* n;
    set_element** ret;

    ret = apr_palloc(s->pool, sizeof(set_element* ) * (s->members + 1));

    j = 0;
    for (i = 0; i < s->hashsize; i++)
        for (n = s->table[i]; n; n = n->next)
            ret[j++] = n;

    ret[j] = NULL;
    return ret;
}

set* set_union(apr_pool_t* pool, const set* s1, const set* s2)
{
    set* s;
    int i;
    set_element* n;

    s = set_new(pool, s1->members + s2->members);

    for (i = 0; i < s1->hashsize; i++)
        for (n = s1->table[i]; n; n = n->next)
            set_add_noresize(s, n->name, n->data);

    for (i = 0; i < s2->hashsize; i++)
        for (n = s2->table[i]; n; n = n->next)
            set_add_noresize(s, n->name, n->data);

#if defined(DEBUG_HASH)
    dump_hash_values(s);
#endif
    return s;
}

void set_union_inplace(set* s, const set* s2)
{
    int i;
    set_element* n;

    resize(s, s->members + s2->members);
    for (i = 0; i < s2->hashsize; i++)
        for (n = s2->table[i]; n; n = n->next)
            set_add_noresize(s, n->name, n->data);

    assert(s->members == _count_members(s));
#if defined(DEBUG_HASH)
    dump_hash_values(s);
#endif
}

set* set_diff(apr_pool_t* pool, const set* s1, const set* s2)
{
    set* s;
    int i;
    set_element* n;

    s = set_new(pool, s1->hashsize);

    for (i = 0; i < s->hashsize; i++)
        for (n = s1->table[i]; n; n = n->next)
            if (!set_get(s2, n->name))
                set_add_noresize(s, n->name, n->data);

#if defined(DEBUG_HASH)
    dump_hash_values(s);
#endif
    return s;
}

void set_diff_inplace(set* s, const set* s2)
{
    int i;
    set_element* n;
    
    for (i = 0; i < s->hashsize; i++) {
        set_element* prev = NULL; 
        for (n = s->table[i]; n; n = n->next) {
            if (set_get(s2, n->name)) {
                if (prev)
                    prev->next = n->next;
                else 
                    s->table[i] = n->next;

                s->members--;
            }
            else prev = n;
        }
    }

#if defined(DEBUG_HASH)
    dump_hash_values(s);
#endif
}

set* set_intersect(apr_pool_t* pool, const set* s1, const set* s2)
{
    int i;
    set_element* n;
    set* s;

    /* always loop over the set with fewer elements: s1 > s2 */
    if (s1->members < s2->members) {
        /* swap */
        const set* tmp = s1;
        s1 = s2;
        s2 = tmp;
    }

    s = set_new(pool, s1->hashsize);

    for (i = 0; i < s2->hashsize; i++)
        for (n = s2->table[i]; n; n = n->next)
            if (set_get(s1, n->name))
                set_add_noresize(s, n->name, n->data);
    return s;
}

set* set_remove(set* s, const char* name)
{
    set_element* n;
    set_element* prev = NULL;
    int i = set_hash(s, name);

    for (n = s->table[i]; n; prev = n, n = n->next) {
        if (!strcmp(n->name, name)) {
            if (prev)
                prev->next = n->next;
            else
                s->table[i] = n->next;
            s->members--;
            return s;
        }
    }

    return s;
}

char* set_dump(const set* s)
{
    set_element** memb;
    int i;

    fprintf(stderr, "Members: %ld\n", (long)s->members);
    memb = set_members(s);
    for (i=0; memb[i]; i++) {
        fprintf(stderr, " - %s => %s\n", memb[i]->name, (char*)set_get_data(s, memb[i]->name));
    }
    return "";
}

void dump_hash_values(const set* s)
{
    int i;
    int used = 0, max_chain = 0;
    for (i=0; i<s->hashsize; i++) {
        int this_chain = 0;
        set_element* n = s->table[i];
        if (n) used++;
        while (n) {
            this_chain++;
            n = n->next;
        }
        max_chain = max_chain < this_chain ? this_chain : max_chain;
    }
    printf("DEBUG: dump_hash_values: used: %d, s->members: %d, s->hashsize: %d, s->max_chain: %d\n", used, s->members, s->hashsize, max_chain);
}

/* memory needed to store set */
int set_pack_size(const set* s) {
  set_element* e;
  int size = 0;
  int i=0;
  for (i = 0; i < s->hashsize; i++) {
    for (e = s->table[i]; e; e = e->next) {
      //printf("element name: %s\n", e->name);
      size += strlen(e->name) + 1 + sizeof(int); /* string + NULL  + intlen */
      //printf("element data: %s\n", e ? e->data : NULL);
      if (e->data) {
        size += strlen(e->data) + 1 + sizeof(int); /* string + NULL  + intlen */
      } else {
        size += sizeof(int); /* just a -1 value */
      }
    }
  }
  size += sizeof(int); /* total size */
  //printf("set_pack_size is: %d\n", size);
  return size;
}

/* pack a set into contiguous memory
 * format:
 *   int(total size of structure)
 *   sequence of:
 *   KEY: int(strlen) str\0
 *   VAL: int(strlen) str\0 -- OR int(-1) if no value
 * */
void * set_pack(const set* s) {
  int i;
  set_element *e;
  void *p, *ret;
  apr_pool_t* pool = s->pool;
  int size = set_pack_size(s);
  p = ret = apr_palloc(pool, size);

  /* record how big this structure is */
  memcpy(p, &size, sizeof(size));
  p += sizeof(size);
//  printf("in set_pack, size is: %d\n", size);
  printf("s->hashsize is %d\n", s->hashsize);
  for (i = 0; i < s->hashsize; i++) {
    for (e = s->table[i]; e; e = e->next) {
     printf("in set pack, i = %d, e = %p, e->name = %s\n", i, e, e->name);
      int len = strlen(e->name);
//      printf("strlen is : %d\n", len);
      memcpy(p, &len, sizeof(len));
      p += sizeof(len);
      strcpy(p, e->name);
      p += len + 1; /* len + null byte*/

//printf("e->data is: %p\n", e->data);
      if (e->data) {
        len = strlen(e->data);
        memcpy(p, &len, sizeof(len));
        p += sizeof(len);
        strcpy(p, e->data);
        p += len + 1; /* len + null byte*/
      } else {
        /* encoding for NULL data, len = -1 and no data */
        len = -1;
        memcpy(p, &len, sizeof(len));
        p += sizeof(len);
      }
    }
  }
  return ret;
}

typedef struct range
{
    set* nodes;
    int quoted;
} range;


void * set_pack_from_range(range *r) {
  return set_pack(r->nodes);
}

set * set_unpack(apr_pool_t* pool, void * packed_data) {
  int size;
  void * p = packed_data;
  //printf("XXXXXX p = %p\n");
  memcpy(&size, p, sizeof(size));
  //printf("in set_unpack, size is: %d\n", size);
  p += sizeof(size);

  set * ret = set_new(pool, 0);
  while (1) {
    void * key;
    void * val;
    int string_len = 0;
    memcpy(&string_len, p, sizeof(string_len));
//    printf("in set_unpack, string_len is: %d\n", string_len);
    p += sizeof(string_len);
    key = p;
    p += strlen(p) + 1;
    /*data */
    memcpy(&string_len, p, sizeof(string_len));
    p += sizeof(string_len);
    if (string_len == -1) {
      val = NULL;
    } else {
      val = p;
      p += strlen(p) + 1;
    }
    set_add(ret, key, val);
//    printf("packed_data = %p and data+size = %p and  p = %p\n", packed_data, (packed_data + size), p);
    if ((packed_data + size) <= p) {
      break;
    }
  }
  return ret;
}
