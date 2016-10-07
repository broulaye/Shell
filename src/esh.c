/*
 * esh - the 'pluggable' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 */
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>
#include <assert.h>
#include <setjmp.h>
#include "esh.h"
#include "esh-sys-utils.h"

static jmp_buf jump_buf;

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
        " -h            print this help\n"
        " -p  plugindir directory from which to load plug-ins\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt by assembling fragments from loaded plugins that 
 * implement 'make_prompt.'
 *
 * This function demonstrates how to iterate over all loaded plugins.
 */
static char *
build_prompt_from_plugins(void)
{
    char *prompt = NULL;
    struct list_elem * e = list_begin(&esh_plugin_list);

    for (; e != list_end(&esh_plugin_list); e = list_next(e)) {
        struct esh_plugin *plugin = list_entry(e, struct esh_plugin, elem);

        if (plugin->make_prompt == NULL)
            continue;

        /* append prompt fragment created by plug-in */
        char * p = plugin->make_prompt();
        if (prompt == NULL) {
            prompt = p;
        } else {
            prompt = realloc(prompt, strlen(prompt) + strlen(p) + 1);
            strcat(prompt, p);
            free(p);
        }
    }

    /* default prompt */
    if (prompt == NULL)
        prompt = strdup("esh> ");

    return prompt;
}

/** Handles a SIGTTOU signal. */
static char *
handle_sigttou(int signal, siginfo_t *sig_inf, void *txt) {
    assert(signal == SIGTTOU);
    printf("SIGTTOU signal received\n");
    if (kill(sig_inf -> si_pid, SIGSTOP) < 0) {
        esh_sys_fatal_error("Kill: SIGSTOP failed\n");
    }
}

/** Handles a SIGTSTP signal. */
static void
handle_sigtstp(int signal, siginfo *sig_inf, void *txt) {
    assert(signal == SIGTSTP);
    printf("\n");
    longjmp(jump_buf, 1);
} 

/** Handles a SIGINT signal. */
static void
handle_sigint(int signal, siginfo_t *sig_inf, void *txt) {
    assert(signal == SIGINT);
    printf("\n");
    longjmp(jump_buf, 1);
}

/* You may use this code in your shell without attribution. */
static int
get_command_type(char* argv) {
    if (strcmp("jobs", argv) == 0) {
        return 1;
    }
    else if (strcmp("fg", argv) == 0) {
        return 2;
    }
    else if (strcmp("bg", argv) == 0) {
        return 3;
    }
    else if (strcmp("kill", argv) == 0) {
        return 4;
    }
    else if (strcmp("stop", argv) == 0) {
        return 5;
    }
    else if (strcmp("quit", argv) == 0) {
        return 6;
    }

    return 0;
}

/* The shell object plugins use.
 * Some methods are set to defaults.
 */
struct esh_shell shell =
{
    .build_prompt = build_prompt_from_plugins,
    .readline = readline,       /* GNU readline(3) */ 
    .parse_command_line = esh_parse_command_line /* Default parser */
};

int
main(int ac, char *av[])
{
    
    int opt;
    list_init(&esh_plugin_list);

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "hp:")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;

        case 'p':
            esh_plugin_load_from_directory(optarg);
            break;
        }
    }

    esh_plugin_initialize(&shell);

    /* Read/eval loop. */
    for (;;) {
        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? shell.build_prompt() : NULL;
        char * cmdline = shell.readline(prompt);
        free (prompt);

        if (cmdline == NULL)  /* User typed EOF */
            break;

        struct esh_command_line * cline = shell.parse_command_line(cmdline);
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) {    /* User hit enter */
            esh_command_line_free(cline);
            continue;
        }

        switch (get_command_type()) {

            case 6:
            // quit command
                exit(EXIT_SUCCESS);
            default:
            // executing a program

        }

        esh_command_line_free(cline);
    }
    return 0;
}
