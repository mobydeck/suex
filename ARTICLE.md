# sudo is 45 Years Old. Your Container Doesn't Care.

There is a small binary sitting on almost every Linux system on the planet. It weighs a few megabytes, has tens of thousands of lines of code, ships with its own configuration language, and has had enough CVEs over the years to fill a decent-sized spreadsheet. Most of us use it dozens of times a day without thinking about it.

That binary is `sudo`.

And nowadays, in a world of Docker containers, ephemeral CI runners, and Kubernetes pods that live for thirty seconds and die without ceremony, most of what `sudo` does is completely irrelevant to the job we actually need it to do.

This is the story of what that job actually is, why it got so complicated, and how a tiny C program can do it better.

---

## The Job Is Actually Simple

When you type `sudo some-command`, the thing you actually want is: run this program with a different user's privileges. That is the whole job.

The kernel has known how to do this since the 1970s. The mechanism is called the setuid bit, and it is genuinely elegant. Before we talk about `sudo`, it helps to understand what the kernel is doing underneath all of it.

### A Small File, A Big Bit

Every file on a Linux system has permission bits. You know the usual ones: read, write, execute for owner, group, and others. But there are three more bits above those nine, and one of them is the setuid bit.

When the setuid bit is set on an executable, the kernel does something interesting when someone runs it: instead of using the calling user's UID as the effective user ID of the process, it uses the file owner's UID. The running process gets to be someone else.

This is how `passwd` works. When you type `passwd` to change your password, that program needs to write to `/etc/shadow`, which is only readable and writable by root. But the `passwd` binary itself is owned by root and has the setuid bit set:

```
-rwsr-xr-x 1 root root 68208 /usr/bin/passwd
```

See that `s` where the `x` should be for the owner? That is the setuid bit. When you run `passwd`, the kernel sees that bit, looks at the file owner (root), and sets the effective UID of the process to 0. Your shell's UID is still your regular user. But the `passwd` process runs as root.

The kernel handles this during `execve()`. Internally, every process has a credential structure that tracks multiple UIDs: the real UID (who you actually are), the effective UID (who you appear to be for permission checks), and the saved set-user-ID (a saved copy of the effective UID from before any changes). When `execve()` runs a setuid binary, the kernel copies the file owner's UID into the effective UID of the new process, and saves a copy. Clean, simple, no daemon needed.

### So Why Do sudo and su Exist?

Because the plain setuid bit has a limitation: it only switches to the file owner. If you want to switch to an arbitrary user, you need a program that is setuid root, looks up the target user, and calls `setuid()` to change the effective UID to whatever you want.

That is the core of what `sudo` and `su` do. They are setuid root programs that call `setuid()`.

Everything else in `sudo` is policy enforcement: who is allowed to run what, as whom, under what conditions. That policy machinery is where the complexity lives.

---

## How sudo Actually Works

`sudo` is a setuid root program. When it runs, its effective UID is 0 from the moment the kernel launches it. Then it does roughly the following:

1. Reads `/etc/sudoers` (and any files in `/etc/sudoers.d/`)
2. Figures out who you are (real UID)
3. Checks if your user or group has permission to run the requested command as the requested target user
4. Optionally prompts for a password and verifies it against PAM or the shadow file
5. Calls `setuid()` and `setgid()` to drop to the target user
6. Calls `exec()` to run your command

The sudoers format has grown into something that deserves its own certification. You can specify commands by path, by regex, by alias. You can limit which target users are allowed. You can set environment variables, specify password timeout behavior, require TTY, use `NOPASSWD`. There are `Defaults` stanzas that affect global behavior. There is a `visudo` command specifically because the file format is delicate enough that a typo can lock you out.

`su` is somewhat simpler but has its own mess. It spawns a child process (not a clean `exec`), sets up a new session, optionally invokes PAM, deals with terminal handling, and has to forward signals from itself to the child. The classic problem with `su` in containers shows up immediately:

```bash
# Running in a Docker container
$ docker run -it --rm alpine su postgres -c 'ps aux'
PID   USER     TIME   COMMAND
    1 postgres   0:00 ash -c ps aux
   12 postgres   0:00 ps aux
```

Two processes. The `su` itself spawned a shell (`ash -c`), and the shell spawned `ps`. Your init system, or anything watching PID 1, now sees the wrong thing. Signal handling gets weird. SIGTERM goes to the shell, not your actual program. The shell might or might not forward it.

---

## The Complexity Problem in a Containerized World

Here is the situation. You have a Docker image. At startup, your entrypoint script needs to drop from root to a less privileged user before running the main process. You need one thing: run this program as this user.

For this you do not need:
- A configuration language
- Password prompts
- PAM integration
- An audit log
- Session management
- TTY negotiation
- `/etc/sudoers`

All of that is overhead for a multi-user timesharing environment that does not exist in your container. Your container has one user, a single purpose, and a lifetime measured in seconds or hours. But `sudo` carries the weight of forty-five years of enterprise timesharing assumptions.

Beyond the complexity overhead, there is the security surface. `sudo` in 2021 had CVE-2021-3156: a heap-based buffer overflow in the way it parsed command arguments, present in every `sudo` version for over ten years, exploitable to get a root shell on any standard Linux installation. In 2025, two more critical CVEs landed: CVE-2025-32462 and CVE-2025-32463. The second one, rated CVSS 9.3, allowed an attacker to trick `sudo` into loading a malicious shared library by manipulating `/etc/nsswitch.conf` in a chroot directory, achieving arbitrary code execution as root.

More code, more attack surface. It is not complicated.

---

## The Alternatives That Got Partway There

The container ecosystem noticed this problem, and a few projects tried to solve it.

### gosu

`gosu` was created by Tianon Greer specifically for Docker use cases. It does the right thing: parse arguments, switch user, call `exec()`. No intermediate shell, no child process. The spawned command replaces `gosu` entirely, inheriting its file descriptors, becoming PID 1 if that is what was started, and handling signals exactly as if `gosu` had never existed.

```dockerfile
ENTRYPOINT ["gosu", "www-data", "nginx", "-g", "daemon off;"]
```

Clean. Works. But `gosu` is written in Go. The compiled binary pulls in Go's runtime and standard library. The result is roughly 1.8MB on Alpine, which does not sound catastrophic until you are doing this for hundreds of microservices and every byte of your base image matters. More importantly, Go's runtime starts a small goroutine scheduler, meaning you briefly have more than one thread before the `exec()` call. Mostly harmless, but philosophically irritating for something so simple.

### su-exec

Alpine Linux adopted `su-exec` as their alternative. Written in C, the entire program is under 200 lines and compiles to about 10KB. It calls `setuid()`, `setgid()`, `setgroups()`, then `execvp()`. Done.

```dockerfile
RUN apk add su-exec
ENTRYPOINT ["su-exec", "www-data:www-data", "nginx"]
```

This is much closer to the right answer. But `su-exec` has no access control whatsoever. It runs as a setuid root binary, and anyone who can execute it can switch to any user. In a container that is often fine, but the moment you want to use this pattern outside a container (in a build system, on a shared host, in a CI environment with multiple users), you have a problem.

### tini and dumb-init

These are init process supervisors that solve a related but different problem: PID 1 in containers needs to reap zombie processes and forward signals properly. They are not privilege-switching tools. Worth knowing about, but a different category.

---

## The Missing Piece: Access Control Without the Overhead

What if you want to switch users as cleanly as `su-exec`, with the direct `exec()` model, but with actual access control that works outside a container? You want:

1. A setuid root binary that performs a clean `exec()` into your target process
2. Some mechanism to control who can use it
3. No configuration language, no password prompts, no PAM, no daemons

Linux already has the mechanism for item two: groups. The `suex` approach is to use a dedicated Unix group as the access control list. If you are in the `suex` group, you can use the tool. If you are not, you cannot. The kernel enforces group membership. No policy files required.

This is `suex`.

---

## suex and sush: The Direct Path

`suex` and its companion `sush` (a shell launcher built on the same model) are available at [https://github.com/mobydeck/suex](https://github.com/mobydeck/suex). Both are small C programs that use the setuid mechanism directly and cleanly.

### Setup

```bash
# Create the access control group
groupadd --system suex

# Set the setuid bit and restrict to the suex group
chown root:suex suex sush
chmod 4750 suex sush

# Grant access to a specific user
usermod -a -G suex youruser
```

The `chmod 4750` is where it all comes together. `4` sets the setuid bit. `7` gives the owner (root) full permissions. `5` gives the `suex` group read and execute. `0` gives everyone else nothing. A user outside the `suex` group cannot even execute the binary. The kernel enforces this before a single line of C code runs.

### Running Commands

```bash
# As root, drop to a less privileged user
suex www-data nginx -g 'daemon off;'

# As a suex group member, run something as root
suex /usr/sbin/iptables -L

# Switch to a specific user and group
suex deploy:deploygroup /usr/bin/run-deployment

# Run in a clean login environment
suex -l postgres /usr/bin/pg_ctl start

# Open an interactive shell as another user
sush postgres
```

### What the Code Actually Does

The core logic in `suex.c` is direct and auditable. After checking that you are either root or in the `suex` group, the program:

1. Parses the target user and group from the argument
2. Looks up the user in `/etc/passwd` with `getpwnam()` or `getpwuid()`
3. Calls `setup_groups()` which uses `getgrouplist()` and `setgroups()` to configure the full supplementary group list for the target user
4. Calls `setgid()` to set the primary group
5. Calls `setuid()` to set the user ID
6. Calls `execvp()` to replace itself with your command

That last step is critical. `execvp()` does not fork. It replaces the current process image entirely. The `suex` binary ceases to exist. Your command becomes the process, with the same PID, the same file descriptors, the same signal disposition. `suex` is not in the process tree at all.

Compare the two models:

```
# With sudo or su:
PID 1 --- sudo --- your-command

# With suex:
PID 1 --- your-command
```

In containers, this matters for PID 1 behavior. In pipelines, this matters for signal handling. In scripts, this matters because you are not accidentally wrapping your process in extra layers.

### The Groups Problem That su-exec Ignores

One thing that `su-exec` gets wrong and `suex` gets right: supplementary groups. When a user belongs to multiple groups (say `docker`, `www-data`, and `developers`), those memberships need to be configured for the target process, not just the primary group.

`suex` calls `getgrouplist()` followed by `setgroups()` to ensure the target process has the complete group membership of the target user. This is what `su -` and `sudo -u` do correctly. Many minimal alternatives skip this step, which means the spawned process cannot access group-owned resources it should legitimately have access to. That is a subtle bug that can take a long time to track down.

### sush: When You Need a Shell

`sush` is the companion for interactive use. It builds on the same model but handles the additional ceremony of launching a proper login shell:

```bash
sush username          # launches username's default shell
sush -s /bin/zsh art   # launches zsh for user art
```

Looking at `sush.c`, it explicitly builds a clean environment array: `HOME`, `SHELL`, `USER`, `LOGNAME`, `PATH`, `MAIL`, and `TERM`. It constructs the shell's `argv[0]` with a leading dash:

```c
sprintf(shell_args[0], "-%s", shell_name);
```

That leading dash is the Unix convention for telling a shell it was started as a login shell, triggering it to read `.profile`, `.bash_profile`, or whatever the equivalent is. Then it calls `execve()` with the explicit environment array, not inheriting the parent's environment variables.

The PATH setup is opinionated in a good way. Root gets the full admin path. Non-root users get `/usr/local/bin:/usr/bin:/bin`. Both get `~/.local/bin` prepended, respecting the modern convention for user-installed tools.

---

## The Two-Mode Model

`suex` has dual behavior depending on who runs it, which maps cleanly to real use cases.

For root users, `suex` works like `su`: you are stepping down to lower privileges. A container entrypoint running as root uses `suex www-data app-server` to drop privileges before exec-ing the main process. No password prompt, no policy check, just the privilege drop you requested.

For non-root users in the `suex` group, `suex` works like a scoped `sudo`: you can elevate to root or switch to specific users. This is the CI runner, the deployment account, the build system user that needs to run one specific thing as a different user. The access control is group membership, enforced by the filesystem.

---

## Security: What You Get and What You Trade

The `suex` model is deliberately minimal.

What you get:

**Access control via groups**: Enforced by the kernel's filesystem permission checks before any userspace code runs. No policy file to misconfigure, no parser to exploit.

**Clean execution**: The target process runs directly, not as a child of a privilege manager. Signals go to the right place. TTY works correctly. PID is what you expect.

**Full user context**: Proper UID, GID, and supplementary groups for the target user. Not a stripped-down version that breaks group-based permissions.

**Auditability**: The binary is small enough to read in an afternoon. No plugin system, no shared library loading, no complex parsing. Attack surface proportional to feature set.

What you trade:

**Fine-grained command authorization**: You cannot say user Alice can run `/usr/bin/systemctl` but not `/usr/bin/bash`. If you need that level of control, sudoers exists for a reason.

**Password prompts**: The access decision is made at the group level. If you need mandatory password confirmation for privileged operations, this is not your tool.

**Audit logging**: No per-command entries in syslog. Group membership is your audit trail.

Most of what you trade is machinery you did not need in the first place for the environments where `suex` excels.

---

## The Pattern in Practice

A Dockerfile entrypoint that uses `suex` cleanly:

```dockerfile
FROM debian:bookworm-slim

COPY suex /sbin/suex
RUN chown root:root /sbin/suex && chmod 4755 /sbin/suex

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
```

```bash
#!/bin/sh
# entrypoint.sh - runs as root, drops to app user

# Do root-only initialization
chown -R app:app /data

# Replace this process with the app, running as app user
exec suex app "$@"
```

The `exec` in the shell script replaces the shell process with `suex`. `suex` then replaces itself with your application via `execvp()`. The final result: your container's PID 1 is exactly the application you care about, running as exactly the user you specified, with no supervisor or wrapper anywhere in the tree.

A build system using privilege-separated tasks:

```bash
# The CI agent is a member of the suex group
# Run the deployment script as the deploy user
suex deploy /usr/local/bin/deploy-to-staging

# Open an interactive session for debugging
sush deploy
```

---

## The Kernel Does the Heavy Lifting

It is worth pausing on what is not happening here. There is no daemon. There is no IPC channel to a privilege broker. There is no configuration file being parsed. There is no shared library being loaded at privilege escalation time.

The kernel is doing the security-critical work. It enforces that `suex` is owned by root and has the setuid bit. It enforces that only users in the `suex` group can execute the binary. It handles the UID and GID transitions in the kernel's own credential management code when `setuid()` and `setgid()` are called.

The setuid mechanism has been in Unix since 1979. It has been analyzed, formally studied, and tested across forty-five years and millions of systems. `suex` does not abstract over that mechanism. It uses the mechanism itself, with minimal indirection between the kernel's capability and the result you want.

---

## So, Is This For You?

If you are running containers and your entrypoint needs to drop privileges cleanly: yes, `suex` handles that better than `gosu` and with more safety than `su-exec`.

If you manage a shared build or CI environment and need a controlled way to switch users without giving out root passwords or maintaining sudoers files: yes, `suex` is a good fit.

If you run a traditional multi-user Linux system where different users need to run different commands with different privileges and you need auditing, password confirmation, and fine-grained command whitelisting: use `sudo`. It exists for a reason and does that job well.

The point is not that `sudo` is bad. It is a solution to a different problem, and carrying it into a world of ephemeral containers and automated pipelines means carrying forty-five years of assumptions that do not apply.

`suex` and `sush` are the Unix idiom applied correctly to where computing actually is now: small, focused, built on mechanisms the kernel already provides, and simple enough to understand in twenty minutes of reading.

That is the whole job. No more, no less.

---

*`suex` and `sush` are available at [https://github.com/mobydeck/suex](https://github.com/mobydeck/suex). MIT licensed, written in C, no dependencies beyond libc.*
