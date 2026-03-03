#include "RathEngine/Scene/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rath {
    Camera::Camera(f32 fovDegrees, f32 aspect, f32 nearClip, f32 farClip)
        : m_Fov(fovDegrees), m_Aspect(aspect), m_NearClip(nearClip), m_FarClip(farClip) {
        UpdateView();
        UpdateProjection();
    }

    void Camera::UpdateView() {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) * glm::mat4(glm::quat(glm::radians(m_Rotation)));
        m_View = glm::inverse(transform);
        m_ViewProjection = m_Projection * m_View;
    }

    void Camera::UpdateProjection() {
        m_Projection = glm::perspective(glm::radians(m_Fov), m_Aspect, m_NearClip, m_FarClip);
        m_Projection[1][1] *= -1.0f; // Vulkan Y-coordinate inversion
        m_ViewProjection = m_Projection * m_View;
    }
}
