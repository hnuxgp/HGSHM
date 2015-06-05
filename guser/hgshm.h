#ifndef _HGSHM_H
#define _HGSHM_H
void * hgshm_init (char * dev, void (*cb)(void *), void *cb_arg);
void hgshm_close();
int hgshm_notify(int);
int hgshm_get_index(void);
void * hgshm_getshm(int index, size_t *sz);
size_t hgshm_get_shm_slice_sz(void);
#endif /* _HGSHM_H */
