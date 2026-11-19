#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "parser.h"

typedef struct job {
    int id;
    pid_t pgid;
    pid_t last_pid;
    pid_t *pids;
    int count;
    int remaining;
    char *text;
    struct job *next;
} job;

static job *jobs = NULL;
static int next_job_id = 1;
static int interactive = 0;
static pid_t shell_pgid;
static volatile sig_atomic_t interrupted = 0;

static void on_sigint(int signum)
{
    ssize_t written;

    (void)signum;
    interrupted = 1;
    written = write(STDOUT_FILENO, "\n", 1);
    (void)written;
}

static void set_signal(int number, void (*handler)(int))
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(number, &action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void free_job(job *item)
{
    free(item->pids);
    free(item->text);
    free(item);
}

static void remove_job(job *item)
{
    job **cursor;

    cursor = &jobs;
    while (*cursor != NULL && *cursor != item)
        cursor = &(*cursor)->next;
    if (*cursor != NULL)
        *cursor = item->next;
    free_job(item);
}

static void mark_finished(pid_t pid)
{
    job *item;
    int i;

    for (item = jobs; item != NULL; item = item->next) {
        for (i = 0; i < item->count; i++) {
            if (item->pids[i] == pid) {
                item->pids[i] = 0;
                item->remaining--;
                return;
            }
        }
    }
}

static void reap_jobs(void)
{
    pid_t pid;
    int status;
    job *item;
    job *next;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        mark_finished(pid);
    for (item = jobs; item != NULL; item = next) {
        next = item->next;
        if (item->remaining == 0)
            remove_job(item);
    }
}

static void redirect_fd(const char *path, int target, int flags)
{
    int fd;

    if (path == NULL)
        return;
    fd = open(path, flags, 0666);
    if (fd == -1) {
        fprintf(stderr, "%s: Error. %s\n", path, strerror(errno));
        _exit(EXIT_FAILURE);
    }
    if (dup2(fd, target) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }
    close(fd);
}

static void child_command(tline *line, int index, int input_fd,
                          int output_fd, int close_fd, pid_t pgid)
{
    if (setpgid(0, pgid) == -1 && errno != EACCES) {
        perror("setpgid");
        _exit(EXIT_FAILURE);
    }
    set_signal(SIGINT, SIG_DFL);
    set_signal(SIGQUIT, SIG_DFL);
    set_signal(SIGTTOU, SIG_DFL);

    if (input_fd != STDIN_FILENO && dup2(input_fd, STDIN_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }
    if (output_fd != STDOUT_FILENO && dup2(output_fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }
    if (input_fd != STDIN_FILENO)
        close(input_fd);
    if (output_fd != STDOUT_FILENO)
        close(output_fd);
    if (close_fd >= 0)
        close(close_fd);

    if (index == 0)
        redirect_fd(line->redirect_input, STDIN_FILENO, O_RDONLY);
    if (index == line->ncommands - 1) {
        redirect_fd(line->redirect_output, STDOUT_FILENO,
                    O_WRONLY | O_CREAT | O_TRUNC);
        redirect_fd(line->redirect_error, STDERR_FILENO,
                    O_WRONLY | O_CREAT | O_TRUNC);
    }
    if (line->commands[index].filename == NULL) {
        fprintf(stderr, "%s: No se encuentra el mandato\n",
                line->commands[index].argv[0]);
        _exit(127);
    }
    execv(line->commands[index].filename, line->commands[index].argv);
    fprintf(stderr, "%s: Error. %s\n", line->commands[index].argv[0],
            strerror(errno));
    _exit(127);
}

static void wait_for_job(job *item)
{
    pid_t pid;
    int status;

    if (interactive && tcsetpgrp(STDIN_FILENO, item->pgid) == -1)
        perror("tcsetpgrp");
    while (item->remaining > 0) {
        pid = waitpid(-item->pgid, &status, 0);
        if (pid > 0)
            mark_finished(pid);
        else if (pid == -1 && errno == EINTR)
            continue;
        else if (pid == -1 && errno == ECHILD)
            break;
        else {
            perror("waitpid");
            break;
        }
    }
    if (interactive && tcsetpgrp(STDIN_FILENO, shell_pgid) == -1)
        perror("tcsetpgrp");
}

static void run_pipeline(tline *line, const char *text)
{
    job *item;
    job **tail;
    int i;
    int complete;
    int previous;
    int pipe_fd[2];
    pid_t pid;

    item = calloc(1, sizeof(*item));
    if (item == NULL) {
        perror("calloc");
        return;
    }
    item->pids = calloc((size_t)line->ncommands, sizeof(*item->pids));
    item->text = strdup(text);
    if (item->pids == NULL || item->text == NULL) {
        perror("memoria");
        free_job(item);
        return;
    }
    item->count = line->ncommands;
    previous = STDIN_FILENO;
    for (i = 0; i < line->ncommands; i++) {
        pipe_fd[0] = -1;
        pipe_fd[1] = -1;
        if (i + 1 < line->ncommands && pipe(pipe_fd) == -1) {
            perror("pipe");
            break;
        }
        pid = fork();
        if (pid == -1) {
            perror("fork");
            if (pipe_fd[0] >= 0)
                close(pipe_fd[0]);
            if (pipe_fd[1] >= 0)
                close(pipe_fd[1]);
            break;
        }
        if (pid == 0)
            child_command(line, i, previous,
                          pipe_fd[1] >= 0 ? pipe_fd[1] : STDOUT_FILENO,
                          pipe_fd[0], item->pgid);
        if (item->pgid == 0)
            item->pgid = pid;
        if (setpgid(pid, item->pgid) == -1 && errno != EACCES && errno != ESRCH)
            perror("setpgid");
        item->pids[i] = pid;
        item->last_pid = pid;
        item->remaining++;
        if (previous != STDIN_FILENO)
            close(previous);
        if (pipe_fd[1] >= 0)
            close(pipe_fd[1]);
        previous = pipe_fd[0] >= 0 ? pipe_fd[0] : STDIN_FILENO;
    }
    if (previous != STDIN_FILENO)
        close(previous);
    if (item->remaining == 0) {
        free_job(item);
        return;
    }
    complete = (i == line->ncommands);
    if (!complete) {
        /* A partial pipeline is waited for, never left as an untracked job. */
        for (i = 0; i < item->count; i++) {
            if (item->pids[i] > 0)
                kill(item->pids[i], SIGTERM);
        }
    }
    if (line->background && complete) {
        item->id = next_job_id++;
        tail = &jobs;
        while (*tail != NULL)
            tail = &(*tail)->next;
        *tail = item;
        printf("[%d]\n", (int)item->last_pid);
        fflush(stdout);
    } else {
        /* Foreground jobs also use the list so wait_for_job can track every PID. */
        item->next = jobs;
        jobs = item;
        wait_for_job(item);
        remove_job(item);
    }
}

static void builtin_cd(tcommand *command)
{
    const char *path;
    char *cwd;

    if (command->argc > 2) {
        fprintf(stderr, "cd: demasiados argumentos\n");
        return;
    }
    path = command->argc == 2 ? command->argv[1] : getenv("HOME");
    if (path == NULL) {
        fprintf(stderr, "cd: HOME no está definida\n");
        return;
    }
    if (chdir(path) == -1) {
        fprintf(stderr, "cd: %s\n", strerror(errno));
        return;
    }
    cwd = getcwd(NULL, 0);
    if (cwd == NULL)
        perror("getcwd");
    else {
        printf("%s\n", cwd);
        free(cwd);
    }
}

static void builtin_umask(tcommand *command)
{
    const char *value;
    mode_t mask;
    mode_t old;
    int i;

    if (command->argc > 2) {
        fprintf(stderr, "umask: demasiados argumentos\n");
        return;
    }
    if (command->argc == 2) {
        value = command->argv[1];
        if (strlen(value) != 4 || value[0] != '0') {
            fprintf(stderr, "umask: se espera 0 y tres dígitos octales\n");
            return;
        }
        for (i = 1; i < 4; i++) {
            if (value[i] < '0' || value[i] > '7') {
                fprintf(stderr, "umask: se espera 0 y tres dígitos octales\n");
                return;
            }
        }
        mask = (mode_t)strtoul(value, NULL, 8);
        umask(mask);
    }
    old = umask(0);
    umask(old);
    printf("%04o\n", (unsigned int)old);
}

static void builtin_jobs(void)
{
    job *item;

    reap_jobs();
    for (item = jobs; item != NULL; item = item->next) {
        if (item->id != 0)
            printf("[%d]+   Running\t%s\n", item->id, item->text);
    }
}

static void builtin_fg(tcommand *command)
{
    job *item;
    job *chosen;
    char *end;
    long id;

    reap_jobs();
    chosen = NULL;
    if (command->argc > 2) {
        fprintf(stderr, "fg: demasiados argumentos\n");
        return;
    }
    if (command->argc == 2) {
        errno = 0;
        id = strtol(command->argv[1], &end, 10);
        if (errno != 0 || *end != '\0' || id < 1) {
            fprintf(stderr, "fg: identificador inválido\n");
            return;
        }
        for (item = jobs; item != NULL; item = item->next) {
            if (item->id == id)
                chosen = item;
        }
    } else {
        for (item = jobs; item != NULL; item = item->next) {
            if (item->id != 0 && (chosen == NULL || item->id > chosen->id))
                chosen = item;
        }
    }
    if (chosen == NULL) {
        fprintf(stderr, "fg: trabajo no encontrado\n");
        return;
    }
    printf("%s\n", chosen->text);
    fflush(stdout);
    if (kill(-chosen->pgid, SIGCONT) == -1 && errno != ESRCH)
        perror("kill");
    wait_for_job(chosen);
    remove_job(chosen);
}

static int run_builtin(tline *line)
{
    tcommand *command;
    const char *name;

    if (line->ncommands != 1 || line->commands[0].argc == 0)
        return 0;
    command = &line->commands[0];
    name = command->argv[0];
    if (strcmp(name, "cd") != 0 && strcmp(name, "exit") != 0 &&
        strcmp(name, "umask") != 0 && strcmp(name, "jobs") != 0 &&
        strcmp(name, "fg") != 0)
        return 0;
    if (line->background || line->redirect_input != NULL ||
        line->redirect_output != NULL || line->redirect_error != NULL) {
        fprintf(stderr, "%s: no admite background ni redirecciones\n", name);
        return 1;
    }
    if (strcmp(name, "cd") == 0)
        builtin_cd(command);
    else if (strcmp(name, "umask") == 0)
        builtin_umask(command);
    else if (strcmp(name, "jobs") == 0)
        builtin_jobs();
    else if (strcmp(name, "fg") == 0)
        builtin_fg(command);
    else if (command->argc != 1)
        fprintf(stderr, "exit: demasiados argumentos\n");
    else
        return 2;
    return 1;
}

int main(void)
{
    char *input;
    char *text;
    size_t capacity;
    ssize_t length;
    tline *line;
    int result;
    job *item;

    input = NULL;
    capacity = 0;
    interactive = isatty(STDIN_FILENO);
    shell_pgid = getpgrp();
    set_signal(SIGINT, on_sigint);
    set_signal(SIGQUIT, SIG_IGN);
    set_signal(SIGTTOU, SIG_IGN);
    for (;;) {
        reap_jobs();
        printf("msh> ");
        fflush(stdout);
        errno = 0;
        length = getline(&input, &capacity, stdin);
        if (length == -1) {
            if (errno == EINTR || interrupted) {
                interrupted = 0;
                clearerr(stdin);
                continue;
            }
            break;
        }
        interrupted = 0;
        text = strdup(input);
        if (text == NULL) {
            perror("strdup");
            break;
        }
        if (length > 0 && text[length - 1] == '\n')
            text[length - 1] = '\0';
        line = tokenize(input);
        if (line != NULL && line->ncommands > 0) {
            result = run_builtin(line);
            if (result == 2) {
                free(text);
                break;
            }
            if (result == 0)
                run_pipeline(line, text);
        }
        free(text);
    }
    free(input);
    while (jobs != NULL) {
        item = jobs;
        jobs = item->next;
        free_job(item);
    }
    return EXIT_SUCCESS;
}
