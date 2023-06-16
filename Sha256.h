// SPDX-License-Identifier: AGPL-3.0-or-later
// (C) 2023 Bernhard Rosenkränzer <bero@lindev.ch>
#pragma once

#include "String.h"

class Sha256 {
public:
	static String checksum(String const &filename);
};
