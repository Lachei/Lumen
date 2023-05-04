#pragma once
#include "Integrator.h"
#include "SceneConfig.h"

struct PSSMLTConfig : SceneConfig {
	float mutations_per_pixel = 100.0f;
	int num_mlt_threads = 360000;
	int num_bootstrap_samples = 360000;
	PSSMLTConfig() : SceneConfig("PSSMLT") {}
};

class PSSMLT : public Integrator {
   public:
	PSSMLT(LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(lumen_scene->config), integrator_config(lumen_scene->integrator_config) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	void prefix_scan(int level, int num_elems, int& counter, RenderGraph* rg);
	PCMLT pc_ray{};
	PushConstantCompute pc_compute{};
	// PSSMLT buffers
	Buffer bootstrap_buffer;
	Buffer cdf_buffer;
	Buffer cdf_sum_buffer;
	Buffer seeds_buffer;
	Buffer mlt_samplers_buffer;
	Buffer light_primary_samples_buffer;
	Buffer cam_primary_samples_buffer;
	Buffer connection_primary_samples_buffer;
	Buffer mlt_col_buffer;
	Buffer chain_stats_buffer;
	Buffer splat_buffer;
	Buffer past_splat_buffer;
	Buffer light_path_buffer;
	Buffer camera_path_buffer;

	Buffer bootstrap_cpu;
	Buffer cdf_cpu;

	std::vector<Buffer> block_sums;

	int mutation_count;
	int light_path_rand_count;
	int cam_path_rand_count;
	int connect_path_rand_count;

	SceneConfig& config;
	nlohmann::json integrator_config;;
};