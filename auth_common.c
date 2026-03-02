#include "auth_common.h"

// Check if the current user belongs to the suex group
int user_in_suex_group(void)
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
