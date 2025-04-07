#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

#include "ParticleSystem.hpp"
#include "../helpers/Log.hpp"
#include "../core/hyprlock.hpp"
#include <random>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <GLES3/gl32.h>
#include "Shaders.hpp"
#include "Renderer.hpp"

CParticleSystem::CParticleSystem(const Vector2D& screenSize) : m_screenSize(screenSize) {
    // Inicializar generador de números aleatorios
    std::random_device rd;
    m_rng = std::mt19937(rd());

    // Cargar shader para partículas
    m_shader.program    = CRenderer().createProgram(PARTICLEVERTSRC, PARTICLEFRAGSRC);
    m_shader.proj       = glGetUniformLocation(m_shader.program, "proj");
    m_shader.color      = glGetUniformLocation(m_shader.program, "color");
    m_shader.posAttrib  = glGetAttribLocation(m_shader.program, "pos");
    m_shader.velAttrib  = glGetAttribLocation(m_shader.program, "velocity");
    m_shader.sizeAttrib = glGetAttribLocation(m_shader.program, "size");
    m_shader.lifeAttrib = glGetAttribLocation(m_shader.program, "life");

    // Crear VBO
    glGenBuffers(1, &m_vbo);
}

CParticleSystem::~CParticleSystem() {
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    if (m_shader.program != 0) {
        glDeleteProgram(m_shader.program);
        m_shader.program = 0;
    }
}

void CParticleSystem::seed(const CTexture& sourceTexture, int particleCount) {
    // Obtener datos de la textura
    int width  = sourceTexture.m_vSize.x;
    int height = sourceTexture.m_vSize.y;

    // Crear buffer temporal para leer los píxeles
    std::vector<uint8_t> pixels(width * height * 4); // RGBA
    glBindTexture(GL_TEXTURE_2D, sourceTexture.m_iTexID);
    GLuint tempFBO;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);

    // Enlaza la textura al framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sourceTexture.m_iTexID, 0);

    // Lee los píxeles
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Limpia
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &tempFBO);

    m_particles.clear();
    m_particles.reserve(particleCount);

    // Distribuciones aleatorias
    std::uniform_real_distribution<float> velDist(-50.0f, 50.0f); // Velocidad inicial
    std::uniform_real_distribution<float> sizeDist(2.0f, 5.0f);   // Tamaño de partícula
    std::uniform_real_distribution<float> lifeDist(0.8f, 1.0f);   // Vida inicial

    // Contador de intentos para evitar bucles infinitos
    int attempts    = 0;
    int maxAttempts = particleCount * 5;

    while (m_particles.size() < (size_t)particleCount && attempts < maxAttempts) {
        attempts++;

        // Elegir un píxel aleatorio
        int x   = m_rng() % width;
        int y   = m_rng() % height;
        int idx = (y * width + x) * 4;

        // Verificar si es transparente
        uint8_t alpha = pixels[idx + 3];
        if (alpha < 10)
            continue; // Ignorar píxeles muy transparentes

        // Crear partícula
        Particle p;
        p.position     = Vector2D(x, y);
        p.velocity     = Vector2D(velDist(m_rng), velDist(m_rng));
        p.size         = sizeDist(m_rng);
        p.originalSize = p.size;
        p.life         = lifeDist(m_rng);

        // Guardar color de la partícula (preservando el color original)
        p.color.r = pixels[idx] / 255.0f;
        p.color.g = pixels[idx + 1] / 255.0f;
        p.color.b = pixels[idx + 2] / 255.0f;
        p.color.a = alpha / 255.0f;

        m_particles.push_back(p);
    }

    Debug::log(LOG, "Sistema de partículas inicializado con {} partículas", m_particles.size());

    // Preparar datos para el VBO
    updateVBO();
}

void CParticleSystem::update(float deltaTime) {
    // Limitar deltaTime para evitar saltos grandes
    deltaTime = std::min(deltaTime, 0.1f);

    // Vectores temporales para fuerzas
    Vector2D gravity(0.0f, 20.0f); // Gravedad suave hacia abajo

    // Centro de atracción o repulsión (efecto "Devs")
    Vector2D center(m_screenSize.x / 2.0f, m_screenSize.y / 2.0f);
    float    attractForce = -30.0f; // Negativo para repulsión, positivo para atracción

    for (auto& p : m_particles) {
        // Calcular dirección al centro
        Vector2D toCenter = center - p.position;
        float    distance = center.distance(p.position);

        // Evitar división por cero
        if (distance > 0.1f) {
            Vector2D forceDir = toCenter / distance;

            // Fuerza que varía con la distancia (ley de cuadrado inverso)
            float    forceMagnitude  = attractForce / (distance * 0.1f);
            Vector2D attractionForce = forceDir * forceMagnitude;

            // Aplicar fuerzas
            p.velocity += (gravity + attractionForce) * deltaTime;
        }

        // Añadir un poco de ruido (movimiento browniano)
        std::uniform_real_distribution<float> noiseDist(-5.0f, 5.0f);
        p.velocity.x += noiseDist(m_rng) * deltaTime;
        p.velocity.y += noiseDist(m_rng) * deltaTime;

        // Actualizar posición
        p.position += p.velocity * deltaTime;

        // Reducir vida
        p.life -= deltaTime * 0.2f; // Ajustar para controlar duración

        // Reducir tamaño a medida que envejece
        p.size = p.originalSize * (0.5f + p.life * 0.5f);

        // Aplicar amortiguamiento para que eventualmente se detengan
        p.velocity *= (1.0f - deltaTime * 0.1f);
    }

    // Eliminar partículas muertas
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.life <= 0.0f; }), m_particles.end());

    // Actualizar VBO
    updateVBO();
}

void CParticleSystem::render(float globalOpacity) {
    if (m_particles.empty())
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_shader.program);

    // Configurar matrices
    Mat3x3 projectionMatrix = Mat3x3::outputProjection(m_screenSize, HYPRUTILS_TRANSFORM_NORMAL);
    glUniformMatrix3fv(m_shader.proj, 1, GL_TRUE, projectionMatrix.getMatrix().data());

    // Color base (ajustado por la opacidad global)
    float baseColor[4] = {1.0f, 1.0f, 1.0f, globalOpacity};
    glUniform4fv(m_shader.color, 1, baseColor);

    // Configurar atributos
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Posición
    glEnableVertexAttribArray(m_shader.posAttrib);
    glVertexAttribPointer(m_shader.posAttrib, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    // Velocidad (para efectos en el shader)
    glEnableVertexAttribArray(m_shader.velAttrib);
    glVertexAttribPointer(m_shader.velAttrib, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    // Tamaño de partícula
    glEnableVertexAttribArray(m_shader.sizeAttrib);
    glVertexAttribPointer(m_shader.sizeAttrib, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(4 * sizeof(float)));

    // Vida
    glEnableVertexAttribArray(m_shader.lifeAttrib);
    glVertexAttribPointer(m_shader.lifeAttrib, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));

    // Dibujar partículas
    glEnable(GL_POINT_SPRITE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDrawArrays(GL_POINTS, 0, m_particles.size());

    // Desactivar atributos
    glDisableVertexAttribArray(m_shader.posAttrib);
    glDisableVertexAttribArray(m_shader.velAttrib);
    glDisableVertexAttribArray(m_shader.sizeAttrib);
    glDisableVertexAttribArray(m_shader.lifeAttrib);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void CParticleSystem::updateVBO() {
    if (m_particles.empty())
        return;

    // Preparar datos para el VBO
    std::vector<float> vboData;
    vboData.reserve(m_particles.size() * 6); // posX, posY, velX, velY, size, life

    for (const auto& p : m_particles) {
        vboData.push_back(p.position.x);
        vboData.push_back(p.position.y);
        vboData.push_back(p.velocity.x);
        vboData.push_back(p.velocity.y);
        vboData.push_back(p.size);
        vboData.push_back(p.life);
    }

    // Subir datos al VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float), vboData.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
