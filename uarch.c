#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdlib.h>

// Structure to map system architecture to unofficial name
struct arch_map {
	const char *system_arch;
	const char *unofficial_arch;
	const char *original_arch;	// For -a flag, NULL means use system_arch
};

// Mapping table with original architecture names
static const struct arch_map arch_mappings[] = {
	{"x86_64", "amd64", NULL},
	{"i686", "i386", NULL},
	{"i586", "i386", NULL},
	{"i486", "i386", NULL},
	{"i386", "i386", NULL},
	{"aarch64", "arm64", NULL},
	{"arm64", "arm64", "aarch64"},	// macOS reports arm64
	{"armv7l", "armhf", NULL},
	{"armv6l", "armhf", NULL},
	{"mips", "mips", NULL},
	{"mips64", "mips", NULL},
	{"ppc64le", "ppc64el", NULL},
	{NULL, NULL, NULL}	// Terminator
};

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-a]\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -a    Print system architecture instead of unofficial name\n");
	fprintf(stderr,
		"\nMaps system architecture to unofficial Linux architecture names:\n");
	fprintf(stderr, "  amd64    - 64-bit x86 architecture\n");
	fprintf(stderr, "  i386     - 32-bit x86 architecture\n");
	fprintf(stderr, "  armhf    - ARM Hard Float\n");
	fprintf(stderr, "  arm64    - 64-bit ARM architecture\n");
	fprintf(stderr, "  mips     - MIPS architecture\n");
	fprintf(stderr, "  ppc64el  - PowerPC 64-bit little-endian\n");
	exit(1);
}

// Check if running on macOS
static int is_macos(void)
{
#ifdef __APPLE__
	return 1;
#else
	return 0;
#endif
}

int main(int argc, char *argv[])
{
	struct utsname un;
	int show_original = 0;
	int opt;

	// Parse command line options
	while ((opt = getopt(argc, argv, "ah")) != -1) {
		switch (opt) {
		case 'a':
			show_original = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}

	// Get system information
	if (uname(&un) < 0) {
		perror("uname");
		return 1;
	}
	// Handle macOS x86_64 special case
	if (is_macos() && strcmp(un.machine, "x86_64") == 0) {
		if (show_original) {
			printf("x86_64\n");
			return 0;
		}
	}
	// Look up the architecture mapping
	const struct arch_map *mapping;
	for (mapping = arch_mappings; mapping->system_arch != NULL; mapping++) {
		if (strcmp(un.machine, mapping->system_arch) == 0) {
			if (show_original) {
				// If original_arch is specified, use it, otherwise use system_arch
				printf("%s\n",
				       mapping->original_arch ? mapping->
				       original_arch : mapping->system_arch);
			} else {
				printf("%s\n", mapping->unofficial_arch);
			}
			return 0;
		}
	}

	// If no match found, print the system architecture
	printf("%s\n", un.machine);
	return 0;
}
