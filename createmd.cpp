// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Rpm.h"
#include "Sha256.h"
#include "Compression.h"
#include "Archive.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QDomDocument>
#include <QTextStream>
#include <iostream>

extern "C" {
#include <time.h>
#include <archive_entry.h>
}

/**
 * Finalize the metadata
 *
 * This compresses the metadata files, renames them to
 * their final names (checksum included in filename),
 * and creates the corresponding repomd.xml file.
 *
 * @param d directory containing the metadata
 * @return \c true on success
 */
// TODO add some error checking
static bool finalizeMetadata(QDir const &d) {
	Compression::CompressFile(d.filePath("primary.xml"));
	Compression::CompressFile(d.filePath("filelists.xml"));
	Compression::CompressFile(d.filePath("other.xml"));
	Compression::CompressFile(d.filePath("appstream.xml"), Compression::Format::GZip);
	Compression::CompressFile(d.filePath("appstream-icons.tar"), Compression::Format::GZip);

	QHash<String,String> checksum{
		{"primary", Sha256::checksum(d.filePath("primary.xml"))},
		{"filelists", Sha256::checksum(d.filePath("filelists.xml"))},
		{"other", Sha256::checksum(d.filePath("other.xml"))},
		{"appstream", Sha256::checksum(d.filePath("appstream.xml"))},
		{"appstream-icons", Sha256::checksum(d.filePath("appstream-icons.tar"))},
		{"primaryXZ", Sha256::checksum(d.filePath("primary.xml.xz"))},
		{"filelistsXZ", Sha256::checksum(d.filePath("filelists.xml.xz"))},
		{"otherXZ", Sha256::checksum(d.filePath("other.xml.xz"))},
		{"appstreamGZ", Sha256::checksum(d.filePath("appstream.xml.gz"))},
		{"appstream-iconsGZ", Sha256::checksum(d.filePath("appstream-icons.tar.gz"))}
	};

	QFile::rename(d.filePath("primary.xml.xz"), d.filePath(checksum["primaryXZ"] + "-primary.xml.xz"));
	QFile::rename(d.filePath("filelists.xml.xz"), d.filePath(checksum["filelistsXZ"] + "-filelists.xml.xz"));
	QFile::rename(d.filePath("other.xml.xz"), d.filePath(checksum["otherXZ"] + "-other.xml.xz"));
	QFile::rename(d.filePath("appstream.xml.gz"), d.filePath(checksum["appstreamGZ"] + "-appstream.xml.gz"));
	QFile::rename(d.filePath("appstream-icons.tar.gz"), d.filePath(checksum["appstream-iconsGZ"] + "-appstream-icons.tar.gz"));

	QFile repomd(d.filePath("repomd.xml"));
	repomd.open(QFile::WriteOnly|QFile::Truncate);
	QTextStream repomdTs(&repomd);
	time_t timestamp = time(0);
	repomdTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl
		<< "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\">" << Qt::endl
		<< "	<revision>" << timestamp << "</revision>" << Qt::endl;
	for(String const &file : QList<String>{"primary", "filelists", "other", "appstream", "appstream-icons"}) {
		String compressedFile = (file.startsWith("appstream")) ? file + "GZ" : file + "XZ";
		String compressExtension = (file.startsWith("appstream")) ? ".gz" : ".xz";
		struct stat s, uncompressed;
		stat(d.filePath(checksum[compressedFile] + "-" + file + ".xml" + compressExtension).toUtf8(), &s);
		stat(d.filePath(file + ".xml").toUtf8(), &uncompressed);
		repomdTs << "	<data type=\"" << file << "\">" << Qt::endl
			<< "		<checksum type=\"sha256\">" << checksum[compressedFile] << "</checksum>" << Qt::endl
			<< "		<open-checksum type=\"sha256\">" << checksum[file] << "</open-checksum>" << Qt::endl
			<< "		<location href=\"repodata/" << checksum[compressedFile] << "-" << file << ((file == "appstream-icons") ? ".tar" : ".xml") + compressExtension + "\"/>" << Qt::endl
			<< "		<timestamp>" << s.st_mtime << "</timestamp>" << Qt::endl
			<< "		<size>" << s.st_size << "</size>" << Qt::endl
			<< "		<open-size>" << uncompressed.st_size << "</open-size>" << Qt::endl
			<< "	</data>" << Qt::endl;
	}
	repomdTs << "</repomd>" << Qt::endl;
	repomd.close();

	QFile::remove(d.filePath("primary.xml"));
	QFile::remove(d.filePath("filelists.xml"));
	QFile::remove(d.filePath("other.xml"));
	QFile::remove(d.filePath("appstream.xml"));
	QFile::remove(d.filePath("appstream-icons.tar"));
	return true;
}

static bool removeMd(QDomElement &dom, QString const &tag, QString const &attribute, QString const &match) {
	QDomNodeList n = dom.elementsByTagName(tag);
	for(int i=0; i<n.size(); i++) {
		if(n.at(i).toElement().attribute(attribute) == match) {
			dom.removeChild(n.at(i));
			return true;
		}
	}
	return false;
}

static bool removeAppstreamMd(QDomElement &dom, QString const &pkgname, QStringList &iconsToRemove) {
	bool ok = false;
	QDomNodeList n = dom.elementsByTagName("component");
	// Got to iterate backwards because removing an earlier child
	// messes with removing a later one
	for(int i=n.size()-1; i>=0; i--) {
		QDomElement e = n.at(i).toElement();
		if(e.elementsByTagName("pkgname").at(0).toElement().text() == pkgname) {
			QDomNodeList icons = e.elementsByTagName("icon");
			for(int icon=0; icon<icons.size(); icon++) {
				QDomElement icn=icons.at(icon).toElement();
				if(icn.attribute("type") == "cached")
					iconsToRemove.append(icn.text());
			}
			dom.removeChild(n.at(i));
			ok = true;
			// We can't break here because a package may contain
			// multiple desktop files
		}
	}
	return ok;
}

static bool updateMetadata(String const &path) {
	QDir d(path);
	if(!d.exists()) {
		std::cerr << path << " not found, ignoring" << std::endl;
		return false;
	}
	QDir oldRepodata(path + "/repodata");
	if(!oldRepodata.exists()) {
		std::cerr << "No prior repodata in " << path << ", ignoring" << std::endl;
		return false;
	}
	QFile oldRepomdFile(oldRepodata.filePath("repomd.xml"));
	if(!oldRepomdFile.open(QFile::ReadOnly)) {
		std::cerr << "Can't open repomd.xml in " << path << ", ignoring" << std::endl;
		return false;
	}
	QDomDocument oldRepomd;
	oldRepomd.setContent(&oldRepomdFile);
	oldRepomdFile.close();
	if(oldRepomd.documentElement().tagName() != "repomd") {
		std::cerr << "Prior repomd.xml for " << path << " seems invalid, ignoring" << std::endl;
		return false;
	}
	time_t timestamp = 0;
	QDomNodeList dataTags=oldRepomd.elementsByTagName("data");
	QHash<QString,QDomDocument> oldMetadata;
	QString oldIconsFile;
	for(int i=0; i<dataTags.count(); i++) {
		QDomElement e=dataTags.at(i).toElement();
		QString const type = e.attribute("type");
		if(type == "primary") {
			QDomNode tsNode = e.elementsByTagName("timestamp").at(0);
			if(tsNode.isElement()) {
				timestamp = tsNode.toElement().text().toULongLong();
			}
		}
		QDomElement l=e.elementsByTagName("location").at(0).toElement();
		if(!l.hasAttribute("href")) {
			std::cerr << "No valid location data for " << qPrintable(type) << " in old repomd.xml";
			return false;
		}
		QString oldMdFile = path + "/" + l.attribute("href");
		if(type == "appstream-icons") {
			oldIconsFile = oldMdFile;
			// No need to load appstream-icons into memory, it doesn't have
			// any metadata we care about
			continue;
		}
		QByteArray oldMd = Compression::uncompressedFile(oldMdFile);
		QDomDocument dom;
		if(!dom.setContent(oldMd)) {
			std::cerr << "XML parser failed on " << qPrintable(oldMdFile) << std::endl;
			std::cerr << oldMd.data() << std::endl;
			return false;
		}
		oldMetadata.insert(type, dom);
	}
	oldRepomd.clear();
	if(timestamp == 0) {
		std::cerr << "Prior repomd.xml for " << path << " doesn't have a valid timestamp, assuming mtime" << std::endl;
		timestamp = oldRepomdFile.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch();
	}

	QFileInfoList rpms = d.entryInfoList(QStringList() << "*.rpm", QDir::Files|QDir::Readable, QDir::Time);
	QDomElement metadata = oldMetadata["primary"].documentElement();
	if(metadata.tagName() != "metadata") {
		std::cerr << "Prior primary.xml seems invalid, ignoring " << path << std::endl;
		return false;
	}
	QDomElement filelists = oldMetadata["filelists"].documentElement();
	if(filelists.tagName() != "filelists") {
		std::cerr << "Prior filelists.xml seems invalid, ignoring " << path << std::endl;
		return false;
	}
	QDomElement otherdata = oldMetadata["other"].documentElement();
	if(otherdata.tagName() != "otherdata") {
		std::cerr << "Prior other.xml seems invalid, ignoring " << path << std::endl;
		return false;
	}
	QDomElement components = oldMetadata["appstream"].documentElement();
	if(components.tagName() != "components") {
		std::cerr << "Prior appstream.xml seems invalid, ignoring " << path << std::endl;
		return false;
	}
	QDomNodeList pkgs = metadata.elementsByTagName("package");
	QStringList packagesWithChangedTimestamp;
	int countChange = 0;
	QStringList iconsToRemove;
	for(int i=0; i<pkgs.size(); i++) {
		struct stat s;
		QDomElement p = pkgs.at(i).toElement();
		QDomElement l = p.elementsByTagName("location").at(0).toElement();
		QString pkgFile = l.attribute("href");
		if(pkgFile.isEmpty()) {
			std::cerr << "package without location tag in old primary.xml. Ignoring the package." << std::endl;
			continue;
		}

		QDomElement t = p.elementsByTagName("time").at(0).toElement();
		time_t oldTs = t.attribute("file").toULongLong();

		String pkgPath = path + "/" + pkgFile.toUtf8();
		int st = stat(pkgPath, &s);

		// Everything as expected...
		if(st == 0 && (oldTs == s.st_mtime))
			continue;

		// The package has been removed or changed...
		String oldChecksum;
		QDomNodeList cs = p.elementsByTagName("checksum");
		for(int csi = 0; csi < cs.size(); csi++) {
			QDomElement c = cs.at(csi).toElement();
			if(c.attribute("pkgid").toUpper() == "YES") {
				oldChecksum = c.text();
				break;
			}
		}

		String checksum;
		if(st == 0)
			checksum = Sha256::checksum(pkgPath);

		if(checksum == oldChecksum) {
			// File is still the same, just update the metadata
			t.setAttribute("file", QString::number(s.st_mtime));
			packagesWithChangedTimestamp.append(pkgFile);
			continue;
		}

		QString name = p.elementsByTagName("name").at(0).toElement().text();

		// File was modified or deleted -- remove the metadata
		// and recreate it when looking for new files
		metadata.removeChild(p);
		removeMd(filelists, "package", "pkgid", oldChecksum);
		removeMd(otherdata, "package", "pkgid", oldChecksum);
		removeAppstreamMd(components, name, iconsToRemove);
		countChange--;
	}

	QHash<String,QByteArray> iconsToAdd;

	for(QFileInfo const &f : rpms) {
		if(f.lastModified().toSecsSinceEpoch() < timestamp) {
			// older than previous metadata, we're done
			// (and the list is sorted by time, newest first)
			break;
		}
		// No need to analyze the file if we already know only the timestamp changed
		if(packagesWithChangedTimestamp.contains(f.fileName()))
			continue;

		Rpm r(f.filePath());
		String checksum = r.sha256();
		
		// Add to primary.xml
		QDomDocument &primary = oldMetadata["primary"];
		QDomElement package = primary.createElement("package");
		package.setAttribute("type", "rpm");
		
		QDomElement e = primary.createElement("name");
		e.appendChild(primary.createTextNode(r.name()));
		package.appendChild(e);

		e = primary.createElement("arch");
		e.appendChild(primary.createTextNode(r.arch()));
		package.appendChild(e);

		e = primary.createElement("version");
		e.setAttribute("epoch", r.epoch());
		e.setAttribute("ver", QString::fromUtf8(r.version()));
		e.setAttribute("rel", QString::fromUtf8(r.release()));
		package.appendChild(e);

		e = primary.createElement("checksum");
		e.setAttribute("type", "sha256");
		e.setAttribute("pkgid", "YES");
		e.appendChild(primary.createTextNode(checksum));
		package.appendChild(e);

		e = primary.createElement("summary");
		e.appendChild(primary.createTextNode(r.summary()));
		package.appendChild(e);

		e = primary.createElement("description");
		e.appendChild(primary.createTextNode(r.description()));
		package.appendChild(e);

		e = primary.createElement("packager");
		e.appendChild(primary.createTextNode(r.packager()));
		package.appendChild(e);

		e = primary.createElement("url");
		e.appendChild(primary.createTextNode(r.url()));
		package.appendChild(e);

		e = primary.createElement("time");
		e.setAttribute("file", f.lastModified().toSecsSinceEpoch());
		e.setAttribute("build", static_cast<unsigned long long>(r.buildTime()));
		package.appendChild(e);

		e = primary.createElement("size");
		e.setAttribute("package", static_cast<unsigned long long>(r.size()));
		e.setAttribute("installed", static_cast<unsigned long long>(r.installedSize()));
		e.setAttribute("archive", static_cast<unsigned long long>(r.archiveSize()));
		package.appendChild(e);

		e = primary.createElement("location");
		e.setAttribute("href", f.fileName());
		package.appendChild(e);

		QDomElement format = primary.createElement("format");
		
		e = primary.createElement("rpm:license");
		e.appendChild(primary.createTextNode(r.license()));
		format.appendChild(e);

		e = primary.createElement("rpm:vendor");
		e.appendChild(primary.createTextNode(r.vendor()));
		format.appendChild(e);

		e = primary.createElement("rpm:group");
		e.appendChild(primary.createTextNode(r.group()));
		format.appendChild(e);

		e = primary.createElement("rpm:buildhost");
		e.appendChild(primary.createTextNode(r.buildHost()));
		format.appendChild(e);

		e = primary.createElement("rpm:sourcerpm");
		e.appendChild(primary.createTextNode(r.sourceRpm()));
		format.appendChild(e);

		e = primary.createElement("rpm:header-range");
		e.setAttribute("start", static_cast<unsigned long long>(r.headersStart()));
		e.setAttribute("end", static_cast<unsigned long long>(r.headersEnd()));
		format.appendChild(e);

		constexpr struct {
			char const * const name;
			DepType const depType;
		} depTypes[] = {
			{ "provides", DepType::Provides },
			{ "requires", DepType::Requires },
			{ "conflicts", DepType::Conflicts },
			{ "obsoletes", DepType::Obsoletes },
			{ "recommends", DepType::Recommends },
			{ "suggests", DepType::Suggests },
			{ "supplements", DepType::Supplements },
			{ "enhances", DepType::Enhances }
		};

		for(int i=0; i<sizeof(depTypes)/sizeof(*depTypes); i++) {
			e = primary.createElement(QString("rpm:") + depTypes[i].name);
			QList<Dependency> deps = r.dependencies(depTypes[i].depType);
			for(auto const &dep : deps) {
				QDomElement de = primary.createElement("rpm:entry");
				de.setAttribute("name", static_cast<QString>(dep.name()));
				String rf = dep.repoMdFlags();
				if(rf)
					de.setAttribute("flags", static_cast<QString>(rf));
				String v = dep.version();
				if(v) {
					int colon = v.indexOf(':');
					if(colon > 0)
						de.setAttribute("epoch", static_cast<QString>(v.first(colon)));
					int dash = v.lastIndexOf('-');
					de.setAttribute("ver", static_cast<QString>(v.mid(colon+1, dash-colon-1)));
					if(dash > 0)
						de.setAttribute("rel", static_cast<QString>(v.mid(dash+1)));
				}

				e.appendChild(de);
			}
			if(deps.count())
				format.appendChild(e);
		}
		for(FileInfo const &f : r.fileList(true)) {
			e = primary.createElement("file");
			if(f.attributes() & RPMFILE_GHOST)
				e.setAttribute("type", "ghost");
			else if(S_ISDIR(f.mode()))
				e.setAttribute("type", "dir");
			e.appendChild(primary.createTextNode(f.name()));
			format.appendChild(e);
		}

		package.appendChild(format);
		metadata.appendChild(package);

		// Add to filelists.xml
		QDomDocument &filelists = oldMetadata["filelists"];
		package = filelists.createElement("package");
		package.setAttribute("pkgid", static_cast<QString>(checksum));
		package.setAttribute("name", static_cast<QString>(r.name()));
		package.setAttribute("arch", static_cast<QString>(r.arch()));

		e = filelists.createElement("version");
		e.setAttribute("epoch", r.epoch());
		e.setAttribute("ver", static_cast<QString>(r.version()));
		e.setAttribute("release", static_cast<QString>(r.release()));
		package.appendChild(e);

		for(FileInfo const &f : r.fileList(false)) {
			e = filelists.createElement("file");
			if(f.attributes() & RPMFILE_GHOST)
				e.setAttribute("type", "ghost");
			else if(S_ISDIR(f.mode()))
				e.setAttribute("type", "dir");
			e.appendChild(filelists.createTextNode(f.name()));
			package.appendChild(e);
		}
		filelists.appendChild(package);

		// Add to other.xml
		QDomDocument &other = oldMetadata["other"];
		package = other.createElement("package");
		package.setAttribute("pkgid", static_cast<QString>(checksum));
		package.setAttribute("name", static_cast<QString>(r.name()));
		package.setAttribute("arch", static_cast<QString>(r.arch()));

		e = other.createElement("version");
		e.setAttribute("epoch", r.epoch());
		e.setAttribute("ver", static_cast<QString>(r.version()));
		e.setAttribute("release", static_cast<QString>(r.release()));
		package.appendChild(e);

		other.appendChild(package);

		// Add to appstream.xml
		QDomDocument &appstream = oldMetadata["appstream"];
		QHash<String,QByteArray> icons;
		String md = r.appstreamMd(&icons);
		// Not every package has something appstream cares about
		if(md) {
			QDomDocument newAppstream;
			if(!newAppstream.setContent(md)) {
				std::cerr << "Appstream MD not recognized as valid XML" << std::endl;
			}

			QDomNode imp = appstream.importNode(newAppstream.documentElement(), true);
			components.appendChild(imp);

			for(auto it=icons.begin(), ite=icons.end(); it != ite; ++it)
				iconsToAdd.insert(it.key(), it.value());
		}

		countChange++;
	}

	// refresh package count...
	metadata.setAttribute("packages", metadata.attribute("packages").toULongLong()+countChange);
	filelists.setAttribute("packages", filelists.attribute("packages").toULongLong()+countChange);
	otherdata.setAttribute("packages", otherdata.attribute("packages").toULongLong()+countChange);

	String tempName = ".repodata.temp." + String::number(getpid());
	d.mkdir(tempName, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
	QDir rd(path + "/" + tempName);
	if(!rd.exists()) {
		std::cerr << "Can't create/use repodata directory in " << qPrintable(path) << ", ignoring" << std::endl;
		return false;
	}

	for(QString const &x : QStringList{"primary", "filelists", "other", "appstream"}) {
		QFile xmlFile(rd.filePath(x + ".xml"));
		if(!xmlFile.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't write to " << qPrintable(xmlFile.fileName()) << std::endl;
			return false;
		}
		xmlFile.write(oldMetadata[x].toByteArray());
		xmlFile.close();
	}

	// Update appstream-icons.tar if necessary
	if(iconsToRemove.isEmpty() && iconsToAdd.isEmpty()) {
		// Until finalizeMetadata() gets smarter, we have to uncompress
		// it anyway so we get uncompressed checksum, size etc.
		// Ideally at some point we'll just QFile::copy the original
		// file.
		QFile iconCache(rd.filePath("appstream-icons.tar"));
		if(iconCache.open(QFile::WriteOnly|QFile::Truncate))
			iconCache.write(Compression::uncompressedFile(oldIconsFile));
		iconCache.close();
	} else {
		QStringList ignore = iconsToRemove;
		for(String const &i : iconsToAdd.keys())
			ignore.append(i);

		Archive out(rd.filePath("appstream-icons.tar"));
		archive *in = archive_read_new();
		archive_read_support_format_all(in);
		archive_read_support_filter_all(in);
		if(archive_read_open_filename(in, oldIconsFile.toUtf8(), 16384) != ARCHIVE_OK) {
			std::cerr << "Can't open icon cache for " << path << std::endl;
			return false;
		}
		archive_entry *e;
		while(archive_read_next_header(in, &e) == ARCHIVE_OK) {
			char const * fn = archive_entry_pathname(e);
			if(ignore.contains(fn)) {
				archive_read_data_skip(in);
				continue;
			}
			size_t size = archive_entry_size(e);
			char buf[size];
			int r = archive_read_data(in, &buf, size);
			out.addFile(fn, QByteArray(buf, size));
		}

		for(QHash<String,QByteArray>::ConstIterator it=iconsToAdd.begin(); it!=iconsToAdd.end(); ++it)
			out.addFile(it.key(), it.value());
	}

	if(!finalizeMetadata(rd)) {
		std::cerr << "Error while finalizing metadata" << std::endl;
		return false;
	}

	QDir realRepodata(path + "/repodata");
	realRepodata.removeRecursively();
	d.rename(tempName, "repodata");

	return true;
}

static bool createMetadata(String const &path) {
	QDir d(path);
	if(!d.exists()) {
		std::cerr << path << " not found, ignoring" << std::endl;
		return false;
	}
	QStringList rpms = d.entryList(QStringList() << "*.rpm", QDir::Files|QDir::Readable, QDir::Name);
	if(rpms.isEmpty()) {
		std::cerr << "No rpms found in " << qPrintable(path) << ", ignoring" << std::endl;
		return false;
	}
	String tempName = ".repodata.temp." + String::number(getpid());
	d.mkdir(tempName, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
	QDir rd(path + "/" + tempName);
	if(!rd.exists()) {
		std::cerr << "Can't create/use repodata directory in " << qPrintable(path) << ", ignoring" << std::endl;
		return false;
	}
	QFile primary(rd.filePath("primary.xml"));
	if(!primary.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't create primary.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
		return false;
	}
	QFile filelists(rd.filePath("filelists.xml"));
	if(!filelists.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't create filelists.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
		return false;
	}
	QFile other(rd.filePath("other.xml"));
	if(!other.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't create other.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
		return false;
	}
	QFile appstream(rd.filePath("appstream.xml"));
	if(!appstream.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't create appstream.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
		return false;
	}
	appstream.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<components origin=\"openmandriva\" version=\"0.14\">\n");
	Archive appstreamIcons(rd.filePath("appstream-icons.tar"));

	QTextStream primaryTs(&primary);
	QTextStream filelistsTs(&filelists);
	QTextStream otherTs(&other);

	primaryTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl <<
		"<metadata xmlns=\"http://linux.duke.edu/metadata/common\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"" << rpms.count() << "\">" << Qt::endl;

	filelistsTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl <<
		"<filelists xmlns=\"http://linux.duke.edu/metadata/filelists\" packages=\"" << rpms.count() << "\">" << Qt::endl;

	otherTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl <<
		"<otherdata xmlns=\"http://linux.duke.edu/metadata/other\" packages=\"" << rpms.count() << "\">" << Qt::endl;

	for(QString const &rpm : rpms) {
		Rpm r(d.filePath(rpm));
		primaryTs << "<package type=\"rpm\">" << Qt::endl
			<< "	<name>" << r.name() << "</name>" << Qt::endl
			<< "	<arch>" << r.arch() << "</arch>" << Qt::endl
			<< "	<version epoch=\"" << r.epoch() << "\" ver=\"" << r.version() << "\" rel=\"" << r.release() << "\"/>" << Qt::endl
			<< "	<checksum type=\"sha256\" pkgid=\"YES\">" << r.sha256() << "</checksum>" << Qt::endl
			<< "	<summary>" << r.summary().xmlEncode() << "</summary>" << Qt::endl
			<< "	<description>" << r.description().xmlEncode() << "</description>" << Qt::endl
			<< "	<packager>" << r.packager().xmlEncode() << "</packager>" << Qt::endl
			<< "	<url>" << r.url().xmlEncode() << "</url>" << Qt::endl
			<< "	<time file=\"" << r.time() << "\" build=\"" << r.buildTime() << "\"/>" << Qt::endl
			<< "	<size package=\"" << r.size() << "\" installed=\"" << r.installedSize() << "\" archive=\"" << r.archiveSize() << "\"/>" << Qt::endl
			<< "	<location href=\"" << rpm << "\"/>" << Qt::endl
			<< "	<format>" << Qt::endl
			<< "		<rpm:license>" << r.license().xmlEncode() << "</rpm:license>" << Qt::endl
			<< "		<rpm:vendor>" << r.vendor().xmlEncode() << "</rpm:vendor>" << Qt::endl
			<< "		<rpm:group>" << r.group().xmlEncode() << "</rpm:group>" << Qt::endl
			<< "		<rpm:buildhost>" << r.buildHost() << "</rpm:buildhost>" << Qt::endl
			<< "		<rpm:sourcerpm>" << r.sourceRpm() << "</rpm:sourcerpm>" << Qt::endl
			<< "		<rpm:header-range start=\"" << r.headersStart() << "\" end=\"" << r.headersEnd() << "\"/>" << Qt::endl
			<< r.dependenciesMd()
			<< r.fileListMd(true)
			<< "	</format>" << Qt::endl
			<< "</package>" << Qt::endl;

		filelistsTs << "<package pkgid=\"" << r.sha256() << "\" name=\"" << r.name() << "\" arch=\"" << r.arch() << "\">" << Qt::endl
			<< "	<version " << r.repoMdVersion() << "/>" << Qt::endl
			<< r.fileListMd()
			<< "</package>" << Qt::endl;

		otherTs << "<package pkgid=\"" << r.sha256() << "\" name=\"" << r.name() << "\" arch=\"" << r.arch() << "\">" << Qt::endl
			<< "	<version " << r.repoMdVersion() << "/>" << Qt::endl
			<< "</package>" << Qt::endl;

		QHash<String,QByteArray> icons;
		appstream.write(r.appstreamMd(&icons));
		for(auto icon=icons.cbegin(), iend=icons.cend(); icon != iend; ++icon) {
			appstreamIcons.addFile(icon.key(), icon.value());
		}
	}
	primaryTs << "</metadata>" << Qt::endl;
	filelistsTs << "</filelists>" << Qt::endl;
	otherTs << "</otherdata>" << Qt::endl;
	appstream.write("</components>\n");

	primary.close();
	filelists.close();
	other.close();
	appstream.close();
	appstreamIcons.close();

	if(!finalizeMetadata(rd)) {
		std::cerr << "Error while finalizing metadata" << std::endl;
		return false;
	}

	QDir realRepodata(path + "/repodata");
	realRepodata.removeRecursively();
	d.rename(tempName, "repodata");

	return true;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("createmd");
	QCoreApplication::setApplicationVersion("0.0.1");

	QCommandLineParser cp;
	cp.setApplicationDescription("RPM repository metadata creator");
	cp.addOption({{"u", "update"}, QCoreApplication::translate("main", "Update metadata instead of regenerating it")});
	cp.addHelpOption();
	cp.addVersionOption();
	cp.addPositionalArgument("path", QCoreApplication::translate("main", "Directory containing the RPM files"));
	cp.process(app);

	if(cp.positionalArguments().isEmpty()) {
		std::cerr << "Usage: " << argv[0] << "/path/to/rpm/files" << std::endl;
		return 1;
	}

	bool const update = cp.isSet("u");

	for(QString const &path : cp.positionalArguments()) {
		bool const ok = update ? updateMetadata(path) : createMetadata(path);
		if(!ok)
			std::cerr << "Couldn't generate metadata for " << path << ", ignoring" << std::endl;
	}
}
