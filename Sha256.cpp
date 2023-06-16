// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Sha256.h"
#include <QFile>
#include <QCryptographicHash>

extern "C" {
#include <fcntl.h>
}

String Sha256::checksum(String const &filename) {
	QFile f(filename);
	f.open(QFile::ReadOnly);
	posix_fadvise(f.handle(), 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(f.handle(), 0, 0, POSIX_FADV_WILLNEED);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(&f);
	f.close();
	return hash.result().toHex();
}
