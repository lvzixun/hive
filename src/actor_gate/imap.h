#ifndef _IMAP_H_
#define _IMAP_H_


struct imap_context;


struct imap_context* imap_create();
void imap_free(struct imap_context* imap);

// the value is no-null point
void imap_set(struct imap_context* imap, int key, void* value);

void* imap_remove(struct imap_context* imap, int key);
void* imap_query(struct imap_context* imap, int key);

typedef void(*observer)(int key, void* value);
void imap_dump(struct imap_context* imap, observer observer_cb);

#endif
