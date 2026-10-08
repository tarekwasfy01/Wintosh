#ifndef __WINE_THUNKS_H
#define __WINE_THUNKS_H

#if defined(__APPLE__) && defined(__LP64__)
extern void __wine_thunk_to_windows(void);
extern void __wine_thunk_to_host(void);
#else
#define __wine_thunk_to_windows()
#define __wine_thunk_to_host()
#endif

#endif
