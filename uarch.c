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

// Mapping table: system/kernel names → normalized names
// original_arch: non-NULL when the kernel name differs from the canonical Linux name
static const struct arch_map arch_mappings[] = {
	// x86
	{"x86_64", "amd64", NULL},
	{"x86-64", "amd64", NULL},
	{"i686", "i386", NULL},
	{"i586", "i386", NULL},
	{"i486", "i386", NULL},
	{"i386", "i386", NULL},
	// ARM 64-bit
	{"aarch64", "arm64", NULL},
	{"arm64", "arm64", "aarch64"},	// macOS reports arm64 instead of aarch64
	// ARM 32-bit
	{"armv7l", "armhf", NULL},
	{"armv7", "armhf", NULL},
	{"armv6l", "armel", NULL},
	{"armv5tel", "armel", NULL},
	{"armv5l", "armel", NULL},
	// RISC-V
	{"riscv64", "riscv64", NULL},
	// IBM / PowerPC
	{"s390x", "s390x", NULL},
	{"ppc64le", "ppc64el", NULL},
	{"ppc64", "ppc64", NULL},
	{"powerpc", "powerpc", NULL},
	// MIPS
	{"mips64el", "mips64el", NULL},
	{"mips64", "mips64", NULL},
	{"mipsel", "mipsel", NULL},
	{"mips", "mips", NULL},
	// LoongArch
	{"loongarch64", "loong64", NULL},
	{NULL, NULL, NULL}	// Terminator
};

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-a] [ARCH]\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -a    Print original kernel name instead of normalized name\n\n");
	fprintf(stderr,
		"Without ARCH argument, detects the current system architecture.\n");
	fprintf(stderr,
		"With ARCH argument, converts the given name without calling uname.\n\n");
	fprintf(stderr, "Normalized names:\n");
	fprintf(stderr, "  amd64    x86_64\n");
	fprintf(stderr, "  i386     x86 32-bit\n");
	fprintf(stderr, "  arm64    aarch64 (64-bit ARM)\n");
	fprintf(stderr, "  armhf    ARMv7 hard float\n");
	fprintf(stderr, "  armel    ARMv6 and earlier\n");
	fprintf(stderr, "  riscv64  RISC-V 64-bit\n");
	fprintf(stderr, "  s390x    IBM Z\n");
	fprintf(stderr, "  ppc64el  PowerPC 64-bit little-endian\n");
	fprintf(stderr, "  mips64el MIPS 64-bit little-endian\n");
	fprintf(stderr, "  loong64  LoongArch 64-bit\n");
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

static void print_arch(const char *arch_str, int show_original)
{
	const struct arch_map *mapping;
	for (mapping = arch_mappings; mapping->system_arch != NULL; mapping++) {
		if (strcmp(arch_str, mapping->system_arch) == 0) {
			if (show_original) {
				printf("%s\n",
				       mapping->original_arch ? mapping->
				       original_arch : mapping->system_arch);
			} else {
				printf("%s\n", mapping->unofficial_arch);
			}
			return;
		}
	}
	// No match: print as-is
	printf("%s\n", arch_str);
}

int main(int argc, char *argv[])
{
	struct utsname un;
	int show_original = 0;
	int opt;

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

	// Converter mode: arch name given as argument
	if (optind < argc) {
		print_arch(argv[optind], show_original);
		return 0;
	}
	// Detection mode: query the current system
	if (uname(&un) < 0) {
		perror("uname");
		return 1;
	}
	// macOS on x86_64: uname reports x86_64 even under Rosetta, handle explicitly
	if (is_macos() && strcmp(un.machine, "x86_64") == 0) {
		if (show_original) {
			printf("x86_64\n");
			return 0;
		}
	}

	print_arch(un.machine, show_original);
	return 0;
}
