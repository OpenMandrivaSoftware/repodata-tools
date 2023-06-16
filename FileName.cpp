// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "FileName.h"

char const * const FileName::basename() const {
	char const * const bn = strrchr(constData(), '/');
	if(bn)
		return bn+1;
	return constData();
}

char const * const FileName::dirname() const {
	int const lastSlash = lastIndexOf('/');
	if(lastSlash < 0)
		return constData();
	return QByteArray(constData(), lastSlash).constData();
}
