// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include "String.h"
extern "C" {
#include <archive.h>
}

class Archive {
public:
	Archive(String const &filename, int format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED);
	~Archive();
	bool addFile(String const &filename, QByteArray const &contents) const;
	void close();
private:
	archive *_archive;
	bool _isOpen;
};
