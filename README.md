# suex

A lightweight privilege switching tool for executing programs with different user and group
permissions, `su` and `sudo` alternative.

## Purpose

`suex` is a utility that allows you to run programs with different user and group privileges.
Unlike traditional tools like `su` or `sudo`, `suex` executes programs directly rather than
as child processes, which provides better handling of TTY and signals.

**Important**: `suex` requires root privileges to operate as it performs uid/gid changes.

## Key Features

- Direct program execution (not spawning child processes)
- Support for both username/group names and numeric uid/gid
- Simpler and more streamlined than traditional `su`/`sudo`
- Better TTY and signal handling

## Usage

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

## Attribution

This project is a reimplementation of [su-exec](https://github.com/ncopa/su-exec),
enhanced for improved usability and maintainability.
