#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
	Camera(float fov, float aspectRatio, glm::vec3 position, glm::vec3 up, glm::vec3 front);

	void setPosition(glm::vec3 position);
	void setOrientation(glm::vec3 front, glm::vec3 up);

	glm::mat4 getViewMatrix() const;
	glm::mat4 getProjectionMatrix() const;

	void update(float deltaTime);

private:
	glm::vec3 position;
	glm::vec3 front;
	glm::vec3 up;

	float fov;
	float aspectRatio;
	float nearPlane;
	float farPlane;

	glm::mat4 view;
	glm::mat4 projection;

	void updateViewMatrix();
	void updateProjectionMatrix();
};

#endif