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

static struct termios *termi;
static jmp_buf jump_buf;
static void change_chld_stat(pid_t chld, int stat);
int jcount;

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
        " -h            print this help\n"
        " -p  plugindir directory from which to load plug-ins\n",
        progname);

    exit(EXIT_SUCCESS);
}
struct list jobs;

/**
*This method print the commands in the pipeline
*/
static void print_command(struct esh_pipeline *Ljobs) {
    printf("\n[%d]", Ljobs->jid);
    if(&Ljobs->elem == list_prev(list_rbegin(&jobs))) {
        printf("-");
    }
    if(&Ljobs->elem == list_rbegin(&jobs)) {
        printf("+");
    }

    if(Ljobs->status == BACKGROUND) {
        printf(" Running\t\t");
    }
    if(Ljobs->status == STOPPED) {
        printf(" Stopped\t\t");
    }
    if(Ljobs->status == FOREGROUND) {
        printf(" Foreground\t\t");
    }
    if(Ljobs->status == NEEDSTERMINAL) {
        printf(" Need Terminal\t\t");
    }

    printf("(");
    struct list_elem * e = list_begin (&Ljobs->commands);
    for (; e != list_end (&Ljobs->commands); e = list_next (e)) {
        struct esh_command *cmd = list_entry(e, struct esh_command, elem);
        char **p = cmd->argv;
        while (*p) {
            printf("%s", *p);
            p++;
            if(*p != NULL) {
                printf(" ");
            }
        }
        if(e != list_rbegin(&Ljobs->commands)) {
            printf("|");
        }

    }


    if (Ljobs->bg_job)
        printf(" &");

    printf(")\n");
}

static struct esh_pipeline * get_job_from_jid(int jid) {
	struct list_elem * e = list_begin (&jobs);
	for (; e != list_end(&jobs); e = list_next(e)) {
		struct esh_pipeline *job = list_entry(e, struct esh_pipeline, elem);
		if (job->jid == jid) {
			return job;
		}
	}
	return NULL;
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

/** Handles a SIGTTOU signal.
static void
handle_sigttou(int signal, siginfo_t *sig_inf, void *p) {
    assert(signal == SIGTTOU);
    printf("SIGTTOU signal received\n");
    if (kill(sig_inf -> si_pid, SIGSTOP) < 0) {
        esh_sys_fatal_error("Kill: SIGSTOP failed\n");
    }
}*/

/*
 * SIGCHLD handler.
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD
 * signal may be delivered for multiple children that have
 * exited.
 */
static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        change_chld_stat(child, status);
    }
}

/** Handles a SIGTSTP signal. */
static void
handle_sigtstp(int signal, siginfo_t *sig_inf, void *p) {
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


static struct esh_command * get_command_from_pid(pid_t pid) {
    struct list_elem * e = list_begin(&jobs);
	for (; e != list_end(&jobs); e = list_next(e)) {
		struct esh_pipeline *pipe = list_entry(e, struct esh_pipeline, elem);
		struct list_elem *c = list_begin(&pipe->commands);
		for (; c != list_end(&pipe->commands); c = list_next(c)) {
			struct esh_command *command = list_entry(c, struct esh_command, elem);
			if (command->pid == pid) {
			return command;
		}
		}
	}
	return NULL;
}

/**
 * Assign ownership of ther terminal to process group
 * pgrp, restoring its terminal state if provided.
 *
 * Before printing a new prompt, the shell should
 * invoke this function with its own process group
 * id (obtained on startup via getpgrp()) and a
 * sane terminal state (obtained on startup via
 * esh_sys_tty_init()).
 */
static void
give_terminal_to(pid_t pgrp, struct termios *pg_tty_state)
{
    esh_signal_block(SIGTTOU);
    int rc = tcsetpgrp(esh_sys_tty_getfd(), pgrp);
    if (rc == -1)
        esh_sys_fatal_error("tcsetpgrp: ");

    if (pg_tty_state)
        esh_sys_tty_restore(pg_tty_state);
    esh_signal_unblock(SIGTTOU);
}

/* You may use this code in your shell without attribution. */
static void change_chld_stat(pid_t chld, int stat) {
	assert(chld > 0);
	struct esh_command *cmd = get_command_from_pid(chld);
	if (cmd == NULL) {
		printf("no such job\n");
		//give_terminal_to(getpgrp(), termi);

		return;
	}
	struct esh_pipeline * chld_pipe = cmd->pipeline;
	if (WIFEXITED(stat)) {
		chld_pipe->status = BACKGROUND;
		if (&cmd->elem == list_rbegin(&chld_pipe->commands)) {
			list_remove(&chld_pipe->elem);
			give_terminal_to(getpgrp(), termi);
		}
	}
	if(WIFSIGNALED(stat)) {
        if(WTERMSIG(stat) == 9) {
            list_remove(&chld_pipe->elem);
        }
        else {
            chld_pipe->status = BACKGROUND;
            list_remove(&chld_pipe->elem);
            give_terminal_to(getpgrp(), termi);
        }

	}
	if (WIFSTOPPED(stat)) {
		if (WSTOPSIG(stat) == 19) {
			printf("19\n");
			chld_pipe->status = STOPPED;
		}
		else {
			chld_pipe->status = STOPPED;
			print_command(chld_pipe);
			give_terminal_to(getpgrp(), termi);
		}
	}

	if (list_empty(&jobs)) {
		jcount = 0;
	}
}

static void job_wait(struct esh_pipeline *job) {
	assert(esh_signal_is_blocked(SIGCHLD));

	while (job->status == FOREGROUND &&  !list_empty(&job->commands)) {
		int stat;
		pid_t chld = waitpid(-1, &stat, WUNTRACED);
//printf("chld: %d\ncaller: %d\nstat: %d\n", chld, getpid(), stat);
		if (chld != -1) {
			change_chld_stat(chld, stat);
		}
	}
}

/** processes the builtin commands, or returns false if input is not builtin */
static bool Process(char** argv) {
	if(strcmp(argv[0], "kill") == 0) {
		//printf("Kill: %s\n", argv[1]);
		if(argv[1] == NULL) {
            printf("kill: usage: kill");
		}
		struct esh_pipeline *job = get_job_from_jid(atoi(argv[1]));
		if( job != NULL) {
            if (kill(job->pgrp, SIGKILL) < 0) {
                esh_sys_fatal_error("-bash: kill: (%s) - Operation not permitted\n", argv[1]);
            }
		}
		else {
             esh_sys_fatal_error("-bash: kill: (%s) - Operation not permitted\n", argv[1]);
		}

		return true;
	}

	else if (strcmp(argv[0], "jobs") == 0) {
		struct list_elem * j = list_begin(&jobs);

		for(; j != list_end(&jobs); j = list_next(j)){
			struct esh_pipeline *Ljobs = list_entry(j, struct esh_pipeline, elem);
			print_command(Ljobs);
		}
		return true;
	}
	else if (strcmp(argv[0], "bg") == 0) {
		if (!list_empty(&jobs)) {
			if (argv[1] == NULL) {
				struct list_elem * j = list_rbegin(&jobs);
				struct esh_pipeline *job = list_entry(j, struct esh_pipeline, elem);
				job->status = BACKGROUND;
				printf("[%d]+", job->jid);
				esh_pipeline_print(job);
				printf("\n");
				if (kill(job->pgrp, SIGCONT) < 0) {
					esh_sys_fatal_error("bg: kill failed\n");
				}
			}
			else {
				struct list_elem * j = list_begin(&jobs);
				struct esh_pipeline *job;
				int jid = atoi(argv[1]);
				int found = 0;
				for (; j != list_end(&jobs); j = list_next(j)) {
					job = list_entry(j, struct esh_pipeline, elem);
					if (job->jid == jid) {
						found++;
						break;
					}
				}
				if (!found) {
					//Job not there
					printf("bg: %d: no such job\n", jid);
				}
				else {
					job->status = BACKGROUND;
					printf("[%d]+", job->jid);
					esh_pipeline_print(job);
					printf("\n");
					if (kill(job->pgrp, SIGCONT) < 0) {
						esh_sys_fatal_error("bg: kill failed\n");
					}
				}
			}
		}
		else {
			printf("-bash: bg: current: no such job\n");
		}
		return true;
	}
	else if (strcmp(argv[0], "fg") == 0) {
		if (argv[1] == NULL) {
			struct list_elem * j = list_rbegin(&jobs);
			struct esh_pipeline *job = list_entry(j, struct esh_pipeline, elem);
			if(list_empty(&jobs)) {
				printf("-bash: fg: current: no such job\n");
			}
			else {
				esh_signal_block(SIGCHLD);
				job->status = FOREGROUND;
				print_command(job);
				give_terminal_to(job->pgrp, termi);
				if (kill(job->pgrp, SIGCONT) < 0) {
					esh_sys_fatal_error("bg: kill failed\n");
				}
				job_wait(job);

				//list_remove(&job->elem);
				esh_signal_unblock(SIGCHLD);
			}

		}
		else {
			struct list_elem * j = list_begin(&jobs);
			struct esh_pipeline *job;
			int jid = atoi(argv[1]);
			int found = 0;
			for (; j != list_end(&jobs); j = list_next(j)) {
				job = list_entry(j, struct esh_pipeline, elem);
				if (job->jid == jid) {
					found++;
					break;
				}
			}
			if (!found) {
				//Job not there
				printf("bg: %d: no such job\n", jid);
			}
			else {
				//list_remove(&job->elem);

				esh_signal_block(SIGCHLD);
				job->status = FOREGROUND;
				print_command(job);
				give_terminal_to(job->pgrp, termi);
				if (kill(job->pgrp, SIGCONT) < 0) {
					esh_sys_fatal_error("bg: kill failed\n");
				}
				job_wait(job);
				esh_signal_unblock(SIGCHLD);
			}
		}
		return true;
	}
	else if (strcmp(argv[0], "stop") == 0) {
		if (argv[1] == NULL) {
			printf("usage: stop [jid]\n");
		}
		else {
			struct list_elem *j = list_begin(&jobs);
			struct esh_pipeline *job;
			for (; j != list_end(&jobs); j = list_next(j)) {
				job = list_entry(j, struct esh_pipeline, elem);
				if (job->jid == atoi(argv[1])) {
					break;
				}
			}
			kill(job->pgrp, SIGSTOP);
		}
		return true;
	}
	/* exit the shell */
	else if (strcmp(argv[0], "exit") == 0) {
		exit(EXIT_SUCCESS);
	}
	else {
		return false;
	}
}



/**
static struct esh_pipeline * get_job_from_pgrp(pid_t pgrp) {
	struct list_elem * e = list_begin(&jobs);
	for (; e != list_end(&jobs); e = list_next(e)) {
		struct esh_pipeline *pipe = list_entry(e, struct esh_pipeline, elem);
		if (pipe->pgrp == pgrp) {
			return pipe;
		}
	}
	return NULL;
}*/



int
main(int ac, char *av[])
{
    esh_signal_sethandler(SIGTSTP, handle_sigtstp);
    esh_signal_sethandler(SIGINT, handle_sigint);
    int opt;
    jcount = 0;
    list_init(&esh_plugin_list);
    list_init(&jobs);
    termi = esh_sys_tty_init();
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
    setjmp(jump_buf);

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

	struct list_elem *pipe_chld;
	int proc_pipe[2], io_pipe[2];
	struct list_elem * e = list_begin(&cline->pipes);
	for (; e != list_end(&cline->pipes); e = list_next(e)) {
		struct esh_pipeline *_pipe = list_entry(e, struct esh_pipeline, elem);
		struct list_elem *c = list_begin(&_pipe->commands);
		for (; c != list_end(&_pipe->commands); c = list_next(c)) {
			struct esh_command *command = list_entry(c, struct esh_command, elem);
			if (!Process(command->argv)) {
				esh_signal_sethandler(SIGCHLD, sigchld_handler);
				esh_signal_unblock(SIGCHLD);
				if (c == list_begin(&_pipe->commands)) {
					jcount++;
					_pipe->jid = jcount;
					e = list_rend(&cline->pipes);
					pipe_chld = list_pop_front(&cline->pipes);
					list_push_back(&jobs, pipe_chld);
				}
				if (list_size(&_pipe->commands) > 1 && c != list_rbegin(&_pipe->commands)) {
					pipe(proc_pipe);
				}
				esh_signal_block(SIGCHLD);

				pid_t fork_pid = fork();

				if (fork_pid < 0) {
					//error
					esh_sys_fatal_error("execute: fork failed");
				}
				else if (fork_pid == 0) {
					//child  process
					fork_pid = getpid();
					if (c == list_begin(&_pipe->commands)) {
						_pipe->pgrp = fork_pid;
					}
					setpgid(fork_pid, _pipe->pgrp);
					command->pid = fork_pid;

					if (list_size(&_pipe->commands) > 1 && c != list_rbegin(&_pipe->commands)) {
						close(proc_pipe[0]);
						dup2(proc_pipe[1], 1);
						close(proc_pipe[1]);
					}
					if (list_size(&_pipe->commands) > 1 && c != list_begin(&_pipe->commands)) {
						close(io_pipe[1]);
						dup2(io_pipe[0], 0);
						close(io_pipe[0]);
					}
					if (command->iored_input != NULL) {
						int input = open(command->iored_input, O_RDONLY);
						if (input < 0) {
							esh_sys_fatal_error("execute: open failed\n");
						}
						if (dup2(input, 0) < 0) {
							esh_sys_fatal_error("execute: dup2 failed\n");
						}
						close(input);
					}
					if (command->iored_output != NULL) {
						int output;
						if (command->append_to_output) {
							if ((output = open(command->iored_output, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) < 0) {
								esh_sys_fatal_error("execute: open failed\n");
							}
						} else {
							if ((output = open(command->iored_output, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) < 0) {
								esh_sys_fatal_error("execute: open failed\n");
							}
						}
						if (dup2(output, 1) < 0) {
							esh_sys_fatal_error("execute: dup2 failed\n");
						}
						close(output);
					}
					if (_pipe->bg_job) {
						_pipe->status = BACKGROUND;
					}
					else {
						_pipe->status = FOREGROUND;
						give_terminal_to(_pipe->pgrp, termi);
					}

					if (execvp(command->argv[0], command->argv) < 0) {
						esh_sys_fatal_error("%s: command not found\n", command->argv[0]);
					}
				}
				else if (fork_pid > 0) {
					//parent process
					if (c == list_begin(&_pipe->commands)) {
						_pipe->pgrp = fork_pid;
					}

					command->pid = fork_pid;
					setpgid(fork_pid, _pipe->pgrp);

					if (list_size(&_pipe->commands) > 1 && c == list_begin(&_pipe->commands)) {
						io_pipe[0] = proc_pipe[0];
						io_pipe[1] = proc_pipe[1];
					}
					if (list_size(&_pipe->commands) > 1 && c == list_rbegin(&_pipe->commands)) {
						close(proc_pipe[0]);
						close(proc_pipe[1]);
						close(io_pipe[0]);
						close(io_pipe[1]);
					}
					if (list_size(&_pipe->commands) > 1 && c != list_rbegin(&_pipe->commands) && c != list_begin(&_pipe->commands)) {
						close(io_pipe[0]);
						close(io_pipe[1]);
						io_pipe[0] = proc_pipe[0];
						io_pipe[1] = proc_pipe[1];
					}

					if (_pipe->bg_job) {
						_pipe->status = BACKGROUND;
						printf("[%d] %d\n", _pipe->jid, _pipe->pgrp);
					}
					if (c == list_rbegin(&_pipe->commands) && !_pipe->bg_job) {
						_pipe->status = FOREGROUND;
						job_wait(_pipe);
						give_terminal_to(getpgrp(), termi);
					}
				}

				esh_signal_unblock(SIGCHLD);
			}
		}
	}

        esh_command_line_free(cline);
    }
    return 0;
}
