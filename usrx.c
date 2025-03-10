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

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s COMMAND USER\n", progname);
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

static void print_user_info(const char *username)
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
			printf("Encrypted password: %s\n", sp->sp_pwdp);
			printf("\nPassword Aging Information:\n");
			print_shadow_days(sp);
		} else {
			printf("No shadow information available\n");
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		usage(basename(argv[0]));
	}

	const char *cmd = argv[1];
	const char *username = argv[2];
	struct passwd *pw;
	struct group *gr;
	struct spwd *sp;

	pw = getpwnam(username);
	if (pw == NULL) {
		fprintf(stderr, "User '%s' not found\n", username);
		return 1;
	}

	if (strcmp(cmd, "info") == 0) {
		print_user_info(username);
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
	} else {
		usage(basename(argv[0]));
	}

	return 0;
}
