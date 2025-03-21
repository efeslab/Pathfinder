#include <libpmemobj.h>
#include <libpmem.h>

typedef struct driver_root {
  int a;
  int b;
  char _unused;
} driver_root_t;

POBJ_LAYOUT_BEGIN(driver);
POBJ_LAYOUT_ROOT(driver, driver_root_t);
POBJ_LAYOUT_END(driver);