// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Rpm.h"
#include "DesktopFile.h"
#include "Archive.h"
#include <QFile>
#include <QCryptographicHash>
#include <QDomDocument>
#include <QHash>
#include <iostream>
#include <cstring>

extern "C" {
#include <archive.h>
#include <archive_entry.h>
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

String Rpm::appstreamMd(QHash<String,QByteArray> *icons) const {
	if(icons)
		icons->clear();
	String ret;
	QList<String> appstreamFiles;
	QList<String> desktopFiles;
	QList<String> iconFiles;
	for(FileInfo const &fi : fileList(false)) {
		if(fi.name().startsWith("/usr/share/metainfo/") || fi.name().startsWith("/usr/share/appdata/"))
			appstreamFiles.append(fi.name());
		else if(fi.name().startsWith("/usr/share/applications/"))
			desktopFiles.append(fi.name());
		else if(fi.name().startsWith("/usr/share/icons/") || fi.name().startsWith("/usr/share/pixmaps"))
			iconFiles.append(fi.name());
	}
	QHash<String,QByteArray> appstreams = extractFiles(appstreamFiles + desktopFiles);
	if(appstreamFiles.count()) {
		for(auto it = appstreams.cbegin(), end = appstreams.cend(); it != end; ++it) {
			// We don't need to try to build metadata from desktop
			// files if we have appstream files (but we need to read
			// them anyway to supplement the appstream files)
			if(it.key().startsWith("/usr/share/applications/"))
				continue;
			// Here, we actually have to use a real XML parser instead of the simplistic
			// assumptions we use elsewhere in the code: since we don't control the
			// input files, they may not be indented reasonably and they may not even
			// be valid.
			QDomDocument dom;
			dom.setContent(it.value());
			QDomElement root = dom.documentElement();
			if(root.tagName() == "application") {
				// Seems to be an old version of the standard, spotted in
				// brasero-3.12.3, Clementine-1.4.0-rc2, empathy-3.12.14
				root.setTagName("component");
				root.setAttribute("type", "desktop-application");
			}
			if(root.tagName() != "component") {
				std::cerr << "Appstream metadata with document element \"" << root.tagName() << "\" rather than \"component\" found: " << it.key() << " in " << _filename << std::endl;
				continue;
			}
			// This is not strictly correct according to the standard, but a forgotten
			// type="desktop" seems to be far more common than a legitimately untyped
			// metainfo file.
			if(!root.hasAttribute("type"))
				root.setAttribute("type",  "desktop-application");

			// This is extremely common (in fact, more so than desktop-application),
			// but seems to be wrong according to the spec
			if(root.attribute("type") == "desktop")
				root.setAttribute("type", "desktop-application");

			QDomElement id = root.firstChildElement("id");
			if(id.isNull()) {
				// No id -- so let's create one from the filename instead
				id = dom.createElement("id");

				String fakeId = FileName(it.key()).basename(".metainfo.xml");
				// Since there is no consensus about *.metainfo.xml vs. *.appdata.xml,
				// strip that off too
				if(fakeId.endsWith(".appdata.xml"))
					fakeId = fakeId.sliced(0, fakeId.length()-12);

				id.appendChild(dom.createTextNode(fakeId));
				if(root.firstChild().isNull())
					root.appendChild(id);
				else
					root.insertBefore(id, root.firstChild());
			}
			if(root.firstChildElement("source_pkgname").isNull()) {
				QDomElement pkgname = dom.createElement("source_pkgname");
				String srpmName=sourceRpm();
				// strip off -VERSION-RELEASE.src.rpm
				if(srpmName.contains('-'))
					srpmName=srpmName.sliced(0, srpmName.lastIndexOf('-'));
				if(srpmName.contains('-'))
					srpmName=srpmName.sliced(0, srpmName.lastIndexOf('-'));
				pkgname.appendChild(dom.createTextNode(srpmName));
				root.insertAfter(pkgname, id);
			}
			if(root.firstChildElement("pkgname").isNull()) {
				QDomElement pkgname = dom.createElement("pkgname");
				pkgname.appendChild(dom.createTextNode(name()));
				root.insertAfter(pkgname, id);
			}
			// spec says update_contact must not be exposed to the end user
			while(!root.firstChildElement("update_contact").isNull())
				root.removeChild(root.firstChildElement("update_contact"));
			// updatecontent is wrong, but relatively common especially in
			// GNOME stuff. Let's remote it too.
			while(!root.firstChildElement("updatecontact").isNull())
				root.removeChild(root.firstChildElement("updatecontact"));

			// If we have a matching desktop file, we can supplement the
			// metainfo with it metainfo files frequently "forget" the
			// icon as well as categories.
			String desktopFile;

			QDomElement launchable = root.firstChildElement("launchable");
			while(!launchable.isNull()) {
				if(launchable.attribute("type") == "desktop-id") {
					String d = "/usr/share/applications/" + launchable.text();
					if(desktopFiles.contains(d)) {
						desktopFile = d;
						break;
					} else if(desktopFiles.contains(d + ".desktop")) {
						// Just to make sure. There's no known cases
						// of this, but it seems easy to "forget" to
						// append .desktop to the ID...
						desktopFile = d + ".desktop";
						break;
					}

				}
				launchable = launchable.nextSiblingElement("launchable");
			}

			// The desktop file *should* be referenced with a
			// <launchable type="desktop-id"> tag, but frequently isn't,
			// so we'll also look for a desktop file matching the ID.
			if(!desktopFile) {
				String d = "/usr/share/applications/" + id.text() + ".desktop";
				if(desktopFiles.contains(d))
					desktopFile = d;
				if(!desktopFile) {
					// A few bogus appdata files (e.g. konsole and falkon)
					// already list ".desktop" as part of their id
					// (a desktop to appdata converter gone wrong?)
					d = "/usr/share/applications/" + id.text();
					if(desktopFiles.contains(d))
						desktopFile = d;
					// Lastly, let's try just the name
					if(!desktopFile) {
						d = "/usr/share/applications/" + name() + ".desktop";
						if(desktopFiles.contains(d))
							desktopFile = d;
					}
				}
			}

			if(desktopFile) {
				// We found a matching desktop file -- so let's make
				// sure it's listed as launchable too...
				launchable = root.firstChildElement("launchable");
				if(launchable.isNull()) {
					launchable = dom.createElement("launchable");
					launchable.setAttribute("type", "desktop-id");
					launchable.appendChild(dom.createTextNode(FileName(desktopFile).basename()));
					root.appendChild(launchable);
				}

				DesktopFile df(appstreams[desktopFile]);
				QDomElement icon = root.firstChildElement("icon");
				if(icon.isNull() && df.hasKey("Icon")) {
					String iconName = df.value("Icon");
					icon = dom.createElement("icon");
					icon.setAttribute("type", "stock");
					icon.appendChild(dom.createTextNode(iconName));
					root.appendChild(icon);

					if(icons) {
						QList<String> relevantIcons;
						for(String const &i : iconFiles) {
							if(i.startsWith("/usr/share/icons/") && (i.endsWith("/64x64/apps/" + iconName  + ".png") || i.endsWith("/128x128/apps/" + iconName + ".png")))
								relevantIcons.append(i);
						}
						if(!relevantIcons.count()) {
							// the spec says png icons are preferred, but vector is
							// allowed, so if we can't find the PNGs, fall back
							// to SVGs
							for(String const &i : iconFiles) {
								if(i.startsWith("/usr/share/icons/") && (i.endsWith("/scalable/apps/" + iconName  + ".svg") || i.endsWith("/scalable/apps/" + iconName + ".svgz")))
									relevantIcons.append(i);
							}
						}
						if(relevantIcons.count()) {
							QHash<String,QByteArray> iconData = extractFiles(relevantIcons);
							for(auto i = iconData.cbegin(), e = iconData.cend(); i != e; ++i) {
								QList<QByteArray> n=i.key().split('/');
								String size=n.at(n.length()-3);
								String name = size + "/" + iconName + "." + n.at(n.length()-1).split('.').at(1);
								icons->insert(name, i.value());
								String simpleSize=size.split('x').at(0);
								icon = dom.createElement("icon");
								icon.setAttribute("type", "cached");
								if(size == "scalable") {
									icon.setAttribute("width", 64);
									icon.setAttribute("height", 64);
								} else {
									icon.setAttribute("width", QString(simpleSize));
									icon.setAttribute("height", QString(simpleSize));
								}
								icon.appendChild(dom.createTextNode(name));
								root.appendChild(icon);
							}
						}
					}
				}

				QDomElement categories = root.firstChildElement("categories");
				if(categories.isNull() && df.hasKey("Categories")) {
					categories = dom.createElement("categories");
					for(QByteArray const &s : df.value("Categories").split(';')) {
						if(s.isEmpty())
							continue;
						QDomElement category = dom.createElement("category");
						category.appendChild(dom.createTextNode(s));
						categories.appendChild(category);
					}
					root.appendChild(categories);
				}
			}

			String md(dom.toByteArray());
			// Strip XML header, repeating <?xml version ..... is harmful
			while(!md.startsWith("<component") && md.contains('\n'))
				md=md.sliced(md.indexOf('\n')+1);
			ret += md.trimmed() + "\n";
		}
	} else if(desktopFiles.count()) {
		// No appstream files, but we can get much of the same content from desktop files...
		QHash<String,QByteArray> desktops = extractFiles(desktopFiles);
		for(auto i=desktops.cbegin(), end=desktops.cend(); i!=end; ++i) {
			String md;
			String desktopName = FileName(i.key()).basename(".desktop");
			String id = desktopName;
			// IDs can't contain special characters, but we must
			// leave desktopName unmodified...
			id.replace(' ', '_').replace('-','_');
			md += "<component type=\"desktop\">\n"
				" <id>" + id + "</id>\n"
				" <pkgname>" + name() + "</pkgname>\n";

			String srpmName=sourceRpm();
			// strip off -VERSION-RELEASE.src.rpm
			if(srpmName.contains('-'))
				srpmName=srpmName.sliced(0, srpmName.lastIndexOf('-'));
			if(srpmName.contains('-'))
				srpmName=srpmName.sliced(0, srpmName.lastIndexOf('-'));

			md += " <source_pkgname>" + srpmName + "</source_pkgname>\n"
				" <launchable type=\"desktop-id\">" + desktopName + ".desktop</launchable>\n"
				" <description><p>" + description() + "</p></description>\n";
			DesktopFile df(i.value());
			QHash<String, String> entries = df["Desktop Entry"];
			for(auto dfe=df["Desktop Entry"].cbegin(), dfend=df["Destkop Entry"].cend(); dfe != dfend; ++dfe) {
				if(dfe.key() == "Icon") {
					String iconName = dfe.value();
					md += " <icon type=\"stock\">" + iconName + "</icon>\n";
					if(icons) {
						QList<String> relevantIcons;
						for(String const &i : iconFiles) {
							if(i.startsWith("/usr/share/icons/") && (i.endsWith("/64x64/apps/" + iconName  + ".png") || i.endsWith("/128x128/apps/" + iconName + ".png")))
								relevantIcons.append(i);
						}
						if(!relevantIcons.count()) {
							// the spec says png icons are preferred, but vector is
							// allowed, so if we can't find the PNGs, fall back
							// to SVGs
							for(String const &i : iconFiles) {
								if(i.startsWith("/usr/share/icons/") && (i.endsWith("/scalable/apps/" + iconName  + ".svg") || i.endsWith("/scalable/apps/" + iconName + ".svgz")))
									relevantIcons.append(i);
							}
						}
						if(relevantIcons.count()) {
							QHash<String,QByteArray> iconData = extractFiles(relevantIcons);
							for(auto i = iconData.cbegin(), e = iconData.cend(); i != e; ++i) {
								QList<QByteArray> n=i.key().split('/');
								String size=n.at(n.length()-3);
								String name = size + "/" + iconName + "." + n.at(n.length()-1).split('.').at(1);
								icons->insert(name, i.value());
								if(size == "scalable") {
									md += " <icon type=\"cached\" width=\"64\" height=\"64\">" + name + "</icon>\n";
								} else {
									String simpleSize=size.split('x').at(0);
									md += " <icon type=\"cached\" width=\"" + simpleSize + "\" height=\"" + simpleSize + "\">" + name + "</icon>\n";
								}
							}
						}
					}
				} else if(dfe.key() == "Name") {
					md += " <name>" + dfe.value() + "</name>\n";
				} else if(dfe.key() == "GenericName") {
					md += " <summary>" + dfe.value() + "</summary>\n";
				} else if(dfe.key() == "Categories") {
					md += " <categories>\n";
					for(QByteArray c : dfe.value().split(';')) {
						if(c.length())
							md += "  <category>" + c + "</category>\n";
					}
					md += " </categories>\n";
				}
			}
			md += "</component>\n";
			ret += md;
		}
	}
	return ret;
}

QHash<String,QByteArray> Rpm::extractFiles(QList<String> const &filenames) const {
	QHash<String,QByteArray> ret;
	archive *a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	int r = archive_read_open_filename(a, _filename, 16384);
	if(r != ARCHIVE_OK) {
		return ret;
	}
	ret.reserve(filenames.count());
	archive_entry *e;
	while(archive_read_next_header(a, &e) == ARCHIVE_OK) {
		char const * fn = archive_entry_pathname(e);
		if(*fn == '.') // rpm seems to store filenames with a leading dot
			fn++;
		if(filenames.contains(fn)) {
			size_t size=archive_entry_size(e);
			char buf[size];
			int r = archive_read_data(a, &buf, size);
			ret.insert(fn, QByteArray(buf, size));
			if(ret.count() == filenames.count()) {
				// No need to keep reading the archive...
				break;
			}
		} else
			archive_read_data_skip(a);
	}
	archive_read_free(a);
	return ret;
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

