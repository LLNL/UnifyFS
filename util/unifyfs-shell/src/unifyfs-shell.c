/*
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2017, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mpi.h>
#include <unifyfs.h>

#include "unifyfs-shell.h"

static unifyfs_shell_cmd_t* commands[] = {
    &unifyfs_shell_cmd_status,
    &unifyfs_shell_cmd_ls,
    &unifyfs_shell_cmd_mkdir,
    &unifyfs_shell_cmd_cd,
    &unifyfs_shell_cmd_pwd,
    &unifyfs_shell_cmd_stat,
};

static unifyfs_shell_env_t* env;

static int target;
static char cmdbuf[LINE_MAX];

#define UNIFYFS_SHELL_MAX_ARGC  16

static int shell_argc;
static char* shell_argv[UNIFYFS_SHELL_MAX_ARGC];

static int parse_input(char* input)
{
    int ret = 0;
    char* pos = input;

    shell_argc = 0;

     while (1) {
        if (shell_argc >= UNIFYFS_SHELL_MAX_ARGC) {
            ret = -E2BIG;
            goto fin;
        }

        while (isspace(pos[0])) {
            pos++;
        }

        shell_argv[shell_argc++] = pos;

        while (pos[0] != '\0' && !isspace(pos[0])) {
            pos++;
        }

        if (pos[0] == '\0') {
            break;
        }

        pos[0] = '\0';
        pos++;
    }

fin:
     return ret;
}

static inline unifyfs_shell_cmd_t* find_command(const char* str)
{
    int i = 0;
    int n_commands = sizeof(commands)/sizeof(unifyfs_shell_cmd_t*);
    unifyfs_shell_cmd_t* cmd = NULL;

    for (i = 0; i < n_commands; i++) {
        if (0 == strcmp(commands[i]->name, str)) {
            cmd = commands[i];
            break;
        }
    }

    return cmd;
}

static int execute_command(char* input, FILE* out)
{
    int ret = 0;
    unifyfs_shell_cmd_t* cmd = NULL;

    ret = parse_input(input);
    if (ret < 0) {
        fprintf(out, "too many arguments\n");
        goto fin;
    }

    env->output = out;
    optind = 1;  /* for possible getopt */

    cmd = find_command(shell_argv[0]);
    if (cmd) {
        ret = cmd->func(shell_argc, shell_argv, env);
    } else {
        fprintf(out, "Invalid command: %s\n", input);
    }

fin:
    fputc('\0', out);
    fflush(out);

    return ret;
}

static inline char* strtrim(char* str)
{
    char* pos = str;
    char* rpos = &str[strlen(str) - 1];

    if (pos[0] == '\0') {
        return NULL;
    }

    while (isspace(pos[0])) {
        pos++;

        if (pos[0] == '\0') {
            return NULL;
        }
    }

    while (isspace(rpos[0]) && pos < rpos) {
        rpos[0] = '\0';
        rpos--;
    }

    return pos;
}

static char* shell_prompt(void)
{
    char* input = NULL;

    do {
        printf("[%d@unifyfs-shell]: ", target);
        fflush(stdout);

        input = strtrim(fgets(cmdbuf, LINE_MAX-1, stdin));
    } while (!input);

    return input;
}

static inline void unifyfs_shell_help(void)
{
    int i = 0;
    int n_commands = sizeof(commands)/sizeof(unifyfs_shell_cmd_t*);

    printf(
      "Builtin commands:\n"
      "  help           this help message\n"
      "  target [RANK]  print/select the target rank to execute commands\n"
      "  exit           exit the shell\n"
      "\n"
      "Available commands (try --help for details):\n  ");

    for (i = 0; i < n_commands; i++) {
        printf("%s%s", commands[i]->name,
                       i == n_commands - 1 ? "\n" : ", ");
    }
}

static int execute_builtin_commands(char* input)
{
    int ret = 1;

    if (strncmp("exit", input, strlen("exit")) == 0) {
        target = -1;
    } else if (strncmp("target", input, strlen("target")) == 0) {
        char* pos = strtrim(&input[strlen("target")]);
        int newtarget = -1;

        if (pos) {
            newtarget = atoi(pos);
            if (newtarget < 0 || newtarget >= env->total_ranks) {
                printf("Target rank should be between 0 and %d\n",
                       env->total_ranks);
            } else {
                target = newtarget;
            }
        }

        printf("current target rank = %d\n", target);
    } else if (strncmp("help", input, strlen("help")) == 0) {
        unifyfs_shell_help();
    } else {
        ret = 0;
    }

    return ret;
}

int unifyfs_shell(unifyfs_shell_env_t* e)
{
    int ret = 0;
    char* input = NULL;
    int count = 0;
    MPI_Status status = { 0, };

    if (!e || !e->mountpoint) {
        return -EINVAL;
    }

    env = e;

    while (1) {
        if (0 == env->rank) {
            do {
                input = shell_prompt();

                ret = execute_builtin_commands(input);
                if (ret) {
                    if (target >= 0) {
                        input = NULL;
                    }
                } else if (target == 0) { /* local execution on rank 0 */
                    ret = execute_command(input, stdout);
                    input = NULL;
                }
            } while (!input);
        }

        MPI_Bcast(&target, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (target < 0) { /* quit the shell */
            goto fin;
        }

        if (env->rank == 0) {
            char* outbuf = NULL;
            unsigned long len = strlen(input) + 1;

            MPI_Send(input, len, MPI_CHAR, target, 0, MPI_COMM_WORLD);

            MPI_Probe(target, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_CHAR, &count);

            outbuf = realloc(outbuf, count);
            if (!outbuf) {
                perror("failed to allocate memory");
                ret = -errno;
                goto fin;
            }

            outbuf[count-1] = '\0';
            MPI_Recv(outbuf, count, MPI_CHAR, target, 0,
                     MPI_COMM_WORLD, &status);

            printf("%s", outbuf);
        } else if (env->rank == target) {
            FILE* out = NULL;
            char* buf = NULL;
            size_t size = 0;

            MPI_Probe(0, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_CHAR, &count);
            MPI_Recv(cmdbuf, count, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

            out = open_memstream(&buf, &size);

            ret = execute_command(cmdbuf, out);

            MPI_Send(buf, size, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

            fclose(out);
        }

        MPI_Barrier(MPI_COMM_WORLD);
    }

fin:
    return ret;
}

