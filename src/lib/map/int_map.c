#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <libved.h>
#include <libved+.h>
#include "../lib+.h"

private void int_map_free_slot (mint_t *item) {
  while (item) {
    mint_t *tmp = item->next;
    free (item->key);
    free (item);
    item = tmp;
  }
}

private void int_map_clear (intmap_t *map) {
  for (size_t i = 0; i < map->num_slots; i++)
    int_map_free_slot (map->slots[i]);

  map->num_keys = 0;
}

private void int_map_free (intmap_t *map) {
  if (map is NULL) return;
  for (size_t i = 0; i < map->num_slots; i++)
    int_map_free_slot (map->slots[i]);

  free (map->slots);
  free (map);
}

private intmap_t *int_map_new (int num_slots) {
  intmap_t *imap = AllocType (intmap);

  if (1 > num_slots) num_slots = MAP_DEFAULT_LENGTH;

  imap->slots = Alloc (sizeof (mint_t *) * num_slots);
  imap->num_slots = num_slots;
  imap->num_keys = 0;
  for (;--num_slots >= 0;) imap->slots[num_slots] = 0;
  return imap;
}

private mint_t *__int_map_get__ (intmap_t *imap, char *key, uint idx) {
  mint_t *slot = imap->slots[idx];
  while (slot) {
    if (Cstring.eq (slot->key, key)) return slot;
    slot = slot->next;
  }
  return NULL;
}

private int int_map_get (intmap_t *imap, char *key) {
  uint idx = MAP_HASH_KEY (imap, key);
  mint_t *im = __int_map_get__ (imap, key, idx);
  ifnot (NULL is im) return im->val;
  return 0;
}

private uint int_map_set (intmap_t *imap, char *key, int val) {
  uint idx = MAP_HASH_KEY (imap, key);
  mint_t *item = __int_map_get__ (imap, key, idx);
  ifnot (NULL is item) {
    item->val = val;
    return idx;
  } else {
    item = AllocType (mint);
    item->key = Cstring.dup (key, bytelen (key));
    item->val = val;
    item->next = imap->slots[idx];

    imap->slots[idx] = item;
    imap->num_keys++;
  }
  return idx;
}

private uint int_map_set_with_keylen (intmap_t *imap, char *key) {
  uint idx = MAP_HASH_KEY (imap, key);
  mint_t *item = __int_map_get__ (imap, key, idx);
  if (NULL is item) {
    item = AllocType (mint);
    size_t len = bytelen (key);
    item->key = Cstring.dup (key, len);
    item->val = len;
    item->next = imap->slots[idx];

    imap->slots[idx] = item;
    imap->num_keys++;
  } else
    item->val = bytelen (key);

  return idx;
}

private int int_map_key_exists (intmap_t *imap, char *key) {
  uint idx = MAP_HASH_KEY (imap, key);
  mint_t *item = __int_map_get__ (imap, key, idx);
  return (NULL isnot item);
}

public Class (intmap) __init_int_map__ (void) {
  return ClassInit (intmap,
    .self = SelfInit (intmap,
      .new = int_map_new,
      .free = int_map_free,
      .clear = int_map_clear,
      .get = int_map_get,
      .set = int_map_set,
      .set_with_keylen = int_map_set_with_keylen,
      .key_exists = int_map_key_exists
    )
  );
}

public void __deinit_int_map__ (Class (intmap) *this) {
  (void) this;
}
#undef GET_KEY
