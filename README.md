# suex

A lightweight privilege switching tool for executing programs with different user and group
permissions, `su` and `sudo` alternative.

# usrx

A companion utility for querying user information from system files (`/etc/passwd`, `/etc/group`,
and `/etc/shadow`).

## Purpose

`suex` is a utility that allows you to run programs with different user and group privileges.
Unlike traditional tools like `su` or `sudo`, `suex` executes programs directly rather than
as child processes, which provides better handling of TTY and signals.

**Important**: `suex` requires root privileges to operate as it performs uid/gid changes.

`usrx` provides a simple command-line interface to retrieve various user-related information
from system files. It can query basic user information, group memberships, and (with root
privileges) password-related data.

## Key Features

- Direct program execution (not spawning child processes)
- Support for both username/group names and numeric uid/gid
- Simpler and more streamlined than traditional `su`/`sudo`
- Better TTY and signal handling

## `suex` usage

Basic syntax:
```shell
suex USER[:GROUP] COMMAND [ARGUMENTS...]
```

Where:
- `USER`: Username or numeric uid
- `GROUP`: (Optional) Group name or numeric gid
- `COMMAND`: The program to execute
- `ARGUMENTS`: Any additional arguments for the command

### Examples

Run nginx with specific user and group:
```shell
suex nginx:www-data /usr/sbin/nginx -c /etc/nginx/nginx.conf
```

Run a program with just a different user:
```shell
suex nobody /bin/program
```

Using numeric IDs:
```shell
suex 100:1000 /bin/program
```

## Advantages Over su/sudo

The main advantage of `suex` is its direct execution model. When using traditional tools
like `su`, commands are executed as child processes, which can lead to complications with
TTY handling and signal processing. `suex` avoids these issues by executing the program
directly.

```shell
# with su
$ docker run -it --rm alpine:edge su postgres -c 'ps aux'
PID   USER     TIME   COMMAND
    1 postgres   0:00 ash -c ps aux
   12 postgres   0:00 ps aux

# with suex
$ docker run -it --rm -v $PWD/suex:/sbin/suex:ro alpine:edge suex postgres ps aux
PID   USER     TIME   COMMAND
    1 postgres   0:00 ps aux
```

## `usrx` usage

Basic syntax:
```shell
usrx COMMAND USER
```

### Available Commands

Standard commands (available to all users):
- `info` - Display all available information about the user
- `home` - Print user's home directory
- `shell` - Print user's login shell
- `gecos` - Print user's GECOS field
- `id` - Print user's UID
- `gid` - Print user's primary GID
- `group` - Print user's primary group name
- `groups` - Print all groups the user belongs to

Root-only commands (requires root privileges):
- `passwd` - Print user's encrypted password
- `days` - Print detailed password aging information

### Examples

Get user's home directory:
```shell
$ usrx home username
/home/username
```

List all groups for a user:
```shell
$ usrx groups username
users sudo docker developers
```

Get comprehensive user information (as root):
```shell
$ sudo usrx info username
User Information for 'username':
------------------------
Username: username
User ID: 1000
Primary group ID: 1000
Primary group name: username
Home directory: /home/username
Shell: /bin/bash
GECOS field: John Doe
Groups: username(1000), sudo(27), docker(998)

Shadow Information (root only):
-----------------------------
[password and aging information]
```

## Security Notes

- The `passwd` and `days` commands require root privileges as they access `/etc/shadow`
- When installed setuid root (`sudo chmod u+s usrx`), these commands become available to all users
- Consider the security implications before setting the setuid bit


## Attribution

This project is a reimplementation of [su-exec](https://github.com/ncopa/su-exec),
enhanced for improved usability and maintainability.
