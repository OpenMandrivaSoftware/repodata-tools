#include "DesktopFile.h"
#include <QBuffer>
#include <iostream>

DesktopFile::DesktopFile(QByteArray contents) {
	QBuffer b(&contents);
	b.open(QBuffer::ReadOnly);
	String currentSection;
	QHash<String,String> sectionContent;
	while(b.canReadLine()) {
		String l=b.readLine().trimmed();
		if(l.startsWith('[') && l.endsWith(']')) {
			if(sectionContent.count())
				insert(currentSection, sectionContent);
			currentSection = l.sliced(1, l.length()-2);
			sectionContent.clear();
		} else if(l.contains('=')) {
			sectionContent.insert(l.sliced(0, l.indexOf('=')), l.sliced(l.indexOf('=')+1));
		}
	}
	if(sectionContent.count())
		insert(currentSection, sectionContent);
}

QList<String> DesktopFile::sections() const {
	QList<String> ret;
	for(auto i=cbegin(), e=cend(); i!=e; ++i)
		ret.append(i.key());
	return ret;
}

String DesktopFile::value(String const &key, String const &dflt, String const &section) const {
	return QHash<String, QHash<String, String>>::value(section).value(key, dflt);
}

bool DesktopFile::hasKey(String const &key, String const &section) const {
	if(!QHash<String, QHash<String, String>>::contains(section))
		return false;
	return QHash<String, QHash<String, String>>::value(section).contains(key);
}
