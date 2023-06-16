// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

extern "C" {
#include <fcntl.h>
}

/**
 * File descriptor storage that closes the file when the
 * variable goes out of scope. Simply use it instead of an
 * "int" when using open() and friends.
 */
class Fd {
public:
	Fd(int fd):_fd(fd) {}
	~Fd() { close(_fd); }
	operator int() { return _fd; }
private:
	int _fd;
};
