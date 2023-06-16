// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Rpm.h"
#include <QFile>
#include <QCryptographicHash>
#include <iostream>
#include <cstring>

extern "C" {
#include <fcntl.h>
#include <arpa/inet.h>
}

rpmts Rpm::_ts = nullptr;

void Rpm::initRpm() {
	rpmReadConfigFiles(NULL, NULL);
	_ts = rpmtsCreate();
	rpmtsSetVSFlags(_ts, _RPMVSF_NODIGESTS | _RPMVSF_NOSIGNATURES | RPMVSF_NOHDRCHK);
}

Rpm::Rpm(FileName const &filename):_filename(filename) {
	if(!_ts)
		initRpm();

	FD_t rpmFd = Fopen(filename, "r");
	int rc = rpmReadPackageFile(_ts, rpmFd, NULL, &_hdr);
	if(rc == RPMRC_NOKEY || rc == RPMRC_NOTTRUSTED) {
		std::cerr << filename << ": signature problem " << rc << std::endl;
	} else if(rc != RPMRC_OK) {
		std::cerr << "Can't open " << filename << ": " << rc << std::endl;
		return;
	}

	// Let's get file data and the start and end of headers in the file
	// while it's open anyway... repoMd needs it unconditionally
	int fd=Fileno(rpmFd);

	struct stat s;
	fstat(fd, &s);
	_fileSize = s.st_size;
	_fileMtime = s.st_mtime;

	lseek(fd, 104, SEEK_SET);
	uint32_t sigindex, sigdata;
	read(fd, &sigindex, 4);
	sigindex = htonl(sigindex);
	read(fd, &sigdata, 4);
	sigdata = htonl(sigdata);
	uint32_t sigindexsize = sigindex * 16;
	uint32_t sigsize = sigdata + sigindexsize;
	uint32_t disttoboundary = sigsize % 8;
	if(disttoboundary)
		disttoboundary = 8-disttoboundary;
	_headersStart = 112 + sigsize + disttoboundary;
	lseek(fd, _headersStart+8, SEEK_SET);
	uint32_t hdrindex, hdrdata;
	read(fd, &hdrindex, 4);
	hdrindex = htonl(hdrindex);
	read(fd, &hdrdata, 4);
	hdrdata = htonl(hdrdata);
	uint32_t hdrindexsize = hdrindex * 16;
	uint32_t hdrsize = hdrdata + hdrindexsize + 16;
	_headersEnd = _headersStart + hdrsize;

	Fclose(rpmFd);
}

Rpm::~Rpm() {
}

static constexpr struct {
	char const * const mdTag;
	enum rpmTag_e const rpmTag;
} md2rpm[] = {
	{ "license", RPMTAG_LICENSE },
	{ "vendor", RPMTAG_VENDOR },
	{ "group", RPMTAG_GROUP },
	{ "buildhost", RPMTAG_BUILDHOST },
	{ "sourcerpm", RPMTAG_SOURCERPM },
};

String Dependency::repoMdFlags() const {
	switch(_flags&0xf) {
	case 0:
		return String();
	case 2:
		return "LT";
	case 4:
		return "GT";
	case 8:
		return "EQ";
	case 8|2:
		return "LE";
	case 8|4:
		return "GE";
	}
	return String();
}

String Dependency::repoMdVersion() const {
	if(!_version)
		return String();

	String v, ret;
	int colon = _version.indexOf(':');
	if(colon > 0)
		ret = "epoch=\"" + _version.first(colon) + "\" ";
	int dash = _version.lastIndexOf('-');
	ret += "ver=\"" + _version.mid(colon+1, dash-colon-1) + "\"";
	if(dash > 0)
		ret += " rel=\"" + _version.mid(dash+1) + "\"";

	return ret;
}

String Rpm::repoMdVersion() const {
	return "epoch=\"" + String::number(epoch()) + "\" ver=\"" + version() + "\" rel=\"" + release() + "\"";
}

static constexpr struct {
	char const * const repoMdTag;
	int const nameTag;
	int const flagTag;
	int const versionTag;
} depType[] = {
	{ "provides", RPMTAG_PROVIDES, RPMTAG_PROVIDEFLAGS, RPMTAG_PROVIDEVERSION },
	{ "requires", RPMTAG_REQUIRES, RPMTAG_REQUIREFLAGS, RPMTAG_REQUIREVERSION },
	{ "conflicts", RPMTAG_CONFLICTS, RPMTAG_CONFLICTFLAGS, RPMTAG_CONFLICTVERSION },
	{ "obsoletes", RPMTAG_OBSOLETES, RPMTAG_OBSOLETEFLAGS, RPMTAG_OBSOLETEVERSION },
	{ "recommends", RPMTAG_RECOMMENDS, RPMTAG_RECOMMENDFLAGS, RPMTAG_RECOMMENDVERSION },
	{ "suggests", RPMTAG_SUGGESTS, RPMTAG_SUGGESTFLAGS, RPMTAG_SUGGESTVERSION },
	{ "supplements", RPMTAG_SUPPLEMENTS, RPMTAG_SUPPLEMENTFLAGS, RPMTAG_SUPPLEMENTVERSION },
	{ "enhances", RPMTAG_ENHANCES, RPMTAG_ENHANCEFLAGS, RPMTAG_ENHANCEVERSION },
};

QList<Dependency> Rpm::dependencies(enum DepType type) const {
	QList<Dependency> ret;
	rpmtd deps = rpmtdNew();
	rpmtd depFlags = rpmtdNew();
	rpmtd depVersion = rpmtdNew();
	rpmtdInit(deps);
	rpmtdInit(depFlags);
	rpmtdInit(depVersion);
	if(headerGet(_hdr, depType[static_cast<uint8_t>(type)].nameTag, deps, HEADERGET_MINMEM|HEADERGET_EXT) &&
	   headerGet(_hdr, depType[static_cast<uint8_t>(type)].flagTag, depFlags, HEADERGET_MINMEM|HEADERGET_EXT) &&
	   headerGet(_hdr, depType[static_cast<uint8_t>(type)].versionTag, depVersion, HEADERGET_MINMEM|HEADERGET_EXT)
	  ) {
		while((rpmtdNext(deps) != -1) &&
		      (rpmtdNext(depFlags) != -1) &&
		      (rpmtdNext(depVersion) != -1)
		     ) {
			ret.append(Dependency(rpmtdGetString(deps), rpmtdGetNumber(depFlags), rpmtdGetString(depVersion)));
		}
	}
	rpmtdFreeData(deps);
	rpmtdFreeData(depFlags);
	rpmtdFreeData(depVersion);
	rpmtdFree(deps);
	rpmtdFree(depFlags);
	rpmtdFree(depVersion);
	return ret;
}

String Dependency::repoMd() const {
	String ret = "<rpm:entry name=\"" + name() + "\"";
	String s = repoMdFlags();
	if(s)
		ret += " flags=\"" + s + "\"";
	s = repoMdVersion();
	if(s)
		ret += " " + s;
	ret += "/>";
	return ret;
}

String Rpm::dependenciesMd(enum DepType type) const {
	QList<Dependency> deps = dependencies(type);
	if(!deps.size())
		return String();
	String ret = String("		<rpm:") + depType[static_cast<uint8_t>(type)].repoMdTag + ">\n";
	for(Dependency const &d : dependencies(type))
		ret += "			" + d.repoMd() + "\n";
	ret += String("		</rpm:") + depType[static_cast<uint8_t>(type)].repoMdTag + ">\n";
	return ret;
}

String Rpm::dependenciesMd() const {
	return dependenciesMd(DepType::Provides) +
		dependenciesMd(DepType::Requires) +
		dependenciesMd(DepType::Conflicts) +
		dependenciesMd(DepType::Obsoletes) +
		dependenciesMd(DepType::Suggests) +
		dependenciesMd(DepType::Recommends) +
		dependenciesMd(DepType::Supplements) +
		dependenciesMd(DepType::Enhances);
}

String Rpm::sha256() {
	if(_sha256.isEmpty()) {
		QFile rpm;
		int fd = open(_filename, O_RDONLY);
		lseek(fd, 0, SEEK_SET);
		posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
		posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
		rpm.open(fd, QFile::ReadOnly, QFile::AutoCloseHandle);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		hash.addData(&rpm);
		rpm.close();
		_sha256=hash.result().toHex();
	}
	return _sha256;
}

Files Rpm::fileList(bool onlyPrimary) const {
	// Also potentially of interest:
	// RPMTAG_DIRINDEXES seems to hold a number associated with the directory the file is in
	// RPMTAG_BASENAMES holds the basename of every file
	// RPMTAG_FILEDIGESTS holds the SHA256 checksum of every file (in string format; empty for symlinks and directories)
	Files fn;
	// Filenames
	rpmtd filenames = rpmtdNew();
	// RPMTAG_FILEFLAGS attributes -- see enum rpmfileAttrs_e in <rpm/rpmfiles.h>
	// probably most important: RPMFILE_GHOST, RPMFILE_CONFIG, RPMFILE_MISSINGOK,
	// RPMFILE_NOREPLACE, RPMFILE_DOC, RPMFILE_LICENSE, RPMFILE_PUBKEY
	rpmtd fileflags = rpmtdNew();
	// File modes, same as st_mode in struct stat
	rpmtd filemodes = rpmtdNew();
	rpmtdInit(filenames);
	rpmtdInit(fileflags);
	rpmtdInit(filemodes);
	constexpr headerGetFlags flags = HEADERGET_MINMEM|HEADERGET_EXT;
	if(headerGet(_hdr, RPMTAG_FILENAMES, filenames, flags) &&
	   headerGet(_hdr, RPMTAG_FILEFLAGS, fileflags, flags) &&
	   headerGet(_hdr, RPMTAG_FILEMODES, filemodes, flags)
	  ) {
		while((rpmtdNext(filenames) != -1) &&
		      (rpmtdNext(fileflags) != -1) &&
		      (rpmtdNext(filemodes) != -1)
		     ) {
			FileInfo fi(rpmtdGetString(filenames), static_cast<rpmfileAttrs_e>(rpmtdGetNumber(fileflags)), rpmtdGetNumber(filemodes));
			// The definition of what is "primary" and what isn't is very vague.
			// According to https://createrepo.baseurl.org/:
			// "CERTAIN files - specifically files matching: /etc*,
			// *bin/*, /usr/lib/sendmail"
			// So we'll take anything in /etc and anything that's
			// executable and not a shared library (seems to make more
			// sense than *bin/*, given there's such things as /opt)
			if(!onlyPrimary ||
			   ((S_ISREG(fi.mode()) && (fi.mode() & 0111) && !fi.name().contains(".so")) ||
			    fi.name().startsWith("/etc/"))
			  )
				fn.append(fi);
		}
	}
	rpmtdFreeData(filenames);
	rpmtdFree(filenames);
	return fn;
}

String Rpm::fileListMd(bool onlyPrimary) const {
	String ret;
	String indent = onlyPrimary ? "		" : "	";
	// FIXME Need to handle <file type="dir", <file type="ghost", ...
	for(FileInfo const &f : fileList(onlyPrimary)) {
		ret += indent + "<file";
		if(f.attributes() & RPMFILE_GHOST)
			ret += " type=\"ghost\"";
		if(S_ISDIR(f.mode()))
			ret += " type=\"dir\"";
		ret += ">" + f.name() + "</file>\n";
	}
	return ret;
}

