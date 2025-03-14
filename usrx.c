#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <crypt.h>
#include <termios.h>

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s COMMAND [OPTIONS] USER\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -j     Output in JSON format (only for info command)\n");
	fprintf(stderr,
		"  -i     Skip encrypted password in output (when insecure)\n");
	fprintf(stderr, "Commands:\n");
	fprintf(stderr, "  info   - print all available information\n");
	fprintf(stderr, "  home   - print home directory\n");
	fprintf(stderr, "  shell  - print login shell\n");
	fprintf(stderr, "  gecos  - print GECOS field\n");
	fprintf(stderr, "  id     - print user ID\n");
	fprintf(stderr, "  gid    - print primary group ID\n");
	fprintf(stderr, "  group  - print primary group name\n");
	fprintf(stderr, "  groups - print all groups\n");
	fprintf(stderr, "Root-only commands:\n");
	fprintf(stderr, "  passwd - print encrypted password\n");
	fprintf(stderr, "  days   - print password aging information\n");
	fprintf(stderr,
		"  check USER [PASSWORD] - verify if password is correct\n");
	fprintf(stderr,
		"                          (reads from stdin if PASSWORD not provided)\n");

	exit(1);
}

static void print_shadow_days(const struct spwd *sp)
{
	printf("Last password change (days since Jan 1, 1970): %ld\n",
	       sp->sp_lstchg);
	printf("Minimum days between password changes: %ld\n", sp->sp_min);
	printf("Maximum days between password changes: %ld\n", sp->sp_max);
	printf("Warning days before password expires: %ld\n", sp->sp_warn);
	printf
	    ("Days after password expires until account becomes inactive: %ld\n",
	     sp->sp_inact);
	printf("Account expiration date (days since Jan 1, 1970): %ld\n",
	       sp->sp_expire);
}

static void print_groups(const char *username, gid_t primary_gid)
{
	struct group *gr;
	int ngroups = 0;
	gid_t *groups = NULL;

	// Get number of groups
	getgrouplist(username, primary_gid, NULL, &ngroups);

	groups = malloc(ngroups * sizeof(gid_t));
	if (groups == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return;
	}

	if (getgrouplist(username, primary_gid, groups, &ngroups) != -1) {
		printf("Groups: ");
		for (int i = 0; i < ngroups; i++) {
			gr = getgrgid(groups[i]);
			if (gr != NULL) {
				printf("%s(%d)%s", gr->gr_name, groups[i],
				       (i < ngroups - 1 ? ", " : ""));
			}
		}
		printf("\n");
	}

	free(groups);
}

// Helper function to print JSON string with proper escaping
static void print_json_string(const char *str)
{
	printf("\"");
	for (const char *p = str; *p; p++) {
		switch (*p) {
		case '"':
			printf("\\\"");
			break;
		case '\\':
			printf("\\\\");
			break;
		case '\b':
			printf("\\b");
			break;
		case '\f':
			printf("\\f");
			break;
		case '\n':
			printf("\\n");
			break;
		case '\r':
			printf("\\r");
			break;
		case '\t':
			printf("\\t");
			break;
		default:
			if ((unsigned char)*p >= 32 && (unsigned char)*p <= 126) {
				putchar(*p);
			} else {
				printf("\\u%04x", (unsigned char)*p);
			}
		}
	}
	printf("\"");
}

static void print_groups_json(const char *username, gid_t primary_gid)
{
	struct group *gr;
	int ngroups = 0;
	gid_t *groups = NULL;

	// Get number of groups
	getgrouplist(username, primary_gid, NULL, &ngroups);

	groups = malloc(ngroups * sizeof(gid_t));
	if (groups == NULL) {
		printf("[]");
		return;
	}

	if (getgrouplist(username, primary_gid, groups, &ngroups) != -1) {
		printf("[");
		for (int i = 0; i < ngroups; i++) {
			gr = getgrgid(groups[i]);
			if (gr != NULL) {
				if (i > 0)
					printf(",");
				printf("{\"name\":");
				print_json_string(gr->gr_name);
				printf(",\"gid\":%d}", groups[i]);
			}
		}
		printf("]");
	} else {
		printf("[]");
	}

	free(groups);
}

static void print_user_info_json(const char *username, int skip_password)
{
	struct passwd *pw;
	struct group *gr;
	struct spwd *sp;
	int is_root = (getuid() == 0);

	pw = getpwnam(username);
	if (pw == NULL) {
		printf("{\"error\":\"User '%s' not found\"}\n", username);
		return;
	}

	printf("{\n");
	printf("  \"user\":");
	print_json_string(pw->pw_name);
	printf(",\n");

	gr = getgrgid(pw->pw_gid);
	if (gr != NULL) {
		printf("  \"group\":");
		print_json_string(gr->gr_name);
		printf(",\n");
	}

	printf("  \"uid\":%u,\n", pw->pw_uid);
	printf("  \"gid\":%u,\n", pw->pw_gid);

	printf("  \"home\":");
	print_json_string(pw->pw_dir);
	printf(",\n");
	printf("  \"shell\":");
	print_json_string(pw->pw_shell);
	printf(",\n");

	if (pw->pw_gecos && strlen(pw->pw_gecos) > 0) {
		printf("  \"gecos\":");
		print_json_string(pw->pw_gecos);
		printf(",\n");
	}

	printf("  \"groups\":");
	print_groups_json(username, pw->pw_gid);

	if (is_root) {
		sp = getspnam(username);
		if (sp != NULL) {
			printf(",\n  \"shadow\": {\n");
			if (!skip_password) {
				printf("    \"encrypted_password\":");
				print_json_string(sp->sp_pwdp);
				printf(",\n");
			}
			printf("    \"last_change\":%ld,\n", sp->sp_lstchg);
			printf("    \"min_days\":%ld,\n", sp->sp_min);
			printf("    \"max_days\":%ld,\n", sp->sp_max);
			printf("    \"warn_days\":%ld,\n", sp->sp_warn);
			printf("    \"inactive_days\":%ld,\n", sp->sp_inact);
			printf("    \"expiration\":%ld\n", sp->sp_expire);
			printf("  }");
		}
	}
	printf("\n}\n");
}

static void print_user_info_text(const char *username, int skip_password)
{
	struct passwd *pw;
	struct group *gr;
	struct spwd *sp;
	int is_root = (getuid() == 0);

	pw = getpwnam(username);
	if (pw == NULL) {
		fprintf(stderr, "User '%s' not found\n", username);
		return;
	}

	printf("User Information for '%s':\n", username);
	printf("------------------------\n");
	printf("Username: %s\n", pw->pw_name);
	printf("User ID: %u\n", pw->pw_uid);
	printf("Primary group ID: %u\n", pw->pw_gid);

	gr = getgrgid(pw->pw_gid);
	if (gr != NULL) {
		printf("Primary group name: %s\n", gr->gr_name);
	}

	printf("Home directory: %s\n", pw->pw_dir);
	printf("Shell: %s\n", pw->pw_shell);

	if (pw->pw_gecos && strlen(pw->pw_gecos) > 0) {
		printf("GECOS: %s\n", pw->pw_gecos);
	}

	print_groups(username, pw->pw_gid);

	if (is_root) {
		printf("\nShadow Information (root only):\n");
		printf("-----------------------------\n");
		sp = getspnam(username);
		if (sp != NULL) {
			if (!skip_password) {
				printf("Encrypted password: %s\n", sp->sp_pwdp);
			}
			printf("Password Aging Information:\n");
			print_shadow_days(sp);
		} else {
			printf("No shadow information available\n");
		}
	}
}

static void print_user_info(const char *username, int json_output,
			    int skip_password)
{
	if (json_output) {
		print_user_info_json(username, skip_password);
	} else {
		print_user_info_text(username, skip_password);
	}
}

static int verify_password(const char *username, const char *password)
{
	struct spwd *sp;
	char *encrypted;

	if (getuid() != 0) {
		fprintf(stderr, "This command requires root privileges\n");
		return 1;
	}

	sp = getspnam(username);
	if (sp == NULL) {
		fprintf(stderr, "Failed to get shadow entry for '%s'\n",
			username);
		return 1;
	}
	// Encrypt the provided password with the salt from the shadow file
	encrypted = crypt(password, sp->sp_pwdp);
	if (encrypted == NULL) {
		fprintf(stderr, "crypt() failed\n");
		return 1;
	}
	// Compare the encrypted password with the one from shadow file
	return strcmp(encrypted, sp->sp_pwdp) == 0 ? 0 : 1;
}

static char *read_password(void)
{
	char *password = malloc(1024);
	if (password == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return NULL;
	}
	// Check if stdin is a terminal
	if (isatty(STDIN_FILENO)) {
		struct termios old_flags, new_flags;

		// Get current terminal settings
		if (tcgetattr(STDIN_FILENO, &old_flags) != 0) {
			fprintf(stderr, "Failed to get terminal attributes\n");
			free(password);
			return NULL;
		}
		// Disable echo
		new_flags = old_flags;
		new_flags.c_lflag &= ~ECHO;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_flags) != 0) {
			fprintf(stderr, "Failed to set terminal attributes\n");
			free(password);
			return NULL;
		}
		// Prompt and read password
		fprintf(stderr, "Password: ");
		if (fgets(password, 1024, stdin) == NULL) {
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_flags);
			fprintf(stderr, "\nFailed to read password\n");
			free(password);
			return NULL;
		}
		// Restore terminal settings
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_flags);
		fprintf(stderr, "\n");
	} else {
		// Reading from pipe or redirect
		if (fgets(password, 1024, stdin) == NULL) {
			fprintf(stderr, "Failed to read password\n");
			free(password);
			return NULL;
		}
	}

	// Remove trailing newline
	size_t len = strlen(password);
	if (len > 0 && password[len - 1] == '\n') {
		password[len - 1] = '\0';
	}

	return password;
}

int main(int argc, char *argv[])
{
	if (argc < 3) {
		usage(basename(argv[0]));
	}

	const char *cmd = argv[1];
	const char *username;
	struct passwd *pw;
	struct group *gr;
	struct spwd *sp;
	int json_output = 0;
	int skip_password = 0;
	int arg_offset = 0;

	// Handle flags for info command
	if (strcmp(cmd, "info") == 0) {
		int i = 2;
		while (i < argc - 1) {	// Leave room for username
			if (strcmp(argv[i], "-j") == 0) {
				json_output = 1;
				arg_offset++;
			} else if (strcmp(argv[i], "-i") == 0) {
				skip_password = 1;
				arg_offset++;
			} else {
				break;
			}
			i++;
		}

		if (argc != (3 + arg_offset)) {
			usage(basename(argv[0]));
		}
	}

	// Get username from correct position
	username = argv[2 + arg_offset];

	pw = getpwnam(username);
	if (pw == NULL) {
		fprintf(stderr, "User '%s' not found\n", username);
		return 1;
	}

	if (strcmp(cmd, "info") == 0) {
		print_user_info(username, json_output, skip_password);
	} else if (strcmp(cmd, "home") == 0) {
		printf("%s\n", pw->pw_dir);
	} else if (strcmp(cmd, "shell") == 0) {
		printf("%s\n", pw->pw_shell);
	} else if (strcmp(cmd, "gecos") == 0) {
		printf("%s\n", pw->pw_gecos);
	} else if (strcmp(cmd, "id") == 0) {
		printf("%u\n", pw->pw_uid);
	} else if (strcmp(cmd, "gid") == 0) {
		printf("%u\n", pw->pw_gid);
	} else if (strcmp(cmd, "group") == 0) {
		gr = getgrgid(pw->pw_gid);
		if (gr != NULL) {
			printf("%s\n", gr->gr_name);
		}
	} else if (strcmp(cmd, "groups") == 0) {
		print_groups(username, pw->pw_gid);
	} else if (strcmp(cmd, "passwd") == 0 || strcmp(cmd, "days") == 0) {
		if (getuid() != 0) {
			fprintf(stderr,
				"This command requires root privileges\n");
			return 1;
		}

		sp = getspnam(username);
		if (sp == NULL) {
			fprintf(stderr, "Failed to get shadow entry for '%s'\n",
				username);
			return 1;
		}

		if (strcmp(cmd, "passwd") == 0) {
			printf("%s\n", sp->sp_pwdp);
		} else {	// days
			print_shadow_days(sp);
		}
	} else if (strcmp(cmd, "check") == 0) {
		char *password;
		int result;

		if (argc == 3) {
			// Read password from stdin
			password = read_password();
			if (password == NULL) {
				return 1;
			}
		} else if (argc == 4) {
			// Use password from command line
			password = strdup(argv[3]);
			if (password == NULL) {
				fprintf(stderr, "Memory allocation failed\n");
				return 1;
			}
		} else {
			fprintf(stderr, "Usage: %s check USER [PASSWORD]\n",
				argv[0]);
			return 1;
		}

		result = verify_password(username, password);

		// Clean up sensitive data
		explicit_bzero(password, strlen(password));
		free(password);

		return result;
	} else {
		usage(basename(argv[0]));
	}

	return 0;
}
