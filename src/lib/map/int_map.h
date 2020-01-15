#ifndef INT_MAP_H
#define INT_MAP_H

#define MAP_DEFAULT_LENGTH 32
#define MAP_HASH_KEY(__map__, __key__) ({           \
  ssize_t hs = 5381; int i = 0;                     \
  while (key[i]) hs = ((hs << 5) + hs) + key[i++];  \
  hs % __map__->num_slots;                          \
})

DeclareClass (intmap);

public Class (intmap) ImapClass;
#define Imap ImapClass.self

NewType (mint,
  char *key;
  int   val;
  mint_t *next;
);

NewType (intmap,
  mint_t **slots;
  size_t
    num_slots,
    num_keys;
);

NewSelf (intmap,
  void
    (*free) (intmap_t *),
    (*clear) (intmap_t *);

  intmap_t *(*new) (int);

  int
    (*get) (intmap_t *, char *),
    (*key_exists) (intmap_t *, char *);

  uint
    (*set) (intmap_t *, char *, int),
    (*set_with_keylen) (intmap_t *, char *);
);

NewClass (intmap,
  Self (intmap) self;
);

public Class (intmap) __init_int_map__ (void);
public void __deinit_int_map__ (Class (intmap) *);

#endif
