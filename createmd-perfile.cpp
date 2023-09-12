// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Rpm.h"
#include "Sha256.h"
#include "Compression.h"
#include "Archive.h"
#include <QGuiApplication>
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

static bool verbose;

/**
 * Extract metadata from a package
 * @param d Directory containing the package
 * @param rpm rpm filename
 */
static bool extractMetadata(QDir &d, QString const &rpm) {
	QDir rd(d.absolutePath() + "/repodata/perfile");
	if(!rd.exists()) {
		d.mkdir("repodata", QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
		d.mkdir("repodata/perfile", QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
		if(!rd.exists()) {
			std::cerr << "Can't create/use repodata directory in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
			return false;
		}
	}

	QFile primary(rd.filePath(rpm + ".primary.xml"));
	if(!primary.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't write to " << rd.filePath(rpm + ".primary.xml") << std::endl;
		return false;
	}
	QFile filelists(rd.filePath(rpm + ".filelists.xml"));
	if(!filelists.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't write to " << rd.filePath(rpm + ".filelists.xml") << std::endl;
		return false;
	}
	QFile other(rd.filePath(rpm + ".other.xml"));
	if(!other.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't write to " << rd.filePath(rpm + ".other.xml") << std::endl;
		return false;
	}

	Rpm r(d.filePath(rpm));

	QTextStream primaryTs(&primary);
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

	QTextStream filelistsTs(&filelists);
	filelistsTs << "<package pkgid=\"" << r.sha256() << "\" name=\"" << r.name() << "\" arch=\"" << r.arch() << "\">" << Qt::endl
		<< "	<version " << r.repoMdVersion() << "/>" << Qt::endl
		<< r.fileListMd()
		<< "</package>" << Qt::endl;

	QTextStream otherTs(&other);
	otherTs << "<package pkgid=\"" << r.sha256() << "\" name=\"" << r.name() << "\" arch=\"" << r.arch() << "\">" << Qt::endl
		<< "	<version " << r.repoMdVersion() << "/>" << Qt::endl
		<< "</package>" << Qt::endl;

	QHash<String,QByteArray> icons;
	String appstreamMd = r.appstreamMd(&icons);
	if(!appstreamMd.isEmpty()) {
		QFile appstream(rd.filePath(rpm + ".appstream.xml"));
		if(!appstream.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't write to " << rd.filePath(rpm + ".appstream.xml") << std::endl;
			return false;
		}
		appstream.write(appstreamMd);
		appstream.close();

		QDir appstreamIcons(rd.filePath(rpm + ".appstream-icons"));
		if(appstreamIcons.exists())
			appstreamIcons.removeRecursively();
		for(auto icon=icons.cbegin(), iend=icons.cend(); icon != iend; ++icon) {
			FileName fn=rpm + ".appstream-icons/" + icon.key();
			rd.mkpath(fn.dirname());
			QFile iconFile(rd.filePath(fn));
			if(!iconFile.open(QFile::WriteOnly|QFile::Truncate)) {
				std::cerr << "Can't write to " << iconFile.fileName() << std::endl;
				continue;
			}
			iconFile.write(icon.value());
		}
	}

	primary.close();
	filelists.close();
	other.close();
	return true;
}

static bool createMetadata(String const &path) {
	QDir d(path);
	if(!d.exists()) {
		std::cerr << path << " not found, ignoring" << std::endl;
		return false;
	}
	QStringList rpms = d.entryList(QStringList() << "*.rpm", QDir::Files|QDir::Readable);
	if(rpms.isEmpty()) {
		std::cerr << "No rpms found in " << qPrintable(path) << ", ignoring" << std::endl;
		return false;
	}
	for(QString const &rpm : rpms) {
		extractMetadata(d, rpm);
	}

	return true;
}

static bool cleanup(QDir &d) {
	QStringList const rpms = d.entryList(QStringList() << "*.rpm", QDir::Files|QDir::Readable);
	QDir rd(d.absolutePath() + "/repodata/perfile");
	QStringList const mdFiles = rd.entryList();
	for(QString const &file : mdFiles) {
		if(!file.contains(".rpm.")) {
			if(file != "." && file != "..")
				std::cerr << "Non-metadata file in metadata directory: " << qPrintable(file) << std::endl;
			continue;
		}
		QString rpm = file.left(file.lastIndexOf(".rpm.")+4);
		if(!rpms.contains(rpm)) {
			if(verbose)
				std::cerr << "Stale metadata for: " << qPrintable(rpm) << std::endl;
			if(file.endsWith(".appstream-icons")) {
				QDir icon(rd.filePath(file));
				icon.removeRecursively();
			} else {
				QFile::remove(rd.filePath(file));
			}
		}
	}
	return true;
}

static QStringList newFiles(QDir &d) {
	QStringList ret;
	QStringList const rpms = d.entryList(QStringList() << "*.rpm", QDir::Files|QDir::Readable);
	QDir rd(d.absolutePath() + "/repodata/perfile");
	QStringList const mdFiles = rd.entryList();
	for(QString const &rpm : rpms) {
		if(!mdFiles.contains(rpm + ".primary.xml")) {
			if(verbose)
				std::cerr << "New file: " << qPrintable(rpm) << std::endl;
			ret << rpm;
		}
	}
	return ret;
}

static QStringList modifiedFiles(QDir &d) {
	QStringList ret;
	QFileInfoList const rpms = d.entryInfoList(QStringList() << "*.rpm", QDir::Files|QDir::Readable);
	for(QFileInfo const &rpm : rpms) {
		QString md=d.absolutePath() + "/repodata/perfile/" + rpm.fileName() + ".primary.xml";
		QFileInfo mdInfo(md);
		if(!mdInfo.exists()) {
			std::cerr << "No metadata found for " << qPrintable(rpm.fileName()) << std::endl;
			continue;
		}
		if(mdInfo.lastModified() < rpm.lastModified()) {
			if(verbose)
				std::cerr << "Modified file: " << qPrintable(rpm.fileName()) << std::endl;
			ret << rpm.fileName();
		}
	}
	return ret;
}

static QStringList recursiveEntryList(QDir const &d, QString const &prefix=QString()) {
	QStringList ret;
	QStringList files = d.entryList(QDir::Files);
	if(prefix.isEmpty())
		ret = files;
	else {
		for(QString const &f : files)
			ret << (prefix + "/" + f);
	}

	QStringList subdirs = d.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
	if(!subdirs.isEmpty()) {
		for(QString const &sd : subdirs) {
			ret << recursiveEntryList(d.absoluteFilePath(sd),  prefix + (prefix.isEmpty() ? "" : "/") + sd);
		}
	}
	return ret;
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
	QStringList oldMetadata = d.entryList(QStringList() << "*.?z", QDir::Files);

	Compression::CompressFile(d.absoluteFilePath("primary.xml"));
	Compression::CompressFile(d.absoluteFilePath("filelists.xml"));
	Compression::CompressFile(d.absoluteFilePath("other.xml"));
	Compression::CompressFile(d.absoluteFilePath("appstream.xml"), Compression::Format::GZip);
	Compression::CompressFile(d.absoluteFilePath("appstream-icons.tar"), Compression::Format::GZip);

	QHash<String,String> checksum{
		{"primary", Sha256::checksum(d.absoluteFilePath("primary.xml"))},
		{"filelists", Sha256::checksum(d.absoluteFilePath("filelists.xml"))},
		{"other", Sha256::checksum(d.absoluteFilePath("other.xml"))},
		{"appstream", Sha256::checksum(d.absoluteFilePath("appstream.xml"))},
		{"appstream-icons", Sha256::checksum(d.absoluteFilePath("appstream-icons.tar"))},
		{"primaryXZ", Sha256::checksum(d.absoluteFilePath("primary.xml.xz"))},
		{"filelistsXZ", Sha256::checksum(d.absoluteFilePath("filelists.xml.xz"))},
		{"otherXZ", Sha256::checksum(d.absoluteFilePath("other.xml.xz"))},
		{"appstreamGZ", Sha256::checksum(d.absoluteFilePath("appstream.xml.gz"))},
		{"appstream-iconsGZ", Sha256::checksum(d.absoluteFilePath("appstream-icons.tar.gz"))}
	};

	std::cerr << "mv " << qPrintable(d.absoluteFilePath("primary.xml.xz")) << " " << qPrintable(d.absoluteFilePath(checksum["primaryXZ"] + "-primary.xml.xz")) << std::endl;

	QFile::rename(d.absoluteFilePath("primary.xml.xz"), d.absoluteFilePath(checksum["primaryXZ"] + "-primary.xml.xz"));
	QFile::rename(d.absoluteFilePath("filelists.xml.xz"), d.absoluteFilePath(checksum["filelistsXZ"] + "-filelists.xml.xz"));
	QFile::rename(d.absoluteFilePath("other.xml.xz"), d.absoluteFilePath(checksum["otherXZ"] + "-other.xml.xz"));
	QFile::rename(d.absoluteFilePath("appstream.xml.gz"), d.absoluteFilePath(checksum["appstreamGZ"] + "-appstream.xml.gz"));
	QFile::rename(d.absoluteFilePath("appstream-icons.tar.gz"), d.absoluteFilePath(checksum["appstream-iconsGZ"] + "-appstream-icons.tar.gz"));

	QFile repomd(d.absoluteFilePath("repomd.xml"));
	repomd.open(QFile::WriteOnly|QFile::Truncate);
	QTextStream repomdTs(&repomd);
	time_t timestamp = time(0);
	repomdTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl
		<< "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\">" << Qt::endl
		<< "	<revision>" << timestamp << "</revision>" << Qt::endl;
	for(String const &file : QList<String>{"primary", "filelists", "other", "appstream", "appstream-icons"}) {
		String compressedFile = (file.startsWith("appstream")) ? file + "GZ" : file + "XZ";
		String compressExtension = (file.startsWith("appstream")) ? ".gz" : ".xz";
		String extension = (file == "appstream-icons") ? ".tar" : ".xml";
		struct stat s, uncompressed;
		stat(d.absoluteFilePath(checksum[compressedFile] + "-" + file + extension + compressExtension).toUtf8(), &s);
		stat(d.absoluteFilePath(file + ".xml").toUtf8(), &uncompressed);
		repomdTs << "	<data type=\"" << file << "\">" << Qt::endl
			<< "		<checksum type=\"sha256\">" << checksum[compressedFile] << "</checksum>" << Qt::endl
			<< "		<open-checksum type=\"sha256\">" << checksum[file] << "</open-checksum>" << Qt::endl
			<< "		<location href=\"repodata/" << checksum[compressedFile] << "-" << file << extension + compressExtension + "\"/>" << Qt::endl
			<< "		<timestamp>" << s.st_mtime << "</timestamp>" << Qt::endl
			<< "		<size>" << s.st_size << "</size>" << Qt::endl
			<< "		<open-size>" << uncompressed.st_size << "</open-size>" << Qt::endl
			<< "	</data>" << Qt::endl;
		QFile::remove(d.absoluteFilePath(file + extension));
	}
	repomdTs << "</repomd>" << Qt::endl;
	repomd.close();

	for(QString const &file : oldMetadata) {
		QFile::remove(d.absoluteFilePath(file));
	}

	return true;
}

static bool mergeMetadata(QDir &d, String const &origin="openmandriva") {
	QDir rd(d.absolutePath() + "/repodata");
	QDir pf(d.absolutePath() + "/repodata/perfile");
	if(!pf.exists()) {
		std::cerr << "No metadata in " << qPrintable(pf.absolutePath()) << std::endl;
		return false;
	}

	QFile primary(rd.absoluteFilePath("primary.xml"));
	if(!primary.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't open " << qPrintable(primary.fileName()) << std::endl;
		return false;
	} else {
		QStringList const primaryFiles = pf.entryList(QStringList() << "*.primary.xml", QDir::Files|QDir::Readable, QDir::Name);

		primary.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<metadata xmlns=\"http://linux.duke.edu/metadata/common\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"");
		primary.write(QString::number(primaryFiles.count()).toLocal8Bit() );
		primary.write("\">\n");
		for(QString const &p : primaryFiles) {
			QFile f(pf.absoluteFilePath(p));
			if(!f.open(QFile::ReadOnly)) {
				std::cerr << "Can't open " << qPrintable(p) << std::endl;
				continue;
			}
			primary.write(f.readAll());
			f.close();
		}
		primary.write("</metadata>");
		primary.close();
	}

	QFile filelists(rd.absoluteFilePath("filelists.xml"));
	if(!filelists.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't open " << qPrintable(filelists.fileName()) << std::endl;
		return false;
	} else {
		QStringList const filelistsFiles = pf.entryList(QStringList() << "*.filelists.xml", QDir::Files|QDir::Readable, QDir::Name);

		filelists.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<filelists xmlns=\"http://linux.duke.edu/metadata/filelists\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"");
		filelists.write(QString::number(filelistsFiles.count()).toLocal8Bit() );
		filelists.write("\">\n");
		for(QString const &p : filelistsFiles) {
			QFile f(pf.absoluteFilePath(p));
			if(!f.open(QFile::ReadOnly)) {
				std::cerr << "Can't open " << qPrintable(p) << std::endl;
				continue;
			}
			filelists.write(f.readAll());
			f.close();
		}
		filelists.write("</filelists>");
		filelists.close();
	}

	QFile other(rd.absoluteFilePath("other.xml"));
	if(!other.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't open " << qPrintable(other.fileName()) << std::endl;
		return false;
	} else {
		QStringList const otherFiles = pf.entryList(QStringList() << "*.other.xml", QDir::Files|QDir::Readable, QDir::Name);

		other.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<otherdata xmlns=\"http://linux.duke.edu/metadata/other\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"");
		other.write(QString::number(otherFiles.count()).toLocal8Bit() );
		other.write("\">\n");
		for(QString const &p : otherFiles) {
			QFile f(pf.absoluteFilePath(p));
			if(!f.open(QFile::ReadOnly)) {
				std::cerr << "Can't open " << qPrintable(p) << std::endl;
				continue;
			}
			other.write(f.readAll());
			f.close();
		}
		other.write("</otherdata>");
		other.close();
	}

	QFile appstream(rd.absoluteFilePath("appstream.xml"));
	if(!appstream.open(QFile::WriteOnly|QFile::Truncate)) {
		std::cerr << "Can't open " << qPrintable(appstream.fileName()) << std::endl;
		return false;
	} else {
		QStringList const appstreamFiles = pf.entryList(QStringList() << "*.appstream.xml", QDir::Files|QDir::Readable, QDir::Name);

		appstream.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<components origin=\"");
		appstream.write(origin);
		appstream.write("\" version=\"0.14\">\n");
		for(QString const &p : appstreamFiles) {
			QFile f(pf.absoluteFilePath(p));
			if(!f.open(QFile::ReadOnly)) {
				std::cerr << "Can't open " << qPrintable(p) << std::endl;
				continue;
			}
			appstream.write(f.readAll());
			f.close();
		}
		appstream.write("</components>");
		appstream.close();
	}

	QStringList const appstreamIcons = pf.entryList(QStringList() << "*.appstream-icons", QDir::Dirs|QDir::Readable, QDir::Name);
	Archive icons(rd.absoluteFilePath("appstream-icons.tar"));
	for(QString const &iconDir : appstreamIcons) {
		QDir d(pf.absoluteFilePath(iconDir));
		QStringList iconFiles = recursiveEntryList(d);
		for(QString const &file : iconFiles) {
			QFile f(d.absoluteFilePath(file));
			if(f.open(QFile::ReadOnly)) {
				icons.addFile(file, f.readAll());
				f.close();
			}
		}
	}
	icons.close();

	return true; 
}

int main(int argc, char **argv) {
	setenv("QT_QPA_PLATFORM", "offscreen", 1);
	QGuiApplication app(argc, argv);
	QGuiApplication::setApplicationName("createmd-perfile");
	QGuiApplication::setApplicationVersion("0.0.1");

	QCommandLineParser cp;
	cp.setApplicationDescription("RPM repository metadata creator");
	cp.addOptions({
		{{"c", "cleanup"}, QGuiApplication::translate("main", "Clean up [remove stale metadata files] only")},
		{{"o", "origin"}, QGuiApplication::translate("main", "Origin identifier to be used (only while generating from scratch)"), "origin"},
		{{"V", "verbose"}, QGuiApplication::translate("main", "Verbose debugging output")},
	});
	cp.addHelpOption();
	cp.addVersionOption();
	cp.addPositionalArgument("path", QGuiApplication::translate("main", "Directory containing the RPM files"), "[path...]");
	cp.process(app);

	if(cp.positionalArguments().isEmpty()) {
		std::cerr << "Usage: " << argv[0] << "/path/to/rpm/files" << std::endl;
		return 1;
	}

	bool const cleanupOnly = cp.isSet("c");
	verbose = cp.isSet("V");
	String origin = cp.value("o");
	if(!origin)
		origin = "openmandriva";

	for(QString const &path : cp.positionalArguments()) {
		QDir d(path);
		cleanup(d);
		if(cleanupOnly)
			continue;
		QStringList files = newFiles(d);
		for(QString const &f : files)
			extractMetadata(d, f);
		files = modifiedFiles(d);
		for(QString const &f : files)
			extractMetadata(d, f);
		mergeMetadata(d, origin);
		finalizeMetadata(d.absoluteFilePath("repodata"));
	}
}
