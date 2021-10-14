#ifndef HASHTABLE_H
#define HASHTABLE_H

#define HASHTABLE_SIZE 65537

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct hashtable_node_t {
  size_t key;
  void* data;
  struct hashtable_node_t *next;
} hashtable_node_t;

typedef hashtable_node_t** hashtable_t;

hashtable_t ht_create();
void ht_insert(hashtable_t ht, size_t key, void* data);
void* ht_search(hashtable_t ht, size_t key);
void* ht_remove(hashtable_t ht, size_t key);

#endif