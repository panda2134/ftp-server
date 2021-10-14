#include "hashtable.h"

hashtable_t ht_create() {
  hashtable_t ht = (hashtable_t) \
    calloc(HASHTABLE_SIZE, sizeof(hashtable_node_t*));
  for (size_t i = 0; i < HASHTABLE_SIZE; i++) {
    ht[i] = (hashtable_node_t*) malloc(sizeof(hashtable_node_t));
    ht[i]->key = -1; // dummy node
    ht[i]->data = NULL;
    ht[i]->next = NULL;
  }
  return ht;
}

void ht_insert(hashtable_t ht, size_t key, void* data) {
  assert(key >= 0);
  size_t index = key % HASHTABLE_SIZE;
  hashtable_node_t* node = ht[index];
  for (; node->next != NULL; node = node->next) {
    /* do nothing */
  }
  node->next = (hashtable_node_t*) malloc(sizeof(hashtable_node_t));
  node->next->key = key;
  node->next->data = data;
  node->next->next = NULL;
}

void* ht_search(hashtable_t ht, size_t key) {
  assert(key >= 0);
  size_t index = key % HASHTABLE_SIZE;
  hashtable_node_t* node = ht[index];
  for (; node != NULL; node = node->next) {
    if (node->key == key) {
      return node->data;
    }
  }
  return NULL;
}

void* ht_remove(hashtable_t ht, size_t key) {
  assert(key >= 0);
  size_t index = key % HASHTABLE_SIZE;
  hashtable_node_t* node = ht[index];
  for(; node->next != NULL && node->next->key != key; node = node->next) {
    /* do nothing */
  }
  if (node->next == NULL) {
    return NULL;
  } else { // node->next->key == key
    hashtable_node_t *node_del = node->next;
    node->next = node_del->next;
    void* data = node_del->data;
    free(node_del);
    return data;
  }
}