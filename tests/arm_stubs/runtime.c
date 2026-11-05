// Single-threaded bare-metal test support for toolchains using newlib locks.
// No firmware target links this file.
#include <sys/lock.h>
void __retarget_lock_init(_LOCK_T lock) { (void)lock; }
void __retarget_lock_init_recursive(_LOCK_T lock) { (void)lock; }
void __retarget_lock_close(_LOCK_T lock) { (void)lock; }
void __retarget_lock_close_recursive(_LOCK_T lock) { (void)lock; }
void __retarget_lock_acquire(_LOCK_T lock) { (void)lock; }
void __retarget_lock_acquire_recursive(_LOCK_T lock) { (void)lock; }
void __retarget_lock_release(_LOCK_T lock) { (void)lock; }
void __retarget_lock_release_recursive(_LOCK_T lock) { (void)lock; }
int __lock___sfp_recursive_mutex, __lock___malloc_recursive_mutex;
