#ifndef AUTH_COMMON_H
#define AUTH_COMMON_H

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

// Maximum number of groups to check
#define MAX_GROUPS 100
// Name of the group that can use this utility
#define SUEX_GROUP "suex"

// Check if the current user belongs to the suex group
int user_in_suex_group(void);

#endif /* AUTH_COMMON_H */
