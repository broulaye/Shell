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
handle_sigttou(int signal, siginfo_t *sig_inf, void *p) {
    assert(signal == SIGTTOU);
    printf("SIGTTOU signal received\n");
    if (kill(sig_inf -> si_pid, SIGSTOP) < 0) {
        esh_sys_fatal_error("Kill: SIGSTOP failed\n");
    }
}

/** Handles a SIGTSTP signal. */
static void
handle_sigtstp(int signal, siginfo *sig_inf, void *p) {
    assert(signal == SIGTSTP);
    printf("\n");
    longjmp(jump_buf, 1);
} 

/** Handles a SIGINT signal. */
static void
handle_sigint(int signal, siginfo_t *sig_inf, void *p) {
    assert(signal == SIGINT);
    printf("\n");
    longjmp(jump_buf, 1);
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

struct list *jobs = list_init(jobs);

static void Process(char** argv) {
	if(strcmp(argv[0], "kill") == 0) {
		printf("Kill: %s\n", argv[1];
		kill(atoi(argv[1]), SIGKILL);
	}

	if (strcmp(argv[0], "jobs") == 0) {
		struct list_elem * j = list_begin(get_job());

		for(; j != list_end(get_job()); j = list_next(j)){
			struct esh_pipeline *Ljobs = list_entry(j, struct esh_pipeline, elem);

			esh_pipeline_print(Ljobs);
		}
	}
}

struct esh_pipeline * get_job_from_jid(int jid) {
	struct list_elem * e = list_begin (&jobs);
	for (; e != list_end(&jobs); e = list_next(e)) {
		struct esh_pipeline *job = list_entry(e, struct esh_pipeline, elem);
		if (job->jid == jid) {
			return job;
		}
	}
	return NULL;
}

struct esh_pipeline * get_job_from_pgrp(pid_t pgrp) {
	struct list_elem * e = list_begin(&jobs);
	for (; e != list_end(&jobs); e = list_next(e)) {
		struct esh_pipeline *pipe = list_entry(e, struct esh_pipeline, elem);
		struct list_elem *c = list_begin(&pipe->commands);
		for (; c != list_end(&pipe->commands); c = list_next(c)){
			struct esh_command *command = list_entry(c, struct esh_command, elem);
			if (command->pid == pid) {
				return command;
			}
		}
	}
	return NULL;
}

struct esh_pipeline * esh_pipeline_create(struct esh_command *cmd) {
	struct esh_pipeline *new_pipeline;
	new_pipeline->commands = list_init(new_pipeline->commands);
	push_front(new_pipeline->commands, cmd);
	return new_pipeline;
}

void esh_pipeline_finish(struct esh_pipeline *pipe) {
	pipe->iored_input = list_front(pipe->commands)->iored_input;
	pipe->iored_output = list_back(pipe->commands)->iored_output;
}

struct esh_command_line * esh_command_line_create(struct esh_pipeline *pipe) {
	esh_command_line command_line;
	list_init(command_line->list);
	list_push_front(command_line->list, pipe);
	return command_line;
}


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

	struct list_elem * e = list_begin(&cline->pipes);
	for (; e != list_end(&cline->pipes); e = list_next(e)) {
		struct esh_pipesline *pipe = list_entry(e, struct esh_pipeline, elem);
		struct list_elem *c = list_begin(&pipe->commands);
		for (; c != list_end(&pipe->commands); c = list_next(c)) {
			struct esh_command *command = list_entry(c, struct esh_command, elem);
			Process(command->argv);
		}
	}

        esh_command_line_free(cline);
    }
    return 0;
}
