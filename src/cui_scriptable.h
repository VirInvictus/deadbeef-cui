#ifndef CUI_SCRIPTABLE_H
#define CUI_SCRIPTABLE_H

#include "cui_globals.h"

typedef struct scriptableKeyValue_s {
    struct scriptableKeyValue_s *next;
    char *key;
    char *value;
} scriptableKeyValue_t;

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
