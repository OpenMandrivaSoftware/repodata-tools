// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include "String.h"
#include <QList>

extern "C" {
#include <rpm/rpmfiles.h>
}

/**
 * A file name.
 */
class FileName:public String {
public:
	FileName(char const *data, qsizetype size = -1):String(data, size) {}
	FileName(QString const &s):String(s) {}
	char const * const basename() const;
	char const * const dirname() const;
};

class FileInfo {
public:
	FileInfo(FileName const &name, enum rpmfileAttrs_e attr, mode_t mode):_name(name),_attributes(attr),_mode(mode) {}
	FileName const &name() const { return _name; }
	rpmfileAttrs_e attributes() const { return _attributes; }
	mode_t mode() const { return _mode; }
private:
	FileName _name;
	enum rpmfileAttrs_e _attributes;
	mode_t _mode;
};

class Files:public QList<FileInfo> {
};

class FileNames:public QList<FileName> {
};
