// Copyright 2011-2020, Molecular Matters GmbH <office@molecular-matters.com>
// See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

// the main include that always needs to be included in every translation unit that uses the PSD library
#include "../Psd/Psd.h"

// for convenience reasons, we directly include the platform header from the PSD library.
// we could have just included <Windows.h> as well, but that is unnecessarily big, and triggers lots of warnings.
#include "../Psd/PsdPlatform.h"

// in the sample, we use the provided malloc allocator for all memory allocations. likewise, we also use the provided
// native file interface.
// in your code, feel free to use whatever allocator you have lying around.
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdNativeFile.h"

#include "../Psd/PsdDocument.h"
#include "../Psd/PsdColorMode.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdChannel.h"
#include "../Psd/PsdChannelType.h"
#include "../Psd/PsdLayerMask.h"
#include "../Psd/PsdVectorMask.h"
#include "../Psd/PsdLayerMaskSection.h"
#include "../Psd/PsdImageDataSection.h"
#include "../Psd/PsdImageResourcesSection.h"
#include "../Psd/PsdParseDocument.h"
#include "../Psd/PsdParseLayerMaskSection.h"
#include "../Psd/PsdParseImageDataSection.h"
#include "../Psd/PsdParseImageResourcesSection.h"
#include "../Psd/PsdLayerCanvasCopy.h"
#include "../Psd/PsdInterleave.h"
#include "../Psd/PsdPlanarImage.h"
#include "../Psd/PsdExport.h"
#include "../Psd/PsdExportDocument.h"

#include "PsdTgaExporter.h"
#include "PsdDebug.h"

PSD_PUSH_WARNING_LEVEL(0)
	// disable annoying warning caused by xlocale(337): warning C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc
	#pragma warning(disable:4530)
	#include <string>
	#include <sstream>
PSD_POP_WARNING_LEVEL

PSD_USING_NAMESPACE;


#ifdef __linux
	#include <climits>
	#include <cstring>
#endif

// helpers for reading PSDs
namespace
{
	static const unsigned int CHANNEL_NOT_FOUND = UINT_MAX;


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T, typename DataHolder>
	static void* ExpandChannelToCanvas(Allocator* allocator, const DataHolder* layer, const void* data, unsigned int canvasWidth, unsigned int canvasHeight)
	{
		T* canvasData = static_cast<T*>(allocator->Allocate(sizeof(T)*canvasWidth*canvasHeight, 16u));
		memset(canvasData, 0u, sizeof(T)*canvasWidth*canvasHeight);

		imageUtil::CopyLayerData(static_cast<const T*>(data), canvasData, layer->left, layer->top, layer->right, layer->bottom, canvasWidth, canvasHeight);

		return canvasData;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	static void* ExpandChannelToCanvas(const Document* document, Allocator* allocator, Layer* layer, Channel* channel)
	{
		if (document->bitsPerChannel == 8)
			return ExpandChannelToCanvas<uint8_t>(allocator, layer, channel->data, document->width, document->height);
		else if (document->bitsPerChannel == 16)
			return ExpandChannelToCanvas<uint16_t>(allocator, layer, channel->data, document->width, document->height);
		else if (document->bitsPerChannel == 32)
			return ExpandChannelToCanvas<float32_t>(allocator, layer, channel->data, document->width, document->height);

		return nullptr;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	static void* ExpandMaskToCanvas(const Document* document, Allocator* allocator, T* mask)
	{
		if (document->bitsPerChannel == 8)
			return ExpandChannelToCanvas<uint8_t>(allocator, mask, mask->data, document->width, document->height);
		else if (document->bitsPerChannel == 16)
			return ExpandChannelToCanvas<uint16_t>(allocator, mask, mask->data, document->width, document->height);
		else if (document->bitsPerChannel == 32)
			return ExpandChannelToCanvas<float32_t>(allocator, mask, mask->data, document->width, document->height);

		return nullptr;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	unsigned int FindChannel(Layer* layer, int16_t channelType)
	{
		for (unsigned int i = 0; i < layer->channelCount; ++i)
		{
			Channel* channel = &layer->channels[i];
			if (channel->data && channel->type == channelType)
				return i;
		}

		return CHANNEL_NOT_FOUND;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, unsigned int width, unsigned int height)
	{
		T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

		const T* r = static_cast<const T*>(srcR);
		const T* g = static_cast<const T*>(srcG);
		const T* b = static_cast<const T*>(srcB);
		imageUtil::InterleaveRGB(r, g, b, T(0), image, width, height);

		return image;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, const void* srcA, unsigned int width, unsigned int height)
	{
		T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

		const T* r = static_cast<const T*>(srcR);
		const T* g = static_cast<const T*>(srcG);
		const T* b = static_cast<const T*>(srcB);
		const T* a = static_cast<const T*>(srcA);
		imageUtil::InterleaveRGBA(r, g, b, a, image, width, height);

		return image;
	}
}


// helpers for writing PSDs
namespace
{
	static const unsigned int IMAGE_WIDTH = 256u;
	static const unsigned int IMAGE_HEIGHT = 256u;

	static uint8_t g_multiplyData[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint8_t g_xorData[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint8_t g_orData[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint8_t g_andData[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint8_t g_checkerBoardData[IMAGE_HEIGHT][IMAGE_WIDTH] = {};

	static uint16_t g_multiplyData16[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint16_t g_xorData16[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint16_t g_orData16[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint16_t g_andData16[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static uint16_t g_checkerBoardData16[IMAGE_HEIGHT][IMAGE_WIDTH] = {};

	static float32_t g_multiplyData32[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static float32_t g_xorData32[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static float32_t g_orData32[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static float32_t g_andData32[IMAGE_HEIGHT][IMAGE_WIDTH] = {};
	static float32_t g_checkerBoardData32[IMAGE_HEIGHT][IMAGE_WIDTH] = {};


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	void GenerateImageData(void)
	{
		for (unsigned int y = 0; y < IMAGE_HEIGHT; ++y)
		{
			for (unsigned int x = 0; x < IMAGE_WIDTH; ++x)
			{
				g_multiplyData[y][x] = (x*y >> 8u) & 0xFF;
				g_xorData[y][x] = (x ^ y) & 0xFF;
				g_orData[y][x] = (x | y) & 0xFF;
				g_andData[y][x] = (x & y) & 0xFF;
				g_checkerBoardData[y][x] = (x / 8 + y / 8) & 1 ? 255u : 128u;

				g_multiplyData16[y][x] = (x*y) & 0xFFFF;
				g_xorData16[y][x] = ((x ^ y) * 256) & 0xFFFF;
				g_orData16[y][x] = ((x | y) * 256) & 0xFFFF;
				g_andData16[y][x] = ((x & y) * 256) & 0xFFFF;
				g_checkerBoardData16[y][x] = (x / 8 + y / 8) & 1 ? 65535u : 32768u;

				g_multiplyData32[y][x] = (1.0f / 65025.0f) * (x*y);
				g_xorData32[y][x] = (1.0f / 65025.0f) * ((x ^ y) * 256);
				g_orData32[y][x] = (1.0f / 65025.0f) * ((x | y) * 256);
				g_andData32[y][x] = (1.0f / 65025.0f) * ((x & y) * 256);
				g_checkerBoardData32[y][x] = (x / 8 + y / 8) & 1 ? 1.0f : 0.5f;
			}
		}
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
static std::wstring GetSampleInputPath(void)
{
	// TODO: add support for other platforms
//#ifdef _WIN32
	return L"../../bin/";
//#endif
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
static std::wstring GetSampleOutputPath(void)
{
	// TODO: add support for other platforms
//#ifdef _WIN32
	return L"../../bin/";
//#endif
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
int SampleReadPsd(void)
{
	const std::wstring srcPath = GetSampleInputPath() + L"Sample.psd";

	MallocAllocator allocator;
	NativeFile file(&allocator);

	// try opening the file. if it fails, bail out.
	if (!file.OpenRead(srcPath.c_str()))
	{
		PSD_SAMPLE_LOG("Cannot open file.\n");
		return 1;
	}

	// create a new document that can be used for extracting different sections from the PSD.
	// additionally, the document stores information like width, height, bits per pixel, etc.
	Document* document = CreateDocument(&file, &allocator);
	if (!document)
	{
		PSD_SAMPLE_LOG("Cannot create document.\n");
		file.Close();
		return 1;
	}

	// the sample only supports RGB colormode
	if (document->colorMode != colorMode::RGB)
	{
		PSD_SAMPLE_LOG("Document is not in RGB color mode.\n");
		DestroyDocument(document, &allocator);
		file.Close();
		return 1;
	}

	// extract image resources section.
	// this gives access to the ICC profile, EXIF data and XMP metadata.
	{
		ImageResourcesSection* imageResourcesSection = ParseImageResourcesSection(document, &file, &allocator);
		PSD_SAMPLE_LOG("XMP metadata:\n");
		PSD_SAMPLE_LOG(imageResourcesSection->xmpMetadata);
		PSD_SAMPLE_LOG("\n");
		DestroyImageResourcesSection(imageResourcesSection, &allocator);
	}

	// extract all layers and masks.
	bool hasTransparencyMask = false;
	LayerMaskSection* layerMaskSection = ParseLayerMaskSection(document, &file, &allocator);
	if (layerMaskSection)
	{
		hasTransparencyMask = layerMaskSection->hasTransparencyMask;

		// extract all layers one by one. this should be done in parallel for maximum efficiency.
		for (unsigned int i = 0; i < layerMaskSection->layerCount; ++i)
		{
			Layer* layer = &layerMaskSection->layers[i];
			ExtractLayer(document, &file, &allocator, layer);

			// check availability of R, G, B, and A channels.
			// we need to determine the indices of channels individually, because there is no guarantee that R is the first channel,
			// G is the second, B is the third, and so on.
			const unsigned int indexR = FindChannel(layer, channelType::R);
			const unsigned int indexG = FindChannel(layer, channelType::G);
			const unsigned int indexB = FindChannel(layer, channelType::B);
			const unsigned int indexA = FindChannel(layer, channelType::TRANSPARENCY_MASK);

			// note that channel data is only as big as the layer it belongs to, e.g. it can be smaller or bigger than the canvas,
			// depending on where it is positioned. therefore, we use the provided utility functions to expand/shrink the channel data
			// to the canvas size. of course, you can work with the channel data directly if you need to.
			void* canvasData[4] = {};
			unsigned int channelCount = 0u;
			if ((indexR != CHANNEL_NOT_FOUND) && (indexG != CHANNEL_NOT_FOUND) && (indexB != CHANNEL_NOT_FOUND))
			{
				// RGB channels were found.
				canvasData[0] = ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexR]);
				canvasData[1] = ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexG]);
				canvasData[2] = ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexB]);
				channelCount = 3u;

				if (indexA != CHANNEL_NOT_FOUND)
				{
					// A channel was also found.
					canvasData[3] = ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexA]);
					channelCount = 4u;
				}
			}

			// interleave the different pieces of planar canvas data into one RGB or RGBA image, depending on what channels
			// we found, and what color mode the document is stored in.
			uint8_t* image8 = nullptr;
			uint16_t* image16 = nullptr;
			float32_t* image32 = nullptr;
			if (channelCount == 3u)
			{
				if (document->bitsPerChannel == 8)
				{
					image8 = CreateInterleavedImage<uint8_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], document->width, document->height);
				}
				else if (document->bitsPerChannel == 16)
				{
					image16 = CreateInterleavedImage<uint16_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], document->width, document->height);
				}
				else if (document->bitsPerChannel == 32)
				{
					image32 = CreateInterleavedImage<float32_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], document->width, document->height);
				}
			}
			else if (channelCount == 4u)
			{
				if (document->bitsPerChannel == 8)
				{
					image8 = CreateInterleavedImage<uint8_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], canvasData[3], document->width, document->height);
				}
				else if (document->bitsPerChannel == 16)
				{
					image16 = CreateInterleavedImage<uint16_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], canvasData[3], document->width, document->height);
				}
				else if (document->bitsPerChannel == 32)
				{
					image32 = CreateInterleavedImage<float32_t>(&allocator, canvasData[0], canvasData[1], canvasData[2], canvasData[3], document->width, document->height);
				}
			}

			allocator.Free(canvasData[0]);
			allocator.Free(canvasData[1]);
			allocator.Free(canvasData[2]);
			allocator.Free(canvasData[3]);

			// get the layer name.
			// Unicode data is preferred because it is not truncated by Photoshop, but unfortunately it is optional.
			// fall back to the ASCII name in case no Unicode name was found.
			std::wstringstream layerName;
			if (layer->utf16Name)
			{
				#ifdef _WIN32
				//In Windows wchar_t is utf16
				PSD_STATIC_ASSERT(sizeof(wchar_t) == sizeof(uint16_t));
				layerName << reinterpret_cast<wchar_t*>(layer->utf16Name);
				#else
				//In Linux, wchar_t is utf32
				//Convert code from https://stackoverflow.com/questions/23919515/how-to-convert-from-utf-16-to-utf-32-on-linux-with-std-library#comment95663809_23920015
				auto is_surrogate = [](uint16_t uc) -> bool
				{ 
					return ((uc - 0xd800u) < 2048u ); 
				};
				auto is_high_surrogate = [](uint16_t uc) -> bool
				{
					return ((uc & 0xfffffc00) == 0xd800 );
				};
				auto is_low_surrogate = [](uint16_t uc ) -> bool
				{
					return ((uc & 0xfffffc00) == 0xdc00 );
				};
				auto surrogate_to_utf32 = [](uint16_t high,uint16_t low) -> wchar_t
				{
					return ((high << 10) + low - 0x35fdc00);
				};
				PSD_STATIC_ASSERT(sizeof(wchar_t) == sizeof(uint32_t));

				//Begin convert
				size_t u16len = 0;
				const uint16_t * cur = layer->utf16Name;
				while(*cur != uint16_t('\0')){
					cur++;
					u16len++;
				}
				//Len it

				const uint16_t * const end = layer->utf16Name + u16len;
				const uint16_t * input = layer->utf16Name;
				while (input < end) {
					const uint16_t uc = *input++;
					if (!is_surrogate(uc)) {
						layerName << wchar_t(uc); 
					} 
					else {
						if (is_high_surrogate(uc) && input < end && is_low_surrogate(*input)){
							layerName << wchar_t(surrogate_to_utf32(uc, *input++));
						}
						else{
							// Error
							// Impossible
							std::abort();
						}
					}
				}
				#endif
			}
			else
			{
				layerName << layer->name.c_str();
			}

			// at this point, image8, image16 or image32 store either a 8-bit, 16-bit, or 32-bit image, respectively.
			// the image data is stored in interleaved RGB or RGBA, and has the size "document->width*document->height".
			// it is up to you to do whatever you want with the image data. in the sample, we simply write the image to a .TGA file.
			if (channelCount == 3u)
			{
				if (document->bitsPerChannel == 8u)
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"layer";
					filename << layerName.str();
					filename << L".tga";
					tgaExporter::SaveRGB(filename.str().c_str(), document->width, document->height, image8);
				}
			}
			else if (channelCount == 4u)
			{
				if (document->bitsPerChannel == 8u)
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"layer";
					filename << layerName.str();
					filename << L".tga";
					tgaExporter::SaveRGBA(filename.str().c_str(), document->width, document->height, image8);
				}
			}

			allocator.Free(image8);
			allocator.Free(image16);
			allocator.Free(image32);

			// in addition to the layer data, we also want to extract the user and/or vector mask.
			// luckily, this has been handled already by the ExtractLayer() function. we just need to check whether a mask exists.
			if (layer->layerMask)
			{
				// a layer mask exists, and data is available. work out the mask's dimensions.
				const unsigned int width = static_cast<unsigned int>(layer->layerMask->right - layer->layerMask->left);
				const unsigned int height = static_cast<unsigned int>(layer->layerMask->bottom - layer->layerMask->top);

				// similar to layer data, the mask data can be smaller or bigger than the canvas.
				// the mask data is always single-channel (monochrome), and has a width and height as calculated above.
				void* maskData = layer->layerMask->data;
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"layer";
					filename << layerName.str();
					filename << L"_usermask.tga";
					tgaExporter::SaveMonochrome(filename.str().c_str(), width, height, static_cast<const uint8_t*>(maskData));
				}

				// use ExpandMaskToCanvas create an image that is the same size as the canvas.
				void* maskCanvasData = ExpandMaskToCanvas(document, &allocator, layer->layerMask);
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"canvas";
					filename << layerName.str();
					filename << L"_usermask.tga";
					tgaExporter::SaveMonochrome(filename.str().c_str(), document->width, document->height, static_cast<const uint8_t*>(maskCanvasData));
				}

				allocator.Free(maskCanvasData);
			}

			if (layer->vectorMask)
			{
				// accessing the vector mask works exactly like accessing the layer mask.
				const unsigned int width = static_cast<unsigned int>(layer->vectorMask->right - layer->vectorMask->left);
				const unsigned int height = static_cast<unsigned int>(layer->vectorMask->bottom - layer->vectorMask->top);

				void* maskData = layer->vectorMask->data;
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"layer";
					filename << layerName.str();
					filename << L"_vectormask.tga";
					tgaExporter::SaveMonochrome(filename.str().c_str(), width, height, static_cast<const uint8_t*>(maskData));
				}

				void* maskCanvasData = ExpandMaskToCanvas(document, &allocator, layer->vectorMask);
				{
					std::wstringstream filename;
					filename << GetSampleOutputPath();
					filename << L"canvas";
					filename << layerName.str();
					filename << L"_vectormask.tga";
					tgaExporter::SaveMonochrome(filename.str().c_str(), document->width, document->height, static_cast<const uint8_t*>(maskCanvasData));
				}

				allocator.Free(maskCanvasData);
			}
		}

		DestroyLayerMaskSection(layerMaskSection, &allocator);
	}

	// extract the image data section, if available. the image data section stores the final, merged image, as well as additional
	// alpha channels. this is only available when saving the document with "Maximize Compatibility" turned on.
	if (document->imageDataSection.length != 0)
	{
		ImageDataSection* imageData = ParseImageDataSection(document, &file, &allocator);
		if (imageData)
		{
			// interleave the planar image data into one RGB or RGBA image.
			// store the rest of the (alpha) channels and the transparency mask separately.
			const unsigned int imageCount = imageData->imageCount;

			// note that an image can have more than 3 channels, but still no transparency mask in case all extra channels
			// are actual alpha channels.
			bool isRgb = false;
			if (imageCount == 3)
			{
				// imageData->images[0], imageData->images[1] and imageData->images[2] contain the R, G, and B channels of the merged image.
				// they are always the size of the canvas/document, so we can interleave them using imageUtil::InterleaveRGB directly.
				isRgb = true;
			}
			else if (imageCount >= 4)
			{
				// check if we really have a transparency mask that belongs to the "main" merged image.
				if (hasTransparencyMask)
				{
					// we have 4 or more images/channels, and a transparency mask.
					// this means that images 0-3 are RGBA, respectively.
					isRgb = false;
				}
				else
				{
					// we have 4 or more images stored in the document, but none of them is the transparency mask.
					// this means we are dealing with RGB (!) data, and several additional alpha channels.
					isRgb = true;
				}
			}

			uint8_t* image8 = nullptr;
			uint16_t* image16 = nullptr;
			float32_t* image32 = nullptr;
			if (isRgb)
			{
				// RGB
				if (document->bitsPerChannel == 8)
				{
					image8 = CreateInterleavedImage<uint8_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, document->width, document->height);
				}
				else if (document->bitsPerChannel == 16)
				{
					image16 = CreateInterleavedImage<uint16_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, document->width, document->height);
				}
				else if (document->bitsPerChannel == 32)
				{
					image32 = CreateInterleavedImage<float32_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, document->width, document->height);
				}
			}
			else
			{
				// RGBA
				if (document->bitsPerChannel == 8)
				{
					image8 = CreateInterleavedImage<uint8_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, imageData->images[3].data, document->width, document->height);
				}
				else if (document->bitsPerChannel == 16)
				{
					image16 = CreateInterleavedImage<uint16_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, imageData->images[3].data, document->width, document->height);
				}
				else if (document->bitsPerChannel == 32)
				{
					image32 = CreateInterleavedImage<float32_t>(&allocator, imageData->images[0].data, imageData->images[1].data, imageData->images[2].data, imageData->images[3].data, document->width, document->height);
				}
			}

			if (document->bitsPerChannel == 8)
			{
				std::wstringstream filename;
				filename << GetSampleOutputPath();
				filename << L"merged.tga";
				if (isRgb)
				{
					tgaExporter::SaveRGB(filename.str().c_str(), document->width, document->height, image8);
				}
				else
				{
					tgaExporter::SaveRGBA(filename.str().c_str(), document->width, document->height, image8);
				}
			}

			allocator.Free(image8);
			allocator.Free(image16);
			allocator.Free(image32);

			// extract image resources in order to acquire the alpha channel names.
			ImageResourcesSection* imageResources = ParseImageResourcesSection(document, &file, &allocator);
			if (imageResources)
			{
				// store all the extra alpha channels. in case we have a transparency mask, it will always be the first of the
				// extra channels.
				// alpha channel names can be accessed using imageResources->alphaChannels[index].
				// loop through all alpha channels, and skip all channels that were already merged (either RGB or RGBA).
				const unsigned int skipImageCount = isRgb ? 3u : 4u;
				for (unsigned int i = 0u; i < imageCount - skipImageCount; ++i)
				{
					AlphaChannel* channel = imageResources->alphaChannels + i;

					if (document->bitsPerChannel == 8)
					{
						std::wstringstream filename;
						filename << GetSampleOutputPath();
						filename << L"extra_channel_";
						filename << channel->asciiName.c_str();
						filename << L".tga";

						tgaExporter::SaveMonochrome(filename.str().c_str(), document->width, document->height, static_cast<const uint8_t*>(imageData->images[i + skipImageCount].data));
					}
				}

				DestroyImageResourcesSection(imageResources, &allocator);
			}

			DestroyImageDataSection(imageData, &allocator);
		}
	}

	// don't forget to destroy the document, and close the file.
	DestroyDocument(document, &allocator);
	file.Close();

	return 0;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
int SampleWritePsd(void)
{
	GenerateImageData();

	{
		const std::wstring dstPath = GetSampleOutputPath() + L"SampleWrite_8.psd";

		MallocAllocator allocator;
		NativeFile file(&allocator);

		// try opening the file. if it fails, bail out.
		if (!file.OpenWrite(dstPath.c_str()))
		{
			PSD_SAMPLE_LOG("Cannot open file.\n");
			return 1;
		}

		// write an RGB PSD file, 8-bit
		ExportDocument* document = CreateExportDocument(&allocator, IMAGE_WIDTH, IMAGE_HEIGHT, 8u, exportColorMode::RGB);
		{
			// metadata can be added as simple key-value pairs.
			// when loading the document, they will be contained in XMP metadata such as e.g.
			// <xmp:MyAttribute>MyValue</xmp:MyAttribute>
			AddMetaData(document, &allocator, "MyAttribute", "MyValue");

			// when adding a layer to the document, you first need to get a new index into the layer table.
			// with a valid index, layers can be updated in parallel, in any order.
			// this also allows you to only update the layer data that has changed, which is crucial when working with large data sets.
			const unsigned int layer1 = AddLayer(document, &allocator, "MUL pattern");
			const unsigned int layer2 = AddLayer(document, &allocator, "XOR pattern");
			const unsigned int layer3 = AddLayer(document, &allocator, "Mixed pattern with transparency");

			// note that each layer has its own compression type. it is perfectly legal to compress different channels of different layers with different settings.
			// RAW is pretty much just a raw data dump. fastest to write, but large.
			// RLE stores run-length encoded data which can be good for 8-bit channels, but not so much for 16-bit or 32-bit data.
			// ZIP is a good compromise between speed and size.
			// ZIP_WITH_PREDICTION first delta encodes the data, and then zips it. slowest to write, but also smallest in size for most images.
			UpdateLayer(document, &allocator, layer1, exportChannel::RED, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer1, exportChannel::GREEN, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer1, exportChannel::BLUE, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData[0][0], compressionType::RAW);

			UpdateLayer(document, &allocator, layer2, exportChannel::RED, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer2, exportChannel::GREEN, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer2, exportChannel::BLUE, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData[0][0], compressionType::RAW);

			UpdateLayer(document, &allocator, layer3, exportChannel::RED, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer3, exportChannel::GREEN, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer3, exportChannel::BLUE, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_orData[0][0], compressionType::RAW);

			// note that transparency information is always supported, regardless of the export color mode.
			// it is saved as true transparency, and not as separate alpha channel.
			UpdateLayer(document, &allocator, layer1, exportChannel::ALPHA, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer2, exportChannel::ALPHA, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer3, exportChannel::ALPHA, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_orData[0][0], compressionType::RAW);

			// merged image data is optional. if none is provided, black channels will be exported instead.
			UpdateMergedImage(document, &allocator, &g_multiplyData[0][0], &g_xorData[0][0], &g_orData[0][0]);

			// when adding a channel to the document, you first need to get a new index into the channel table.
			// with a valid index, channels can be updated in parallel, in any order.
			// add four spot colors (red, green, blue, and a mix) as additional channels.
			{
				const unsigned int spotIndex = AddAlphaChannel(document, &allocator, "Spot Red", 65535u, 0u, 0u, 0u, 100u, AlphaChannel::Mode::SPOT);
				UpdateChannel(document, &allocator, spotIndex, &g_multiplyData[0][0]);
			}
			{
				const unsigned int spotIndex = AddAlphaChannel(document, &allocator, "Spot Green", 0u, 65535u, 0u, 0u, 75u, AlphaChannel::Mode::SPOT);
				UpdateChannel(document, &allocator, spotIndex, &g_xorData[0][0]);
			}
			{
				const unsigned int spotIndex = AddAlphaChannel(document, &allocator, "Spot Blue", 0u, 0u, 65535u, 0u, 50u, AlphaChannel::Mode::SPOT);
				UpdateChannel(document, &allocator, spotIndex, &g_orData[0][0]);
			}
			{
				const unsigned int spotIndex = AddAlphaChannel(document, &allocator, "Mix", 20000u, 50000u, 30000u, 0u, 100u, AlphaChannel::Mode::SPOT);
				UpdateChannel(document, &allocator, spotIndex, &g_orData[0][0]);
			}

			WriteDocument(document, &allocator, &file);
		}

		DestroyExportDocument(document, &allocator);
		file.Close();
	}
	{
		const std::wstring dstPath = GetSampleOutputPath() + L"SampleWrite_16.psd";

		MallocAllocator allocator;
		NativeFile file(&allocator);

		// try opening the file. if it fails, bail out.
		if (!file.OpenWrite(dstPath.c_str()))
		{
			PSD_SAMPLE_LOG("Cannot open file.\n");
			return 1;
		}

		// write a Grayscale PSD file, 16-bit.
		// Grayscale works similar to RGB, only the types of export channels change.
		ExportDocument* document = CreateExportDocument(&allocator, IMAGE_WIDTH, IMAGE_HEIGHT, 16u, exportColorMode::GRAYSCALE);
		{
			const unsigned int layer1 = AddLayer(document, &allocator, "MUL pattern");
			UpdateLayer(document, &allocator, layer1, exportChannel::GRAY, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData16[0][0], compressionType::RAW);

			const unsigned int layer2 = AddLayer(document, &allocator, "XOR pattern");
			UpdateLayer(document, &allocator, layer2, exportChannel::GRAY, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData16[0][0], compressionType::RLE);

			const unsigned int layer3 = AddLayer(document, &allocator, "AND pattern");
			UpdateLayer(document, &allocator, layer3, exportChannel::GRAY, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_andData16[0][0], compressionType::ZIP);

			const unsigned int layer4 = AddLayer(document, &allocator, "OR pattern with transparency");
			UpdateLayer(document, &allocator, layer4, exportChannel::GRAY, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_orData16[0][0], compressionType::ZIP_WITH_PREDICTION);
			UpdateLayer(document, &allocator, layer4, exportChannel::ALPHA, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_checkerBoardData16[0][0], compressionType::ZIP_WITH_PREDICTION);

			UpdateMergedImage(document, &allocator, &g_multiplyData16[0][0], &g_xorData16[0][0], &g_andData16[0][0]);

			WriteDocument(document, &allocator, &file);
		}

		DestroyExportDocument(document, &allocator);
		file.Close();
	}
	{
		const std::wstring dstPath = GetSampleOutputPath() + L"SampleWrite_32.psd";

		MallocAllocator allocator;
		NativeFile file(&allocator);

		// try opening the file. if it fails, bail out.
		if (!file.OpenWrite(dstPath.c_str()))
		{
			PSD_SAMPLE_LOG("Cannot open file.\n");
			return 1;
		}

		// write an RGB PSD file, 32-bit
		ExportDocument* document = CreateExportDocument(&allocator, IMAGE_WIDTH, IMAGE_HEIGHT, 32u, exportColorMode::RGB);
		{
			const unsigned int layer1 = AddLayer(document, &allocator, "MUL pattern");
			UpdateLayer(document, &allocator, layer1, exportChannel::RED, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData32[0][0], compressionType::RAW);
			UpdateLayer(document, &allocator, layer1, exportChannel::GREEN, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData32[0][0], compressionType::RLE);
			UpdateLayer(document, &allocator, layer1, exportChannel::BLUE, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData32[0][0], compressionType::ZIP);

			const unsigned int layer2 = AddLayer(document, &allocator, "Mixed pattern with transparency");
			UpdateLayer(document, &allocator, layer2, exportChannel::RED, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_multiplyData32[0][0], compressionType::RLE);
			UpdateLayer(document, &allocator, layer2, exportChannel::GREEN, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_xorData32[0][0], compressionType::ZIP);
			UpdateLayer(document, &allocator, layer2, exportChannel::BLUE, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_orData32[0][0], compressionType::ZIP_WITH_PREDICTION);
			UpdateLayer(document, &allocator, layer2, exportChannel::ALPHA, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, &g_checkerBoardData32[0][0], compressionType::RAW);

			UpdateMergedImage(document, &allocator, &g_multiplyData32[0][0], &g_xorData32[0][0], &g_checkerBoardData32[0][0]);

			WriteDocument(document, &allocator, &file);
		}

		DestroyExportDocument(document, &allocator);
		file.Close();
	}

	return 0;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
#if _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPTSTR, int)
#else
int main(int /*argc*/, const char * /*argv[]*/)
#endif
{
	{
		const int result = SampleReadPsd();
		if (result != 0)
		{
			return result;
		}
	}
	{
		const int result = SampleWritePsd();
		if (result != 0)
		{
			return result;
		}
	}

	return 0;
}
