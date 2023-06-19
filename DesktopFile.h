// SPDX-License--Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include <QHash>
#include "String.h"

class DesktopFile:public QHash<String,QHash<String,String>> {
public:
	DesktopFile(QByteArray contents);
	QList<String> sections() const;
	String value(String const &key, String const &dflt=String(), String const &section="Desktop Entry") const;
	bool hasKey(String const &key, String const &section="Desktop Entry") const;
};
