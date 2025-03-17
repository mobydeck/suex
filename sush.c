#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// Maximum path length for shell
#define MAX_PATH 4096
// Maximum number of groups to check
#define MAX_GROUPS 100
// Name of the group that can use this utility
#define SUEX_GROUP "suex"

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

// Check if the current user belongs to the suex group
int user_in_suex_group()
{
	// Get the suex group ID
	struct group *suex_group = getgrnam(SUEX_GROUP);
	if (!suex_group) {
		// Group doesn't exist, consider this a failure
		return 0;
	}

	gid_t suex_gid = suex_group->gr_gid;

	// Get current user info
	uid_t uid = getuid();

	// If we're already root, we don't need to check group membership
	if (uid == 0) {
		return 1;
	}
	// Get user group memberships
	gid_t groups[MAX_GROUPS];
	int ngroups = MAX_GROUPS;

	struct passwd *pw = getpwuid(uid);
	if (!pw) {
		return 0;	// Can't get user info
	}

	if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0) {
		fprintf(stderr,
			"Warning: Too many groups, may not validate all group memberships\n");
	}
	// Check if user is in the suex group
	for (int i = 0; i < ngroups; i++) {
		if (groups[i] == suex_gid) {
			return 1;
		}
	}

	return 0;
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

	// Set up environment variables for the user
	char *env_vars[] = {
		NULL,		// HOME
		NULL,		// SHELL
		NULL,		// USER
		NULL,		// LOGNAME
		NULL,		// PATH
		NULL,		// MAIL
		NULL		// Terminator
	};

	char home[MAX_PATH];
	snprintf(home, MAX_PATH, "HOME=%s", pw->pw_dir);
	env_vars[0] = strdup(home);

	char shell_env[MAX_PATH];
	snprintf(shell_env, MAX_PATH, "SHELL=%s", shell_path);
	env_vars[1] = strdup(shell_env);

	char user_env[MAX_PATH];
	snprintf(user_env, MAX_PATH, "USER=%s", pw->pw_name);
	env_vars[2] = strdup(user_env);

	char logname_env[MAX_PATH];
	snprintf(logname_env, MAX_PATH, "LOGNAME=%s", pw->pw_name);
	env_vars[3] = strdup(logname_env);

	// Keep default PATH or set to a sensible default
	char *path = getenv("PATH");
	if (path) {
		char path_env[MAX_PATH];
		snprintf(path_env, MAX_PATH, "PATH=%s", path);
		env_vars[4] = strdup(path_env);
	} else {
		env_vars[4] = strdup("PATH=/bin:/usr/bin");
	}

	char mail_env[MAX_PATH];
	snprintf(mail_env, MAX_PATH, "MAIL=/var/mail/%s", pw->pw_name);
	env_vars[5] = strdup(mail_env);

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
	for (int i = 0; env_vars[i] != NULL; i++) {
		free(env_vars[i]);
	}

	return EXIT_FAILURE;
}
