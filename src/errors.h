/* errors.h
 * Error-handling utils.
 */
#ifndef error_h
#define error_h

#include "defs.h"

/**
 * Constructs error message from errno and logs it.
 */
int error(const str message);

#endif /* error_h */
