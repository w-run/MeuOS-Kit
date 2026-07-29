#ifndef MEUOS_SPAWN_H
#define MEUOS_SPAWN_H

#include <sys/types.h>
#include <sched.h>

#ifdef __cplusplus
extern "C" {
#endif

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
