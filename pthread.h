#ifndef _PTHREAD_H
#define _PTHREAD_H    1

//////////////////////////////////////////////////////////
// a mocjed pthread.h file for MSVC to use:
//

typedef int pthread_t;

inline pthread_t pthread_self(void) {
  return 0;
}

#endif // _PTHREAD_H
