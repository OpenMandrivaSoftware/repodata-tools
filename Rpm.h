// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include "FileName.h"
#include <string>
#include <iostream>

extern "C" {
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
}

enum class DepType {
	Provides = 0,
	Requires,
	Conflicts,
	Obsoletes,
	Recommends,
	Suggests,
	Supplements,
	Enhances
};

class Dependency {
public:
	Dependency(String const &name, uint64_t flags=0, String const &version=String()):_name(name),_flags(flags),_version(version) {}
	String const &name() const { return _name; }
	uint64_t flags() const { return _flags; }
	String repoMdFlags() const;
	String const &version() const { return _version; }
	String repoMdVersion() const;
	String repoMd() const;
private:
	String _name;
	uint64_t _flags;
	String _version;
};

/**
 * Retrieve information about rpms
 */
class Rpm {
public:
	Rpm(FileName const &filename);
	~Rpm();
	Files fileList(bool onlyPrimary=false) const;
	String fileListMd(bool onlyPrimary=false) const;
	String name() const { return headerString(RPMTAG_NAME); }
	String arch() const { return headerString(RPMTAG_ARCH); }
	int epoch() const { return headerNumber(RPMTAG_EPOCH); }
	String version() const { return headerString(RPMTAG_VERSION); }
	String repoMdVersion() const;
	String release() const { return headerString(RPMTAG_RELEASE); }
	String summary() const { return headerString(RPMTAG_SUMMARY); }
	String description() const { return headerString(RPMTAG_DESCRIPTION); }
	String packager() const { return headerString(RPMTAG_PACKAGER); }
	String url() const { return headerString(RPMTAG_URL); }
	time_t time() const { return _fileMtime; }
	time_t buildTime() const { return headerNumber(RPMTAG_BUILDTIME); }
	size_t size() const { return _fileSize; }
	size_t installedSize() const { return headerNumber(RPMTAG_LONGSIZE); }
	size_t archiveSize() const { return headerNumber(RPMTAG_ARCHIVESIZE); }
	String license() const { return headerString(RPMTAG_LICENSE); }
	String vendor() const { return headerString(RPMTAG_VENDOR); }
	String group() const { return headerString(RPMTAG_GROUP); }
	String buildHost() const { return headerString(RPMTAG_BUILDHOST); }
	String sourceRpm() const { return headerString(RPMTAG_SOURCERPM); }
	uint64_t headersStart() const { return _headersStart; }
	uint64_t headersEnd() const { return _headersEnd; }
	QList<Dependency> dependencies(enum DepType type = DepType::Provides) const;
	String dependenciesMd(enum DepType type) const;
	String dependenciesMd() const;
	String sha256();
	/**
	 * Wrapper around rpmlib headerGetString, mostly for internal use
	 */
	String headerString(rpmTagVal tag) const { return headerGetString(_hdr, tag); }
	/**
	 * Wrapper around rpmlib headerGetString, mostly for internal use
	 */
	uint64_t headerNumber(rpmTagVal tag) const { return headerGetNumber(_hdr, tag); }
private:
	static void initRpm();
private:
	static rpmts _ts;
	FileName const	_filename;
	Header	_hdr;
	String		_sha256;
	uint32_t	_headersStart;
	uint32_t	_headersEnd;
	time_t		_fileMtime;
	size_t		_fileSize;
};
