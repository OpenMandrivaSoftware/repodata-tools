// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include "String.h"
extern "C" {
#include <archive.h>
}

class Compression {
public:
	enum class Format {
		GZip = 0,
		Bzip2,
		Compress,
		Lzma,
		Xz,
		Lzip,
		LRzip,
		LZOP,
		GRZip,
		LZ4,
		Zstd,
	};

public:
	static bool CompressFile(String const &source, Format c=Format::Xz, String target=String());
	static QByteArray uncompressedFile(String const &source);
};
