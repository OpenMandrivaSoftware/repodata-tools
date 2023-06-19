// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Archive.h"
extern "C" {
#include <archive_entry.h>
}

Archive::Archive(String const &filename, int format):_isOpen(true) {
	_archive = archive_write_new();
	archive_write_set_format(_archive, format);
	archive_write_open_filename(_archive, filename);
}

Archive::~Archive() {
	close();
}

void Archive::close() {
	if(!_isOpen)
		return;
	_isOpen = false;
	archive_write_close(_archive);
	archive_write_free(_archive);
}

bool Archive::addFile(String const &filename, QByteArray const &contents) const {
	archive_entry *e = archive_entry_new();
	if(!e)
		return false;
	bool ret = true;
	archive_entry_set_pathname(e, filename);
	archive_entry_set_size(e, contents.size());
	archive_entry_set_filetype(e, AE_IFREG);
	archive_entry_set_perm(e, 0644);
	archive_write_header(_archive, e);
	if(archive_write_data(_archive, contents.constData(), contents.length()) != ARCHIVE_OK)
		ret = false;
	archive_entry_free(e);
	return ret;
}
