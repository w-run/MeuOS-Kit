#ifndef MEUOS_SPAWN_H
#define MEUOS_SPAWN_H

#include <sys/types.h>
#include <sched.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* posix_spawnattr flags (POSIX.1-2008). */
#define POSIX_SPAWN_RESETIDS   0x01 /* reset uid/gid to effective */
#define POSIX_SPAWN_SETPGROUP  0x02 /* set the pgroup (__pgrp) */
#define POSIX_SPAWN_SETSIGDEF  0x04 /* reset sigs in __sd to default */
#define POSIX_SPAWN_SETSIGMASK 0x08 /* set sigmask to __ss */

typedef struct {
	short __flags;
	pid_t __pgrp;
	sigset_t __sd;
	sigset_t __ss;
	struct sched_param __sp;
	int __policy;
	int __pad[16];
} posix_spawnattr_t;

typedef struct {
	int __allocated;
	int __used;
	void *__actions;
	int __pad[16];
} posix_spawn_file_actions_t;

int posix_spawn(pid_t *, const char *, const posix_spawn_file_actions_t *,
    const posix_spawnattr_t *, char *const[], char *const[]);
int posix_spawnp(pid_t *, const char *, const posix_spawn_file_actions_t *,
    const posix_spawnattr_t *, char *const[], char *const[]);
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *, int,
    const char *, int, mode_t);
int posix_spawnattr_init(posix_spawnattr_t *);
int posix_spawnattr_destroy(posix_spawnattr_t *);
int posix_spawnattr_setflags(posix_spawnattr_t *, short);
int posix_spawnattr_getflags(const posix_spawnattr_t *, short *);

#ifdef __cplusplus
}
#endif

#endif
