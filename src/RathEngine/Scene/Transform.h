#pragma once
#include "RathEngine/Core/Types.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rath {
    struct Transform {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees
        glm::vec3 scale{1.0f, 1.0f, 1.0f};

        [[nodiscard]] glm::mat4 GetMatrix() const {
            glm::mat4 mat(1.0f);
            mat = glm::translate(mat, position);
            mat = mat * glm::mat4(glm::quat(glm::radians(rotation)));
            mat = glm::scale(mat, scale);
            return mat;
        }
    };
}
