void CParticleSystem::seed(const CTexture& sourceTexture, int particleCount) {
    // Obtener datos de la textura
    int width  = sourceTexture.m_vSize.x;
    int height = sourceTexture.m_vSize.y;

    // Crear buffer temporal para leer los píxeles
    std::vector<uint8_t> pixels(width * height * 4); // RGBA
    glBindTexture(GL_TEXTURE_2D, sourceTexture.m_iTexID);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    m_particles.clear();
    m_particles.reserve(particleCount);

    // Distribuciones aleatorias
    std::random_device                    rd;
    std::mt19937                          gen(rd());
    std::uniform_real_distribution<float> velDist(-50.0f, 50.0f); // Velocidad
    std::uniform_real_distribution<float> sizeDist(2.0f, 5.0f);   // Tamaño
    std::uniform_real_distribution<float> lifeDist(0.8f, 1.0f);   // Vida inicial

    // Contador de intentos para evitar bucles infinitos
    int attempts    = 0;
    int maxAttempts = particleCount * 5;

    while (m_particles.size() < particleCount && attempts < maxAttempts) {
        attempts++;

        // Elegir un píxel aleatorio
        int x   = gen() % width;
        int y   = gen() % height;
        int idx = (y * width + x) * 4;

        // Verificar si es transparente
        uint8_t alpha = pixels[idx + 3];
        if (alpha < 10)
            continue; // Ignorar píxeles muy transparentes

        // Crear partícula
        Particle p;
        p.position = Vector2D(x, y);
        p.velocity = Vector2D(velDist(gen), velDist(gen));
        p.size     = sizeDist(gen);
        p.life     = lifeDist(gen);

        // Guardar color de la partícula (opcional, si quieres mantener el color original)
        p.color.r = pixels[idx] / 255.0f;
        p.color.g = pixels[idx + 1] / 255.0f;
        p.color.b = pixels[idx + 2] / 255.0f;
        p.color.a = alpha / 255.0f;

        m_particles.push_back(p);
    }

    // Si necesitas VBO para renderizar
    if (m_particles.empty())
        return;

    // Crear o actualizar VBO
    if (m_vbo == 0) {
        glGenBuffers(1, &m_vbo);
    }

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

    Debug::log(LOG, "Particle system seeded with {} particles", m_particles.size());
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
        float    distance = toCenter.length();

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

    // Actualizar VBO con nuevos datos
    if (m_particles.empty())
        return;

    std::vector<float> vboData;
    vboData.reserve(m_particles.size() * 6);

    for (const auto& p : m_particles) {
        vboData.push_back(p.position.x);
        vboData.push_back(p.position.y);
        vboData.push_back(p.velocity.x);
        vboData.push_back(p.velocity.y);
        vboData.push_back(p.size);
        vboData.push_back(p.life);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float), vboData.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Método adicional para renderizar las partículas
void CParticleSystem::render(float globalOpacity) {
    if (m_particles.empty())
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_shader.program);

    // Configurar matrices
    Mat3x3 projectionMatrix = Mat3x3::outputProjection(m_screenSize, HYPRUTILS_TRANSFORM_NORMAL);
    glUniformMatrix3fv(m_shader.proj, 1, GL_TRUE, projectionMatrix.getMatrix().data());

    // Color base (multiplicado por el color de cada partícula)
    float baseColor[4] = {1.0f, 1.0f, 1.0f, globalOpacity};
    glUniform4fv(m_shader.color, 1, baseColor);

    // Configurar atributos
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Posición
    glEnableVertexAttribArray(m_shader.posAttrib);
    glVertexAttribPointer(m_shader.posAttrib, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    // Velocidad (si se usa en el shader para efectos)
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
