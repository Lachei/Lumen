#pragma once
#include "shaders/commons.h"

#define CAST_CONFIG(ptr, cast) ((cast*)ptr)

struct CameraSettings {
	float fov;
	glm::vec3 pos = glm::vec3(0);
	glm::vec3 dir = glm ::vec3(0);
	glm::mat4 cam_matrix = glm::mat4();
};

struct SceneConfig {
	int path_length = 6;
	glm::vec3 sky_col = glm::vec3(0);
	std::string integrator_name = "Path";
	CameraSettings cam_settings;

	SceneConfig() = default;
	SceneConfig(const std::string& integrator_name)
		: integrator_name(integrator_name) {}
};

