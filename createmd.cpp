// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "Rpm.h"
#include "Sha256.h"
#include "Compression.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <iostream>

extern "C" {
#include <time.h>
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("createmd");
	QCoreApplication::setApplicationVersion("0.0.1");

	QCommandLineParser cp;
	cp.setApplicationDescription("RPM repository metadata creator");
	cp.addHelpOption();
	cp.addPositionalArgument("path", QCoreApplication::translate("main", "Directory containing the RPM files"));
	cp.process(app);

	if(cp.positionalArguments().isEmpty()) {
		std::cerr << "Usage: " << argv[0] << "/path/to/rpm/files" << std::endl;
		return 1;
	}

	for(QString const &path : cp.positionalArguments()) {
		QDir d(path);
		if(!d.exists()) {
			std::cerr << qPrintable(path) << " not found, ignoring" << std::endl;
			continue;
		}
		QStringList rpms = d.entryList(QStringList() << "*.rpm", QDir::Files|QDir::Readable, QDir::Name);
		if(rpms.isEmpty()) {
			std::cerr << "No rpms found in " << qPrintable(path) << ", ignoring" << std::endl;
			continue;
		}
		String tempName = ".repodata.temp." + String::number(getpid());
		d.mkdir(tempName, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ExeGroup|QFile::ReadOther|QFile::ExeOther);
		QDir rd(path + "/" + tempName);
		if(!rd.exists()) {
			std::cerr << "Can't create/use repodata directory in " << qPrintable(path) << ", ignoring" << std::endl;
			continue;
		}
		QFile primary(rd.filePath("primary.xml"));
		if(!primary.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't create primary.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
			continue;
		}
		QFile filelists(rd.filePath("filelists.xml"));
		if(!filelists.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't create filelists.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
			continue;
		}
		QFile other(rd.filePath("other.xml"));
		if(!other.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't create other.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
			continue;
		}
		QFile appstream(rd.filePath("appstream.xml"));
		if(!appstream.open(QFile::WriteOnly|QFile::Truncate)) {
			std::cerr << "Can't create appstream.xml in " << qPrintable(rd.absolutePath()) << ", ignoring" << std::endl;
			continue;
		}
		appstream.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<components origin=\"openmandriva\" version=\"0.14\">\n");

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
				<< "		<rpm:license>" << r.license() << "</rpm:license>" << Qt::endl
				<< "		<rpm:vendor>" << r.vendor() << "</rpm:vendor>" << Qt::endl
				<< "		<rpm:group>" << r.group() << "</rpm:group>" << Qt::endl
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

			appstream.write(r.appstreamMd());
		}
		primaryTs << "</metadata>" << Qt::endl;
		filelistsTs << "</filelists>" << Qt::endl;
		otherTs << "</otherdata>" << Qt::endl;
		appstream.write("</components>\n");

		primary.close();
		filelists.close();
		other.close();
		appstream.close();

		Compression::CompressFile(rd.filePath("primary.xml"));
		Compression::CompressFile(rd.filePath("filelists.xml"));
		Compression::CompressFile(rd.filePath("other.xml"));
		Compression::CompressFile(rd.filePath("appstream.xml"), Compression::Format::GZip);

		QHash<String,String> checksum{
			{"primary", Sha256::checksum(rd.filePath("primary.xml"))},
			{"filelists", Sha256::checksum(rd.filePath("filelists.xml"))},
			{"other", Sha256::checksum(rd.filePath("other.xml"))},
			{"appstream", Sha256::checksum(rd.filePath("appstream.xml"))},
			{"primaryXZ", Sha256::checksum(rd.filePath("primary.xml.xz"))},
			{"filelistsXZ", Sha256::checksum(rd.filePath("filelists.xml.xz"))},
			{"otherXZ", Sha256::checksum(rd.filePath("other.xml.xz"))},
			{"appstreamGZ", Sha256::checksum(rd.filePath("appstream.xml.gz"))}
		};

		QFile::rename(rd.filePath("primary.xml.xz"), rd.filePath(checksum["primaryXZ"] + "-primary.xml.xz"));
		QFile::rename(rd.filePath("filelists.xml.xz"), rd.filePath(checksum["filelistsXZ"] + "-filelists.xml.xz"));
		QFile::rename(rd.filePath("other.xml.xz"), rd.filePath(checksum["otherXZ"] + "-other.xml.xz"));
		QFile::rename(rd.filePath("appstream.xml.gz"), rd.filePath(checksum["appstreamGZ"] + "-appstream.xml.gz"));

		QFile repomd(rd.filePath("repomd.xml"));
		repomd.open(QFile::WriteOnly|QFile::Truncate);
		QTextStream repomdTs(&repomd);
		time_t timestamp = time(0);
		repomdTs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl
			<< "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\" xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\">" << Qt::endl
			<< "	<revision>" << timestamp << "</revision>" << Qt::endl;
		for(String const &file : QList<String>{"primary", "filelists", "other", "appstream"}) {
			String compressedFile = (file == "appstream") ? file + "GZ" : file + "XZ";
			String compressExtension = (file == "appstream") ? ".gz" : ".xz";
			struct stat s, uncompressed;
			stat(rd.filePath(checksum[compressedFile] + "-" + file + ".xml" + compressExtension).toUtf8(), &s);
			stat(rd.filePath(file + ".xml").toUtf8(), &uncompressed);
			repomdTs << "	<data type=\"" << file << "\">" << Qt::endl
				<< "		<checksum type=\"sha256\">" << checksum[compressedFile] << "</checksum>" << Qt::endl
				<< "		<open-checksum type=\"sha256\">" << checksum[file] << "</open-checksum>" << Qt::endl
				<< "		<location href=\"repodata/" << checksum[compressedFile] << "-" << file << ".xml" + compressExtension + "\"/>" << Qt::endl
				<< "		<timestamp>" << s.st_mtime << "</timestamp>" << Qt::endl
				<< "		<size>" << s.st_size << "</size>" << Qt::endl
				<< "		<open-size>" << uncompressed.st_size << "</open-size>" << Qt::endl
				<< "	</data>" << Qt::endl;
		}
		repomdTs << "</repomd>" << Qt::endl;
		repomd.close();

		QDir realRepodata(path + "/repodata");
		realRepodata.removeRecursively();
		d.rename(tempName, "repodata");
	}
}
