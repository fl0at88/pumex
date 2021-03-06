//
// Copyright(c) 2017-2018 Pawe� Ksi�opolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#pragma once
#include <memory>
#include <map>
#include <glm/glm.hpp>
#include <pumex/Export.h>

namespace pumex
{

// In Vulkan Y coordinate is directed downwards the screen as oposed to OpenGL (upwards the screen)
// To facilitate this we premultiply projection matrix by below defined correction matrix
const glm::mat4 vulkanPerspectiveCorrectionMatrix(1, 0, 0, 0,   0, -1, 0, 0,   0, 0, 1, 0,   0, 0, 0.0, 1);

// class that represents camera object and may be transfered to GPU ( using Buffer<Camera> or Buffer<vector<Camera>> and UniformBuffer ) for use in shaders
class PUMEX_EXPORT Camera
{
public:
  explicit Camera() = default;
  explicit Camera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec4& pos, float timeSinceStart);

  void             setViewMatrix( const glm::mat4& matrix );
  inline glm::mat4 getViewMatrix() const;
  inline glm::mat4 getViewMatrixInverse() const;

  void             setProjectionMatrix(const glm::mat4& matrix, bool usePerspectiveCorrection = true);
  glm::mat4        getProjectionMatrix(bool usePerspectiveCorrection = true) const;

  void             setObserverPosition(const glm::vec3& pos);
  void             setObserverPosition( const glm::vec4& pos );
  inline glm::vec4 getObserverPosition() const;

  void             setTimeSinceStart(float timeSinceStart);
  inline float     getTimeSinceStart() const;

protected:
  glm::mat4 viewMatrix;
  glm::mat4 viewMatrixInverse;
  glm::mat4 projectionMatrix;
  glm::vec4 observerPosition; // used for LOD computations. Usually the same as in viewMatrix
  glm::vec4 params;           // params.x = timeSinceStart
};

template <typename T>
glm::tmat4x4<T, glm::defaultp> orthoGL( T left, T right, T bottom, T top,	T zNear, T zFar	)
{
  glm::tmat4x4<T, glm::defaultp> Result(1);
  Result[0][0] = static_cast<T>(2) / (right - left);
  Result[1][1] = static_cast<T>(2) / (top - bottom);
  Result[3][0] = - (right + left) / (right - left);
  Result[3][1] = - (top + bottom) / (top - bottom);

  Result[2][2] = - static_cast<T>(2) / (zFar - zNear);
  Result[3][2] = - (zFar + zNear) / (zFar - zNear);

  return Result;
}

glm::mat4 Camera::getViewMatrix() const        { return viewMatrix; }
glm::mat4 Camera::getViewMatrixInverse() const { return viewMatrixInverse; }
glm::vec4 Camera::getObserverPosition() const  { return observerPosition; }
float     Camera::getTimeSinceStart() const    { return params.x; }

}
