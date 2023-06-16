// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include <QByteArray>
#include <QString>
#include <iostream>

/**
 * A string that interoperates with both C style strings (rpmlib API)
 * and QStrings (Qt API).
 * This is QByteArray based rather than using QString as a typical Qt API would
 * because the main purpose for now is using inside rpm metadata generation,
 * which needs to be FAST (and therefore should avoid superfluous conversions
 * between rpm's native UTF-8 and Qt's native UTF-16).
 * This should be replaced if and when Qt switches to UTF-8.
 */
class String:public QByteArray {
public:
	String():QByteArray() {}
	String(QByteArray const &a):QByteArray(a) {}
	String(char const *data, qsizetype size = -1):QByteArray(data, size) {}
	String(QString const &s):QByteArray(s.toUtf8()) {}
	operator bool() const { return size(); }
	operator char const *() const { return constData(); }
	operator QString() const { return QString::fromUtf8(constData()); }
	String xmlEncode() const;
};

std::ostream &operator <<(std::ostream &os, String const &s);
