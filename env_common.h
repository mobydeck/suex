#ifndef ENV_COMMON_H
#define ENV_COMMON_H

/*
 * Terminal and session variables to inherit from the calling environment
 * when building a clean login environment.
 */
static const char *session_vars[] = {
	"TERM", "COLORTERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION",
	"LANG", "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "LC_TIME",
	"LC_NUMERIC", "LC_MONETARY", "LC_COLLATE",
	"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY",
	"TMUX", "TMUX_PANE", "STY",
	"SSH_TTY", "SSH_CLIENT", "SSH_CONNECTION", "SSH_AUTH_SOCK",
	NULL
};

#endif /* ENV_COMMON_H */
