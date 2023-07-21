#include "LumenPCH.h"
#include "MultiViewMarcher.h"
#include <tinyexr.h>
#include <ranges>

inline auto s_range(const auto& v){return std::ranges::iota_view(size_t(0), v.size());}

struct ChannelData{
	std::string channel;
	std::vector<float> data;
};
struct ExrData{
	std::vector<ChannelData> channels;
	int h, w;
	glm::mat4 view_matrix;
	glm::mat4 projection_matrix;
};
ExrData load_depth_exr(std::string_view filename, std::span<std::string> channels) {
	std::cout << "Load exr " << filename << std::endl;
	ExrData data;
	// loading header info
	EXRVersion version{};
	EXRHeader header{};
	EXRImage image{};
	const char* err{};
	int ret = ParseEXRVersionFromFile(&version, filename.data());
	if (ret != TINYEXR_SUCCESS) {
		std::cout << "Parse Version error" << std::endl;
		return {};
	}
	InitEXRHeader(&header);
	ret = ParseEXRHeaderFromFile(&header, &version, filename.data(), &err);
	if (ret != TINYEXR_SUCCESS) {
		std::cout << "Parse header error:" << err << std::endl;
		FreeEXRHeader(&header);
		return {};
	}
	std::fill_n(header.requested_pixel_types, header.num_channels, TINYEXR_PIXELTYPE_FLOAT);
	InitEXRImage(&image);
	ret = LoadEXRImageFromFile(&image, &header, filename.data(), &err);
	if (ret != TINYEXR_SUCCESS) {
		FreeEXRHeader(&header);
		FreeEXRImage(&image);
		std::cout << "Load image error: " << err << std::endl;
		return {};
	}
	data.w = image.width;
	data.h = image.height;
	for ( auto channel: channels){
		// Loading channel data
		std::cout << "Parsing channel " << channel << std::endl;
		int channel_index = 0;
		for (; channel_index < header.num_channels && header.channels[channel_index].name != channel; ++channel_index);
		if (channel_index == header.num_channels) {
			std::cout << "Could not find channel" << std::endl;
			continue;
		}
		float *channel_data = (float*)(image.images[channel_index]);
		
		// loading matrix data
		std::string matrix_filename = std::string(filename.substr(0, filename.find_last_of("."))) + ".json";
		std::ifstream matrix_file(matrix_filename, std::ios_base::binary);
		if (!matrix_file) {
			std::cout << "Load camera matrices error" << std::endl;
			continue;
		}
		std::string matrix_string{ std::istreambuf_iterator<char>(matrix_file), std::istreambuf_iterator<char>()};
		nlohmann::json matrices = nlohmann::json::parse(matrix_string);
		std::vector<float> view_data = matrices["view_matrix"].get<std::vector<float>>();
		std::vector<float> projection_data = matrices["projection_matrix"].get<std::vector<float>>();
		std::copy(view_data.begin(), view_data.end(), &data.view_matrix[0][0]);
		std::copy(projection_data.begin(), projection_data.end(), &data.projection_matrix[0][0]);
		data.channels.emplace_back(ChannelData{
										.channel = std::string(channel),
										.data = {channel_data, channel_data + data.w * data.h}});
	}
	FreeEXRHeader(&header);
	FreeEXRImage(&image);
	return data;
}
struct DepthColor{
	std::vector<std::vector<float>> depths;
	std::vector<std::vector<uint>> colors;
};
DepthColor convert_exr_data(const std::vector<ExrData>& images){
	DepthColor depth_color{std::vector<std::vector<float>>(images.size()), std::vector<std::vector<uint>>(images.size())};
	for (auto i: s_range(images)) {
		// copying depth over
		int depth_index = 0;
		for (; depth_index < images[i].channels.size() && images[i].channels[depth_index].channel != "D"; ++depth_index);
		if (depth_index >= images[i].channels.size()) {
			std::cout << "Missing depth information for image" << std::endl;
			continue;
		}
		depth_color.depths[i] = images[i].channels[depth_index].data;

		// assemble color
		int r_index = 0, g_index = 0, b_index = 0;
		for (; r_index < images[i].channels.size() && images[i].channels[r_index].channel != "R"; ++r_index);
		for (; g_index < images[i].channels.size() && images[i].channels[g_index].channel != "G"; ++g_index);
		for (; b_index < images[i].channels.size() && images[i].channels[b_index].channel != "R"; ++b_index);
		if (r_index >= images[i].channels.size() ||
			g_index >= images[i].channels.size() ||
			b_index >= images[i].channels.size()) {
			std::cout << "Missing color information for image" << std::endl;
			continue;
		}
			
		const auto& r = images[i].channels[r_index];
		const auto& g = images[i].channels[g_index];
		const auto& b = images[i].channels[b_index];
		depth_color.colors[i].resize(r.data.size());
		auto convert_col_comp = [](float v, int p) { return uint(v * 255.f) << (p * 8);};
		for (auto p: s_range(r.data)) {
			uint c = convert_col_comp(r.data[p], 3) |
						convert_col_comp(g.data[p], 2) |
						convert_col_comp(b.data[p], 1) |
						convert_col_comp(1.f, 0);
			depth_color.colors[i][p] = c;
		}
	}
	return depth_color;
}
std::vector<MultiViewInfo> extract_multi_view_infos(const std::vector<ExrData>& files) {
	std::vector<MultiViewInfo> infos(files.size());
	for(auto i: s_range(files)) {
		infos[i].cam_view_proj = files[i].projection_matrix * files[i].view_matrix;
		infos[i].cam_origin = glm::inverse(files[i].view_matrix)[3];
		infos[i].size_x = files[i].w;
		infos[i].size_y = files[i].h;
		// addresses are filled in after the data buffers have been created
	}
	return infos;
}

void MultiViewMarcher::init() {
	// loading the scene files and converting them to the correct format
	DepthColor scene_data;
	std::vector<MultiViewInfo> multi_view_infos;
	{
		std::vector<std::string> exr_files;
		for (auto entry: std::filesystem::directory_iterator("depths/")) {
			if (entry.path().extension().string() == ".exr")
				exr_files.emplace_back(entry.path().string());
		}
		std::vector<ExrData> frames(exr_files.size());
		std::vector<std::string> channels{"R", "G", "B", "D"};
		for ( auto i: s_range(exr_files))
			frames[i] = load_depth_exr(exr_files[i], channels);
		scene_data = convert_exr_data(frames);
		multi_view_infos = extract_multi_view_infos(frames);
	}
	// end loading the scenes

	Integrator::init();

	// creating all gpu buffer for rendering
	// first all data buffers (also writes address information into the multi view infos array)
	auto sampler_info = vk::sampler_create_info();
	sampler_info.minFilter = sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.maxLod = FLT_MAX;
	sampler_info.addressModeU = sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vk::check(vkCreateSampler(instance->vkb.ctx.device, &sampler_info, nullptr, &texture_sampler), "Could not create image sammpler");
	textures.resize(2 * multi_view_infos.size());
	for (auto i: s_range(multi_view_infos)) {
		auto img_extent = VkExtent2D{multi_view_infos[i].size_x, multi_view_infos[i].size_y};
		auto depth_info = make_img2d_ci(img_extent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT, false);
		textures[i * 2].load_from_data(&instance->vkb.ctx, scene_data.depths[i].data(), scene_data.depths[i].size() * sizeof(scene_data.depths[i][0]),
										depth_info, texture_sampler, VK_IMAGE_USAGE_SAMPLED_BIT);
		auto color_info = make_img2d_ci(img_extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
		textures[i * 2 + 1].load_from_data(&instance->vkb.ctx, scene_data.colors[i].data(), scene_data.colors[i].size() * sizeof(scene_data.colors[i][0]),
										color_info, texture_sampler, VK_IMAGE_USAGE_SAMPLED_BIT);
		multi_view_infos[i].depth_texture_index = i * 2;
		multi_view_infos[i].color_texture_index = i * 2 + 1;
	}
	// now as the addresses have been filled into multi view info create multi view info buffer
	multi_view_infos_buffer.create("multi_view_infos", &instance->vkb.ctx, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
								VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SHARING_MODE_EXCLUSIVE,
								multi_view_infos.size() * sizeof(multi_view_infos[0]), multi_view_infos.data(), true);

	pc.size_x = instance->width;
	pc.size_y = instance->height;
	pc.amt_multi_views = static_cast<uint>(multi_view_infos.size());
	pc.multi_view_infos_addr = multi_view_infos_buffer.get_device_address();
}

void MultiViewMarcher::render() {
	CommandBuffer cmd(&instance->vkb.ctx, /*start*/ true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	pc.cam_view_inv = glm::inverse(camera->view);
	pc.cam_proj_inv = glm::inverse(camera->projection);

	std::vector<ShaderMacro> macros;
	uint dispatch_x = uint(std::ceil(pc.size_x / float(workgroup_size_x)));
	uint dispatch_y = uint(std::ceil(pc.size_y / float(workgroup_size_y)));
	instance->vkb.rg
		->add_compute("Raymarch Scene",
						{.shader = Shader("src/shaders/integrators/mvm/multi_view_marcher.comp"),
						 .macros = macros,
						 .dims = {dispatch_x, dispatch_y, 1}})
		.push_constants(&pc)
		.bind(output_tex)
		.bind_texture_array(textures);

	instance->vkb.rg->run_and_submit(cmd);
}

bool MultiViewMarcher::update() {
	pc.frame_number++;
	bool updated = Integrator::update();
	if (updated) {
		pc.frame_number = 0;
	}
	return updated;
}

void MultiViewMarcher::destroy() { 
	Integrator::destroy(); 
	multi_view_infos_buffer.destroy();
	vkDestroySampler(instance->vkb.ctx.device, texture_sampler, nullptr);
	for (auto& t: textures)
		t.destroy();
}
