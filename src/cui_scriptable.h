#ifndef CUI_SCRIPTABLE_H
#define CUI_SCRIPTABLE_H

#include "cui_globals.h"

typedef struct scriptableKeyValue_s {
    struct scriptableKeyValue_s *next;
    char *key;
    char *value;
} scriptableKeyValue_t;

// HAND-MAINTAINED ABI MIRROR — do not reorder, resize, or extend casually.
// DeaDBeeF does not export the scriptable API to plugins, so this struct
// byte-mirrors the private scriptableItem_s layout from
// .deadbeef/shared/scriptable/scriptable.c (field-for-field, verified).
// The medialib plugin casts our pointer to ITS scriptableItem_t and reads
// these fields by offset: an upstream field reorder that we do not mirror is
// silent memory corruption, not a compile error. If the .deadbeef/ clone is
// ever bumped, re-diff this layout against scriptable.c first (the command is
// in CLAUDE.md §6.2). Only flags/properties/children are written here; calloc
// zeroes the rest.
typedef struct scriptableItem_s {
    struct scriptableItem_s *next;
    uint64_t flags;
    scriptableKeyValue_t *properties;
    struct scriptableItem_s *parent;
    struct scriptableItem_s *children;
    struct scriptableItem_s *childrenTail;
    char *type;
    char *configDialog;
    void *overrides;
} scriptableItem_t;

#define SCRIPTABLE_FLAG_IS_LIST (1 << 2)

scriptableItem_t *my_scriptable_alloc(void);
void my_scriptable_free(scriptableItem_t *item);
void my_scriptable_set_prop(scriptableItem_t *item, const char *key, const char *value);
void my_scriptable_add_child(scriptableItem_t *parent, scriptableItem_t *child);

void init_my_preset(cui_widget_t *cw);

#endif // CUI_SCRIPTABLE_H
