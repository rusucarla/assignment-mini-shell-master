// SPDX-License-Identifier: BSD-3-Clause

#include "cmd.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define READ 0
#define WRITE 1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	// check if dir is NULL
	if (dir == NULL) {
		// if dir is NULL, then i have to go to the home directory
		// get the home directory
		char *home = getenv("HOME");

		if (home == NULL) {
			fprintf(stderr, "cd: HOME not set\n");
			return 1;
		}
		// change the directory
		if (chdir(home) == -1) {
			fprintf(stderr, "cd: %s: No such file or directory\n", home);
			return 1;
		}
		return 0;
	}
	// check if dir is the previous directory
	if (strcmp(dir->string, "-") == 0) {
		// get the previous directory
		char *oldpwd = getenv("OLDPWD");

		if (oldpwd == NULL) {
			fprintf(stderr, "cd: OLDPWD not set\n");
			return 1;
		}
		// change the directory
		if (chdir(oldpwd) == -1) {
			fprintf(stderr, "cd: %s: No such file or directory\n", oldpwd);
			return 1;
		}
		return 0;
	}
	// if dir is not some special case, then i have to go to the directory specified change the directory
	// but fist remember the current directory
	char *oldpwd = getcwd(NULL, 0);
	// put the current directory in OLDPWD
	if (setenv("OLDPWD", oldpwd, 1) == -1) {
		fprintf(stderr, "cd: OLDPWD not set\n");
		return 1;
	}
	free(oldpwd);
	// change the directory
	if (chdir(dir->string) == -1) {
		fprintf(stderr, "cd: %s: No such file or directory\n", dir->string);
		return 1;
	}
	return 0;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */
	exit(SHELL_EXIT); /* TODO: Replace with actual exit code. */
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (s == NULL)
		return 0;
	/* TODO: If builtin command, execute the command. */
	// check if the command is builtin : cd, exit, quit
	if (strcmp(s->verb->string, "cd") == 0) {
		// check if i have redirections
		// before changing the directory, i have to create the file
		if (s->out != NULL) {
			// ususally cd doesn't have redirections
			// but i have to create a new empty file
			// open the file
			int fd = open(s->out->string, O_WRONLY | O_CREAT, 0644);

			if (fd == -1) {
				parse_error("open", level);
				return 1;
			}
			// close the file
			if (close(fd) == -1) {
				parse_error("close", level);
				return 1;
			}
		}
		return shell_cd(s->params);
	} else if (strcmp(s->verb->string, "exit") == 0 || strcmp(s->verb->string, "quit") == 0) {
		return shell_exit();
	}
	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */
	// VARIABLE='VALUE' :
	// -> VARIABLE is s->verb->string
	// -> = is s->verb->next_part->string
	// -> next parts will be extracted with get_word
	if (s->verb->next_part != NULL && strcmp(s->verb->next_part->string, "=") == 0) {
		char *variable = get_word(s->verb->next_part->next_part);
		// try to set the variable
		if (setenv(s->verb->string, variable, 1) == -1) {
			parse_error("setenv", level);
			return EXIT_FAILURE;
		}
		free(variable);
		return EXIT_SUCCESS;
	}

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */
	// fork new process
	pid_t pid = fork();

	if (pid == -1) {
		parse_error("fork", level);
		return 1;
	}
	// child process
	if (pid == 0) {
		// check if i have redirections
		if (s->io_flags == IO_REGULAR) {
			if (s->in != NULL) {
				char *in_file = get_word(s->in);
				// open the file
				int fd = open(in_file, O_RDONLY);

				if (fd == -1) {
					parse_error("opening the in_file failed (regular)", level);
					exit(EXIT_FAILURE);
				}
				// redirect stdin
				if (dup2(fd, STDIN_FILENO) == -1) {
					parse_error("duplicating the in_file failed (regular)", level);
					exit(EXIT_FAILURE);
				}
				// close the file
				if (close(fd) == -1) {
					parse_error("closing the in_file failed (regular)", level);
					exit(EXIT_FAILURE);
				}
				free(in_file);
			}
			// check if the redirection is "&>" -> if yes, then redirect the output and the error
			// if not, then treat them separately
			if (s->out != NULL && s->err != NULL && strcmp(s->out->string, s->err->string) == 0) {
				// open the file
				char *out_err_file = get_word(s->out);
				int fd = open(out_err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				if (fd == -1) {
					parse_error("opening the out_err_file failed (regular)", level);
					exit(EXIT_FAILURE);
				}
				// redirect stdout
				if (dup2(fd, STDOUT_FILENO) == -1) {
					parse_error("duplicating the out_err_file failed (out | regular)", level);
					exit(EXIT_FAILURE);
				}
				// redirect stderr
				if (dup2(fd, STDERR_FILENO) == -1) {
					parse_error("duplicating the out_err_file failed (err | regular)", level);
					exit(EXIT_FAILURE);
				}
				// close the file
				if (close(fd) == -1) {
					parse_error("closing the out_err_file failed (regular)", level);
					exit(EXIT_FAILURE);
				}
				free(out_err_file);
			} else {
				if (s->out != NULL) {
					char *out_file = get_word(s->out);
					// open the file
					int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

					if (fd == -1) {
						parse_error("opening the out_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					// redirect stdout
					if (dup2(fd, STDOUT_FILENO) == -1) {
						parse_error("duplcating the out_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					// close the file
					if (close(fd) == -1) {
						parse_error("closing the out_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					free(out_file);
				}
				if (s->err != NULL) {
					// open the file
					char *err_file = get_word(s->err);
					int fd = open(err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

					if (fd == -1) {
						parse_error("opening the err_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					// redirect stderr
					if (dup2(fd, STDERR_FILENO) == -1) {
						parse_error("duplicating the err_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					// close the file
					if (close(fd) == -1) {
						parse_error("closing the err_file failed (regular)", level);
						exit(EXIT_FAILURE);
					}
					free(err_file);
				}
			}
		} else if (s->io_flags == IO_OUT_APPEND && s->out != NULL) {
			// have to append the output
			// open the file
			char *out_file = get_word(s->out);
			int fd = open(out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);

			if (fd == -1) {
				parse_error("opening the out file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			// redirect stdout
			if (dup2(fd, STDOUT_FILENO) == -1) {
				parse_error("duplicating the out file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			// close the file
			if (close(fd) == -1) {
				parse_error("closing the out file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			free(out_file);
		} else if (s->io_flags == IO_ERR_APPEND && s->err != NULL) {
			// have to append the error
			// open the file
			char *err_file = get_word(s->err);
			int fd = open(err_file, O_WRONLY | O_CREAT | O_APPEND, 0644);

			if (fd == -1) {
				parse_error("opening the err file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			// redirect stderr
			if (dup2(fd, STDERR_FILENO) == -1) {
				parse_error("duplicating the err file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			// close the file
			if (close(fd) == -1) {
				parse_error("closing the err file failed (appending)", level);
				exit(EXIT_FAILURE);
			}
			free(err_file);
		}

		// execute the command
		char **argv;
		int size;
		// get the arguments
		argv = get_argv(s, &size);

		if (execvp(argv[0], argv) == -1) {
			// if execvp fails, then i have to print a special error message
			// as indicated by the last test
			// error message = > Execution failed for 'executable'
			fprintf(stderr, "Execution failed for '%s'\n", argv[0]);
			exit(EXIT_FAILURE);
		}
		// free argv
		for (int i = 0; i < size; i++)
			free(argv[i]);
		free(argv);
	}
	int status;
	// parent process
	if (pid > 0) {
		// wait for child
		if (waitpid(pid, &status, 0) == -1) {
			parse_error("something happened to the child process", level);
			return 1;
		}
	}
	return WEXITSTATUS(status);/* TODO: Replace with actual exit status. */
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	// fork first command
	pid_t pid1 = fork();

	if (pid1 == -1) {
		parse_error("failed to fork", level);
		return 1;
	}
	// first command
	if (pid1 == 0) {
		// execute cmd1
		parse_command(cmd1, level + 1, father);
		exit(EXIT_SUCCESS);
	}
	// fork second command
	pid_t pid2 = fork();

	if (pid2 == -1) {
		parse_error("failed to fork", level);
		return 1;
	}
	// second command
	if (pid2 == 0) {
		// execute cmd2
		parse_command(cmd2, level + 1, father);
		exit(EXIT_SUCCESS);
	}
	// parent process
	int status1, status2;
	// wait for child
	if (waitpid(pid1, &status1, 0) == -1) {
		parse_error("something fishy with the child (waitpid)", level);
		return 1;
	}
	if (waitpid(pid2, &status2, 0) == -1) {
		parse_error("something fishy with the child (waitpid)", level);
		return 1;
	}
	return WEXITSTATUS(status1 && status2); /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
						command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	int status1, status2;
	int pipefd[2];
	// create the pipe
	if (pipe(pipefd) == -1) {
		parse_error("failed to pipe", level);
		return 1;
	}
	// fork first command
	pid_t pid1 = fork();

	if (pid1 == -1) {
		parse_error("failed to fork", level);
		return EXIT_FAILURE;
	}
	if (pid1 == 0) {
		// close the read end of the pipe
		if (close(pipefd[READ]) == -1) {
			parse_error("failed to close the read end of the pipe", level);
			return EXIT_FAILURE;
		}
		// redirect stdout to the write end of the pipe
		if (dup2(pipefd[WRITE], STDOUT_FILENO) == -1) {
			parse_error("failed to duplicate the write end of the pipe", level);
			return EXIT_FAILURE;
		}
		// close the write end of the pipe
		if (close(pipefd[WRITE]) == -1) {
			parse_error("failed to close the write end of the pipe", level);
			return EXIT_FAILURE;
		}
		// execute cmd1
		int status = parse_command(cmd1, level + 1, father);
		// determine the exit status
		exit(status == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	// fork second command
	pid_t pid2 = fork();

	if (pid2 == -1) {
		parse_error("fork", level);
		return 1;
	}
	if (pid2 == 0) {
		// close the write end of the pipe
		if (close(pipefd[WRITE]) == -1) {
			parse_error("failed to close the write end of the pipe", level);
			return EXIT_FAILURE;
		}
		// redirect stdin to the read end of the pipe
		if (dup2(pipefd[READ], STDIN_FILENO) == -1) {
			parse_error("failed to duplicate the read end of the pipe", level);
			return EXIT_FAILURE;
		}
		// close the read end of the pipe
		if (close(pipefd[READ]) == -1) {
			parse_error("failed to close the read end of the pipe", level);
			return 1;
		}
		// execute cmd2
		int status = parse_command(cmd2, level + 1, father);
		// determine the exit status
		exit(status == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	// close the read end of the pipe
	if (close(pipefd[READ]) == -1) {
		parse_error("failed to close the read end of the pipe", level);
		return 1;
	}
	// close the write end of the pipe
	if (close(pipefd[WRITE]) == -1) {
		parse_error("failed to close the write end of the pipe", level);
		return 1;
	}
	// wait for the first command
	if (waitpid(pid1, &status1, 0) == -1) {
		parse_error("something fishy with the child (waitpid)", level);
		return EXIT_FAILURE;
	}
	// wait for the second command
	if (waitpid(pid2, &status2, 0) == -1) {
		parse_error("something fishy with the child (waitpid)", level);
		return EXIT_FAILURE;
	}
	// I will only return data about the second command
	// because the output of the first command is redirected to the input of the second one
	return (WIFEXITED(status2) && WEXITSTATUS(status2));
	/* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	int status = 0;

	if (c == NULL)
		return 0;

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		status = parse_simple(c->scmd, level, father);
		return status; /* TODO: Replace with actual exit code of command. */
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		c->cmd1->up = c;
		c->cmd2->up = c;
		status = parse_command(c->cmd1, level + 1, c);
		status = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		c->cmd1->up = c;
		c->cmd2->up = c;
		status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one returns non zero. */
		c->cmd1->up = c;
		status = parse_command(c->cmd1, level + 1, c);
		// check if the first command returned non zero
		if (status != 0) {
			// have to execute the second command
			c->cmd2->up = c;
			status = parse_command(c->cmd2, level + 1, c);
		}
		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one returns zero. */
		// have to execute the first command
		c->cmd1->up = c;
		status = parse_command(c->cmd1, level + 1, c);
		// check if the first command returned zero
		if (status == 0) {
			// have to execute the second command
			c->cmd2->up = c;
			status = parse_command(c->cmd2, level + 1, c);
		}
		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the input of the second. */
		c->cmd1->up = c;
		c->cmd2->up = c;
		status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
		break;

	default:
		return SHELL_EXIT;
	}

	return status; /* TODO: Replace with actual exit code of command. */
}
