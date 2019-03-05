// BoB robotics includes
#include "common/circstat.h"
#include "common/collision.h"
#include "common/pose.h"
#include "common/read_objects.h"
#include "common/sfml_display.h"
#include "hid/joystick.h"
#include "robots/simulated_tank.h"
#include "robots/tank_pid.h"

// Third-party
#include "third_party/matplotlibcpp.h"

// Eigen
#include <Eigen/Core>
#include <Eigen/StdVector>

// Standard C includes

// Standard C++ includes
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <list>
#include <thread>
#include <vector>

using namespace BoBRobotics;
using namespace units::angle;
using namespace units::literals;
using namespace units::length;
using namespace units::math;
using namespace std::literals;

EIGEN_DEFINE_STL_VECTOR_SPECIALIZATION(Eigen::MatrixXd)

int lines = 0;
namespace plt = matplotlibcpp;

template<class VectorType>
void
polygonToLines(VectorType &lines, const Eigen::MatrixX2d &polygon)
{
    lines.clear();
    if (polygon.rows() <= 1) {
        return;
    }

    for (int i = 0; i < polygon.rows() - 1; i++) {
        lines.emplace_back(2, 2);
        lines.back() << polygon(i, 0), polygon(i, 1),
                polygon(i + 1, 0), polygon(i + 1, 1);
    }
    lines.emplace_back(2, 2);
    const long max = polygon.rows() - 1;
    lines.back() << polygon(max, 0), polygon(max, 1),
            polygon(0, 0), polygon(0, 1);
}

bool
calculateIntersection(Eigen::Vector2d &point, const Eigen::Matrix2d &line1, const Eigen::Matrix2d &line2, bool doplot = false)
{
    const auto min = [](const auto &line, int index) {
        return line.col(index).minCoeff();
    };
    const auto max = [](const auto &line, int index) {
        return line.col(index).maxCoeff();
    };

    if (doplot) {
        plt::figure(1);
        plt::clf();
        std::vector<double> x1, y1, x2, y2;
        x1 = { line1(0, 0), line1(1, 0) };
        y1 = { line1(0, 1), line1(1, 1) };
        x2 = { line2(0, 0), line2(1, 0) };
        y2 = { line2(0, 1), line2(1, 1) };
        plt::plot(x1, y1, "b", x2, y2, "r");
        // plt::xlim(min(line1, 0), max(line1, 0));
        // plt::ylim(min(line1, 1), max(line1, 1));
        plt::pause(0.02);
        plt::ginput(1);
    }

    const auto a1 = line1(1, 1) - line1(0, 1);
    const auto b1 = line1(0, 0) - line1(1, 0);
    const auto c1 = a1 * line1(0, 0) + b1 * line1(0, 1);
    const auto a2 = line2(1, 1) - line2(0, 1);
    const auto b2 = line2(0, 0) - line2(1, 0);
    const auto c2 = a2 * line2(0, 0) + b2 * line2(0, 1);

    const auto det = a1 * b2 - a2 * b1;
    if (det == 0.0) {
        // Lines are parallel
        return false;
    }

    point << (b2 * c1 - b1 * c2) / det,
            (a1 * c2 - a2 * c1) / det;

    // Margin of error for floating-point numbers
    constexpr double tol = 1e-5;

    // Check that the point is on line 1
    const bool a = min(line1, 0) <= point(0) + tol;
    const bool b = max(line1, 0) >= point(0) - tol;
    const bool c = min(line1, 1) <= point(1) + tol;
    const bool d = max(line1, 1) >= point(1) - tol;

    // Check that the point is on line 2
    const bool e = min(line2, 0) <= point(0) + tol;
    const bool f = max(line2, 0) >= point(0) - tol;
    const bool g = min(line2, 1) <= point(1) + tol;
    const bool h = max(line2, 1) >= point(1) - tol;

    return a && b && c && d && e && f && g && h;
}

class ArenaObject
        : public sf::Drawable
{
public:
    template<class VectorArrayType, class MatrixType>
    ArenaObject(const SFMLDisplay<> &display, const VectorArrayType &original, const MatrixType &resized)
        : m_GreenShape(original.size())
        , m_RedShape(original.size())
    {
        // Dark green
        m_GreenShape.setFillColor(sf::Color{ 0x00, 0x88, 0x00 });

        // Add each vertex to the shape
        for (size_t i = 0; i < original.size(); i++) {
            m_GreenShape.setPoint(i, display.vectorToPixel(original[i]));
        }

        m_RedShape.setFillColor(sf::Color::Red);
        for (size_t i = 0; i < original.size(); i++) {
            m_RedShape.setPoint(i, display.vectorToPixel(resized(i, 0), resized(i, 1)));
        }
    }

    virtual void draw(sf::RenderTarget &target, sf::RenderStates states) const override
    {
        target.draw(m_RedShape, states);
        target.draw(m_GreenShape, states);
    }

private:
    sf::ConvexShape m_GreenShape, m_RedShape;
};

double unitToDouble(meter_t x)
{
    return x.value();
}

int
main()
{
    // PID Parameters
    constexpr meter_t stoppingDistance = 3_cm; // if the robot's distance from goal < stopping dist, robot stops
    constexpr float kp = 0.1f;
    constexpr float ki = 0.1f;
    constexpr float kd = 0.1f;
    constexpr float averageSpeed = 0.5f;

    using V = Vector2<meter_t>;
    Robots::SimulatedTank<> tank;
    constexpr Pose2<meter_t, degree_t> initialPose{ 0.3_m, 0_m, 180_deg };
    tank.setPose(initialPose);

    Robots::TankPID pid(tank, kp, ki, kd, stoppingDistance, 3_deg, 45_deg, averageSpeed);
    std::list<Vector2<meter_t>> goals;

    // Display for robot + objects
    SFMLDisplay<> display{ V{ 5_m, 5_m } };
    auto car = display.createCarAgent(tank.getRobotWidth());
    const auto halfWidth = car.getSize().x() / 2;
    const auto halfLength = car.getSize().y() / 2;

    // The x and y dimensions of the robot
    const std::array<V, 4> robotDimensions = {
        V{ -halfWidth, halfLength },
        V{ halfWidth, halfLength },
        V{ halfWidth, -halfLength },
        V{ -halfWidth, -halfLength }
    };
    Eigen::Vector2d robotCentre;
    robotCentre << halfWidth.value(), halfLength.value();

    HID::Joystick joystick;
    tank.addJoystick(joystick);

    // Read objects from file
    const auto objects = readObjects("objects.yaml");
    CollisionDetector collisionDetector{ robotDimensions, objects, 30_cm, 1_cm };

    // Object size + buffer around
    const auto &resizedObjects = collisionDetector.getResizedObjects();

    // Create drawable objects
    std::vector<ArenaObject> objectShapes;
    objectShapes.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); i++) {
        objectShapes.emplace_back(display, objects[i], resizedObjects[i]);
    }

    std::vector<Eigen::Matrix2d> robotLines, objectLines;
    sf::ConvexShape perimeterShape;
    perimeterShape.setFillColor(sf::Color::Transparent);
    perimeterShape.setOutlineColor(sf::Color::Transparent);

    auto avoidLine = display.createLine(sf::Color::Blue);

    bool printedCollisionMessage = false;
    do {
        // Poll joystick for events
        joystick.update();

        const auto pose = tank.getPose();
        if (goals.empty()) {
            collisionDetector.setRobotPose(pose);
            if (collisionDetector.collisionOccurred()) {
                size_t objectId = collisionDetector.getCollidedObjectId();

                const auto robotVerts = collisionDetector.getRobotVertices();
                const auto objectVerts = resizedObjects[objectId];
                Eigen::Vector2d objectCentre;
                objectCentre << objectVerts.col(0).mean(), objectVerts.col(1).mean();

                if (!printedCollisionMessage) {
                    tank.stopMoving();
                    std::cout << "Collided with object " << objectId << std::endl;
                    printedCollisionMessage = true;
                    polygonToLines(robotLines, robotVerts);
                    polygonToLines(objectLines, objectVerts);

                    std::vector<radian_t> intersectionAngles;
                    for (auto &objectLine : objectLines) {
                        for (auto &robotLine : robotLines) {
                            Eigen::Vector2d intersection;
                            if (calculateIntersection(intersection, robotLine, objectLine)) {
                                const double dy = intersection(1) - objectCentre(1);
                                const double dx = intersection(0) - objectCentre(0);
                                intersectionAngles.emplace_back(atan2(dy, dx));
                            }
                        }
                    }

                    const radian_t meanAngle = circularMean(intersectionAngles);
                    auto whichLine = 0;
                    for (; whichLine < objectVerts.rows(); whichLine++) {
                        const double dy = objectVerts(whichLine, 1) - objectCentre(1);
                        const double dx = objectVerts(whichLine, 0) - objectCentre(0);
                        auto vertAngle = radian_t{ atan2(dy, dx) };
                        if (meanAngle < vertAngle) {
                            break;
                        }
                    }
                    if (whichLine == 0) {
                        whichLine = objectVerts.rows() - 1;
                    } else {
                        whichLine--;
                    }

                    // Get perimeter around object
                    Eigen::MatrixX2d objectPerimeter = resizedObjects[objectId];
                    perimeterShape.setPointCount(objectPerimeter.rows());
                    for (int i = 0; i < objectPerimeter.rows(); i++) {
                        perimeterShape.setPoint(i, display.vectorToPixel(objectPerimeter(i, 0), objectPerimeter(i, 1)));
                    }
                    perimeterShape.setOutlineColor(sf::Color::Black);
                    perimeterShape.setOutlineThickness(4);

                    CollisionDetector::resizeObjectBy(objectPerimeter, tank.getRobotWidth());
                    //                std::cout << "Perimeter: " << std::endl
                    //                          << objectPerimeter << std::endl;
                    std::vector<Eigen::MatrixX2d> perimeterLines(objectPerimeter.size());
                    polygonToLines(perimeterLines, objectPerimeter);

                    Eigen::Vector2d robotGoal;
                    robotGoal << 100 * cos(pose.yaw()), 100 * sin(pose.yaw());
                    auto distToGoal = std::numeric_limits<double>::infinity();
                    Eigen::Vector2d leavePoint;
                    size_t whichLeaveLine;
                    Eigen::Matrix2d robotLine;
                    robotLine(0, 0) = unitToDouble(pose.x());
                    robotLine(0, 1) = unitToDouble(pose.y());
                    robotLine(1, 0) = unitToDouble(pose.x()) + 10 * cos(pose.yaw()).value();
                    robotLine(1, 1) = unitToDouble(pose.x()) + 10 * sin(pose.yaw()).value();

                    std::cout << "Robot line: " << robotLine << std::endl;

                    for (size_t i = 0; i < perimeterLines.size(); i++) {
                        Eigen::Vector2d point;
                        if (calculateIntersection(point, perimeterLines[i], robotLine)) {
                            const auto dx = point(0) - robotGoal(0);
                            const auto dy = point(1) - robotGoal(1);
                            const auto dist = hypot(dx, dy);
                            if (dist < distToGoal) {
                                whichLeaveLine = i;
                                leavePoint = point;
                                distToGoal = dist;
                            }
                        }
                    }
                    BOB_ASSERT(!std::isinf(distToGoal));
                    // if (whichLeaveLine == 0) {
                    //     whichLeaveLine = objectVerts.rows() - 1;
                    // } else {
                    //     whichLeaveLine--;
                    // }

                    Eigen::Vector2d robotPoint;
                    robotPoint << unitToDouble(pose.x()), unitToDouble(pose.y());

                    // Eigen::Vector2d startPoint;
                    // BOB_ASSERT(calculateIntersection(startPoint, outwardLine, perimeterLines[whichLine]));

                    std::vector<double> cumulativePerimeterLengths;
                    const auto max = objectPerimeter.rows() - 1;
                    for (int i = 0; i < max; i++) {
                        const auto dx = objectPerimeter(i + 1, 0) - objectPerimeter(i, 0);
                        const auto dy = objectPerimeter(i + 1, 1) - objectPerimeter(i + 1, 1);
                        cumulativePerimeterLengths.push_back(hypot(dx, dy));
                    }
                    const auto dx = objectPerimeter(0, 0) - objectPerimeter(max, 0);
                    const auto dy = objectPerimeter(0, 1) - objectPerimeter(max, 1);
                    cumulativePerimeterLengths.push_back(hypot(dx, dy));

                    //                std::vector<Vector2<meter_t>> avoidPath;
                    //                avoidPath.emplace_back(meter_t{ startPoint.x() }, meter_t{ startPoint.y() });
                    // std::cout << std::endl
                    //           << "Start line: " << std::endl;
                    avoidLine.clear();
                    BOB_ASSERT(whichLeaveLine != whichLine);
                    int lineMax = ((whichLine < whichLeaveLine) ? whichLeaveLine : (whichLeaveLine + objectPerimeter.rows()));
                    // if (lineMax == 0) {
                    //     lineMax = objectPerimeter.rows();
                    // } else {
                    //     lineMax--;
                    // }
                    std::cout << "Lims: " << whichLine << ", " << lineMax << std::endl;
                    avoidLine.append(robotPoint);
                    for (int i = whichLine; i < lineMax; i++) {
                        const Eigen::Vector2d next = objectPerimeter.row((i + 1) % objectPerimeter.rows());
                        //                    avoidPath.emplace_back(meter_t{ next.x() }, meter_t{ next.y() });
                        avoidLine.append(next);
                        goals.emplace_back(meter_t{ next.x() }, meter_t{ next.y() });
                        // std::cout << next << std::endl
                        //           << "." << std::endl;
                    }
                    avoidLine.append(leavePoint);
                    goals.emplace_back(meter_t{ leavePoint.x() }, meter_t{ leavePoint.y() });
                    std::cout << "Driving to " << goals.front() << std::endl;
                    pid.start(goals.front());
                    //                avoidPath.emplace_back(meter_t{ leavePoint.x() }, meter_t{leavePoint.y()});

                    // std::cout << "Starting point:" << std::endl << robotPoint << std::endl;
                    // std::cout << "." << std::endl
                    //           << "End point" << std::endl
                    //           << leavePoint << std::endl;
                }
            } else {
                printedCollisionMessage = false;
            }
        } else if (pid.driveRobot(pose)) {
            // Reached goal
            goals.pop_front();
            if (goals.empty()) {
                avoidLine.clear();
            } else {
                std::cout << "Driving to " << goals.front() << std::endl;
                pid.start(goals.front());
            }
        }

        // Render on display
        car.setPose(pose);
        display.updateAndDrive(tank, objectShapes, avoidLine, car);

        // Small delay so we don't hog CPU
        // plt::pause(0.02);
        std::this_thread::sleep_for(20ms);
    } while (display.isOpen());
    plt::close();
}
