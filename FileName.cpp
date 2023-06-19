// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#include "FileName.h"

String FileName::basename(String const &extension) const {
	String bn = strrchr(constData(), '/');
	if(bn) {
		if(extension && bn.endsWith(QByteArrayView(extension)))
			return bn.sliced(1, bn.length()-extension.length()-1);
		return bn.sliced(1);
	}
	return constData();
}

String FileName::dirname() const {
	int const lastSlash = lastIndexOf('/');
	if(lastSlash < 0)
		return constData();
	return QByteArray(constData(), lastSlash);
}
