#pragma once

#include <vector>
#include <random>
#include "../helpers/Math.hpp"
#include "../helpers/Color.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/Shader.hpp"

struct Particle {
    Vector2D   position;
    Vector2D   velocity;
    float      size;
    float      originalSize;
    float      life; // 0.0 = muerta, 1.0 = nueva
    CHyprColor color;
};

class CParticleSystem {
  public:
    CParticleSystem(const Vector2D& screenSize);
    ~CParticleSystem();

    void seed(const CTexture& sourceTexture, int particleCount);
    void update(float deltaTime);
    void render(float opacity);
    bool isActive() {
        return !m_particles.empty();
    }

  private:
    std::vector<Particle> m_particles;
    GLuint                m_vbo = 0;
    CShader               m_shader;
    Vector2D              m_screenSize;

    // Para generación de números aleatorios
    std::mt19937 m_rng;

    // Método auxiliar para actualizar el VBO
    void updateVBO();
};
