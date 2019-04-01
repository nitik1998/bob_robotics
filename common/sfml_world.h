#pragma once

// BoB robotics includes
#include "../common/assert.h"
#include "../common/pose.h"
#include "../robots/robot.h"

// Third-party includes
#include "../third_party/units.h"

// SFML
#include <SFML/Graphics.hpp>

// Standard C includes
#include <cstring>

// Standard C++ includes
#include <memory>
#include <vector>
#include <stdexcept>

namespace BoBRobotics {
using namespace units::literals;

class CrossShape
  : public sf::Drawable
{
public:
    CrossShape(const sf::Vector2f &position, float size, float thickness, const sf::Color &colour)
        : m_Horizontal({ size, thickness })
        , m_Vertical({ thickness, size })
    {
        m_Horizontal.setOrigin(size / 2.f, thickness / 2.f);
        m_Vertical.setOrigin(thickness / 2.f, size / 2.f);
        m_Horizontal.setPosition(position);
        m_Vertical.setPosition(position);
        m_Horizontal.setFillColor(colour);
        m_Vertical.setFillColor(colour);
    }

    virtual void draw(sf::RenderTarget &target, sf::RenderStates states) const override
    {
        target.draw(m_Horizontal, states);
        target.draw(m_Vertical, states);
    }

private:
    sf::RectangleShape m_Horizontal, m_Vertical;
}; // CrossShape

template<typename LengthUnit = units::length::meter_t>
class SFMLWorld
{
    static_assert(units::traits::is_length_unit<LengthUnit>::value, "LengthUnit is not a unit length type");

public:
    class CarAgent
      : public sf::Drawable
    {
    public:
        CarAgent(const SFMLWorld<LengthUnit> &display, LengthUnit carWidth)
          : m_Display(display)
        {
            std::string(std::getenv("BOB_ROBOTICS_PATH")) + "/robots/car.bmp";
            const char *brPath = std::getenv("BOB_ROBOTICS_PATH");
            if (!brPath) {
                throw std::runtime_error("BOB_ROBOTICS_PATH environment variable is not set");
            }

            const std::string imageFilePath = std::string(brPath) + "/robots/car.bmp";
            if (!m_Texture.loadFromFile(imageFilePath)) {
                throw std::runtime_error("Could not load " + imageFilePath);
            }

            // Make car sprite: scale and set origin to centre of image
            const auto imageSize = m_Texture.getSize();
            m_Sprite.setTexture(m_Texture);
            const auto widthPx = display.lengthToPixel(carWidth);
            const auto scale = widthPx / static_cast<float>(imageSize.y);
            m_Sprite.setOrigin(imageSize.x / 2.f, imageSize.y / 2.f);
            m_Sprite.scale(scale, scale);

            m_Size = Vector2<LengthUnit>(carWidth, carWidth * (static_cast<float>(imageSize.y) / static_cast<float>(imageSize.x)));
        }

        const auto &getSize() const { return m_Size; }

        template<class PoseType>
        void setPose(const PoseType& pose)
        {
            m_Sprite.setRotation(-static_cast<units::angle::degree_t>(pose.yaw()).value());
            m_Sprite.setPosition(m_Display.vectorToPixel(pose));
        }

        virtual void draw(sf::RenderTarget &target, sf::RenderStates states) const override
        {
            target.draw(m_Sprite, states);
        }

    private:
        const SFMLWorld<LengthUnit> &m_Display;
        Vector2<LengthUnit> m_Size;
        sf::Texture m_Texture;
        sf::Sprite m_Sprite;
    };

    class LineStrip
      : public sf::Drawable
    {
    public:
        LineStrip(const SFMLWorld<LengthUnit> &renderer, const sf::Color &colour)
          : m_Renderer(renderer)
          , m_Colour(colour)
        {}

        virtual void draw(sf::RenderTarget &target, sf::RenderStates states) const override
        {
            target.draw(&m_Vertices[0], m_Vertices.size(), sf::PrimitiveType::LinesStrip, states);
        }

        template<typename PositionType>
        void append(const PositionType &position)
        {
            m_Vertices.emplace_back(m_Renderer.vectorToPixel(position), m_Colour);
        }

        void clear() { m_Vertices.clear(); }

    private:
        std::vector<sf::Vertex> m_Vertices;
        const SFMLWorld<LengthUnit> &m_Renderer;
        const sf::Color m_Colour;
    };

    static constexpr int WindowWidth = 800, WindowHeight = 800;

    SFMLWorld(const Vector2<LengthUnit> &arenaSize = { 3.2_m, 3.2_m })
      : SFMLWorld(Vector2<LengthUnit>{ -arenaSize[0] / 2, -arenaSize[1] / 2 },
                     Vector2<LengthUnit>{ arenaSize[0] / 2, arenaSize[1] / 2 })
    {}

    template<typename MaxBoundsType>
    SFMLWorld(const Vector2<LengthUnit> &minBounds,
                 const MaxBoundsType &maxBounds)
      : m_Window(sf::VideoMode(WindowWidth, WindowHeight),
                 "BoB robotics",
                 sf::Style::Titlebar | sf::Style::Close,
                 getContextSettings())
      , m_MinBounds(minBounds)
    {
        m_Window.setVerticalSyncEnabled(true);

        const LengthUnit width = maxBounds[0] - minBounds[0];
        const LengthUnit height = maxBounds[1] - minBounds[1];
        BOB_ASSERT(width > 0_m && height > 0_m);

        sf::Vector2u windowSize;
        if (width > height) {
            m_UnitPerPixel = width / WindowWidth;
            windowSize.x = WindowWidth;
            windowSize.y = static_cast<unsigned>((height / m_UnitPerPixel).value());
            m_Window.setSize(windowSize);
        } else {
            m_UnitPerPixel = height / WindowHeight;
            windowSize.x = static_cast<unsigned>((width / m_UnitPerPixel).value());
            windowSize.y = WindowHeight;
            m_Window.setSize(windowSize);
        }

        // Put cross at origin
        const auto origin = vectorToPixel(0.0, 0.0);
        m_OriginCross = std::make_unique<CrossShape>(origin, OriginLineLength, OriginLineThickness, sf::Color::Black);
    }

    CarAgent createCarAgent(LengthUnit carWidth = 16.4_cm)
    {
        return CarAgent(*this, carWidth);
    }

    LineStrip createLineStrip(const sf::Color &colour) const
    {
        return LineStrip(*this, colour);
    }

    template<class VectorType>
    CrossShape createCrossShape(const VectorType &position, float size, float thickness, const sf::Color &colour)
    {
        return CrossShape(vectorToPixel(position), size, thickness, colour);
    }

    template<typename... Drawables>
    sf::Event update(Drawables&& ...drawables)
    {
        // Set m_Window to be active OpenGL context
        m_Window.setActive(true);

        // Check all the window's events that were triggered since the last iteration of the loop
        sf::Event event;
        memset(static_cast<void *>(&event), 0, sizeof(event));
        while (m_Window.pollEvent(event)) {
            if (handleEvents(event)) {
                return event;
            }
        }

        // Draw on screen
        doDrawing(std::forward<Drawables>(drawables)...);

        // We don't need to be the current OpenGL context any more
        m_Window.setActive(false);

        return event;
    }

    template<typename... Drawables>
    sf::Event updateAndDrive(Robots::Robot &robot, Drawables&& ...drawables)
    {
        // Set m_Window to be active OpenGL context
        m_Window.setActive(true);

        // Check all the window's events that were triggered since the last iteration of the loop
        sf::Event event;
        memset(static_cast<void *>(&event), 0, sizeof(event));
        while (m_Window.pollEvent(event)) {
            if (handleEvents(event)) {
                return event;
            }

            switch (event.type) {
            case sf::Event::KeyPressed:
                switch (event.key.code) {
                case sf::Keyboard::Up:
                    robot.moveForward(1.f);
                    break;
                case sf::Keyboard::Down:
                    robot.moveForward(-1.f);
                    break;
                case sf::Keyboard::Left:
                    robot.turnOnTheSpot(-0.5f);
                    break;
                case sf::Keyboard::Right:
                    robot.turnOnTheSpot(0.5f);
                    break;
                default:
                    break;
                }
                break;
            case sf::Event::KeyReleased:
                switch (event.key.code) {
                case sf::Keyboard::Up:
                case sf::Keyboard::Down:
                case sf::Keyboard::Left:
                case sf::Keyboard::Right:
                    robot.stopMoving();
                default:
                    break;
                }
            default:
                break;
            }
        }

        // Draw on screen
        doDrawing(std::forward<Drawables>(drawables)...);

        // We don't need to be the current OpenGL context any more
        m_Window.setActive(false);

        return event;
    }

    bool mouseClicked() const
    {
        return !m_MouseClickPosition.isnan();
    }

    auto mouseClickPosition() const
    {
        return m_MouseClickPosition;
    }

    bool isOpen() const
    {
        return m_Window.isOpen();
    }

    void close()
    {
        m_Window.close();
    }

    float lengthToPixel(const LengthUnit value) const
    {
        return static_cast<float>((value / m_UnitPerPixel).value());
    }

    auto pixelToVector(int x, int y)
    {
        return Vector2<LengthUnit>(m_MinBounds[0] + m_UnitPerPixel * x,
                                   m_MinBounds[1] + m_UnitPerPixel * (WindowHeight - y));
    }

    template<class VectorType>
    sf::Vector2f vectorToPixel(const VectorType &point) const
    {
        return { lengthToPixel(point.x() - m_MinBounds[0]),
                 static_cast<float>(WindowHeight) - lengthToPixel(point.y() - m_MinBounds[1]) };
    }

    sf::Vector2f vectorToPixel(double x, double y) const
    {
        return { lengthToPixel(LengthUnit{ x } - m_MinBounds[0]),
                 static_cast<float>(WindowHeight) - lengthToPixel(LengthUnit{ y } - m_MinBounds[1]) };
    }

private:
    sf::RenderWindow m_Window;
    std::unique_ptr<CrossShape> m_OriginCross;
    const Vector2<LengthUnit> m_MinBounds;
    LengthUnit m_UnitPerPixel;
    Vector2<LengthUnit> m_MouseClickPosition = Vector2<LengthUnit>::nan();

    static constexpr float OriginLineThickness = 3.f, OriginLineLength = 20.f;

    bool handleEvents(sf::Event &event)
    {
        m_MouseClickPosition = Vector2<LengthUnit>::nan();

        if (event.type == sf::Event::Closed ||
                (event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Q)) {
            m_Window.close();
            return true;
        }

        // Left mouse button pressed
        if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
            m_MouseClickPosition = pixelToVector(event.mouseButton.x, event.mouseButton.y);
        }
        return false;
    }

    template<typename... Drawables>
    void doDrawing(Drawables&& ...drawables)
    {
        // Set background colour
        m_Window.clear(sf::Color::White);

        // Draw objects
        draw(*m_OriginCross, std::forward<Drawables>(drawables)...);

        // Swap buffers
        m_Window.display();
    }

    template<typename DrawableType, typename... Drawables>
    void draw(const std::vector<DrawableType> &drawables, Drawables&& ...others)
    {
        for (auto &drawable : drawables) {
            m_Window.draw(drawable);
        }
        draw(std::forward<Drawables>(others)...);
    }

    template<typename... Drawables>
    void draw(const sf::Drawable &drawable, Drawables&& ...others)
    {
        m_Window.draw(drawable);
        draw(std::forward<Drawables>(others)...);
    }

    void draw()
    {}

    static sf::ContextSettings getContextSettings()
    {
        sf::ContextSettings settings;
        settings.antialiasingLevel = 8;
        return settings;
    }
}; // SFMLWorld

#ifndef NO_HEADER_DEFINITIONS
template<class LengthUnit>
constexpr float SFMLWorld<LengthUnit>::OriginLineThickness;

template<class LengthUnit>
constexpr float SFMLWorld<LengthUnit>::OriginLineLength;
#endif
} // BoBRobotics
