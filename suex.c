#include <sys/types.h>
#include <stdarg.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *argv0;

static void usage(int exitcode)
{
	printf("Usage: %s USER[:GROUP] COMMAND [ARGUMENTS...]\n",
	       basename(argv0));
	exit(exitcode);
}

static void die(int code, const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", basename(argv0));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (errno)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	exit(code);
}

int main(int argc, char *argv[])
{
	char *user, *group, **cmdargv;
	char *end;

	uid_t uid = getuid();
	gid_t gid = getgid();

	argv0 = argv[0];
	if (argc < 3)
		usage(0);

	if (getuid() != 0) {
		die(1, "This program must be run as root");
	}
	user = argv[1];
	group = strchr(user, ':');
	if (group) {
		*group++ = '\0';
	}
	cmdargv = &argv[2];

	struct passwd *pw = NULL;
	if (user[0] != '\0') {
		uid_t nuid = strtol(user, &end, 10);
		if (*end == '\0') {
			if (nuid == 0) {
				die(1, "Cannot run as root (UID 0)");
			}
			uid = nuid;
		} else {
			pw = getpwnam(user);
			if (pw == NULL) {
				die(1, "Failed to find user '%s'", user);
			}
			if (pw->pw_uid == 0) {
				die(1, "Cannot run as root user '%s'", user);
			}
		}
	}
	if (pw == NULL) {
		pw = getpwuid(uid);
	}
	if (pw != NULL) {
		if (pw->pw_uid == 0) {
			die(1, "Cannot run as root user");
		}
		uid = pw->pw_uid;
		gid = pw->pw_gid;
	}
	setenv("HOME", pw != NULL ? pw->pw_dir : "/", 1);

	if (group && group[0] != '\0') {
		pw = NULL;
		gid_t ngid = strtol(group, &end, 10);
		if (*end == '\0') {
			if (ngid == 0) {
				die(1, "Cannot run with root group (GID 0)");
			}
			gid = ngid;
		} else {
			struct group *gr = getgrnam(group);
			if (gr == NULL) {
				die(1, "Failed to find group '%s'", group);
			}
			if (gr->gr_gid == 0) {
				die(1, "Cannot run with root group '%s'",
				    group);
			}
			gid = gr->gr_gid;
		}
	}
	if (pw == NULL) {
		if (setgroups(1, &gid) < 0) {
			die(1, "Failed to set supplemental groups for GID %d",
			    gid);
		}
	} else {
		int ngroups = 0;
		gid_t *glist = NULL;

		while (1) {
			int r = getgrouplist(pw->pw_name, gid, glist, &ngroups);

			if (r >= 0) {
				if (setgroups(ngroups, glist) < 0)
					die(1,
					    "Failed to set supplemental groups for user '%s'",
					    pw->pw_name);
				break;
			}
			glist = realloc(glist, ngroups * sizeof(gid_t));
			if (glist == NULL)
				die(1,
				    "Memory allocation failed for group list");
		}
	}

	if (setgid(gid) < 0) {
		die(1, "Failed to set GID to %d", gid);
	}
	if (setuid(uid) < 0) {
		die(1, "Failed to set UID to %d", uid);
	}
	execvp(cmdargv[0], cmdargv);
	die(1, "Failed to execute '%s'", cmdargv[0]);

	return 1;
}
