#include <errno.h>     // errno
#include <error.h>     // error TODO gnu extension, replace with portable code
#include <stdbool.h>   // true
#include <stdio.h>     // stdin, stderr, getline
#include <stdlib.h>    // free, setenv, unsetenv, getenv
#include <string.h>    // strerror, strcmp, strdup, strsep, strlen, strsignal
#include <sys/types.h> // pid_t
#include <sys/wait.h>  // waitpid
#include <unistd.h>    // fork, execvp, getcwd
#include <wordexp.h>   //

#define MAX_PATH_AT_PROMPT 256

size_t strlcpy(char *dst, char const *src, size_t size) {
    size_t src_len = strlen(src);
    if (size != 0) {
        while (*src != '\0' && size > 1) {
            *dst++ = *src++;
            --size;
        }
        *dst = '\0';
    }
    return src_len;
}

void prompt(void) {
    char cwd[MAX_PATH_AT_PROMPT];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        if (errno == ERANGE) {
            strlcpy(cwd, "!Path too long to be shown!", sizeof(cwd));
        } else {
            error(0, errno, "Couldn't get cwd");
            strlcpy(cwd, "!Failed to get CWD!", sizeof(cwd));
        }
    }
    printf("┌[%s]\n", cwd);
    printf("└─> ");
    fflush(stdout);
}

void do_export(char *const argv[]) {
    for (; *argv != NULL; ++argv) {
        char *to_free, *value;
        value = to_free = strdup(*argv);
        if (!to_free) {
            error(EXIT_FAILURE, errno, "Couldn't copy a string");
        }
        char *key = strsep(&value, "=");
        if (value) {
            if (setenv(key, value, true) == -1) {
                error(0, errno, "Couldn't set %s", *argv);
            }
        }
        free(to_free);
    }
}

void do_unset(char *const argv[]) {
    for (; *argv != NULL; ++argv) {
        if (unsetenv(*argv) == -1) {
            error(0, errno, "Couldn't unset %s", *argv);
        }
    }
}

void do_cd(char *const argv[]) {
    char *dir;
    if (*argv == NULL) {
        dir = getenv("HOME");
        if (!dir) {
            error(0, 0, "HOME not set");
            return;
        }
    } else {
        dir = *argv++;
    }
    if (*argv != NULL) {
        error(0, 0, "Too many arguments");
        return;
    }
    if (chdir(dir) == -1) {
        error(0, errno, "Couldn't cd to %s", dir);
    }
}

void launch(char const *program, char *const argv[]) {
    pid_t pid = fork();
    if (pid == -1) {
        error(0, errno, "Failed to fork");
        return;
    }
    if (pid == 0) {
        int ec = execvp(program, argv);
        if (ec == -1) {
            error(EXIT_FAILURE, errno, "Failed to exec %s", program);
            return;
        }
    } else {
        int wstatus;
        if (waitpid(pid, &wstatus, 0) == -1) {
            error(0, errno, "Failed to wait for child %d", pid);
            return;
        }
        if (WIFEXITED(wstatus)) {
            printf("Process exited with error code %d\n", WEXITSTATUS(wstatus));
        } else if (WIFSIGNALED(wstatus)) {
            printf("Process was terminated by signal %d: %s\n", WTERMSIG(wstatus), strsignal(WTERMSIG(wstatus)));
        } else {
            error(EXIT_FAILURE, 0, "waitpid should have waited for the process termination, but it didn't");
        }
    }
}

void trim_newline(char *line, ssize_t *len) {
    if (*len <= 0) {
        return;
    }
    if (line[*len - 1] == '\n') {
        line[--*len] = '\0';
    }
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    wordexp_t p;

    // init wordexp for future reuse
    wordexp("", &p, 0);

    while (true) {
        prompt();
        if ((nread = getline(&line, &len, stdin)) == -1) {
            printf("\n"); // so that next command started on the newline
            break;
        }
        trim_newline(line, &nread);
        int ec = wordexp(line, &p, WRDE_REUSE | WRDE_SHOWERR | WRDE_UNDEF);
        switch (ec) {
        case 0: // success
            break;
        case WRDE_BADCHAR:
            error(0, 0, "Illegal occurrence of newline or one of |, &, ;, <, >, (, ), {, }.");
            continue;
        case WRDE_BADVAL:
            error(0, 0, "Undefined shell variable was referenced");
            continue;
        case WRDE_CMDSUB:
            error(0, 0, "Command line substitution is prohibited");
            continue;
        case WRDE_NOSPACE:
            error(0, 0, "Out of memory");
            continue;
        case WRDE_SYNTAX:
            error(0, 0, "Syntax error: unbalanced parentheses, unmatched quotes etc");
            continue;
        default:
            error(EXIT_FAILURE, 0, "Unexpected wordexp error code: %d", ec);
        }
        if (p.we_wordc == 0) {
            continue;
        }
        char const *action = p.we_wordv[0];
        if (strcmp("export", action) == 0) {
            do_export(p.we_wordv + 1);
        } else if (strcmp("unset", action) == 0) {
            do_unset(p.we_wordv + 1);
        } else if (strcmp("exit", action) == 0) {
            break;
        } else if (strcmp("cd", action) == 0) {
            do_cd(p.we_wordv + 1);
        } else {
            launch(p.we_wordv[0], p.we_wordv);
        }
    }
    if (ferror(stdin)) {
        error(EXIT_FAILURE, errno, "Failed to read next line");
    }
    wordfree(&p);
    free(line);
}
