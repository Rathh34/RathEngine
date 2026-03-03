#pragma once
#include "RathEngine/Core/Types.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Rath {
    class Camera {
    public:
        Camera(f32 fovDegrees, f32 aspect, f32 nearClip, f32 farClip);

        void SetPosition(const glm::vec3& position) { m_Position = position; UpdateView(); }
        void SetRotation(const glm::vec3& rotation) { m_Rotation = rotation; UpdateView(); }
        void SetAspect(f32 aspect) { m_Aspect = aspect; UpdateProjection(); }

        [[nodiscard]] const glm::vec3& GetPosition() const { return m_Position; }
        [[nodiscard]] const glm::mat4& GetViewProjection() const { return m_ViewProjection; }

    private:
        void UpdateView();
        void UpdateProjection();

        glm::vec3 m_Position{0.0f, 0.0f, 2.0f}; 
        glm::vec3 m_Rotation{0.0f, 0.0f, 0.0f};
        
        f32 m_Fov;
        f32 m_Aspect;
        f32 m_NearClip;
        f32 m_FarClip;

        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
        glm::mat4 m_ViewProjection{1.0f};
    };
}
