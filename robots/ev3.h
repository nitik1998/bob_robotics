#pragma once

// BoB robotics includes
#include "../common/circstat.h"
#include "../common/stopwatch.h"
#include "tank.h"

// EV3 library
#include "ev3dev.h"

// Third-party includes
#include "../third_party/units.h"

// Standard C++ includes
#include <utility>

namespace BoBRobotics {
namespace Robots {
//! Control for a Lego Mindstorms tank robot
class EV3
  : public Tank
{
public:
    EV3(const ev3dev::address_type leftMotorPort = ev3dev::OUTPUT_D,
        const ev3dev::address_type rightMotorPort = ev3dev::OUTPUT_A)
      : m_MotorLeft(leftMotorPort)
      , m_MotorRight(rightMotorPort)
      , m_MaxSpeedTachos(m_MotorLeft.max_speed())
      , m_TachoCountPerRotation(m_MotorLeft.count_per_rot())
    {
        m_MotorStatusTimer.start();
    }

    virtual ~EV3() override
    {
        stopMoving();
        stopReadingFromNetwork();
    }

    virtual void stopMoving() override
    {
        m_MotorLeft.stop();
        m_MotorRight.stop();
        setWheelSpeeds(0.f, 0.f);
    }

    virtual void tank(float left, float right) override
    {
        setWheelSpeeds(left, right);

        const float maxTachos = getMaximumSpeedProportion() * m_MaxSpeedTachos;
        m_MotorLeft.set_speed_sp(-maxTachos * left);
        m_MotorRight.set_speed_sp(-maxTachos * right);
        m_MotorLeft.run_forever();
        m_MotorRight.run_forever();

        // Every 3s check if the motors are overloaded
        using namespace std::literals;
        if (m_MotorStatusTimer.elapsed() > 3s) {
            // Restart timer
            m_MotorStatusTimer.start();

            checkMotor(m_MotorLeft, "left");
            checkMotor(m_MotorRight, "right");
        }
    }

    virtual millimeter_t getRobotWidth() const override
    {
        return 12_cm;
    }

    virtual meters_per_second_t getAbsoluteMaximumSpeed() const override
    {
        return tachoToSpeed(m_MaxSpeedTachos);
    }

    auto getWheelVelocities() const
    {
        return std::make_pair(tachoToSpeed(m_MotorLeft.speed()),
                              tachoToSpeed(m_MotorRight.speed()));
    }

private:
    ev3dev::large_motor m_MotorLeft, m_MotorRight;
    Stopwatch m_MotorStatusTimer;
    const int m_MaxSpeedTachos, m_TachoCountPerRotation;

    meters_per_second_t tachoToSpeed(int tachos) const
    {
        constexpr units::length::meter_t wheelRadius = 5.5_cm / 2;
        const double angularVelocity{ 2 * pi() * static_cast<double>(tachos) / static_cast<double>(m_TachoCountPerRotation) };
        return meters_per_second_t{ angularVelocity * wheelRadius.value() };
    }

    void checkMotor(const ev3dev::large_motor &motor, const std::string &label)
    {
        const auto states = motor.state();
        for (auto &state : states) {
            if (state == "overloaded" || state == "stalled") {
                std::cerr << "Warning: " << label << " motor is " << state << std::endl;
            }
        }
    }
};
}
}
