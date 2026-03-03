#include "RathEngine/Scene/Camera.h"
#include "RathEngine/Core/Input.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace Rath {
    Camera::Camera(f32 fovDegrees, f32 aspect, f32 nearClip, f32 farClip)
        : m_Fov(fovDegrees), m_Aspect(aspect), m_NearClip(nearClip), m_FarClip(farClip) {
        UpdateView();
        UpdateProjection();
    }

    void Camera::Update(f32 dt) {
        // 1. Mouse Look
        if (Input::IsMouseButtonDown(Mouse::ButtonRight)) {
            glm::vec2 mousePos = Input::GetMousePosition();
            if (m_FirstMouse) {
                m_LastMousePosition = mousePos;
                m_FirstMouse = false;
            }

            glm::vec2 delta = mousePos - m_LastMousePosition;
            m_LastMousePosition = mousePos;

            f32 sensitivity = 0.2f;
            m_Rotation.y -= delta.x * sensitivity; // Yaw
            m_Rotation.x -= delta.y * sensitivity; // Pitch
            m_Rotation.x = std::clamp(m_Rotation.x, -89.0f, 89.0f); // Prevent flipping
        } else {
            m_FirstMouse = true;
        }

        // 2. Calculate Direction Vectors
        glm::vec3 forward;
        forward.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
        forward.y = sin(glm::radians(m_Rotation.x));
        forward.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
        forward = glm::normalize(forward);

        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // 3. Keyboard Movement (Using Logical Keys)
        f32 moveSpeed = 5.0f * dt;
        if (Input::IsKeyDown(Key::LeftShift)) moveSpeed *= 3.0f; // Sprint

        if (Input::IsKeyDown(Key::W)) m_Position += forward * moveSpeed;
        if (Input::IsKeyDown(Key::S)) m_Position -= forward * moveSpeed;
        if (Input::IsKeyDown(Key::A)) m_Position -= right * moveSpeed;
        if (Input::IsKeyDown(Key::D)) m_Position += right * moveSpeed;
        if (Input::IsKeyDown(Key::E)) m_Position += up * moveSpeed;
        if (Input::IsKeyDown(Key::Q)) m_Position -= up * moveSpeed;

        UpdateView();
    }

    void Camera::UpdateView() {
        glm::vec3 forward;
        forward.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
        forward.y = sin(glm::radians(m_Rotation.x));
        forward.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));

        m_View = glm::lookAt(m_Position, m_Position + glm::normalize(forward), glm::vec3(0.0f, 1.0f, 0.0f));
        m_ViewProjection = m_Projection * m_View;
    }

    void Camera::UpdateProjection() {
        m_Projection = glm::perspective(glm::radians(m_Fov), m_Aspect, m_NearClip, m_FarClip);
        m_Projection[1][1] *= -1.0f; // Vulkan Y-coordinate inversion
        m_ViewProjection = m_Projection * m_View;
    }
}
