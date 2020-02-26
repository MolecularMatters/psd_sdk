// Copyright 2011-2020, Molecular Matters GmbH <office@molecular-matters.com>
// See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

#pragma once

#include "../Psd/Psdstdint.h"


namespace tgaExporter
{
	/// Assumes 8-bit single-channel data.
	void SaveMonochrome(const wchar_t* filename, unsigned int width, unsigned int height, const uint8_t* data);

	/// Assumes 8-bit RGBA data, but ignores alpha (32-bit data is assumed for performance reasons).
	void SaveRGB(const wchar_t* filename, unsigned int width, unsigned int height, const uint8_t* data);

	/// Assumes 8-bit RGBA data.
	void SaveRGBA(const wchar_t* filename, unsigned int width, unsigned int height, const uint8_t* data);
}
