
#ifdef _MSC_VER
#include <folly/CPortability.h>

extern "C" {

// Do nothing for the system log for now.
// Do nothing for the system log for now.
void openlog(const char*, int, int) {}
void closelog() {}
void syslog(int, const char*, ...) {}

void __stdcall __alarm_callback_func(HWND, UINT, UINT_PTR, DWORD) {
  raise(SIGALRM);
}

unsigned int alarm(unsigned int seconds) {
  SetTimer(NULL, 0, (UINT)(seconds * 1000), __alarm_callback_func);
  return 0;
}

char* asctime_r(const tm* tm, char* buf) {
  char tmpBuf[64];
  if (asctime_s(tmpBuf, sizeof(tmpBuf), tm))
    return NULL;
  return strcpy(buf, tmpBuf);
}

void bzero(void* s, size_t n) {
  ZeroMemory(s, n);
}

int closedir(DIR* dir) {
  if (!FindClose(dir->hand))
    return -1;
  free(dir);
  return 0;
}

char* ctime_r(const time_t* t, char* buf) {
  char tmpBuf[64];
  if (ctime_s(tmpBuf, sizeof(tmpBuf), t))
    return NULL;
  return strcpy(buf, tmpBuf);
}

char* dirname(char* path) {
  if (path == NULL || !strcmp(path, ""))
    return ".";

  size_t len = strlen(path);
  char* pos = strrchr(path, '/');
  if (strrchr(path, '\\') > pos)
    pos = strrchr(path, '\\');
  if (pos == NULL)
    return ".";

  // Final slash with no name.
  if (path + len == pos) {
    *pos = '\0';

    char* pos2 = strrchr(path, '/');
    if (strrchr(path, '\\') > pos2)
      pos2 = strrchr(path, '\\');
    if (pos2 == NULL)
      return ".";

    *pos2 = '\0';
    return path;
  }

  *pos = '\0';
  return path;
}

int dprintf(
  int fd,
  _Printf_format_string_ const char *fmt,
  ...
  ) {
  va_list args;
  va_start(args, fmt);

  int len = vsnprintf(NULL, 0, fmt, args);
  if (len <= 0)
    return -1;
  char* buf = (char*)malloc(len + 1);
  if (vsnprintf(buf, len + 1, fmt, args) == len &&
    _write(fd, buf, len) == len) {
    free(buf);
    va_end(args);
    return len;
  }

  free(buf);
  va_end(args);
  return -1;
}

int finite(double d) {
  return isfinite(d) ? 1 : 0;
}

int getgid() {
  return 1;
}

// No major need to implement this, and getting a non-potentially
// stale ID on windows is a bit involved.
pid_t getppid() {
  return (pid_t)1;
}

int getrlimit(int type, rlimit* dst) {
  if (type == RLIMIT_STACK) {
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    dst->rlim_max = dst->rlim_cur = (size_t)tib->StackBase - (size_t)tib->StackLimit;
    return 0;
  }
  return -1;
}

int getrusage(int who, rusage* usage) {
  ZeroMemory(usage, sizeof(rusage));
  return 0;
}

int gettimeofday(timeval* tv, timezone*) {
  FILETIME ft;
  if (tv) {
    GetSystemTimeAsFileTime(&ft);
    uint64_t ns = *(uint64_t*)&ft;
    tv->tv_usec = (long)((ns / 10ULL) % 1000000ULL);
    tv->tv_sec = (long)((ns - POSIX_WIN_FT_OFFSET) / 10000000ULL);
  }
  return 0;
}

int getuid() {
  return 1;
}

tm* gmtime_r(const time_t* t, tm* res) {
  if (!gmtime_s(res, t))
    return res;
  return NULL;
}

int kill(pid_t p, int sig) {
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)p);
  if (!TerminateProcess(h, (UINT)-1))
    return -1;
  return 0;
}

tm* localtime_r(const time_t* t, tm* o) {
  tm* tmp = localtime(t);
  if (tmp) {
    *o = *tmp;
    return o;
  }
  return NULL;
}

int madvise(const void* addr, size_t len, int advise) {
  // We do nothing at all.
  // Could probably implement dontneed via VirtualAlloc
  // with the MEM_RESET and MEM_RESET_UNDO flags. 
  return 0;
}

size_t malloc_usable_size(void* addr) {
  return _msize(addr);
}

void* memmem(const void* haystack, size_t hlen, const void* needle, size_t nlen) {
  int needle_first;
  const char* p = (const char*)haystack;
  size_t plen = hlen;

  if (!nlen)
    return NULL;

  needle_first = *(unsigned char*)needle;

  while (plen >= nlen && (p = (const char*)memchr(p, needle_first, plen - nlen + 1))) {
    if (!memcmp(p, needle, nlen))
      return (void *)p;

    p++;
    plen = hlen - (p - (const char*)haystack);
  }

  return NULL;
}

void* memrchr(const void* s, int c, size_t n) {
  const unsigned char* p = ((const unsigned char*)s) + n;
  while (p >= (const unsigned char*)s) {
    if (*p == (unsigned char)c) {
      return (void*)p;
    }
    p--;
  }
  return NULL;
}

namespace {
int mkdir(const char* fn, int mode) {
  return _mkdir(fn);
}
}

int mlock(const void* addr, size_t len) {
  if (!VirtualLock((void*)addr, len))
    return -1;
  return 0;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
  // Make sure it's something we support first.

  // Might be reasonable to ignore addr entirely.
  if (addr != NULL)
    return MAP_FAILED;
  // No Anon shared.
  if ((flags & (MAP_ANONYMOUS | MAP_SHARED)) == (MAP_ANONYMOUS | MAP_SHARED))
    return MAP_FAILED;
  // No private copy on write.
  if ((flags & MAP_PRIVATE) == MAP_PRIVATE && fd != -1)
    return MAP_FAILED;

  // Map isn't anon, must be file backed.
  if (!(flags & MAP_ANONYMOUS) && fd == -1)
    return MAP_FAILED;

  DWORD newProt;
  if (prot == PROT_NONE)
    newProt = PAGE_NOACCESS;
  else if (prot == PROT_READ)
    newProt = PAGE_READONLY;
  else if (prot == PROT_EXEC)
    newProt = PAGE_EXECUTE;
  else if (prot == (PROT_READ | PROT_EXEC))
    newProt = PAGE_EXECUTE_READ;
  else if (prot == (PROT_READ | PROT_WRITE))
    newProt = PAGE_READWRITE;
  else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC))
    newProt = PAGE_EXECUTE_READWRITE;
  else
    return MAP_FAILED;

  void* ret;
  if (!(flags & MAP_ANONYMOUS) || (flags & MAP_SHARED)) {
    HANDLE h = INVALID_HANDLE_VALUE;
    if (!(flags & MAP_ANONYMOUS))
      h = (HANDLE)_get_osfhandle(fd);

    HANDLE fmh = CreateFileMapping(h, nullptr, newProt | SEC_COMMIT | SEC_RESERVE,
      (DWORD)((length >> 32) & 0xFFFFFFFF), (DWORD)(length & 0xFFFFFFFF), nullptr);
    // Depending on specifics, off_t may be 32-bit, so get MSVC to be quiet about
    // it.
#pragma warning(push)
#pragma warning(disable: 4293)
    ret = MapViewOfFileEx(fmh, FILE_MAP_ALL_ACCESS,
      (DWORD)((offset >> 32) & 0xFFFFFFFF), (DWORD)(offset & 0xFFFFFFFF), 0, addr);
#pragma warning(pop)
    CloseHandle(fmh);
  }
  else {
    ret = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, newProt);
    if (ret == NULL)
      return MAP_FAILED;
  }

  // TODO: Could technically implement MAP_POPULATE via PrefetchVirtualMemory
  //       Should also see about implementing MAP_NORESERVE
  return ret;
}

int mprotect(void* addr, size_t size, int prot) {
  DWORD newProt;
  if (prot == PROT_NONE)
    newProt = PAGE_NOACCESS;
  else if (prot == PROT_READ)
    newProt = PAGE_READONLY;
  else if (prot == PROT_EXEC)
    newProt = PAGE_EXECUTE;
  else if (prot == (PROT_READ | PROT_EXEC))
    newProt = PAGE_EXECUTE_READ;
  else if (prot == (PROT_READ | PROT_WRITE))
    newProt = PAGE_READWRITE;
  else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC))
    newProt = PAGE_EXECUTE_READWRITE;
  else
    return -1;

  DWORD oldProt;
  BOOL res = VirtualProtect(addr, size, newProt, &oldProt);
  if (!res)
    return -1;
  return 0;
}

int munlock(const void* addr, size_t length) {
  if (!VirtualUnlock((void*)addr, length))
    return -1;
  return 0;
}

int munmap(void* addr, size_t length) {
  // Try to unmap it as a file, otherwise VirtualFree.
  if (!UnmapViewOfFile(addr)) {
    if (!VirtualFree(addr, length, MEM_RELEASE))
      return -1;
    return 0;
  }
  return 0;
}

int nanosleep(const struct timespec* request, struct timespec* remain) {
  Sleep((DWORD)(request->tv_sec * 1000));
  Sleep((DWORD)(request->tv_nsec / 1000000));
  remain->tv_nsec = 0;
  remain->tv_sec = 0;
  return 0;
}

DIR* opendir(const char* name) {
  wchar_t pattern[MAX_PATH + 2];
  size_t len;

  if (mbstowcs_s(&len, pattern, MAX_PATH, name, MAX_PATH - 2))
    return NULL;

  if (len && pattern[len - 1] != '/' && pattern[len - 1] != '\\')
    pattern[len++] = '\\';
  pattern[len++] = '*';
  pattern[len] = 0;

  WIN32_FIND_DATAW fdata;
  HANDLE h = FindFirstFileW(pattern, &fdata);
  if (h == INVALID_HANDLE_VALUE)
    return NULL;

  DIR* dir = (DIR*)malloc(sizeof(DIR));
  dir->dir.d_name = dir->name;
  dir->hand = h;
  dir->cnt = 0;
  wcstombs(dir->name, fdata.cFileName, MAX_PATH * 3);

  if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    dir->dir.d_type = DT_DIR;
  else
    dir->dir.d_type = DT_REG;

  return (DIR*)dir;
}

int pclose(FILE* f) {
  return _pclose(f);
}

FILE* popen(const char* name, const char* mode) {
  return _popen(name, mode);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
  void* ret = _aligned_malloc(size, alignment);
  if (ret == nullptr)
    return -1;
  *memptr = ret;
  return 0;
}

dirent* readdir(DIR* dir) {
  if (dir->cnt) {
    WIN32_FIND_DATAW fdata;
    if (!FindNextFileW(dir->hand, &fdata))
      return NULL;

    wcstombs(dir->name, fdata.cFileName, MAX_PATH * 3);
    if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      dir->dir.d_type = DT_DIR;
    else
      dir->dir.d_type = DT_REG;
  }

  dir->cnt++;
  return &dir->dir;
}

int readdir_r(DIR* dir, dirent* buf, dirent** ent) {
  if (dir->cnt) {
    WIN32_FIND_DATAW fdata;
    if (!FindNextFileW(dir->hand, &fdata)) {
      *ent = NULL;
      return 0;
    }

    wcstombs(dir->name, fdata.cFileName, MAX_PATH * 3);
    if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      dir->dir.d_type = DT_DIR;
    else
      dir->dir.d_type = DT_REG;
  }

  dir->cnt++;
  memcpy(buf, &dir->dir, sizeof(dirent));
  *ent = buf;
  return 0;
}

void rewinddir(DIR* dir) {
  FindClose(dir->hand);

  WIN32_FIND_DATA fdata;
  HANDLE h = FindFirstFile(dir->dir.d_name, &fdata);
  dir->hand = h;
  dir->cnt = 0;
  strcpy_s(dir->name, MAX_PATH * 3, fdata.cFileName);

  if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    dir->dir.d_type = DT_DIR;
  else
    dir->dir.d_type = DT_REG;
}

void* sbrk(intptr_t i) {
  return (void*)-1;
}

void setbuffer(FILE* f, char* buf, size_t size) {
  setvbuf(f, buf, _IOFBF, size);
}

int setrlimit(int type, rlimit* src) {
  // Do nothing for setting them for now
  return 0;
}

unsigned int sleep(unsigned int seconds) {
  Sleep((DWORD)(seconds * 1000));
  return 0;
}

int strcasecmp(const char* a, const char* b) {
  return _stricmp(a, b);
}

char* strcasestr(const char* a, const char* b) {
  char* a2 = _strlwr(strdup(a));
  char* b2 = _strlwr(strdup(b));
  char* tmp = strstr(a2, b2);
  if (!tmp) {
    free(a2);
    free(b2);
    return NULL;
  }
  char* ret = (char*)a + (tmp - a2);
  free(a2);
  free(b2);
  return ret;
}

int strncasecmp(const char* a, const char* b, size_t c) {
  return _strnicmp(a, b, c);
}

char* strndup(const char* a, size_t len) {
  char* buf = (char*)calloc(len + 1, sizeof(char));
  strncpy(buf, a, len);
  return buf;
}

const char* strsignal(int signal) {
#define SIG_CASE(sig) case sig: return "sig"
  switch (signal) {
    SIG_CASE(SIGINT);
    SIG_CASE(SIGILL);
    SIG_CASE(SIGABRT);
    SIG_CASE(SIGFPE);
    SIG_CASE(SIGSEGV);
    SIG_CASE(SIGTERM);
    SIG_CASE(SIGCHLD);
    SIG_CASE(SIGVTALRM);
    SIG_CASE(SIGPROF);
    default: return "";
  }
#undef SIG_CASE
}

char* strtok_r(char* str, char const* delim, char** ctx) {
  return strtok_s(str, delim, ctx);
}

pid_t syscall(int num, ...) {
  if (num == SYS_gettid) {
    return (pid_t)GetCurrentThreadId();
  }
  return (pid_t)-1;
}

size_t sysconf(int tp) {
  switch (tp) {
    case _SC_PAGESIZE: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (size_t)inf.dwPageSize;
    }
    case _SC_NPROCESSORS_ONLN: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (size_t)inf.dwNumberOfProcessors;
    }
    default: return (size_t)-1;
  }
}

void timersub(timeval* a, timeval* b, timeval* res) {
  res->tv_sec = a->tv_sec - b->tv_sec;
  res->tv_usec = a->tv_usec - b->tv_usec;
  if (res->tv_usec < 0) {
    res->tv_sec--;
    res->tv_usec += 1000000;
  }
}

int usleep(unsigned int ms) {
  Sleep((DWORD)(ms / 1000));
  return 0;
}

int uname(utsname* buf) {
  buf->sysname = "Windows";
  buf->nodename = "";
  buf->machine = "";
  OSVERSIONINFO verInf;
  verInf.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
#pragma warning(push)
#pragma warning(disable: 4996)
  // GetVersionEx is deprecated, but they provided no
  // replacement for our use-case.
  GetVersionEx(&verInf);
#pragma warning(pop)
  memcpy(buf->_internal_ver, verInf.szCSDVersion, 128);
  buf->release = buf->version = buf->_internal_ver;
  return 0;
}

int vasprintf(char** dest, const char* format, va_list ap) {
  int len = vsnprintf(NULL, 0, format, ap);
  if (len <= 0)
    return -1;
  char* buf = *dest = (char*)malloc(len + 1);
  if (vsnprintf(buf, len + 1, format, ap) == len)
    return len;
  free(buf);
  return -1;
}

}
#endif
