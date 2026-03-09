#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "auth_common.h"
#include "env_common.h"

// Maximum path length for shell
#define MAX_PATH 4096

/*
 * Build a PATH value for the target user.
 * Strips trailing slashes from home and skips ~/.local/bin when home is "/"
 * to avoid producing paths like //.local/bin.
 */
static void build_path(char *buf, size_t buflen, const char *home, int is_root)
{
	char h[MAX_PATH];
	strncpy(h, home, MAX_PATH - 1);
	h[MAX_PATH - 1] = '\0';
	size_t len = strlen(h);
	while (len > 1 && h[len - 1] == '/')
		h[--len] = '\0';

	int add_local = !(len == 1 && h[0] == '/');

	if (is_root) {
		if (add_local)
			snprintf(buf, buflen,
				 "PATH=%s/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
				 h);
		else
			snprintf(buf, buflen,
				 "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
	} else {
		if (add_local)
			snprintf(buf, buflen,
				 "PATH=%s/.local/bin:/usr/local/bin:/usr/bin:/bin",
				 h);
		else
			snprintf(buf, buflen, "PATH=/usr/local/bin:/usr/bin:/bin");
	}
}

void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [OPTIONS] [USERNAME]\n\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -s SHELL   Use specific shell instead of user's default\n\n");
	fprintf(stderr, "If no USERNAME is specified:\n");
	fprintf(stderr, "  - For all users: launches root's shell\n");
	fprintf(stderr,
		"\nNon-root users must be members of the '%s' group to use this utility.\n",
		SUEX_GROUP);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char *custom_shell = NULL;
	char *target_user = NULL;
	int opt;

	// Check if user has permission to use this tool
	if (!user_in_suex_group()) {
		fprintf(stderr,
			"Error: You must be a member of the '%s' group to use this utility\n",
			SUEX_GROUP);
		exit(EXIT_FAILURE);
	}
	// Parse command line options
	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case 's':
			custom_shell = optarg;
			break;
		default:
			usage(argv[0]);
		}
	}

	// Check for username argument
	if (optind < argc) {
		target_user = argv[optind];
	} else {
		// If no username provided, default to "root"
		target_user = "root";
	}

	// Get target user information
	struct passwd *pw = getpwnam(target_user);
	if (!pw) {
		fprintf(stderr, "Error: User '%s' does not exist\n",
			target_user);
		exit(EXIT_FAILURE);
	}
	// Determine which shell to use
	char shell_path[MAX_PATH];

	if (custom_shell) {
		strncpy(shell_path, custom_shell, MAX_PATH - 1);
		shell_path[MAX_PATH - 1] = '\0';
	} else if (pw->pw_shell && strlen(pw->pw_shell) > 0) {
		strncpy(shell_path, pw->pw_shell, MAX_PATH - 1);
		shell_path[MAX_PATH - 1] = '\0';
	} else {
		// Default to /bin/sh if no shell specified
		strncpy(shell_path, "/bin/sh", MAX_PATH - 1);
	}

	// Extract just the shell name for argv[0]
	char *shell_name = strrchr(shell_path, '/');
	if (shell_name) {
		shell_name++;	// Skip past the '/'
	} else {
		shell_name = shell_path;
	}

	// Create shell execution arguments
	char *shell_args[2];
	shell_args[0] = malloc(strlen(shell_name) + 2);
	if (!shell_args[0]) {
		perror("Failed to allocate memory");
		exit(EXIT_FAILURE);
	}
	sprintf(shell_args[0], "-%s", shell_name);	// Login shell convention
	shell_args[1] = NULL;

	// Count how many session vars are actually set
	int n_session = 0;
	for (int i = 0; session_vars[i]; i++) {
		if (getenv(session_vars[i]))
			n_session++;
	}

	// Allocate env array: 6 fixed + inherited session vars + terminator
	int env_count = 0;
	char **env_vars = malloc((6 + n_session + 1) * sizeof(char *));
	if (!env_vars) {
		perror("Failed to allocate memory");
		exit(EXIT_FAILURE);
	}

	char home[MAX_PATH];
	snprintf(home, MAX_PATH, "HOME=%s", pw->pw_dir);
	env_vars[env_count++] = strdup(home);

	char shell_env[MAX_PATH];
	snprintf(shell_env, MAX_PATH, "SHELL=%s", shell_path);
	env_vars[env_count++] = strdup(shell_env);

	char user_env[MAX_PATH];
	snprintf(user_env, MAX_PATH, "USER=%s", pw->pw_name);
	env_vars[env_count++] = strdup(user_env);

	char logname_env[MAX_PATH];
	snprintf(logname_env, MAX_PATH, "LOGNAME=%s", pw->pw_name);
	env_vars[env_count++] = strdup(logname_env);

	// Set system-default PATH with user's ~/.local/bin
	char path_buf[MAX_PATH];
	build_path(path_buf, MAX_PATH, pw->pw_dir, pw->pw_uid == 0);
	env_vars[env_count++] = strdup(path_buf);

	char mail_env[MAX_PATH];
	snprintf(mail_env, MAX_PATH, "MAIL=/var/mail/%s", pw->pw_name);
	env_vars[env_count++] = strdup(mail_env);

	// Inherit session/terminal variables
	for (int i = 0; session_vars[i]; i++) {
		char *val = getenv(session_vars[i]);
		if (val) {
			char buf[MAX_PATH];
			snprintf(buf, MAX_PATH, "%s=%s", session_vars[i], val);
			env_vars[env_count++] = strdup(buf);
		}
	}
	env_vars[env_count] = NULL;
	// Switch to target user's primary group
	if (setgid(pw->pw_gid) != 0) {
		perror("Failed to set group ID");
		exit(EXIT_FAILURE);
	}
	// Initialize supplementary groups for the user
	if (initgroups(target_user, pw->pw_gid) != 0) {
		perror("Failed to initialize supplementary groups");
		exit(EXIT_FAILURE);
	}
	// Switch to target user
	if (setuid(pw->pw_uid) != 0) {
		perror("Failed to set user ID");
		exit(EXIT_FAILURE);
	}
	// Change to user's home directory
	if (chdir(pw->pw_dir) != 0) {
		fprintf(stderr,
			"Warning: Could not change to home directory '%s': %s\n",
			pw->pw_dir, strerror(errno));
		// Continue anyway - this isn't fatal
	}
	// Execute the shell
	execve(shell_path, shell_args, env_vars);

	// If we get here, execve failed
	perror("Failed to execute shell");

	// Clean up allocated memory (though we shouldn't reach here)
	free(shell_args[0]);
	for (int i = 0; i < env_count; i++) {
		free(env_vars[i]);
	}
	free(env_vars);

	return EXIT_FAILURE;
}
