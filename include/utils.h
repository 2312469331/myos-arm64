#ifndef __UTILS_H__
#define __UTILS_H__

#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))

#define container_of(ptr, type, member)                                        \
  ((type *)((char *)(ptr) - offsetof(type, member)))

#endif
