/**
 * suex.c - Execute commands as another user
 *
 * This utility allows non-root users who belong to the 'suex' group
 * to execute commands either as root or as another user.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Maximum path length for shell
#define MAX_PATH 4096
// Maximum number of groups to check
#define MAX_GROUPS 100
// Name of the group that can use this utility
#define SUEX_GROUP "suex"
// Default user to run as if no user is specified
#define DEFAULT_USER "root"

static char *program_name;

/**
 * Display usage information and exit
 */
static void usage(int exit_code)
{
	printf("Usage: %s [USER[:GROUP]] COMMAND [ARGUMENTS...]\n",
	       basename(program_name));
	printf("       %s +USER[:GROUP] COMMAND [ARGUMENTS...]\n",
	       basename(program_name));
	printf("       %s @USER[:GROUP] COMMAND [ARGUMENTS...]\n",
	       basename(program_name));
	printf("If USER is omitted and caller has permission, runs as root\n");
	exit(exit_code);
}

/**
 * Print error message and exit
 */
static void die(int code, const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", basename(program_name));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (errno) {
		fprintf(stderr, ": %s", strerror(errno));
	}

	fprintf(stderr, "\n");
	exit(code);
}

/**
 * Check if the current user is in the specified group
 */
static int user_in_group(const char *group_name)
{
	int ngroups = 0;
	gid_t *groups = NULL;
	struct group *gr;
	int result = 0;

	// Get the group ID for the specified group
	gr = getgrnam(group_name);
	if (gr == NULL) {
		return 0;	// Group doesn't exist
	}
	// Get number of groups
	ngroups = getgroups(0, NULL);
	if (ngroups <= 0) {
		return 0;
	}
	// Allocate memory for group list
	groups = malloc(ngroups * sizeof(gid_t));
	if (groups == NULL) {
		return 0;
	}
	// Get group list
	if (getgroups(ngroups, groups) == -1) {
		free(groups);
		return 0;
	}
	// Check if user belongs to the specified group
	for (int i = 0; i < ngroups; i++) {
		if (groups[i] == gr->gr_gid) {
			result = 1;
			break;
		}
	}

	free(groups);
	return result;
}

/**
 * Parse a string in format [USER[:GROUP]] into user and group components
 */
static int parse_user_group(const char *arg, char **user, char **group)
{
	// Sanity check
	if (!arg || !user || !group) {
		return -1;
	}
	// Initialize outputs
	*user = NULL;
	*group = NULL;

	// Make a copy of the string since we'll modify it
	char *str = strdup(arg);
	if (!str) {
		return -1;
	}
	// Skip @ or + prefix if present
	char *start = str;
	if (*start == '@' || *start == '+') {
		start++;
	}
	// Check if we have a valid string after the prefix
	if (*start == '\0') {
		free(str);
		return -1;
	}
	// Split on colon for group
	char *colon = strchr(start, ':');
	if (colon) {
		*colon = '\0';	// Split the string

		// Extract group if there's anything after the colon
		if (*(colon + 1) != '\0') {
			*group = strdup(colon + 1);
			if (!*group) {
				free(str);
				return -1;
			}
		}
	}
	// Extract user
	*user = strdup(start);
	if (!*user) {
		free(*group);
		*group = NULL;
		free(str);
		return -1;
	}
	// Clean up the temporary string
	free(str);
	return 0;
}

/**
 * Check if a string looks like a command rather than a user specification
 * Returns 1 if it looks like a command, 0 otherwise
 */
static int looks_like_command(const char *arg)
{
	// If it starts with / or . it's likely a path
	if (arg[0] == '/' || arg[0] == '.') {
		return 1;
	}
	// If it exists as a file in the current directory or PATH, it's a command
	if (access(arg, F_OK) == 0) {
		return 1;
	}
	// Check if it's in PATH
	char *path_env = getenv("PATH");
	if (path_env) {
		char *path_copy = strdup(path_env);
		if (path_copy) {
			char *dir = strtok(path_copy, ":");
			while (dir) {
				char full_path[MAX_PATH];
				snprintf(full_path, MAX_PATH, "%s/%s", dir,
					 arg);
				if (access(full_path, F_OK) == 0) {
					free(path_copy);
					return 1;
				}
				dir = strtok(NULL, ":");
			}
			free(path_copy);
		}
	}

	return 0;
}

/**
 * Set up supplementary groups for the target user
 */
static int setup_groups(const char *username, gid_t target_gid)
{
	if (!username) {
		// Just set the single group
		if (setgroups(1, &target_gid) < 0) {
			return -1;
		}
		return 0;
	}

	int ngroups = 0;
	gid_t *glist = NULL;

	// First call to get the number of groups
	getgrouplist(username, target_gid, NULL, &ngroups);

	if (ngroups <= 0) {
		return -1;
	}
	// Allocate memory for the group list
	glist = malloc(ngroups * sizeof(gid_t));
	if (glist == NULL) {
		return -1;
	}
	// Get the actual groups
	if (getgrouplist(username, target_gid, glist, &ngroups) < 0) {
		free(glist);
		return -1;
	}
	// Set the groups
	if (setgroups(ngroups, glist) < 0) {
		free(glist);
		return -1;
	}

	free(glist);
	return 0;
}

int main(int argc, char *argv[])
{
	char *user = NULL, *group = NULL;
	char **cmd_argv;
	int cmd_index = 1;
	char *end;

	uid_t real_uid = getuid();
	uid_t effective_uid = geteuid();
	gid_t target_gid = getgid();
	uid_t target_uid;

	struct passwd *pw = NULL;
	struct passwd *real_pw = NULL;

	program_name = argv[0];

	// Check if we have enough arguments
	if (argc < 2) {
		usage(1);
	}
	// Check if we have permission to use suex
	int is_root = (real_uid == 0);
	int in_suex_group = user_in_group(SUEX_GROUP);

	// Get real user info
	real_pw = getpwuid(real_uid);
	if (!real_pw) {
		die(1, "Failed to get information for current user");
	}
	// Non-root user must be in suex group
	if (!is_root && !in_suex_group) {
		die(1, "Permission denied: User '%s' not in '%s' group",
		    real_pw->pw_name, SUEX_GROUP);
	}
	// Check if first argument is a user specification
	char *first_arg = argv[1];
	int first_arg_is_user = 0;

	// For non-root users, check if the first argument looks like a command
	if (!is_root && looks_like_command(first_arg)) {
		// First argument is a command, default to root
		user = strdup(DEFAULT_USER);
		if (!user) {
			die(1, "Memory allocation failed");
		}
		cmd_index = 1;	// Command starts at first argument
	} else {
		// Check if first argument looks like a user spec
		if (parse_user_group(first_arg, &user, &group) == 0) {
			first_arg_is_user = 1;
			cmd_index = 2;	// Command starts at second argument
		} else {
			// If root user, must specify a target user
			if (is_root) {
				die(1, "Root user must specify a target user");
			}
			// For non-root users, default to root
			user = strdup(DEFAULT_USER);
			if (!user) {
				die(1, "Memory allocation failed");
			}
			cmd_index = 1;	// Command starts at first argument
		}
	}

	// Set command arguments
	cmd_argv = &argv[cmd_index];

	// Make sure we have a command to execute
	if (cmd_argv[0] == NULL) {
		free(user);
		free(group);
		usage(1);
	}
	// Handle target user
	if (user && user[0] != '\0') {
		// Check if user is numeric UID
		char *endptr;
		long uid_val = strtol(user, &endptr, 10);
		if (*endptr == '\0') {
			// Numeric user ID provided
			target_uid = uid_val;
		} else {
			// Username provided
			pw = getpwnam(user);
			if (pw == NULL) {
				die(1, "Failed to find user '%s'", user);
			}
			target_uid = pw->pw_uid;
			target_gid = pw->pw_gid;
		}
	} else {
		// No user specified, default to root
		target_uid = 0;
		pw = getpwuid(0);
		if (pw) {
			target_gid = pw->pw_gid;
		}
	}

	// Get password entry for target user if not already fetched
	if (pw == NULL) {
		pw = getpwuid(target_uid);
	}
	// Handle target group if specified
	if (group && group[0] != '\0') {
		// Check if group is numeric GID
		char *endptr;
		long gid_val = strtol(group, &endptr, 10);
		if (*endptr == '\0') {
			// Numeric group ID provided
			target_gid = gid_val;
		} else {
			// Group name provided
			struct group *gr = getgrnam(group);
			if (gr == NULL) {
				die(1, "Failed to find group '%s'", group);
			}
			target_gid = gr->gr_gid;
		}
	}
	// Set supplementary groups
	if (pw) {
		if (setup_groups(pw->pw_name, target_gid) < 0) {
			die(1,
			    "Failed to set supplemental groups for user '%s'",
			    pw->pw_name);
		}
		// Set environment variables
		setenv("USER", pw->pw_name, 1);
		setenv("HOME", pw->pw_dir, 1);
	} else {
		if (setup_groups(NULL, target_gid) < 0) {
			die(1, "Failed to set supplemental groups for GID %d",
			    target_gid);
		}
		// Set environment variables
		setenv("USER", target_uid == 0 ? "root" : "nobody", 1);
		setenv("HOME", target_uid == 0 ? "/root" : "/", 1);
	}

	// Clean up
	free(user);
	free(group);

	// Set the new GID and UID
	if (setgid(target_gid) < 0) {
		die(1, "Failed to set GID to %d", target_gid);
	}

	if (setuid(target_uid) < 0) {
		die(1, "Failed to set UID to %d", target_uid);
	}
	// Execute the command
	execvp(cmd_argv[0], cmd_argv);
	die(127, "Failed to execute '%s'", cmd_argv[0]);

	return 1;		// Should never reach here
}
