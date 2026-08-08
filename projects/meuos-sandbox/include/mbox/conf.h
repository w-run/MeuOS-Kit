#ifndef MBOX_CONF_H
#define MBOX_CONF_H

#define MBOX_ARCH_LEN 32
#define MBOX_NET_LEN 32
#define MBOX_MAX_ENV 64
#define MBOX_ENV_KEY_LEN 64
#define MBOX_ENV_VAL_LEN 128
#define MBOX_MAX_SHARE 8
#define MBOX_SHARE_PATH_LEN 256

typedef struct {
    char arch[MBOX_ARCH_LEN];        /* --aarch64 / --x86_64 etc */
    char net[MBOX_NET_LEN];          /* "user" / "none" / "" */
    int timeout;                     /* seconds, -1=unset */
    int mcp_port;                    /* 0=disabled */
    int webpty_port;                 /* 0=disabled */
    int webpty_readonly;

    /* env vars from conf */
    char env_keys[MBOX_MAX_ENV][MBOX_ENV_KEY_LEN];
    char env_vals[MBOX_MAX_ENV][MBOX_ENV_VAL_LEN];
    int env_count;

    /* share dirs from conf */
    char share_host[MBOX_MAX_SHARE][MBOX_SHARE_PATH_LEN];
    char share_guest[MBOX_MAX_SHARE][MBOX_SHARE_PATH_LEN];
    int share_count;

    /* cdrom + usb from conf */
    char cdrom[MBOX_SHARE_PATH_LEN];
    int  usb;
} mbox_config;

void conf_init(mbox_config *cfg);
int  conf_load(const char *path, mbox_config *cfg);

#endif /* MBOX_CONF_H */