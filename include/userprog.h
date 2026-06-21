#ifndef MINIOS_USERPROG_H
#define MINIOS_USERPROG_H

#define USERPROG_MAX_ARGC 8
#define USERPROG_PATH_LEN 128
#define USERPROG_DESC_LEN 128
#define USERPROG_NAME_LEN 32

void userprog_init(void);
void userprog_list(void);

int userprog_exec(const char *path, int argc, char **argv);
int userprog_exec_cmd(char *cmdline);

void userprog_apptest(void);

#endif
