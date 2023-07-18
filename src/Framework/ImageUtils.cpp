#include "LumenPCH.h"
#include "LumenPCH.h"
#include "ImageUtils.h"
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

float* load_exr(const char* img_name, int& width, int& height) {
	// Load the ground truth image
	const char* err = nullptr;
	float* data = nullptr;
	int ret = LoadEXR(&data, &width, &height, img_name, &err);
	if (ret != TINYEXR_SUCCESS) {
		if (err) {
			LUMEN_ERROR("EXR loading error", err);
			FreeEXRErrorMessage(err);
		}
	}
	return data;
}

void save_exr(const float* rgb, std::string_view channels, int width, int height, const char* outfilename) {
	EXRHeader header;
	InitEXRHeader(&header);
	EXRImage image;
	InitEXRImage(&image);
	image.num_channels = channels.size();//3;

	std::vector<std::vector<float>> images(channels.size(), std::vector<float>(width * height));

	// Split RGBRGBRGB... into R, G and B layer
	for (int i = 0; i < width * height; i++) {
		for (int j = 0; j < channels.size(); ++j)
			images[j][i] = rgb[4 * i + channels.size() - j - 1];
	}

	std::vector<float*> image_ptr(channels.size());
	for (int i = 0; i < channels.size(); ++i)
		image_ptr[i] = images[i].data();

	image.images = (unsigned char**)image_ptr.data();
	image.width = width;
	image.height = height;

	header.num_channels = channels.size();
	std::vector<EXRChannelInfo> channel_infos(channels.size());
	header.channels = channel_infos.data();
	// Must be (A)BGR order, since most of EXR viewers expect this channel order
	// However if order was defined differently in the film component other order is possible
	for (int i = 0; i < channels.size(); ++i) {
		channel_infos[i].name[0] = channels[i];
		channel_infos[i].name[1] = '\0';
	}

	std::vector<int> pixel_types(channels.size());
	std::vector<int> requested_pixel_types(channels.size());
	header.pixel_types = pixel_types.data();
	header.requested_pixel_types = requested_pixel_types.data();
	for (int i = 0; i < header.num_channels; i++) {
		header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;		   // pixel type of input image
		header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;  // pixel type of output image to be stored
																   // in .EXR
	}

	const char* err = NULL;	 // or nullptr in C++11 or later.
	int ret = SaveEXRImageToFile(&image, &header, outfilename, &err);
	if (ret != TINYEXR_SUCCESS) {
		LUMEN_ERROR("Save EXR err: {}", err);
		FreeEXRErrorMessage(err);  // free's buffer for an error message
	}
	LUMEN_TRACE("Saved exr file. [ {} ]", outfilename);
}
