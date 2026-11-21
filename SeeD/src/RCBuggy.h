#pragma once

// 1/10 electric off-road buggy, simulated the way the real car works rather than as a scaled-down
// road car (which is all Jolt's VehicleConstraint is -- an engine with a clutch and a gearbox, a
// straight-line strut, and a tire model tuned for 1500 kg).
//
// WHAT JOLT DOES. The chassis is an ordinary dynamic Jolt body (RigidBody + CollisionShape on the
// same entity): Jolt integrates it, collides it with the track, rolls it over, lands it. Each tire
// finds the ground with a Jolt narrow-phase CYLINDER cast the size of that tire, every physics step.
//
// WHAT THIS FILE DOES, from a Jolt step listener (so at Jolt's sub-step rate, 10x per frame):
//   * suspension: the real RC10-style linkage (RCBuggyKinematics.h) -- arm, outer pin, camber link,
//     kingpin, bellcrank -- tabulated, so camber gain, bump steer, kick-up, anti-squat, roll centre
//     and Ackermann all fall out of the hardpoints instead of being tuning knobs. Each corner's arm
//     angle is a real degree of freedom with the wheel's unsprung mass, so wheels hop and droop;
//   * shocks from the setup sheet: spring rate, oil weight, piston holes and thickness (laminar +
//     orifice flow), stroke and droop limiter, preload solved so the car sits at its ride height;
//   * anti-roll bars from wire diameter;
//   * tires: combined-slip Pacejka-style curve, load sensitivity, camber thrust, radial stiffness;
//   * drivetrain: brushless motor from its wind (17.5T, 13.5T, ...) and timing, battery sag,
//     ESC drag brake / brake / reverse, pinion + spur + transmission ratio, slipper clutch, gear
//     (oil) or ball diffs, 2WD or 4WD -- integrated implicitly, because a 40 g wheel against a tire
//     that grips is far too stiff for an explicit step;
//   * aero: body drag, wing and body downforce; wheel gyroscopic torque.
// All of it ends up as one force + torque on the chassis body per Jolt step.
//
// ENTITIES. Chassis = Transform + RigidBody (dynamic) + CollisionShape + RCBuggy + RCBuggyGeometry.
// Its CHILD entities carrying RCBuggyPart are OUTPUT: wheels, arms, caster blocks / hub carriers,
// steering blocks, camber links, steering links, shocks, driveshafts, bellcranks. Their Transform is
// rewritten every frame as a delta from the design pose, so a mesh exported IN PLACE from the CAD /
// Blender model (same origin and axes as RCBuggyGeometry, no re-pivoting) animates correctly.
//
// UNITS. RCBuggy holds the setup sheet, in the setup sheet's own units (mm, deg, lb/in, oil wt,
// teeth, motor turns). RCBuggyGeometry holds CAD hardpoints in metres, RIGHT side only (the left is
// mirrored), in the chassis MODEL frame: +X right, +Y up, +Z forward, origin on the ground plane of
// the design pose.

#include "RCBuggyKinematics.h"

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#pragma float_control(precise, on, push)
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/PhysicsStepListener.h>
#include <Jolt/Core/FPException.h>
#pragma float_control(pop)
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace Components
{
    // The setup sheet. Defaults (MakeRCBuggy) are Brady Carrico's RC10B7.1 sheet, 2026 JConcepts
    // Indoor Nationals, RC One, CRC carpet -- with the values the sheet does not give (spring rates
    // of a colour, the motor in "17.5T Expert") filled with typical numbers.
    struct RCBuggy : ComponentBase<RCBuggy>
    {
        // ---- driver, -1..1, written by Systems::Driver while playing
        float throttleInput = 0.0f;         // > 0 throttle, < 0 brake, then reverse once stopped (like the ESC)
        float steeringInput = 0.0f;         // > 0 steers right
        uint  fourWheelDrive = 0;           // 0 = 2WD (B7.1), 1 = 4WD (B84.1: front + rear diff on one layshaft)

        // ---- motor + ESC + battery
        float motorTurns = 17.5f;           // wind: 21.5, 17.5, 13.5, 10.5, 8.5, 6.5 ... -> kV and resistance
        float motorTiming = 0.0f;           // deg of advance (endbell + ESC boost); 0 = blinky
        float batteryVoltage = 7.6f;        // V under load, 2S LiPo
        float batteryResistance = 0.006f;   // ohm, pack + wires + ESC
        float currentLimit = 180.0f;        // A
        float dragBrake = 0.05f;            // 0..1, braking at neutral throttle
        float brakeStrength = 0.8f;         // 0..1
        float reverseStrength = 0.5f;       // 0..1

        // ---- gearing
        uint  pinion = 32;                  // teeth
        uint  spur = 69;                    // teeth
        float transmissionRatio = 2.6f;     // spur shaft -> diff, internal to the gearbox
        float slipperTorque = 0.0f;         // N.m at the spur that the slipper holds before slipping. 0 = slipper eliminator (locked)

        // ---- differentials: 0 = gear diff (oil), 1 = ball diff, 2 = spool (locked)
        uint  rearDiffType = 0;
        float rearDiffOil = 80000.0f;       // cSt, gear diff
        uint  frontDiffType = 0;            // 4WD only
        float frontDiffOil = 10000.0f;      // cSt
        float ballDiffTorque = 0.15f;       // N.m at the wheels a ball diff holds before it slips

        // ---- front suspension (setup-sheet units)
        float rideHeightFront = 13.0f;      // mm, chassis underside to ground
        float camberFront = -2.0f;          // deg, negative = top in. Sets the camber link length
        float toeFront = -0.5f;             // deg, negative = toe out. Sets the steering link length
        float springRateFront = 3.5f;       // lb/in
        float shockOilFront = 40.0f;        // wt (x12.5 = cSt)
        uint  pistonHolesFront = 2;
        float pistonHoleFront = 1.8f;       // mm
        float shockStrokeFront = 23.0f;     // mm
        float shockLengthFront = 0.0f;      // mm eye to eye at full droop (after limiters). 0 = auto: 35% of the stroke is droop
        float antiRollBarFront = 1.0f;      // mm wire, 0 = none

        // ---- rear suspension
        float rideHeightRear = 13.0f;
        float camberRear = -2.0f;
        float springRateRear = 2.3f;
        float shockOilRear = 35.0f;
        uint  pistonHolesRear = 2;
        float pistonHoleRear = 1.9f;
        float shockStrokeRear = 29.0f;
        float shockLengthRear = 0.0f;
        float antiRollBarRear = 1.4f;

        // ---- shocks, both ends
        float shockBore = 13.0f;            // mm (Associated "big bore")
        float pistonThickness = 1.6f;       // mm

        // ---- steering
        float steeringThrow = 22.0f;        // deg of bellcrank rotation at full lock (EPA)
        float servoSpeed = 0.07f;           // s per 60 deg

        // ---- tires + track
        float tireGripFront = 1.6f;         // peak friction coefficient on this surface
        float tireGripRear = 1.8f;
        float tireRateFront = 6.0f;         // N/mm radial stiffness (insert, compound)
        float tireRateRear = 8.0f;
        float surfaceGrip = 1.0f;           // multiplies both grips: carpet 1, clay ~0.8, dusty ~0.6
        float tirePeakSlipAngle = 20.0f;    // deg where cornering force peaks. Lower = stiffer tire: more response, and static toe fights harder
        float tirePeakSlipRatio = 0.15f;    // wheelspin / lock-up ratio where drive and brake force peak

        // ---- aero + mass
        float wingAngle = 3.0f;             // deg
        float unsprungMassFront = 60.0f;    // g per corner: wheel, tire, hub, half the arm and shock
        float unsprungMassRear = 85.0f;
    };
    static void RCBuggyPropertyDraw(char* p);

    // CAD hardpoints, metres, right side, chassis model frame (see the top of this file). Defaults
    // were measured off a Team Associated B5M model (BuggyRef.blend) -- an older car, same layout.
    struct RCBuggyGeometry : ComponentBase<RCBuggyGeometry>
    {
        float  chassisBottom = 0.0223f;     // Y of the chassis plate underside in the design pose
        float  wheelRadiusFront = 0.0425f;
        float  wheelWidthFront = 0.0325f;
        float  wheelRadiusRear = 0.0434f;
        float  wheelWidthRear = 0.0517f;

        float3 frontLowerInner;             // on the inner hinge pin axis
        float3 frontLowerAxis;              // hinge pin direction (kick-up)
        float3 frontLowerOuter;             // on the outer hinge pin axis
        float3 frontUpperInner;             // camber link ball, tower side
        float3 frontUpperOuter;             // camber link ball, caster block side
        float3 frontKingpinTop;
        float3 frontKingpinBottom;
        float3 frontTieInner;               // steering link ball on the bellcrank
        float3 frontTieOuter;               // steering link ball on the steering block
        float3 frontBellcrankPivot;
        float3 frontBellcrankAxis;
        float3 frontWheelCenter;
        float3 frontAxle;                   // pointing outward
        float3 frontShockTop;
        float3 frontShockBottom;

        float3 rearLowerInner;
        float3 rearLowerAxis;               // its sideways lean is the rear toe-in, its tilt the anti-squat
        float3 rearLowerOuter;
        float3 rearUpperInner;
        float3 rearUpperOuter;
        float3 rearWheelCenter;
        float3 rearAxle;
        float3 rearShockTop;
        float3 rearShockBottom;
        float3 driveshaftInner;             // gearbox / diff output, right side (front ones mirrored to +Z in 4WD)
    };

    // A moving piece of the car, on a CHILD entity of the chassis. Its Transform is output.
    struct RCBuggyPart : ComponentBase<RCBuggyPart>
    {
        uint corner = 0;    // 0 front left, 1 front right, 2 rear left, 3 rear right
        uint part = 0;      // 0 wheel, 1 lower arm, 2 caster block / hub carrier, 3 steering block,
                            // 4 camber link, 5 steering link, 6 shock body (hangs from the top
                            // eyelet), 7 driveshaft, 8 bellcrank, 9 shock shaft (rides the bottom eyelet)
    };
}

// =================================================================================================
// Runtime
// =================================================================================================

namespace RCBuggySim
{
    using RCKin::V3;
    using RCKin::Q;

    static constexpr double pi = 3.14159265358979;
    static constexpr double lbPerInToNPerM = 175.126835;
    static constexpr double airDensity = 1.2;
    static constexpr double oilDensity = 970.0;

    static V3 ToV3(float3 f)
    {
        float v[3];
        store(f, v);
        for (uint i = 0; i < 3; i++)
            if (!isfinite(v[i])) v[i] = 0.0f;
        return V3(v[0], v[1], v[2]);
    }
    static JPH::Vec3 J(const V3& v) { return JPH::Vec3((float)v.x, (float)v.y, (float)v.z); }
    static V3 FromJ(JPH::Vec3Arg v) { return V3(v.GetX(), v.GetY(), v.GetZ()); }
    static double Pos(float v, double fallback) { return (isfinite(v) && v > 0.0f) ? (double)v : fallback; }
    static double Fin(float v, double fallback) { return isfinite(v) ? (double)v : fallback; }
    static double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // Interpolated table read. Linear in theta and beta; quaternions nlerp'd (the steps are tiny).
    struct Lookup
    {
        V3 wheel, axle;
        double shockLength;
        Q armQ, carrierQ, knuckleQ;
        V3 armT, carrierT, knuckleT;
        V3 upperOuter, tieOuter, tieInner, shockBottom;
    };

    static V3 LV(const float* a, const float* b, double t) { return V3(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t); }
    static Q LQ(const float* a, const float* b, double t)
    {
        double s = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]) < 0 ? -1.0 : 1.0;
        return Q(a[0] + (s * b[0] - a[0]) * t, a[1] + (s * b[1] - a[1]) * t, a[2] + (s * b[2] - a[2]) * t, a[3] + (s * b[3] - a[3]) * t).Normalized();
    }
    static V3 LerpV(const V3& a, const V3& b, double t) { return a + (b - a) * t; }
    static Q LerpQ(const Q& a, const Q& b, double t)
    {
        double s = (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0 ? -1.0 : 1.0;
        return Q(a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t).Normalized();
    }

    static void ReadRow(const RCKin::CornerTable& t, int ib, double theta, Lookup& o)
    {
        double ft = Clamp((theta - t.theta0) / t.dTheta, 0.0, (double)(t.nTheta - 1) - 1e-6);
        int i0 = (int)ft;
        double a = ft - i0;
        const RCKin::Sample& s0 = t.At(i0, ib);
        const RCKin::Sample& s1 = t.At(i0 + 1, ib);
        o.wheel = LV(s0.wheel, s1.wheel, a);
        o.axle = RCKin::Normalize(LV(s0.axle, s1.axle, a));
        o.shockLength = s0.shockLength + (s1.shockLength - s0.shockLength) * a;
        o.armQ = LQ(s0.armQ, s1.armQ, a);             o.armT = LV(s0.armT, s1.armT, a);
        o.carrierQ = LQ(s0.carrierQ, s1.carrierQ, a); o.carrierT = LV(s0.carrierT, s1.carrierT, a);
        o.knuckleQ = LQ(s0.knuckleQ, s1.knuckleQ, a); o.knuckleT = LV(s0.knuckleT, s1.knuckleT, a);
        o.upperOuter = LV(s0.upperOuter, s1.upperOuter, a);
        o.tieOuter = LV(s0.tieOuter, s1.tieOuter, a);
        o.tieInner = LV(s0.tieInner, s1.tieInner, a);
        o.shockBottom = LV(s0.shockBottom, s1.shockBottom, a);
    }

    // RIGHT-side pose at (theta, beta).
    static void Read(const RCKin::CornerTable& t, double theta, double beta, Lookup& o)
    {
        if (t.nBeta <= 1)
        {
            ReadRow(t, 0, theta, o);
            return;
        }
        double fb = Clamp((beta - t.beta0) / t.dBeta, 0.0, (double)(t.nBeta - 1) - 1e-6);
        int j0 = (int)fb;
        double b = fb - j0;
        Lookup l0, l1;
        ReadRow(t, j0, theta, l0);
        ReadRow(t, j0 + 1, theta, l1);
        o.wheel = LerpV(l0.wheel, l1.wheel, b);
        o.axle = RCKin::Normalize(LerpV(l0.axle, l1.axle, b));
        o.shockLength = l0.shockLength + (l1.shockLength - l0.shockLength) * b;
        o.armQ = LerpQ(l0.armQ, l1.armQ, b);             o.armT = LerpV(l0.armT, l1.armT, b);
        o.carrierQ = LerpQ(l0.carrierQ, l1.carrierQ, b); o.carrierT = LerpV(l0.carrierT, l1.carrierT, b);
        o.knuckleQ = LerpQ(l0.knuckleQ, l1.knuckleQ, b); o.knuckleT = LerpV(l0.knuckleT, l1.knuckleT, b);
        o.upperOuter = LerpV(l0.upperOuter, l1.upperOuter, b);
        o.tieOuter = LerpV(l0.tieOuter, l1.tieOuter, b);
        o.tieInner = LerpV(l0.tieInner, l1.tieInner, b);
        o.shockBottom = LerpV(l0.shockBottom, l1.shockBottom, b);
    }

    // The same pose on the given side: the left corner at bellcrank angle beta is the mirror image
    // of the right corner at -beta (both bellcranks turn together, the mirror swaps left and right).
    static void ReadSide(const RCKin::CornerTable& t, double theta, double beta, int side, Lookup& o)
    {
        Read(t, theta, side > 0 ? beta : -beta, o);
        if (side > 0)
            return;
        o.wheel = RCKin::MirrorX(o.wheel);
        o.axle = RCKin::MirrorX(o.axle);
        o.armQ = o.armQ.MirrorX();         o.armT = RCKin::MirrorX(o.armT);
        o.carrierQ = o.carrierQ.MirrorX(); o.carrierT = RCKin::MirrorX(o.carrierT);
        o.knuckleQ = o.knuckleQ.MirrorX(); o.knuckleT = RCKin::MirrorX(o.knuckleT);
        o.upperOuter = RCKin::MirrorX(o.upperOuter);
        o.tieOuter = RCKin::MirrorX(o.tieOuter);
        o.tieInner = RCKin::MirrorX(o.tieInner);
        o.shockBottom = RCKin::MirrorX(o.shockBottom);
    }

    // ---- everything the step needs, derived once per (re)build from the two components -----------
    struct EndParams
    {
        RCKin::EndGeometry geometry;        // right side
        RCKin::CornerTable table;
        double radius, width;
        double unsprungMass, armInertia;
        double springRate;                  // N/m at the shock
        double springForceStatic;           // N at the shock in the static pose (= the preload a racer would set)
        double dampLinear, dampQuadratic;   // N per m/s, N per (m/s)^2 at the shock
        double arbRate;                     // N/m at the wheel, per metre of left/right travel difference
        double tireRate, tireDamping;
        double grip;
        double staticLoad;                  // N per tire
        double wheelInertia;
        double motionRatio;                 // shock / wheel travel at static, for the property window
    };

    struct Params
    {
        EndParams end[2];                   // 0 front, 1 rear
        bool fourWD = false;
        double kt, kvRad, resistance, noLoadTorque;
        double batteryVoltage, currentLimit;
        double dragBrake, brakeStrength, reverseStrength;
        double gearPinionSpur, gearTransmission, gearTotal;
        double slipperTorque;               // at the spur shaft, 0 = locked
        double motorInertia;                // rotor + pinion, kg m^2
        uint   diffType[2];                 // 0 front, 1 rear
        double diffViscous[2];              // N.m per rad/s of wheel speed difference
        double ballDiffTorque;
        double steeringThrow, servoRate;
        double dragArea, wingLiftArea, bodyLiftArea;
        V3 wingPoint, bodyLiftPoint, dragPoint;
        V3 driveshaftInner;
        double rollingResistance;
        double tirePeakTanAlpha, tirePeakSlipRatio;
        double totalMass;
        V3 centerOfMass;                    // chassis model frame
    };

    struct Corner
    {
        int end = 0;                        // 0 front, 1 rear
        int side = 1;                       // +1 right, -1 left
        double theta = 0, thetaDot = 0;
        double omega = 0;                   // wheel spin, rad/s, positive = rolling forward
        double spin = 0;                    // accumulated spin angle, for the wheel mesh

        // ground, refreshed every Jolt step by the cylinder cast
        bool hasGround = false;
        JPH::Vec3 groundPoint, groundNormal;

        // last values, for the property window
        float load = 0, slipRatio = 0, slipAngle = 0, camber = 0, toe = 0, travel = 0, shockLength = 0;
        bool contact = false;
    };

    // One car. Registered as a Jolt step listener, so OnStep runs once per Jolt collision step, with
    // every body locked -- it reads and writes ITS OWN chassis body only (Jolt may run listeners in
    // parallel), and only through the no-lock interfaces.
    class Runtime final : public JPH::PhysicsStepListener
    {
    public:
        JPH::BodyID body;
        uint entityRev = 0;
        Params P;
        Corner corner[4];

        // drivetrain state
        double carrierOmega = 0;            // diff carrier(s), rad/s (wheel speed units)
        double diffOmega[2] = { 0, 0 };     // half the left - right difference, per axle (0 front, 1 rear)
        double motorOmega = 0;
        bool   slipping = false;
        bool   reversing = false;
        bool   reverseArmed = false;      // neutral seen with the car stopped: the next push back reverses
        double beta = 0;                    // bellcrank angle, rad

        // inputs, copied in by Sync while the step is not running
        double throttle = 0, steering = 0;

        // what OnStep added to the body so far this Update (see the end of OnStep)
        JPH::Vec3 appliedForce = JPH::Vec3::sZero();
        JPH::Vec3 appliedTorque = JPH::Vec3::sZero();

        // live readouts
        float motorCurrent = 0, motorRPM = 0, forwardSpeed = 0, motorTorque = 0;
        bool  built = false;

        void Reset()
        {
            for (int i = 0; i < 4; i++)
            {
                corner[i].theta = P.end[corner[i].end].table.thetaStatic;
                corner[i].thetaDot = 0;
                corner[i].omega = 0;
            }
            carrierOmega = 0;
            diffOmega[0] = diffOmega[1] = 0;
            motorOmega = 0;
            slipping = false;
            reversing = false;
            reverseArmed = false;
            beta = 0;
        }

        // ---- tire ------------------------------------------------------------------------------
        // Normalised combined slip: each slip is divided by where its own curve peaks, the two are
        // combined as a vector, and one Pacejka-shaped curve sin(C atan(B s)) -- peaking at s = 1 --
        // sets the force along that vector. C = 1.3 keeps ~89% of the peak at full slide, about
        // what a soft carpet tire does.
        struct TireOut { double fx, fy, slipRatio, slipAngle; };
        TireOut Tire(double omega, double radius, double vx, double vy, double load, double mu) const
        {
            const double v0 = 1.0;                      // below this, slips are measured against v0 (stability, see OnStep)
            // C = 1.4 keeps ~80% of the peak at full slide, like a soft carpet tire. Both ends of
            // the curve matter: one that drops far past its peak (C = 1.6 keeps 59%) never hooks
            // back up once a 2WD spins its rears, and a peak at a small slip angle makes static toe
            // fight itself -- at 8 deg each rear pushed 1.5x its own load inward at 3 deg toe-in,
            // and through the arms' scrub that jacked the rear down 11 mm at speed.
            const double kappaPeak = P.tirePeakSlipRatio;
            const double tanAlphaPeak = P.tirePeakTanAlpha;
            const double C = 1.4;
            const double B = 2.0765;                    // tan(pi / (2C)): puts the peak at s = 1
            TireOut out = { 0, 0, 0, 0 };
            double denom = fabs(vx) > v0 ? fabs(vx) : v0;
            double kappa = (omega * radius - vx) / denom;
            double tanAlpha = vy / denom;
            out.slipRatio = kappa;
            out.slipAngle = atan(tanAlpha);
            double sx = kappa / kappaPeak;
            double sy = tanAlpha / tanAlphaPeak;
            double s = sqrt(sx * sx + sy * sy);
            if (s < 1e-9 || load <= 0)
                return out;
            double f = mu * load * sin(C * atan(B * s)) / s;
            out.fx = f * sx;
            out.fy = -f * sy;
            return out;
        }

        // ---- motor -----------------------------------------------------------------------------
        // DC model of a sensored brushless motor + its ESC: current from (duty * V - backEMF) over the
        // winding + battery resistance, clamped by the current limit. Braking shorts the phases
        // (scaled by the brake strength), and partial throttle above the matching speed brakes too --
        // what a synchronous-rectified ESC does.
        void Motor(double omegaMotor, double fwdSpeed, double& torque, double& dTorque, double& current)
        {
            double rTotal = P.resistance;
            double emf = omegaMotor / P.kvRad;
            double t = throttle;
            double duty = 0, brake = 0;
            // Forward / brake / reverse the way an RC ESC does it: pushing back brakes, and only a
            // push that comes after a return to neutral with the car stopped reverses (the
            // "double tap"). reverseStrength 0 is race mode -- no reverse at all.
            if (t > 0.0)
            {
                duty = t;
                reversing = false;
                reverseArmed = false;
            }
            else if (t < 0.0)
            {
                if (!reversing && reverseArmed && P.reverseStrength > 0.0)
                    reversing = true;
                if (reversing)
                    duty = t * P.reverseStrength;
                else
                    brake = -t * P.brakeStrength;
            }
            else
            {
                brake = P.dragBrake;
                reversing = false;
                reverseArmed = fwdSpeed < 0.25;
            }

            double slope;
            if (brake > 0.0)
            {
                current = -brake * emf / rTotal;
                slope = -brake / (P.kvRad * rTotal);
            }
            else
            {
                current = (duty * P.batteryVoltage - emf) / rTotal;
                slope = -1.0 / (P.kvRad * rTotal);
            }
            if (current > P.currentLimit) { current = P.currentLimit; slope = 0; }
            if (current < -P.currentLimit) { current = -P.currentLimit; slope = 0; }

            double friction = P.noLoadTorque * tanh(omegaMotor / 30.0);
            torque = P.kt * current - friction;
            dTorque = P.kt * slope - P.noLoadTorque / 30.0 / (cosh(omegaMotor / 30.0) * cosh(omegaMotor / 30.0));
        }

        // ---- per-substep wheel state shared by the drivetrain and suspension passes --------------
        struct WheelFrame
        {
            Lookup pose, poseUp, poseDown;
            JPH::Vec3 W, C, dWdTheta, dCdTheta, axleOut, fwd, lat, n;
            double pen = 0, load = 0, radius = 0, mu = 0, vx = 0, vy = 0, camber = 0;
            bool contact = false;
            double fx = 0, fy = 0, dfx = 0;     // longitudinal force and its slope in omega
            double capY = 1e30, capX = 1e30;    // friction caps from the chassis' effective mass
        };

        // Angular momentum of everything that spins with the wheels, about the car's right axis.
        double DrivetrainMomentum() const
        {
            double L = P.motorInertia * motorOmega;   // the rotor, mid-motor: axis across the car
            for (int i = 0; i < 4; i++)
                L += P.end[corner[i].end].wheelInertia * corner[i].omega;
            return L;
        }

        void OnStep(const JPH::PhysicsStepListenerContext& ctx) override;
    };

    // ---- the per-step work ---------------------------------------------------------------------------
    inline void Runtime::OnStep(const JPH::PhysicsStepListenerContext& ctx)
    {
        if (!built || ctx.mDeltaTime <= 0.0f)
            return;

        JPH::BodyLockWrite lock(ctx.mPhysicsSystem->GetBodyLockInterfaceNoLock(), body);
        if (!lock.Succeeded())
            return;
        JPH::Body& b = lock.GetBody();
        if (!b.IsDynamic() || !b.IsActive())
            return;

        // Jolt runs listeners on its workers with FP exceptions UNMASKED (Jolt.cmake turns them on
        // in Release too). Masked here: a NaN in this code must not take the engine down. Instead
        // every stage is checked below, and a non-finite result is dropped before it reaches Jolt.
        JPH::FPExceptionDisableInvalid noInvalid;
        JPH::FPExceptionDisableDivByZero noDivByZero;
        JPH::FPExceptionDisableOverflow noOverflow;
        auto finiteV = [](JPH::Vec3Arg v) { return isfinite(v.GetX()) && isfinite(v.GetY()) && isfinite(v.GetZ()); };

        const double dt = ctx.mDeltaTime;
        const JPH::RMat44 bodyTransform = b.GetWorldTransform();     // body origin = chassis model frame
        const JPH::Quat rot = b.GetRotation();
        const JPH::Vec3 com = JPH::Vec3(b.GetCenterOfMassPosition());
        const JPH::Vec3 vLin = b.GetLinearVelocity();
        const JPH::Vec3 vAng = b.GetAngularVelocity();
        const JPH::Vec3 up = rot * JPH::Vec3::sAxisY();
        const JPH::Vec3 forward = rot * JPH::Vec3::sAxisZ();
        const double invMass = b.GetMotionProperties()->GetInverseMass();
        const JPH::Mat44 invInertia = b.GetMotionProperties()->GetInverseInertiaForRotation(JPH::Mat44::sRotation(rot));

        double fwdSpeed = vLin.Dot(forward);
        forwardSpeed = (float)fwdSpeed;

        // ---- servo -----------------------------------------------------------------------------
        double betaTarget = Clamp(steering, -1, 1) * P.steeringThrow;
        double betaStep = P.servoRate * dt;
        beta += Clamp(betaTarget - beta, -betaStep, betaStep);

        // ---- ground: one Jolt cylinder cast per tire ------------------------------------------
        // The tire as a cylinder along its axle, from 30 mm above where the wheel is now, down the
        // chassis' own down axis far enough to cover full droop. The hit is then treated as a plane
        // for the sub-steps below -- 40 mm of travel at 600 Hz never leaves it.
        {
            const JPH::IgnoreSingleBodyFilter bodyFilter(body);
            const JPH::DefaultBroadPhaseLayerFilter bpFilter = ctx.mPhysicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::moving);
            const JPH::DefaultObjectLayerFilter objFilter = ctx.mPhysicsSystem->GetDefaultLayerFilter(PhysicsLayers::moving);
            for (int i = 0; i < 4; i++)
            {
                Corner& c = corner[i];
                const EndParams& E = P.end[c.end];
                Lookup pose;
                ReadSide(E.table, c.theta, beta, c.side, pose);

                JPH::Vec3 W = JPH::Vec3(bodyTransform * J(pose.wheel));
                JPH::Vec3 axle = rot * J(pose.axle);
                const float lift = 0.03f;
                const float reach = lift + 0.06f;

                JPH::CylinderShape cylinder((float)(0.5 * E.width), (float)E.radius, 0.002f);
                cylinder.SetEmbedded();
                JPH::Quat cylRot = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axle.NormalizedOr(JPH::Vec3::sAxisX()));
                JPH::RMat44 start = JPH::RMat44::sRotationTranslation(cylRot, JPH::RVec3(W + up * lift));
                JPH::RShapeCast cast(&cylinder, JPH::Vec3::sOne(), start, -up * reach);

                JPH::ShapeCastSettings settings;
                settings.mUseShrunkenShapeAndConvexRadius = true;
                settings.mReturnDeepestPoint = true;
                JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> hit;
                ctx.mPhysicsSystem->GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), hit, bpFilter, objFilter, bodyFilter);

                c.hasGround = false;
                if (hit.HadHit())
                {
                    JPH::Vec3 normal = -hit.mHit.mPenetrationAxis.NormalizedOr(-up);
                    // A wall is not ground: steeper than ~65 deg from the chassis' up is left to the
                    // chassis collider.
                    if (normal.Dot(up) > 0.42f)
                    {
                        c.hasGround = true;
                        c.groundPoint = JPH::Vec3(hit.mHit.mContactPointOn2);
                        c.groundNormal = normal;
                    }
                }
            }
        }

        // ---- sub-steps -------------------------------------------------------------------------
        // The tire's radial spring against 60-85 g of unsprung mass is ~45 Hz, the bump stop more;
        // 0.35 ms sub-steps keep the semi-implicit integration well inside its stable range.
        int n = (int)ceil(dt / 0.00035);
        if (n < 1) n = 1;
        if (n > 64) n = 64;
        const double h = dt / n;

        JPH::Vec3 forceSum = JPH::Vec3::sZero();
        const double momentumBefore = DrivetrainMomentum();
        JPH::Vec3 torqueSum = JPH::Vec3::sZero();

        int contacts = 0;
        for (int i = 0; i < 4; i++)
            contacts += corner[i].hasGround ? 1 : 0;
        const double contactShare = contacts > 0 ? 1.0 / contacts : 1.0;

        double lastCurrent = 0, lastTorque = 0;
        WheelFrame wf[4];

        for (int sub = 0; sub < n; sub++)
        {
            // -- 1. kinematics, contact and tire forces at the current state
            for (int i = 0; i < 4; i++)
            {
                Corner& c = corner[i];
                const EndParams& E = P.end[c.end];
                WheelFrame& f = wf[i];
                const double dTh = 0.25 * E.table.dTheta;
                ReadSide(E.table, c.theta, beta, c.side, f.pose);
                ReadSide(E.table, c.theta + dTh, beta, c.side, f.poseUp);
                ReadSide(E.table, c.theta - dTh, beta, c.side, f.poseDown);

                f.W = JPH::Vec3(bodyTransform * J(f.pose.wheel));
                f.dWdTheta = rot * J((f.poseUp.wheel - f.poseDown.wheel) * (1.0 / (2 * dTh)));
                f.axleOut = (rot * J(f.pose.axle)).Normalized();
                f.radius = E.radius;
                f.contact = false;
                f.load = 0; f.fx = 0; f.fy = 0; f.dfx = 0; f.pen = 0;

                if (!c.hasGround)
                {
                    f.dCdTheta = f.dWdTheta;
                    f.C = f.W - up * (float)E.radius;
                    continue;
                }

                f.n = c.groundNormal;
                // Lowest point of the tire towards the ground: on the rim circle, pushed half the
                // tread's half-width towards whichever edge leans into the ground (rounded sidewall).
                auto contactPoint = [&](const Lookup& L) -> JPH::Vec3
                {
                    JPH::Vec3 Wp = JPH::Vec3(bodyTransform * J(L.wheel));
                    JPH::Vec3 a = (rot * J(L.axle)).Normalized();
                    JPH::Vec3 u = f.n - a * f.n.Dot(a);
                    float ul = u.Length();
                    JPH::Vec3 uh = ul > 1e-6f ? u / ul : f.n;
                    float na = f.n.Dot(a);
                    float edge = na > 0 ? -1.0f : 1.0f;
                    float lean = fabsf(na) * 4.0f;
                    if (lean > 1.0f) lean = 1.0f;
                    return Wp - uh * (float)E.radius + a * (edge * lean * (float)(0.25 * E.width));
                };
                f.C = contactPoint(f.pose);
                f.dCdTheta = (contactPoint(f.poseUp) - contactPoint(f.poseDown)) / (float)(2 * dTh);
                f.pen = (c.groundPoint - f.C).Dot(f.n);
                if (f.pen <= 0)
                    continue;

                JPH::Vec3 vC = vLin + vAng.Cross(f.C - com) + f.dCdTheta * (float)c.thetaDot;
                double penRate = -vC.Dot(f.n);
                f.load = E.tireRate * f.pen + E.tireDamping * penRate;
                if (f.load <= 0)
                {
                    f.load = 0;
                    continue;
                }
                f.contact = true;

                // Tire frame: forward = axle x normal with the axle pointing to the chassis' right.
                JPH::Vec3 axleRight = f.axleOut * (float)c.side;
                f.fwd = axleRight.Cross(f.n).NormalizedOr(forward);
                f.lat = (axleRight - f.n * axleRight.Dot(f.n)).NormalizedOr(rot * JPH::Vec3::sAxisX());
                f.vx = vC.Dot(f.fwd);
                f.vy = vC.Dot(f.lat);
                f.camber = asin(Clamp(f.axleOut.Dot(f.n), -1, 1));   // negative = top in

                // Load sensitivity: a tire gives less friction per newton the harder it is pressed.
                double loadRatio = f.load / (E.staticLoad > 0.1 ? E.staticLoad : 0.1);
                f.mu = E.grip * Clamp(1.0 - 0.08 * (loadRatio - 1.0), 0.6, 1.2);
                double rEff = E.radius - f.pen * 0.33;
                f.radius = rEff;

                // Explicit coupling to the chassis is only stable while the tire's slip damping
                // (force per m/s of slide) cannot reverse the contact point's velocity within one
                // Jolt step. Cap at the force that would, shared between the touching tires.
                auto effMass = [&](JPH::Vec3Arg d)
                {
                    JPH::Vec3 r = f.C - com;
                    JPH::Vec3 rd = r.Cross(d);
                    double inv = invMass + rd.Dot(invInertia.Multiply3x3(rd));
                    return inv > 1e-9 ? 1.0 / inv : 1e9;
                };
                f.capY = effMass(f.lat) * fabs(f.vy) / dt * contactShare;
                f.capX = effMass(f.fwd) * fabs(f.vx) / dt * contactShare;

                TireOut t0 = Tire(c.omega, rEff, f.vx, f.vy, f.load, f.mu);
                TireOut t1 = Tire(c.omega + 0.5, rEff, f.vx, f.vy, f.load, f.mu);
                f.fx = t0.fx;
                f.fy = t0.fy;
                f.dfx = (t1.fx - t0.fx) / 0.5;
                c.slipRatio = (float)t0.slipRatio;
                c.slipAngle = (float)t0.slipAngle;
            }

            // -- 2. drivetrain, implicit in each speed
            {
                const double Rr[4] = { wf[0].radius, wf[1].radius, wf[2].radius, wf[3].radius };
                auto rolling = [&](int i) { return P.rollingResistance * wf[i].load * Rr[i] * tanh(corner[i].omega / 3.0); };
                auto rollingSlope = [&](int i) { double c = cosh(corner[i].omega / 3.0); return P.rollingResistance * wf[i].load * Rr[i] / (3.0 * c * c); };

                // Driven axles: rear always, front too in 4WD. Wheel index pairs (left, right).
                const int axleWheels[2][2] = { { 0, 1 }, { 2, 3 } };
                bool driven[2] = { P.fourWD, true };

                // Carrier inertia: everything that turns with it, in wheel-speed units.
                double Jw = 0;
                double rhsC = 0, slopeC = 0;
                for (int ax = 0; ax < 2; ax++)
                {
                    if (!driven[ax]) continue;
                    for (int k = 0; k < 2; k++)
                    {
                        int i = axleWheels[ax][k];
                        Jw += P.end[ax].wheelInertia;
                        rhsC += -Rr[i] * wf[i].fx - rolling(i);
                        slopeC += -Rr[i] * wf[i].dfx - rollingSlope(i);
                    }
                }

                const double G = P.gearTotal;
                double motorT, motorSlope, current;

                if (!slipping)
                {
                    Motor(G * carrierOmega, fwdSpeed, motorT, motorSlope, current);
                    double Jc = P.motorInertia * G * G + Jw;
                    double rhs = G * motorT + rhsC;
                    double slope = G * G * motorSlope + slopeC;
                    double newOmega = carrierOmega + h * rhs / (Jc - h * (slope < 0 ? slope : 0));

                    // Torque the slipper would have to carry to keep the motor locked to the carrier.
                    if (P.slipperTorque > 0)
                    {
                        double carrierTorque = G * motorT - P.motorInertia * G * G * (newOmega - carrierOmega) / h;
                        double spurTorque = carrierTorque / P.gearTransmission;
                        if (fabs(spurTorque) > P.slipperTorque)
                        {
                            slipping = true;
                            motorOmega = G * carrierOmega;
                        }
                    }
                    if (!slipping)
                    {
                        carrierOmega = newOmega;
                        motorOmega = G * carrierOmega;
                    }
                    lastCurrent = current;
                    lastTorque = motorT;
                }
                if (slipping)
                {
                    // Motor + spur on one side of the slipper, everything else on the other; the
                    // pads pass slipperTorque one way or the other until the two speeds meet.
                    double g1 = P.gearPinionSpur, g2 = P.gearTransmission;
                    double spurSpeed = motorOmega / g1;
                    double shaftSpeed = g2 * carrierOmega;
                    double rel = spurSpeed - shaftSpeed;
                    double Ts = P.slipperTorque * (rel >= 0 ? 1.0 : -1.0);

                    Motor(motorOmega, fwdSpeed, motorT, motorSlope, current);
                    double newMotor = motorOmega + h * (motorT - Ts / g1) / (P.motorInertia - h * (motorSlope < 0 ? motorSlope : 0));
                    double newCarrier = carrierOmega + h * (g2 * Ts + rhsC) / (Jw - h * (slopeC < 0 ? slopeC : 0));
                    double relNew = newMotor / g1 - g2 * newCarrier;
                    if ((rel >= 0) != (relNew >= 0))
                    {
                        // Speeds crossed: lock, conserving angular momentum across the clutch.
                        double Jm = P.motorInertia * G * G;
                        carrierOmega = (Jm * (newMotor / G) + Jw * newCarrier) / (Jm + Jw);
                        motorOmega = G * carrierOmega;
                        slipping = false;
                    }
                    else
                    {
                        motorOmega = newMotor;
                        carrierOmega = newCarrier;
                    }
                    lastCurrent = current;
                    lastTorque = motorT;
                }

                // Differentials: half the left/right speed difference per driven axle.
                for (int ax = 0; ax < 2; ax++)
                {
                    int L = axleWheels[ax][0], R = axleWheels[ax][1];
                    if (!driven[ax])
                    {
                        // Free-rolling wheels, implicit each.
                        for (int k = 0; k < 2; k++)
                        {
                            int i = axleWheels[ax][k];
                            double I = P.end[ax].wheelInertia;
                            double rhs = -Rr[i] * wf[i].fx - rolling(i);
                            double slope = -Rr[i] * wf[i].dfx - rollingSlope(i);
                            corner[i].omega += h * rhs / (I - h * (slope < 0 ? slope : 0));
                        }
                        continue;
                    }
                    double I = P.end[ax].wheelInertia;
                    double& d = diffOmega[ax];
                    if (P.diffType[ax] == 2)
                    {
                        d = 0;
                    }
                    else
                    {
                        double rhs = -Rr[L] * wf[L].fx + Rr[R] * wf[R].fx - rolling(L) + rolling(R);
                        double slope = -Rr[L] * wf[L].dfx - Rr[R] * wf[R].dfx - rollingSlope(L) - rollingSlope(R);
                        if (P.diffType[ax] == 0)
                        {
                            // Gear diff: the oil is a viscous coupling between the two outputs.
                            rhs -= 4.0 * P.diffViscous[ax] * d;
                            slope -= 4.0 * P.diffViscous[ax];
                            d += h * rhs / (2 * I - h * (slope < 0 ? slope : 0));
                        }
                        else
                        {
                            // Ball diff: locked until the side-to-side torque beats the thrust
                            // spring, then slips at that torque. Stiff viscous coupling, clamped.
                            const double stiff = 0.05;
                            double coupling = Clamp(4.0 * stiff * d, -2.0 * P.ballDiffTorque, 2.0 * P.ballDiffTorque);
                            bool clamped = fabs(4.0 * stiff * d) > 2.0 * P.ballDiffTorque;
                            rhs -= coupling;
                            if (!clamped) slope -= 4.0 * stiff;
                            d += h * rhs / (2 * I - h * (slope < 0 ? slope : 0));
                        }
                    }
                    corner[L].omega = carrierOmega + d;
                    corner[R].omega = carrierOmega - d;
                }
            }

            // -- 3. suspension, tire forces to the chassis
            double travel[4];
            for (int i = 0; i < 4; i++)
                travel[i] = wf[i].pose.wheel.y;     // only differences across an axle are used

            for (int i = 0; i < 4; i++)
            {
                Corner& c = corner[i];
                const EndParams& E = P.end[c.end];
                WheelFrame& f = wf[i];
                const double dTh = 0.25 * E.table.dTheta;

                // Tire force, re-evaluated at the spin speed the drivetrain just settled on.
                JPH::Vec3 F = JPH::Vec3::sZero();
                if (f.contact)
                {
                    TireOut t = Tire(c.omega, f.radius, f.vx, f.vy, f.load, f.mu);
                    double fx = t.fx, fy = t.fy;
                    // Camber thrust, towards the side the top of the tire leans.
                    JPH::Vec3 outwardFlat = (f.axleOut - f.n * f.axleOut.Dot(f.n)).NormalizedOr(f.lat);
                    double camberThrust = 0.5 * f.camber * f.load;      // N, per rad of inclination
                    fy += camberThrust * outwardFlat.Dot(f.lat);
                    if (fabs(fy) > f.capY && fy * f.vy < 0) fy = fy > 0 ? f.capY : -f.capY;
                    if (fabs(fx) > f.capX && fx * f.vx < 0 && fabs(c.omega * f.radius) < fabs(f.vx)) fx = fx > 0 ? f.capX : -f.capX;
                    F = f.n * (float)f.load + f.fwd * (float)fx + f.lat * (float)fy;
                }

                // Generalised forces on the arm angle.
                double dLs = (f.poseUp.shockLength - f.poseDown.shockLength) / (2 * dTh);
                double Ls = f.pose.shockLength;
                double vLs = dLs * c.thetaDot;
                const RCKin::CornerTable& T = E.table;

                double spring = E.springForceStatic + E.springRate * (T.shockLengthStatic - Ls);
                if (spring < 0) spring = 0;
                double stop = 0;
                const double kStop = 30000.0, cStop = 40.0;
                if (Ls > T.shockLengthMax) stop -= kStop * (Ls - T.shockLengthMax) + (vLs > 0 ? cStop * vLs : 0);
                if (Ls < T.shockLengthMin) stop += kStop * (T.shockLengthMin - Ls) - (vLs < 0 ? cStop * vLs : 0);
                double shockForce = spring + stop;                       // + = pushes the shock longer
                double Q = shockForce * dLs;
                double dampLin = E.dampLinear + E.dampQuadratic * fabs(vLs);    // damper, treated implicitly

                // Anti-roll bar: opposes the difference in wheel height across the axle.
                int other = (i % 2 == 0) ? i + 1 : i - 1;
                double dWy = (f.poseUp.wheel.y - f.poseDown.wheel.y) / (2 * dTh);
                Q += -E.arbRate * (travel[i] - travel[other]) * dWy;

                Q += (double)F.Dot(f.dCdTheta);
                // No gravity / chassis-acceleration term on purpose. The unsprung masses are part
                // of the Jolt body's mass (Jolt already applies their weight), and theta is motion
                // RELATIVE to the chassis -- the term would be m_u (g - a_chassis), and a_chassis
                // can only be estimated by differencing Jolt's velocities, which is noisy enough
                // (+-100 m/s^2 step to step) to pump the suspension. Dropping it neglects the
                // unsprung mass's "added mass" on the chassis, a few % of the total.

                double M = E.unsprungMass * (double)f.dWdTheta.LengthSq() + E.armInertia;
                double cTheta = dampLin * dLs * dLs;
                if (f.contact)
                {
                    double nC = f.dCdTheta.Dot(f.n);
                    cTheta += E.tireDamping * nC * nC;
                }
                c.thetaDot = (c.thetaDot + h * Q / M) / (1.0 + h * cTheta / M);
                // The physical acceleration -- NOT the finite difference of thetaDot, which would
                // also carry the clamp below and fire it into the chassis as a huge reaction.
                double thetaAcc = (Q - cTheta * c.thetaDot) / M;
                c.theta += h * c.thetaDot;
                double lo = T.theta0, hi = T.theta0 + T.dTheta * (T.nTheta - 1);
                if (c.theta < lo) { c.theta = lo; if (c.thetaDot < 0) c.thetaDot = 0; }
                if (c.theta > hi) { c.theta = hi; if (c.thetaDot > 0) c.thetaDot = 0; }

                // To the chassis: the full tire force where it acts, minus what went into
                // accelerating the unsprung mass along its path.
                JPH::Vec3 inertial = f.dWdTheta * (float)(-E.unsprungMass * thetaAcc);
                forceSum += F + inertial;
                torqueSum += (f.C - com).Cross(F) + (f.W - com).Cross(inertial);

                c.spin += c.omega * h;
                c.load = (float)f.load;
                c.contact = f.contact;
                c.shockLength = (float)Ls;
            }
        }

        const float inv = 1.0f / (float)n;
        JPH::Vec3 force = forceSum * inv;
        JPH::Vec3 torque = torqueSum * inv;

        // Drivetrain reaction. The tire force is applied at the contact patch, and that ALREADY
        // carries the drive torque's reaction (bearing force at the axle + the gearbox's -T couple
        // == the same force at the ground, while the wheel is in torque balance). What is left is
        // the part of the torque that went into spinning the drivetrain up or down: the chassis
        // gets minus the rate of change of its angular momentum about the axle direction. That is
        // what pitches the car when the wheels spin up in the air, or lock under brakes.
        {
            JPH::Vec3 right = rot * JPH::Vec3::sAxisX();
            double momentumNow = DrivetrainMomentum();
            torque -= right * (float)((momentumNow - momentumBefore) / dt);
        }

        // Spinning wheels are gyroscopes: turning the chassis tilts their angular momentum.
        {
            JPH::Vec3 right = rot * JPH::Vec3::sAxisX();
            double L = 0;
            for (int i = 0; i < 4; i++)
                L += P.end[corner[i].end].wheelInertia * corner[i].omega;
            L += P.motorInertia * motorOmega;    // the rotor, mid-motor: axis across the car
            torque -= vAng.Cross(right * (float)L);
        }

        // Aero, from the air speed along the chassis.
        {
            double speed = vLin.Length();
            if (speed > 0.1)
            {
                JPH::Vec3 drag = -vLin * (float)(0.5 * airDensity * P.dragArea * speed);
                JPH::Vec3 dragAt = JPH::Vec3(bodyTransform * J(P.dragPoint));
                force += drag;
                torque += (dragAt - com).Cross(drag);
                double vf = fwdSpeed > 0 ? fwdSpeed : 0;
                double q = 0.5 * airDensity * vf * vf;
                JPH::Vec3 wing = -up * (float)(q * P.wingLiftArea);
                JPH::Vec3 bodyLift = -up * (float)(q * P.bodyLiftArea);
                force += wing + bodyLift;
                torque += (JPH::Vec3(bodyTransform * J(P.wingPoint)) - com).Cross(wing);
                torque += (JPH::Vec3(bodyTransform * J(P.bodyLiftPoint)) - com).Cross(bodyLift);
            }
        }

        bool stateFinite = isfinite(carrierOmega) && isfinite(motorOmega);
        for (int i = 0; i < 4; i++)
            stateFinite &= isfinite(corner[i].theta) && isfinite(corner[i].thetaDot) && isfinite(corner[i].omega);
        if (!finiteV(force) || !finiteV(torque) || !stateFinite)
        {
            // Never hand Jolt a NaN: drop this step's forces and put the corners back at rest.
            for (int i = 0; i < 4; i++)
            {
                if (!isfinite(corner[i].theta) || !isfinite(corner[i].thetaDot)) { corner[i].theta = P.end[corner[i].end].table.thetaStatic; corner[i].thetaDot = 0; }
                if (!isfinite(corner[i].omega)) corner[i].omega = 0;
            }
            if (!isfinite(carrierOmega)) carrierOmega = 0;
            if (!isfinite(motorOmega)) motorOmega = 0;
            if (!finiteV(force) || !finiteV(torque))
            {
                if (!ctx.mIsFirstStep)
                {
                    b.AddForce(-appliedForce);
                    b.AddTorque(-appliedTorque);
                }
                appliedForce = JPH::Vec3::sZero();
                appliedTorque = JPH::Vec3::sZero();
                return;
            }
        }
        // Jolt clears a body's accumulated force and torque only after the LAST collision step of
        // an Update (PhysicsSystem::CheckSleepAndUpdateBounds, `if (ioStep->mIsLast)`), not after
        // every step -- a plain AddForce here would pile up, step after step, into ~n/2 times the
        // intended force. So add only the change from what this listener applied last step, which
        // also leaves anything else's AddForce on this body alone.
        if (ctx.mIsFirstStep)
        {
            appliedForce = JPH::Vec3::sZero();
            appliedTorque = JPH::Vec3::sZero();
        }
        b.AddForce(force - appliedForce);
        b.AddTorque(torque - appliedTorque);
        appliedForce = force;
        appliedTorque = torque;

        motorCurrent = (float)lastCurrent;
        motorTorque = (float)lastTorque;
        motorRPM = (float)(motorOmega * 60.0 / (2 * pi));
        for (int i = 0; i < 4; i++)
        {
            const Lookup& L = wf[i].pose;
            V3 axle = L.axle;
            corner[i].camber = (float)(asin(Clamp(axle.y, -1, 1)) * 180 / pi);
            corner[i].toe = (float)(atan2(axle.z * corner[i].side, sqrt(axle.x * axle.x + axle.y * axle.y)) * 180 / pi);   // + = toe in, both sides
            const RCKin::CornerTable& T = P.end[corner[i].end].table;
            corner[i].travel = (float)((L.wheel.y - T.At(RCKin::itStaticClamp(T, T.thetaStatic), T.nBeta / 2).wheel[1]) * 1000.0);
        }
    }

    // =============================================================================================
    // Build: components -> Params + tables
    // =============================================================================================

    static RCKin::EndGeometry ReadEndGeometry(const Components::RCBuggyGeometry& g, int end)
    {
        RCKin::EndGeometry e;
        if (end == 0)
        {
            e.lowerInner = ToV3(g.frontLowerInner);
            e.lowerAxis = ToV3(g.frontLowerAxis);
            e.lowerOuter = ToV3(g.frontLowerOuter);
            e.upperInner = ToV3(g.frontUpperInner);
            e.upperOuter = ToV3(g.frontUpperOuter);
            e.wheelCenter = ToV3(g.frontWheelCenter);
            e.axle = ToV3(g.frontAxle);
            e.shockTop = ToV3(g.frontShockTop);
            e.shockBottom = ToV3(g.frontShockBottom);
            e.wheelRadius = Pos(g.wheelRadiusFront, 0.0425);
            e.steered = true;
            e.kingpinTop = ToV3(g.frontKingpinTop);
            e.kingpinBottom = ToV3(g.frontKingpinBottom);
            e.tieInner = ToV3(g.frontTieInner);
            e.tieOuter = ToV3(g.frontTieOuter);
            e.bellcrankPivot = ToV3(g.frontBellcrankPivot);
            e.bellcrankAxis = ToV3(g.frontBellcrankAxis);
        }
        else
        {
            e.lowerInner = ToV3(g.rearLowerInner);
            e.lowerAxis = ToV3(g.rearLowerAxis);
            e.lowerOuter = ToV3(g.rearLowerOuter);
            e.upperInner = ToV3(g.rearUpperInner);
            e.upperOuter = ToV3(g.rearUpperOuter);
            e.wheelCenter = ToV3(g.rearWheelCenter);
            e.axle = ToV3(g.rearAxle);
            e.shockTop = ToV3(g.rearShockTop);
            e.shockBottom = ToV3(g.rearShockBottom);
            e.wheelRadius = Pos(g.wheelRadiusRear, 0.0434);
            e.steered = false;
        }
        if (RCKin::Length(e.lowerAxis) < 1e-6) e.lowerAxis = V3(0, 0, 1);
        if (RCKin::Length(e.axle) < 1e-6) e.axle = V3(1, 0, 0);
        if (e.steered && RCKin::Length(e.bellcrankAxis) < 1e-6) e.bellcrankAxis = V3(0, 1, 0);
        return e;
    }

    // Static wheel loads from the body's real mass and centre of mass, split front/rear by the lever
    // rule on the static wheel-centre Z. Left/right equal.
    static bool BuildParams(const Components::RCBuggy& s, const Components::RCBuggyGeometry& g, double totalMass, V3 com, double gravity, Params& P, String& error)
    {
        P.fourWD = s.fourWheelDrive != 0;
        P.totalMass = totalMass;
        P.centerOfMass = com;

        // ---- motor: wind -> kV and resistance. kV ~ 38500 / turns matches the ROAR-spec sensored
        // motors (17.5T ~ 2200, 13.5T ~ 2850, 10.5T ~ 3650 rpm/V); the phase resistance grows with
        // the square of the turns (17.5T ~ 49 mOhm). Timing advance trades torque for revs.
        double turns = Clamp(Pos(s.motorTurns, 17.5), 2.0, 40.0);
        double timing = Clamp(Fin(s.motorTiming, 0.0), 0.0, 70.0);
        double kv = 38500.0 / turns * (1.0 + 0.006 * timing);
        P.kvRad = kv * 2 * pi / 60.0;
        P.kt = 1.0 / P.kvRad;
        P.resistance = 1.6e-4 * turns * turns * (1.0 + 0.003 * timing) + Pos(s.batteryResistance, 0.006);
        P.noLoadTorque = P.kt * 2.5;                    // ~2.5 A no-load current: bearings, iron, gear mesh
        P.batteryVoltage = Pos(s.batteryVoltage, 7.6);
        P.currentLimit = Pos(s.currentLimit, 180.0);
        P.dragBrake = Clamp(Fin(s.dragBrake, 0.05), 0, 1);
        P.brakeStrength = Clamp(Fin(s.brakeStrength, 0.8), 0, 1);
        P.reverseStrength = Clamp(Fin(s.reverseStrength, 0.5), 0, 1);
        P.motorInertia = 8.0e-7;                        // 12.5 mm rotor + pinion

        // ---- gearing
        double pinion = s.pinion > 0 ? (double)s.pinion : 32.0;
        double spur = s.spur > 0 ? (double)s.spur : 69.0;
        P.gearPinionSpur = spur / pinion;
        P.gearTransmission = Pos(s.transmissionRatio, 2.6);
        P.gearTotal = P.gearPinionSpur * P.gearTransmission;
        P.slipperTorque = Fin(s.slipperTorque, 0) > 0 ? (double)s.slipperTorque : 0;

        // ---- diffs. A gear diff's oil couples its two outputs; 2.5e-8 N.m.s per cSt puts 80k on
        // the order of a tenth of a N.m at 40 rad/s of wheel speed difference -- noticeably locked
        // under power with an inside wheel unloaded, nearly free in a steady corner.
        P.diffType[0] = s.frontDiffType <= 2 ? s.frontDiffType : 0;
        P.diffType[1] = s.rearDiffType <= 2 ? s.rearDiffType : 0;
        P.diffViscous[0] = 2.5e-8 * Pos(s.frontDiffOil, 10000);
        P.diffViscous[1] = 2.5e-8 * Pos(s.rearDiffOil, 80000);
        P.ballDiffTorque = Pos(s.ballDiffTorque, 0.15);

        // ---- steering
        P.steeringThrow = Clamp(Pos(s.steeringThrow, 22.0), 1.0, 45.0) * pi / 180.0;
        P.servoRate = (pi / 3.0) / Clamp(Pos(s.servoSpeed, 0.07), 0.02, 1.0);

        // ---- aero: 1/10 buggy body + wheels ~0.016 m^2 of drag area; the wing's lift slope is a
        // flat plate's with a gurney, on a 165 x 55 mm wing.
        P.dragArea = 0.016;
        P.wingLiftArea = 0.009 * (0.45 + 0.07 * Clamp(Fin(s.wingAngle, 3.0), -5.0, 20.0));
        if (P.wingLiftArea < 0) P.wingLiftArea = 0;
        P.bodyLiftArea = 0.0015;
        V3 rearWheel = ToV3(g.rearWheelCenter);
        V3 frontWheel = ToV3(g.frontWheelCenter);
        P.wingPoint = V3(0, 0.12, rearWheel.z - 0.075);
        P.bodyLiftPoint = V3(0, 0.05, frontWheel.z - 0.02);
        P.dragPoint = V3(0, 0.05, 0.5 * (frontWheel.z + rearWheel.z));
        P.driveshaftInner = ToV3(g.driveshaftInner);
        P.rollingResistance = 0.025;
        P.tirePeakTanAlpha = tan(Clamp(Pos(s.tirePeakSlipAngle, 20.0), 3.0, 45.0) * pi / 180.0);
        P.tirePeakSlipRatio = Clamp(Pos(s.tirePeakSlipRatio, 0.15), 0.03, 1.0);

        // ---- per end
        double wheelbase = frontWheel.z - rearWheel.z;
        if (wheelbase < 0.05)
        {
            error = "the front wheel centre must be ahead (+Z) of the rear one";
            return false;
        }
        double frontShare = Clamp((com.z - rearWheel.z) / wheelbase, 0.05, 0.95);
        double chassisBottom = Fin(g.chassisBottom, 0.0223);

        for (int end = 0; end < 2; end++)
        {
            EndParams& E = P.end[end];
            bool front = end == 0;
            E.geometry = ReadEndGeometry(g, end);
            E.radius = E.geometry.wheelRadius;
            E.width = front ? Pos(g.wheelWidthFront, 0.0325) : Pos(g.wheelWidthRear, 0.0517);
            E.unsprungMass = (front ? Pos(s.unsprungMassFront, 60) : Pos(s.unsprungMassRear, 85)) * 1e-3;
            E.armInertia = 0.008 * 0.073 * 0.073 / 3.0;
            E.wheelInertia = (front ? 0.035 : 0.050) * E.radius * E.radius * 0.6;
            E.tireRate = (front ? Pos(s.tireRateFront, 6.0) : Pos(s.tireRateRear, 8.0)) * 1000.0;
            E.tireDamping = 2.0 * 0.15 * sqrt(E.tireRate * E.unsprungMass);
            E.grip = (front ? Pos(s.tireGripFront, 1.6) : Pos(s.tireGripRear, 1.8)) * Pos(s.surfaceGrip, 1.0);
            E.staticLoad = totalMass * gravity * (front ? frontShare : 1.0 - frontShare) * 0.5;

            double rideHeight = (front ? Pos(s.rideHeightFront, 13.0) : Pos(s.rideHeightRear, 13.0)) * 1e-3;
            double stroke = (front ? Pos(s.shockStrokeFront, 23.0) : Pos(s.shockStrokeRear, 29.0)) * 1e-3;
            double lengthMax = (front ? Fin(s.shockLengthFront, 0) : Fin(s.shockLengthRear, 0)) * 1e-3;

            RCKin::BuildSettings bs;
            double deflection = E.staticLoad / E.tireRate;
            // In the static pose the ground sits (chassisBottom - rideHeight) above the model origin.
            bs.wheelCenterHeight = chassisBottom - rideHeight + E.radius - deflection;
            bs.camber = (front ? Fin(s.camberFront, -2.0) : Fin(s.camberRear, -2.0)) * pi / 180.0;
            bs.toe = front ? Fin(s.toeFront, -0.5) * pi / 180.0 : 0.0;
            bs.shockLengthMax = lengthMax > 0 ? lengthMax : 0;
            bs.shockStroke = stroke;
            bs.steeringThrow = P.steeringThrow;
            E.table = RCKin::BuildCorner(E.geometry, bs);
            if (!E.table.ok)
            {
                error = std::format("{} suspension: {}", front ? "front" : "rear", E.table.error);
                return false;
            }

            // Motion ratio and static shock force from the table around the static pose.
            const RCKin::CornerTable& T = E.table;
            int ib = T.nBeta / 2;
            int it = RCKin::itStaticClamp(T, T.thetaStatic);
            int i0 = it > 0 ? it - 1 : it, i1 = it < T.nTheta - 1 ? it + 1 : it;
            double dTheta = (i1 - i0) * T.dTheta;
            double dLs = (T.At(i1, ib).shockLength - T.At(i0, ib).shockLength) / dTheta;
            double dWy = (T.At(i1, ib).wheel[1] - T.At(i0, ib).wheel[1]) / dTheta;
            E.motionRatio = fabs(dWy) > 1e-9 ? -dLs / dWy : 0.6;

            // Static balance of the arm: the spring holds the tire load through the linkage's
            // leverage. This IS the preload. (The whole tire load, unsprung weight included: the
            // step carries that weight in the Jolt body, see OnStep.)
            E.springForceStatic = fabs(dLs) > 1e-9 ? (E.staticLoad * dWy) / (-dLs) : 0;
            if (E.springForceStatic < 0) E.springForceStatic = 0;
            E.springRate = (front ? Pos(s.springRateFront, 3.5) : Pos(s.springRateRear, 2.3)) * lbPerInToNPerM;

            // Damper: laminar (Hagen-Poiseuille) flow through the piston holes plus an orifice loss.
            double bore = Pos(s.shockBore, 13.0) * 1e-3;
            double shaft = 3.0e-3;
            double areaPiston = pi * 0.25 * (bore * bore - shaft * shaft);
            double holes = (double)((front ? s.pistonHolesFront : s.pistonHolesRear) > 0 ? (front ? s.pistonHolesFront : s.pistonHolesRear) : 2);
            double hole = (front ? Pos(s.pistonHoleFront, 1.8) : Pos(s.pistonHoleRear, 1.9)) * 1e-3;
            double thickness = Pos(s.pistonThickness, 1.6) * 1e-3;
            double viscosity = (front ? Pos(s.shockOilFront, 40) : Pos(s.shockOilRear, 35)) * 12.5e-6 * oilDensity;   // Pa.s
            E.dampLinear = 128.0 * viscosity * thickness * areaPiston * areaPiston / (holes * pi * pow(hole, 4));
            double areaHoles = holes * pi * 0.25 * hole * hole * 0.8;
            E.dampQuadratic = 0.5 * oilDensity * pow(areaPiston, 3) / (areaHoles * areaHoles);

            // Anti-roll bar: torsion of a steel wire (L ~ 70 mm) through 25 mm levers, linked to the
            // arm at about half its length -- so wheel-rate = (G J / L) / lever^2 * 0.5^2.
            double wire = (front ? Fin(s.antiRollBarFront, 0) : Fin(s.antiRollBarRear, 0)) * 1e-3;
            if (wire > 0)
            {
                double J = pi * pow(wire, 4) / 32.0;
                E.arbRate = 79e9 * J / 0.07 / (0.025 * 0.025) * 0.25;
            }
            else
            {
                E.arbRate = 0;
            }
        }
        return true;
    }

    // =============================================================================================
    // The system: records per chassis entity, rebuilt on vehicleDirty, transforms written back
    // =============================================================================================
    struct Manager
    {
        static Manager* instance;
        std::unordered_map<uint, Runtime*> cars;           // key = chassis entity id
        std::unordered_set<uint> warned;
        std::unordered_map<uint, std::vector<World::Entity>> partsByChassis;
        uint frame = 0;

        Runtime* Find(World::Entity ent)
        {
            auto it = cars.find(ent.id);
            if (it == cars.end() || it->second->entityRev != ent.rev)
                return nullptr;
            return it->second;
        }

        void Remove(std::unordered_map<uint, Runtime*>::iterator it)
        {
            if (Physics::instance != nullptr)
                Physics::instance->physicsSystem.RemoveStepListener(it->second);
            delete it->second;
        }

        void Shutdown()
        {
            for (auto it = cars.begin(); it != cars.end(); ++it)
                Remove(it);
            cars.clear();
            partsByChassis.clear();
            warned.clear();
        }

        bool Build(World::Entity ent, JPH::BodyID bodyID, Runtime& rt)
        {
            uint entityId = ent.id;
            double mass = 1.5;
            V3 com(0, 0.035, 0);
            {
                JPH::BodyLockRead lock(Physics::instance->physicsSystem.GetBodyLockInterface(), bodyID);
                if (!lock.Succeeded())
                    return false;
                const JPH::Body& body = lock.GetBody();
                if (!body.IsDynamic())
                {
                    if (warned.insert(entityId).second)
                        IOs::Log("RCBuggy: entity {} -- its RigidBody must be Dynamic (motionType 2), no car built", entityId);
                    return false;
                }
                double invMass = body.GetMotionProperties()->GetInverseMass();
                if (invMass > 0) mass = 1.0 / invMass;
                com = FromJ(body.GetShape()->GetCenterOfMass());
            }
            double g = Physics::instance->physicsSystem.GetGravity().Length();
            if (g < 0.1) g = 9.81;

            Params P;
            String error;
            if (!BuildParams(ent.Get<Components::RCBuggy>(), ent.Get<Components::RCBuggyGeometry>(), mass, com, g, P, error))
            {
                if (warned.insert(entityId).second)
                    IOs::Log("RCBuggy: entity {} -- {}", entityId, error.c_str());
                return false;
            }

            bool hadState = rt.built;
            rt.P = std::move(P);
            for (int i = 0; i < 4; i++)
            {
                rt.corner[i].end = i < 2 ? 0 : 1;
                rt.corner[i].side = (i % 2 == 0) ? -1 : 1;
            }
            if (!hadState)
                rt.Reset();
            rt.body = bodyID;
            rt.entityRev = ent.rev;
            rt.built = true;
            warned.erase(entityId);

            const EndParams& F = rt.P.end[0];
            const EndParams& R = rt.P.end[1];
            IOs::Log("RCBuggy: entity {} built -- {:.0f} g, FDR {:.2f}, camber links {:.1f}/{:.1f} mm, steering links {:.1f} mm, preload {:.1f}/{:.1f} N, Ackermann {:.1f}/{:.1f} deg",
                entityId, mass * 1000.0, rt.P.gearTotal, F.table.camberLinkLength * 1000.0, R.table.camberLinkLength * 1000.0, F.table.tieRodLength * 1000.0,
                F.springForceStatic, R.springForceStatic, F.table.steerAtFullLock[0] * 180 / pi, F.table.steerAtFullLock[1] * 180 / pi);
            return true;
        }

        void Sync(World* world)
        {
            ZoneScopedN("RCBuggySync");
            if (Physics::instance == nullptr)
                return;
            frame++;

            partsByChassis.clear();
            uint partQuery = world->Query(Components::RCBuggyPart::mask, 0, true);
            auto& partResult = world->frameQueries[partQuery];
            for (uint i = 0; i < (uint)partResult.size(); i++)
            {
                World::Entity part = partResult[i];
                if (!part.Has<Components::Parent>())
                    continue;
                World::Entity chassis = World::Entity(part.Get<Components::Parent>().entity);
                if (chassis.id == entityInvalid.id || !chassis.IsValid())
                    continue;
                partsByChassis[chassis.id].push_back(part);
            }

            std::unordered_set<uint> seen;
            uint query = world->Query(Components::RCBuggy::mask | Components::RCBuggyGeometry::mask, 0, true);
            auto& result = world->frameQueries[query];
            for (uint i = 0; i < (uint)result.size(); i++)
            {
                World::Entity ent = result[i];
                seen.insert(ent.id);

                auto bodyRecord = Physics::instance->bodies.find(ent.id);
                bool hasBody = bodyRecord != Physics::instance->bodies.end() && bodyRecord->second.rev == ent.rev;
                bool dirty = ent.Has<Components::State>() && (ent.Get<Components::State>().flags & Components::State::Flags::vehicleDirty);

                auto it = cars.find(ent.id);
                if (it != cars.end() && (it->second->entityRev != ent.rev || !hasBody || it->second->body != bodyRecord->second.bodyID))
                {
                    Remove(it);
                    cars.erase(it);
                    it = cars.end();
                }
                if (!hasBody)
                    continue;

                Runtime* rt = nullptr;
                if (it == cars.end())
                {
                    rt = new Runtime();
                    if (!Build(ent, bodyRecord->second.bodyID, *rt))
                    {
                        delete rt;
                        if (ent.Has<Components::State>())
                            ent.Get<Components::State>().flags.Unset(Components::State::Flags::vehicleDirty);
                        continue;
                    }
                    cars[ent.id] = rt;
                    Physics::instance->physicsSystem.AddStepListener(rt);
                }
                else
                {
                    rt = it->second;
                    if (dirty)
                        Build(ent, bodyRecord->second.bodyID, *rt);     // keeps the running state
                }
                if (ent.Has<Components::State>())
                    ent.Get<Components::State>().flags.Unset(Components::State::Flags::vehicleDirty);

                // Inputs in (the step is not running while systems and this sync do).
                auto& setup = ent.Get<Components::RCBuggy>();
                rt->throttle = Clamp(Fin(setup.throttleInput, 0), -1, 1);
                rt->steering = Clamp(Fin(setup.steeringInput, 0), -1, 1);
                // A sleeping body's listener still runs, but OnStep skips inactive bodies -- and the
                // listener does not run at all while nothing in the world is active.
                if (rt->throttle != 0 || rt->steering != 0)
                    Physics::instance->physicsSystem.GetBodyInterface().ActivateBody(rt->body);

                WriteParts(ent, *rt);
            }

            for (auto it = cars.begin(); it != cars.end();)
            {
                if (!seen.contains(it->first))
                {
                    Remove(it);
                    it = cars.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // ---- output transforms -------------------------------------------------------------------
        static void SetTransform(World::Entity e, const Q& q, const V3& t)
        {
            if (!e.Has<Components::Transform>())
                return;
            auto& transform = e.Get<Components::Transform>();
            float3 position = float3((float)t.x, (float)t.y, (float)t.z);
            quaternion rotation = quaternion((float)q.x, (float)q.y, (float)q.z, (float)q.w);
            float cur[3], nxt[3];
            store(transform.position, cur);
            store(position, nxt);
            bool changed = false;
            for (uint a = 0; a < 3; a++)
                changed |= !isfinite(cur[a]) || fabsf(cur[a] - nxt[a]) > 1e-6f;
            for (uint k = 0; k < 4; k++)
                changed |= !isfinite(transform.rotation.f32[k]) || fabsf(transform.rotation.f32[k] - rotation.f32[k]) > 1e-6f;
            if (!changed)
                return;
            transform.position = position;
            transform.rotation = rotation;
            transform.scale = float3(1, 1, 1);
            transform.version++;
        }

        // A link drawn between a fixed and a moving point: rotate the design segment onto the
        // current one about its first end. Rigid (no stretch) -- the turnbuckle's solved length can
        // differ a little from the CAD's, which shows as a small gap at the far ball.
        static void Aim(V3 designA, V3 designB, V3 nowA, V3 nowB, Q& q, V3& t)
        {
            q = RCKin::FromTo(RCKin::Normalize(designB - designA), RCKin::Normalize(nowB - nowA));
            t = nowA - q.Rotate(designA);
        }

        void WriteParts(World::Entity ent, Runtime& rt)
        {
            auto found = partsByChassis.find(ent.id);
            if (found == partsByChassis.end())
                return;
            for (World::Entity part : found->second)
            {
                const auto& info = part.Get<Components::RCBuggyPart>();
                uint ci = info.corner < 4 ? info.corner : 0;
                const Corner& c = rt.corner[ci];
                const EndParams& E = rt.P.end[c.end];
                const RCKin::EndGeometry& G = E.geometry;
                int side = c.side;
                auto S = [&](const V3& v) { return side > 0 ? v : RCKin::MirrorX(v); };

                Lookup L;
                ReadSide(E.table, c.theta, rt.beta, side, L);

                Q q; V3 t;
                switch (info.part)
                {
                case 0:     // wheel: spin about its own design axle, then the steering block's pose
                {
                    V3 wheelDesign = S(G.wheelCenter);
                    V3 axleDesign = RCKin::Normalize(S(G.axle));        // outward on this side
                    Q spin = Q::AxisAngle(axleDesign * (double)side, c.spin);   // + about the chassis' right = rolling forward
                    q = (L.knuckleQ * spin).Normalized();
                    t = L.knuckleQ.Rotate(wheelDesign - spin.Rotate(wheelDesign)) + L.knuckleT;
                    break;
                }
                case 1: q = L.armQ; t = L.armT; break;
                case 2: q = L.carrierQ; t = L.carrierT; break;
                case 3: q = L.knuckleQ; t = L.knuckleT; break;
                case 4: Aim(S(G.upperInner), S(G.upperOuter), S(G.upperInner), L.upperOuter, q, t); break;
                case 5:
                    if (!G.steered) { q = Q(); t = V3(); break; }
                    Aim(S(G.tieInner), S(G.tieOuter), L.tieInner, L.tieOuter, q, t);
                    break;
                case 6: Aim(S(G.shockTop), S(G.shockBottom), S(G.shockTop), L.shockBottom, q, t); break;
                case 9: Aim(S(G.shockBottom), S(G.shockTop), L.shockBottom, S(G.shockTop), q, t); break;
                case 7:
                {
                    V3 inner = S(rt.P.driveshaftInner);
                    if (c.end == 0) inner.z = -inner.z;     // the front gearbox, mirrored fore/aft
                    Aim(inner, S(G.wheelCenter), inner, L.wheel, q, t);
                    break;
                }
                case 8:
                {
                    if (!G.steered) { q = Q(); t = V3(); break; }
                    double angle = (side > 0 ? rt.beta : -rt.beta) * E.table.bellcrankSign;
                    Q r = Q::AxisAngle(RCKin::Normalize(G.bellcrankAxis), angle);
                    if (side < 0) r = r.MirrorX();
                    V3 pivot = S(G.bellcrankPivot);
                    q = r;
                    t = pivot - r.Rotate(pivot);
                    break;
                }
                default: q = Q(); t = V3(); break;
                }
                SetTransform(part, q, t);
            }
        }
    };
    Manager* Manager::instance = nullptr;
}

// Forward-declared in Physic.h: Physics::SyncAndReadback and Physics::Off call these.
static RCBuggySim::Manager rcBuggyManager;
void RCBuggySync(World* world) { rcBuggyManager.Sync(world); }
void RCBuggyShutdown() { rcBuggyManager.Shutdown(); }

World::Entity RCBuggyChassisOf(World::Entity ent)
{
    if (!ent.IsValid())
        return World::Entity();
    if (ent.Has<Components::RCBuggy>())
        return ent;
    if (ent.Has<Components::RCBuggyPart>() && ent.Has<Components::Parent>())
    {
        World::Entity chassis = World::Entity(ent.Get<Components::Parent>().entity);
        if (chassis.id != entityInvalid.id && chassis.IsValid() && chassis.Has<Components::RCBuggy>())
            return chassis;
    }
    return World::Entity();
}

World::Entity RCBuggyFirst(World* world)
{
    uint query = world->Query(Components::RCBuggy::mask, 0, true);
    auto& result = world->frameQueries[query];
    return result.empty() ? World::Entity() : result[0];
}

void RCBuggySetInput(World::Entity chassis, float throttle, float steering)
{
    if (!chassis.IsValid() || !chassis.Has<Components::RCBuggy>())
        return;
    auto& setup = chassis.Get<Components::RCBuggy>();
    setup.throttleInput = throttle;
    setup.steeringInput = steering;
}

namespace Components
{
    static void RCBuggyPropertyDraw(char* p)
    {
        DefaultPropertyDraw(RCBuggy::mask, p);

        ImGui::Spacing();
        ImGui::SeparatorText("derived");

        const RCBuggy& s = *(const RCBuggy*)p;
        double pinion = s.pinion > 0 ? (double)s.pinion : 32.0;
        double spur = s.spur > 0 ? (double)s.spur : 69.0;
        double fdr = spur / pinion * (s.transmissionRatio > 0 ? s.transmissionRatio : 2.6);
        double turns = s.motorTurns > 0 ? s.motorTurns : 17.5;
        double kv = 38500.0 / turns * (1.0 + 0.006 * (isfinite(s.motorTiming) ? s.motorTiming : 0));
        ImGui::Text("FDR %.2f, motor ~%.0f kV", fdr, kv);

        World::Entity ent = editorState.selectedObject;
        RCBuggySim::Runtime* rt = ent.IsValid() ? rcBuggyManager.Find(ent) : nullptr;
        if (rt == nullptr || !rt->built)
        {
            ImGui::TextDisabled("no car built");
            ImGui::SetItemTooltip("Needs Transform, a Dynamic RigidBody, a CollisionShape, RCBuggy and RCBuggyGeometry\non the same entity. The log says what failed.");
            return;
        }
        const RCBuggySim::Params& P = rt->P;
        double rollout = 2 * RCBuggySim::pi * P.end[1].radius / P.gearTotal * 1000.0;
        double topSpeed = P.batteryVoltage * P.kvRad / P.gearTotal * P.end[1].radius * 3.6;
        ImGui::Text("rollout %.1f mm/motor rev, no-load top speed %.0f km/h", rollout, topSpeed);
        for (int end = 0; end < 2; end++)
        {
            const RCBuggySim::EndParams& E = P.end[end];
            double wheelRate = E.springRate * E.motionRatio * E.motionRatio;
            double sprung = E.staticLoad / 9.81 - E.unsprungMass;
            double freq = sprung > 0 ? sqrt(wheelRate / sprung) / (2 * RCBuggySim::pi) : 0;
            double zeta = sprung > 0 ? E.dampLinear * E.motionRatio * E.motionRatio / (2 * sqrt(wheelRate * sprung)) : 0;
            ImGui::Text("%s: camber link %.1f mm%s, motion ratio %.2f, preload %.1f N, %.1f Hz, damping %.2f",
                end == 0 ? "front" : "rear", E.table.camberLinkLength * 1000.0,
                end == 0 ? std::format(", steering link {:.1f} mm", E.table.tieRodLength * 1000.0).c_str() : "",
                E.motionRatio, E.springForceStatic, freq, zeta);
        }
        ImGui::Text("full lock: inside %.1f deg, outside %.1f deg", P.end[0].table.steerAtFullLock[0] * 180 / RCBuggySim::pi, -P.end[0].table.steerAtFullLock[1] * 180 / RCBuggySim::pi);

        ImGui::SeparatorText("live");
        ImGui::Text("%.1f km/h, motor %.0f rpm, %.0f A%s", rt->forwardSpeed * 3.6f, rt->motorRPM, rt->motorCurrent, rt->slipping ? ", slipper slipping" : "");
        const char* names[4] = { "FL", "FR", "RL", "RR" };
        for (int i = 0; i < 4; i++)
        {
            const RCBuggySim::Corner& c = rt->corner[i];
            ImGui::Text("%s %s %5.1f N  travel %+5.1f mm  camber %+5.1f  slip %+5.2f / %+5.1f deg", names[i], c.contact ? "on " : "air",
                c.load, c.travel, c.camber, c.slipRatio, c.slipAngle * 180.0f / 3.14159265f);
        }
    }
}

// =================================================================================================
// "RC Setup" window: the setup sheet as a configurator
// =================================================================================================
// Edits the selected buggy (or the first one) with the choices a racer actually has -- spring
// colours, oil weights, piston holes, motor winds, pinion/spur -- and shows what the geometry does
// with them: the corner drawn from the front and the side at its live pose with the roll centre /
// anti-squat construction, the camber, bump-steer and motion-ratio curves over the travel, and
// the four corner loads while driving. Every edit raises vehicleDirty, which rebuilds the tables
// without resetting the running car.
void MakeRCBuggy(bool fourWD);

class RCSetupWindow : public EditorWindow
{
public:
    RCSetupWindow() : EditorWindow("RC Setup") {}

    int viewEnd = 0;        // 0 front, 1 rear, for the diagrams

    static bool Combo(const char* label, float& value, const float* options, const char* const* names, int count, const char* fmt = "%.1f")
    {
        int current = -1;
        for (int i = 0; i < count; i++)
            if (fabsf(options[i] - value) < 1e-4f * (fabsf(options[i]) + 1.0f)) current = i;
        char preview[64];
        if (current >= 0 && names != nullptr) snprintf(preview, sizeof(preview), "%s", names[current]);
        else snprintf(preview, sizeof(preview), fmt, value);
        bool changed = false;
        if (ImGui::BeginCombo(label, preview))
        {
            for (int i = 0; i < count; i++)
            {
                char item[64];
                if (names != nullptr) snprintf(item, sizeof(item), "%s", names[i]);
                else snprintf(item, sizeof(item), fmt, options[i]);
                if (ImGui::Selectable(item, i == current))
                {
                    value = options[i];
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    static bool ComboU(const char* label, uint& value, const char* const* names, int count)
    {
        int v = (int)value;
        bool changed = ImGui::Combo(label, &v, names, count);
        if (changed) value = (uint)v;
        return changed;
    }

    static bool SliderStep(const char* label, float& v, float lo, float hi, float step, const char* fmt)
    {
        bool changed = ImGui::SliderFloat(label, &v, lo, hi, fmt);
        if (changed && step > 0) v = roundf(v / step) * step;
        return changed;
    }

    // ---- one end's suspension --------------------------------------------------------------------
    static bool EndSection(Components::RCBuggy& s, bool front)
    {
        static const float springRates[] = { 1.8f, 2.0f, 2.1f, 2.2f, 2.3f, 2.4f, 2.6f, 2.8f, 3.0f, 3.2f, 3.4f, 3.5f, 3.6f, 3.8f, 4.0f, 4.2f, 4.5f };
        static const float oils[] = { 20, 22.5f, 25, 27.5f, 30, 32.5f, 35, 37.5f, 40, 42.5f, 45, 50, 55, 60 };
        static const float holes[] = { 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };
        static const float bars[] = { 0.0f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.8f, 2.0f };
        static const char* barNames[] = { "none", "0.9 mm", "1.0 mm", "1.1 mm", "1.2 mm", "1.3 mm", "1.4 mm", "1.5 mm", "1.6 mm", "1.8 mm", "2.0 mm" };
        static const char* holeCounts[] = { "1 hole", "2 holes", "3 holes", "4 holes", "5 holes", "6 holes" };

        bool c = false;
        ImGui::PushID(front ? "front" : "rear");
        float& ride = front ? s.rideHeightFront : s.rideHeightRear;
        float& camber = front ? s.camberFront : s.camberRear;
        c |= SliderStep("ride height", ride, 8.0f, 30.0f, 0.5f, "%.1f mm");
        ImGui::SetItemTooltip("Chassis underside to the ground. Spring preload is solved to hold it, like setting the collars.");
        c |= SliderStep("camber", camber, -5.0f, 1.0f, 0.25f, "%.2f deg");
        ImGui::SetItemTooltip("Static camber. Sets the camber link (turnbuckle) length.");
        if (front)
        {
            c |= SliderStep("toe", s.toeFront, -3.0f, 3.0f, 0.25f, "%.2f deg");
            ImGui::SetItemTooltip("Negative = toe out. Sets the steering link length.");
        }

        ImGui::SeparatorText("shock");
        float& rate = front ? s.springRateFront : s.springRateRear;
        float& oil = front ? s.shockOilFront : s.shockOilRear;
        uint& holeCount = front ? s.pistonHolesFront : s.pistonHolesRear;
        float& hole = front ? s.pistonHoleFront : s.pistonHoleRear;
        float& stroke = front ? s.shockStrokeFront : s.shockStrokeRear;
        float& length = front ? s.shockLengthFront : s.shockLengthRear;
        float& bar = front ? s.antiRollBarFront : s.antiRollBarRear;
        c |= Combo("spring", rate, springRates, nullptr, IM_ARRAYSIZE(springRates), "%.1f lb/in");
        c |= Combo("oil", oil, oils, nullptr, IM_ARRAYSIZE(oils), "%.1f wt");
        uint holeIndex = holeCount > 0 ? holeCount - 1 : 1;
        if (ComboU("piston", holeIndex, holeCounts, IM_ARRAYSIZE(holeCounts))) { holeCount = holeIndex + 1; c = true; }
        c |= Combo("hole size", hole, holes, nullptr, IM_ARRAYSIZE(holes), "%.1f mm");
        c |= SliderStep("stroke", stroke, 15.0f, 35.0f, 0.5f, "%.1f mm");
        c |= SliderStep("length (0 = auto)", length, 0.0f, 100.0f, 0.5f, length > 0 ? "%.1f mm" : "auto");
        ImGui::SetItemTooltip("Eye to eye at full extension, after the internal limiters -- this sets the droop.\nAuto puts 35% of the stroke below the ride height.");
        c |= Combo("anti-roll bar", bar, bars, barNames, IM_ARRAYSIZE(bars));
        ImGui::PopID();
        return c;
    }

    // ---- diagrams --------------------------------------------------------------------------------
    struct Canvas
    {
        ImDrawList* dl;
        ImVec2 origin;      // screen position of the model point (u0, v0)
        float scale;        // px per metre
        float u0, v0;
        ImVec2 P(double u, double v) const { return ImVec2(origin.x + (float)((u - u0) * scale), origin.y - (float)((v - v0) * scale)); }
    };

    static ImVec2 Intersect(ImVec2 a0, ImVec2 a1, ImVec2 b0, ImVec2 b1, bool& ok)
    {
        float d = (a1.x - a0.x) * (b1.y - b0.y) - (a1.y - a0.y) * (b1.x - b0.x);
        ok = fabsf(d) > 1e-6f;
        if (!ok) return a0;
        float t = ((b0.x - a0.x) * (b1.y - b0.y) - (b0.y - a0.y) * (b1.x - b0.x)) / d;
        return ImVec2(a0.x + t * (a1.x - a0.x), a0.y + t * (a1.y - a0.y));
    }

    // Front view (X, Y) of one corner, right side, with the instant centre and roll centre drawn by
    // construction: extend the arm and camber link to their crossing, aim the contact patch at it,
    // and where that line crosses the car's centreline is the roll centre.
    void DrawFront(const RCBuggySim::Runtime& rt, int end, int cornerIndex)
    {
        using namespace RCBuggySim;
        const EndParams& E = rt.P.end[end];
        const RCKin::EndGeometry& G = E.geometry;
        const Corner& corner = rt.corner[cornerIndex];
        Lookup L;
        Read(E.table, corner.theta, corner.side > 0 ? rt.beta : -rt.beta, L);

        ImVec2 size(ImGui::GetContentRegionAvail().x * 0.5f - 4, 210);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("front", size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(25, 27, 32, 255));
        dl->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);

        Canvas cv;
        cv.dl = dl;
        cv.scale = size.x / 0.15f;
        cv.u0 = 0.0f; cv.v0 = 0.0f;
        cv.origin = ImVec2(p0.x + 12, p0.y + size.y - 30);

        // Ground in the chassis frame at the current pose: under the tire, along the chassis X.
        double groundY = L.wheel.y - E.radius;
        V3 lowerOuter = L.carrierQ.Rotate(G.lowerOuter) + L.carrierT;
        V3 contact(L.wheel.x - L.axle.y * E.radius, groundY, L.wheel.z);
        ImU32 grey = IM_COL32(120, 120, 130, 255), white = IM_COL32(230, 230, 230, 255), accent = IM_COL32(255, 170, 60, 255), cyan = IM_COL32(90, 200, 255, 255);

        dl->AddLine(cv.P(-0.02, groundY), cv.P(0.15, groundY), grey, 1.0f);
        // chassis plate, just under the inner hinge pins
        dl->AddLine(cv.P(0.0, G.lowerInner.y - 0.008), cv.P(0.058, G.lowerInner.y - 0.008), white, 3.0f);
        dl->AddLine(cv.P(0.0, -1), cv.P(0.0, 1), IM_COL32(70, 70, 80, 255), 1.0f);

        // wheel: a rectangle at the axle's camber
        {
            double c = L.axle.x, s = L.axle.y;      // axle direction in X/Y
            double r = E.radius, hw = 0.5 * E.width;
            V3 w = L.wheel;
            auto corner2 = [&](double a, double b) { return cv.P(w.x + c * a - s * b, w.y + s * a + c * b); };
            ImVec2 q[4] = { corner2(-hw, -r), corner2(hw, -r), corner2(hw, r), corner2(-hw, r) };
            dl->AddPolyline(q, 4, IM_COL32(160, 160, 160, 255), ImDrawFlags_Closed, 2.0f);
        }
        dl->AddLine(cv.P(G.lowerInner.x, G.lowerInner.y), cv.P(lowerOuter.x, lowerOuter.y), white, 3.0f);
        dl->AddLine(cv.P(G.upperInner.x, G.upperInner.y), cv.P(L.upperOuter.x, L.upperOuter.y), cyan, 2.0f);
        dl->AddLine(cv.P(G.shockTop.x, G.shockTop.y), cv.P(L.shockBottom.x, L.shockBottom.y), accent, 4.0f);
        dl->AddLine(cv.P(lowerOuter.x, lowerOuter.y), cv.P(L.upperOuter.x, L.upperOuter.y), IM_COL32(200, 200, 200, 255), 2.0f);

        bool ok1, ok2;
        ImVec2 ic = Intersect(cv.P(G.lowerInner.x, G.lowerInner.y), cv.P(lowerOuter.x, lowerOuter.y), cv.P(G.upperInner.x, G.upperInner.y), cv.P(L.upperOuter.x, L.upperOuter.y), ok1);
        ImVec2 patch = cv.P(contact.x, contact.y);
        double rollCenter = 0;
        if (ok1)
        {
            ImVec2 rc = Intersect(patch, ic, cv.P(0, -1), cv.P(0, 1), ok2);
            dl->AddLine(patch, ic, IM_COL32(90, 255, 120, 120), 1.0f);
            if (ok2)
            {
                dl->AddCircleFilled(rc, 4, IM_COL32(90, 255, 120, 255));
                rollCenter = (cv.origin.y - rc.y) / cv.scale + cv.v0 - groundY;
            }
        }
        dl->PopClipRect();
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), white, std::format("{} -- front view   roll centre {:+.1f} mm", end == 0 ? "front" : "rear", rollCenter * 1000.0).c_str());
    }

    // Side view (Z, Y): the arm's hinge axis (kick-up / anti-squat), and the wheel's path.
    void DrawSide(const RCBuggySim::Runtime& rt, int end, int cornerIndex)
    {
        using namespace RCBuggySim;
        const EndParams& E = rt.P.end[end];
        const RCKin::EndGeometry& G = E.geometry;
        const Corner& corner = rt.corner[cornerIndex];
        const RCKin::CornerTable& T = E.table;

        ImVec2 size(ImGui::GetContentRegionAvail().x - 2, 210);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("side", size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(25, 27, 32, 255));
        dl->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);

        Lookup L;
        Read(T, corner.theta, corner.side > 0 ? rt.beta : -rt.beta, L);
        Canvas cv;
        cv.dl = dl;
        cv.scale = size.y / 0.11f;
        cv.u0 = (float)L.wheel.z; cv.v0 = 0.0f;
        cv.origin = ImVec2(p0.x + size.x * 0.5f, p0.y + size.y - 30);
        auto P = [&](double z, double y) { return ImVec2(cv.origin.x - (float)((z - cv.u0) * cv.scale), cv.origin.y - (float)((y - cv.v0) * cv.scale)); };   // forward = left

        ImU32 white = IM_COL32(230, 230, 230, 255), grey = IM_COL32(120, 120, 130, 255);
        double groundY = L.wheel.y - E.radius;
        dl->AddLine(P(L.wheel.z + 0.2, groundY), P(L.wheel.z - 0.2, groundY), grey, 1.0f);
        dl->AddCircle(P(L.wheel.z, L.wheel.y), (float)(E.radius * cv.scale), IM_COL32(160, 160, 160, 255), 48, 2.0f);
        V3 a = RCKin::Normalize(G.lowerAxis);
        V3 i0 = G.lowerInner - a * 0.03, i1 = G.lowerInner + a * 0.03;
        dl->AddLine(P(i0.z, i0.y), P(i1.z, i1.y), white, 3.0f);

        // wheel-centre path over the whole travel, static pose marked
        int ib = T.nBeta / 2;
        for (int it = 0; it + 1 < T.nTheta; it++)
        {
            const RCKin::Sample& s0 = T.At(it, ib);
            const RCKin::Sample& s1 = T.At(it + 1, ib);
            dl->AddLine(P(s0.wheel[2], s0.wheel[1]), P(s1.wheel[2], s1.wheel[1]), IM_COL32(255, 170, 60, 255), 2.0f);
        }
        const RCKin::Sample& st = T.At(RCKin::itStaticClamp(T, T.thetaStatic), ib);
        dl->AddCircleFilled(P(st.wheel[2], st.wheel[1]), 3, IM_COL32(90, 255, 120, 255));

        double pathAngle = 0;
        {
            int is = RCKin::itStaticClamp(T, T.thetaStatic);
            int j0 = is > 0 ? is - 1 : is, j1 = is < T.nTheta - 1 ? is + 1 : is;
            double dz = T.At(j1, ib).wheel[2] - T.At(j0, ib).wheel[2];
            double dy = T.At(j1, ib).wheel[1] - T.At(j0, ib).wheel[1];
            pathAngle = atan2(dz, dy) * 180 / RCBuggySim::pi;    // + = wheel moves forward in bump
        }
        double axisAngle = atan2(a.y, fabs(a.z)) * 180 / RCBuggySim::pi;
        dl->PopClipRect();
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), white, std::format("side view (forward = left)   hinge pin {:.1f} deg   wheel path {:+.1f} deg from vertical", axisAngle, pathAngle).c_str());
    }

    void Curves(const RCBuggySim::Runtime& rt, int end)
    {
        const RCKin::CornerTable& T = rt.P.end[end].table;
        int ib = T.nBeta / 2;
        float camber[96], toe[96], ratio[96];
        int n = T.nTheta < 96 ? T.nTheta : 96;
        for (int it = 0; it < n; it++)
        {
            const RCKin::Sample& s = T.At(it, ib);
            camber[it] = (float)(asin(RCBuggySim::Clamp(s.axle[1], -1, 1)) * 180 / RCBuggySim::pi);
            toe[it] = (float)(atan2(s.axle[2], sqrt(s.axle[0] * s.axle[0] + s.axle[1] * s.axle[1])) * 180 / RCBuggySim::pi);
            int a = it > 0 ? it - 1 : it, b = it < T.nTheta - 1 ? it + 1 : it;
            double dy = T.At(b, ib).wheel[1] - T.At(a, ib).wheel[1];
            ratio[it] = fabs(dy) > 1e-9 ? (float)(-(T.At(b, ib).shockLength - T.At(a, ib).shockLength) / dy) : 0.0f;
        }
        float w = ImGui::GetContentRegionAvail().x / 3.0f - 6;
        ImGui::PlotLines("##camber", camber, n, 0, std::format("camber {:.1f}..{:.1f} deg", camber[0], camber[n - 1]).c_str(), FLT_MAX, FLT_MAX, ImVec2(w, 70));
        ImGui::SetItemTooltip("Camber over the travel, droop (left) to bump (right).");
        ImGui::SameLine();
        ImGui::PlotLines("##toe", toe, n, 0, std::format("toe {:+.2f}..{:+.2f} deg", toe[0], toe[n - 1]).c_str(), FLT_MAX, FLT_MAX, ImVec2(w, 70));
        ImGui::SetItemTooltip("Toe over the travel (bump steer), steering centred. + = toe in.");
        ImGui::SameLine();
        ImGui::PlotLines("##ratio", ratio, n, 0, std::format("motion ratio {:.2f}..{:.2f}", ratio[0], ratio[n - 1]).c_str(), FLT_MAX, FLT_MAX, ImVec2(w, 70));
        ImGui::SetItemTooltip("Shock travel per wheel travel. Rising = progressive.");
    }

    // Top view of the four corners: load as a bar, slip as a colour.
    void Loads(const RCBuggySim::Runtime& rt)
    {
        ImVec2 size(ImGui::GetContentRegionAvail().x, 150);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("loads", size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(25, 27, 32, 255));
        const char* names[4] = { "FL", "FR", "RL", "RR" };
        double total = rt.P.totalMass * 9.81;
        for (int i = 0; i < 4; i++)
        {
            const RCBuggySim::Corner& c = rt.corner[i];
            float cx = p0.x + size.x * (c.side < 0 ? 0.3f : 0.7f);
            float cy = p0.y + (i < 2 ? 40.0f : 110.0f);
            float share = total > 0 ? (float)(c.load / total) : 0.0f;
            float slip = fminf(1.0f, fabsf(c.slipRatio) / 0.2f + fabsf(c.slipAngle) / 0.25f);
            ImU32 col = c.contact ? IM_COL32((int)(80 + 175 * slip), (int)(220 - 120 * slip), 90, 255) : IM_COL32(90, 90, 90, 255);
            float h = 60.0f * fminf(share / 0.5f, 1.0f);
            dl->AddRectFilled(ImVec2(cx - 14, cy + 25 - h), ImVec2(cx + 14, cy + 25), col);
            dl->AddRect(ImVec2(cx - 14, cy - 35), ImVec2(cx + 14, cy + 25), IM_COL32(120, 120, 130, 255));
            dl->AddText(ImVec2(cx + 20, cy - 20), IM_COL32(230, 230, 230, 255), std::format("{} {:.0f}%\n{:+.0f} mm", names[i], share * 100.0f, c.travel).c_str());
        }
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), IM_COL32(230, 230, 230, 255), std::format("{:.1f} km/h  {:.0f} rpm  {:.0f} A", rt.forwardSpeed * 3.6f, rt.motorRPM, rt.motorCurrent).c_str());
    }

    void Update() override final
    {
        ZoneScoped;
        if (!ImGui::Begin("RC Setup", &isOpen, ImGuiWindowFlags_None))
        {
            ImGui::End();
            return;
        }

        World::Entity car = RCBuggyChassisOf(editorState.selectedObject);
        if (!car.IsValid() && World::instance != nullptr)
            car = RCBuggyFirst(World::instance);
        if (!car.IsValid())
        {
            ImGui::TextDisabled("No RC buggy in the scene. Edit > New Entity > RC Buggy 1/10 ...");
            ImGui::End();
            return;
        }

        Components::RCBuggy& s = car.Get<Components::RCBuggy>();
        RCBuggySim::Runtime* rt = rcBuggyManager.Find(car);
        bool changed = false;

        ImGui::Text("%s", car.Has<Components::Name>() ? car.Get<Components::Name>().name : "RC buggy");
        ImGui::SameLine();
        if (ImGui::Button("B7.1 sheet"))
        {
            Components::RCBuggy fresh;
            fresh.throttleInput = s.throttleInput;
            fresh.steeringInput = s.steeringInput;
            fresh.fourWheelDrive = s.fourWheelDrive;
            s = fresh;
            changed = true;
        }
        ImGui::SetItemTooltip("Back to Brady Carrico's RC10B7.1 sheet (2026 JConcepts Indoor Nationals, RC One).");

        float columnWidth = 330.0f;
        if (ImGui::BeginTable("rcsetup", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("setup", ImGuiTableColumnFlags_WidthFixed, columnWidth);
            ImGui::TableSetupColumn("view", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushItemWidth(170);

            if (ImGui::BeginTabBar("tabs"))
            {
                if (ImGui::BeginTabItem("Front")) { changed |= EndSection(s, true); viewEnd = 0; ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Rear")) { changed |= EndSection(s, false); viewEnd = 1; ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Drivetrain"))
                {
                    static const char* layouts[] = { "2WD (B7.1)", "4WD (B84.1)" };
                    changed |= ComboU("layout", s.fourWheelDrive, layouts, 2);
                    int pinion = (int)s.pinion, spur = (int)s.spur;
                    if (ImGui::SliderInt("pinion", &pinion, 15, 45)) { s.pinion = (uint)pinion; changed = true; }
                    if (ImGui::SliderInt("spur", &spur, 60, 90)) { s.spur = (uint)spur; changed = true; }
                    static const float ratios[] = { 2.43f, 2.6f, 2.72f };
                    static const char* ratioNames[] = { "2.43 (laydown)", "2.60 (standard)", "2.72 (4WD)" };
                    changed |= Combo("transmission", s.transmissionRatio, ratios, ratioNames, 3);
                    changed |= SliderStep("slipper (0 = locked)", s.slipperTorque, 0.0f, 2.0f, 0.05f, s.slipperTorque > 0 ? "%.2f N.m" : "eliminator");
                    ImGui::SeparatorText("diffs");
                    static const char* diffTypes[] = { "gear (oil)", "ball", "spool" };
                    static const float diffOils[] = { 1000, 2000, 3000, 5000, 7000, 10000, 15000, 20000, 30000, 50000, 60000, 80000, 100000, 200000, 500000 };
                    changed |= ComboU("rear diff", s.rearDiffType, diffTypes, 3);
                    if (s.rearDiffType == 0) changed |= Combo("rear diff oil", s.rearDiffOil, diffOils, nullptr, IM_ARRAYSIZE(diffOils), "%.0f cSt");
                    if (s.fourWheelDrive)
                    {
                        changed |= ComboU("front diff", s.frontDiffType, diffTypes, 3);
                        if (s.frontDiffType == 0) changed |= Combo("front diff oil", s.frontDiffOil, diffOils, nullptr, IM_ARRAYSIZE(diffOils), "%.0f cSt");
                    }
                    if (s.rearDiffType == 1 || (s.fourWheelDrive && s.frontDiffType == 1))
                        changed |= SliderStep("ball diff", s.ballDiffTorque, 0.02f, 1.0f, 0.01f, "%.2f N.m");
                    double fdr = (s.pinion > 0 ? (double)s.spur / s.pinion : 0) * s.transmissionRatio;
                    ImGui::Text("FDR %.2f : 1", fdr);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Electronics"))
                {
                    static const float winds[] = { 25.5f, 21.5f, 17.5f, 13.5f, 10.5f, 8.5f, 7.5f, 6.5f, 5.5f, 4.5f };
                    changed |= Combo("motor", s.motorTurns, winds, nullptr, IM_ARRAYSIZE(winds), "%.1fT");
                    ImGui::SetItemTooltip("ROAR sensored brushless wind. kV ~ 38500 / turns, resistance ~ turns^2.");
                    changed |= SliderStep("timing", s.motorTiming, 0.0f, 60.0f, 1.0f, s.motorTiming > 0 ? "%.0f deg" : "blinky");
                    changed |= SliderStep("battery", s.batteryVoltage, 6.6f, 8.4f, 0.05f, "%.2f V");
                    changed |= SliderStep("current limit", s.currentLimit, 40.0f, 300.0f, 5.0f, "%.0f A");
                    changed |= SliderStep("drag brake", s.dragBrake, 0.0f, 1.0f, 0.01f, "%.2f");
                    changed |= SliderStep("brake", s.brakeStrength, 0.0f, 1.0f, 0.01f, "%.2f");
                    changed |= SliderStep("reverse", s.reverseStrength, 0.0f, 1.0f, 0.05f, "%.2f");
                    ImGui::SeparatorText("steering");
                    changed |= SliderStep("throw (EPA)", s.steeringThrow, 10.0f, 35.0f, 0.5f, "%.1f deg");
                    changed |= SliderStep("servo speed", s.servoSpeed, 0.03f, 0.2f, 0.005f, "%.3f s/60");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Tires & body"))
                {
                    static const float surfaces[] = { 1.0f, 0.9f, 0.8f, 0.65f, 0.5f };
                    static const char* surfaceNames[] = { "carpet (CRC black)", "astro turf", "high-grip clay", "loose dirt", "dusty / cold" };
                    changed |= Combo("surface", s.surfaceGrip, surfaces, surfaceNames, IM_ARRAYSIZE(surfaces));
                    changed |= SliderStep("front grip", s.tireGripFront, 0.6f, 2.5f, 0.05f, "%.2f");
                    changed |= SliderStep("rear grip", s.tireGripRear, 0.6f, 2.5f, 0.05f, "%.2f");
                    changed |= SliderStep("front tire rate", s.tireRateFront, 2.0f, 15.0f, 0.5f, "%.1f N/mm");
                    changed |= SliderStep("rear tire rate", s.tireRateRear, 2.0f, 15.0f, 0.5f, "%.1f N/mm");
                    changed |= SliderStep("peak slip angle", s.tirePeakSlipAngle, 5.0f, 30.0f, 0.5f, "%.1f deg");
                    ImGui::SetItemTooltip("Where cornering force peaks. Lower = a stiffer, more responsive tire --\nand the more static toe (3 deg at the rear) fights itself.");
                    changed |= SliderStep("peak slip ratio", s.tirePeakSlipRatio, 0.05f, 0.5f, 0.01f, "%.2f");
                    static const float wings[] = { 0.0f, 3.0f, 6.0f };
                    static const char* wingNames[] = { "0 deg", "3 deg", "6 deg" };
                    changed |= Combo("wing angle", s.wingAngle, wings, wingNames, 3);
                    changed |= SliderStep("unsprung front", s.unsprungMassFront, 30.0f, 120.0f, 1.0f, "%.0f g");
                    changed |= SliderStep("unsprung rear", s.unsprungMassRear, 30.0f, 140.0f, 1.0f, "%.0f g");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::PopItemWidth();

            ImGui::TableSetColumnIndex(1);
            if (rt != nullptr && rt->built)
            {
                int cornerIndex = viewEnd == 0 ? 1 : 3;     // the right side, as drawn
                DrawFront(*rt, viewEnd, cornerIndex);
                ImGui::SameLine();
                DrawSide(*rt, viewEnd, cornerIndex);
                Curves(*rt, viewEnd);
                Loads(*rt);
                const RCBuggySim::EndParams& E = rt->P.end[viewEnd];
                double wheelRate = E.springRate * E.motionRatio * E.motionRatio;
                ImGui::Text("camber link %.1f mm, preload %.1f N, wheel rate %.0f N/m%s", E.table.camberLinkLength * 1000.0, E.springForceStatic, wheelRate,
                    viewEnd == 0 ? std::format(", steering link {:.1f} mm, lock {:.0f}/{:.0f} deg", E.table.tieRodLength * 1000.0, E.table.steerAtFullLock[0] * 180 / RCBuggySim::pi, -E.table.steerAtFullLock[1] * 180 / RCBuggySim::pi).c_str() : "");
            }
            else
            {
                ImGui::TextDisabled("not built yet -- the log says why");
            }
            ImGui::EndTable();
        }

        if (changed && car.Has<Components::State>())
            car.Get<Components::State>().flags |= Components::State::Flags::vehicleDirty;

        ImGui::End();
    }
};
RCSetupWindow rcSetupWindow;

// "New Entity > RC Buggy": the chassis plus every moving part as a child entity, ready for the
// in-place meshes. fourWD picks the B84.1 numbers over the B7.1 ones.
void MakeRCBuggy(bool fourWD)
{
    auto mm = [](float x, float y, float z) { return float3(x * 1e-3f, y * 1e-3f, z * 1e-3f); };

    World::Entity chassis;
    chassis.Make(Components::Transform::mask | Components::RigidBody::mask | Components::CollisionShape::mask
        | Components::RCBuggy::mask | Components::RCBuggyGeometry::mask, fourWD ? "RC10B84.1" : "RC10B7.1");

    // Model origin is the ground plane of the design pose, whose chassis sits 22.3 mm up; at 13 mm
    // ride height the origin is 9.3 mm BELOW the ground. Start 10 mm above that and let it settle.
    auto& transform = chassis.Get<Components::Transform>();
    transform.position = float3(0, 0.013f - 0.0223f + 0.010f, 0);
    transform.rotation = quaternion::identity();
    transform.scale = float3(1);
    transform.version = 0;

    auto& rigidBody = chassis.Get<Components::RigidBody>();
    rigidBody.motionType = 2;
    rigidBody.objectLayer = 0;
    rigidBody.mass = fourWD ? 1.650f : 1.492f;      // B7.1 sheet: 1492 g ready to run
    rigidBody.friction = 0.3f;                      // body / chassis plate on the track, not the tires
    rigidBody.restitution = 0.1f;
    rigidBody.linearDamping = 0.0f;                 // aero is modelled
    rigidBody.angularDamping = 0.01f;
    rigidBody.gravityFactor = 1.0f;
    rigidBody.allowSleeping = 0;                    // a sleeping chassis would freeze the step listener
    rigidBody.isSensor = 0;

    // Chassis tub + body shell as one box; the centre of mass where the battery, motor and
    // electronics put it, not at the middle of the box: 28 mm off the ground at ride height,
    // ~38/62 front/rear for the mid-motor 2WD, ~47/53 for the 4WD.
    auto& shape = chassis.Get<Components::CollisionShape>();
    shape.shapeType = 0;
    shape.mesh = Components::Handle<Components::Mesh>{ entityInvalid };
    shape.halfExtent = float3(0.058f, 0.039f, 0.158f);
    shape.offset = float3(0, 0.061f, 0.008f);
    shape.convexRadius = 0.004f;
    shape.overrideCenterOfMass = 1;
    shape.centerOfMass = float3(0, 0.0373f, fourWD ? 0.0043f : -0.0204f);

    Components::RCBuggy setup;
    setup.fourWheelDrive = fourWD ? 1 : 0;
    if (fourWD)
    {
        // B84.1-ish: slipper on, diffs front and rear, more steering, a bit more front spring.
        setup.slipperTorque = 0.6f;
        setup.frontDiffType = 0;
        setup.frontDiffOil = 10000.0f;
        setup.rearDiffOil = 5000.0f;
        setup.springRateFront = 4.0f;
        setup.antiRollBarFront = 1.2f;
        setup.tireGripFront = 1.7f;
        setup.unsprungMassFront = 75.0f;
    }
    chassis.Get<Components::RCBuggy>() = setup;

    // Measured off BuggyRef.blend (Team Associated B5M), converted from Blender (+X left, -Y
    // forward, +Z up, mm) to the model frame here.
    Components::RCBuggyGeometry g;
    g.frontLowerInner = mm(19.0f, 32.7f, 163.0f);
    g.frontLowerAxis = float3(0.0f, 0.5f, 0.866f);          // 30 deg kick-up
    g.frontLowerOuter = mm(91.25f, 36.95f, 158.1f);
    g.frontUpperInner = mm(25.5f, 62.0f, 166.0f);
    g.frontUpperOuter = mm(90.2f, 60.1f, 149.7f);
    g.frontKingpinTop = mm(102.1f, 58.0f, 143.3f);
    g.frontKingpinBottom = mm(102.3f, 29.0f, 164.0f);
    g.frontTieInner = mm(20.75f, 41.8f, 142.4f);
    g.frontTieOuter = mm(91.2f, 40.45f, 127.97f);
    g.frontBellcrankPivot = mm(12.7f, 39.4f, 119.1f);
    g.frontBellcrankAxis = float3(0, 1, 0);
    // Wheel centres and axles measured off the tire meshes themselves (axis of symmetry), not
    // assumed: the wheel MESH is spun about this axle, so any mismatch shows as a wobble. The rear
    // wheels are modelled with ~6 deg of camber and ~2.8 deg of toe-in in the design pose.
    g.frontWheelCenter = mm(107.45f, 42.8f, 149.76f);
    g.frontAxle = float3(1.0f, 0.0068f, 0.0006f);
    g.frontShockTop = mm(27.5f, 87.2f, 138.5f);
    g.frontShockBottom = mm(59.0f, 33.5f, 158.9f);
    g.rearLowerInner = mm(17.35f, 29.05f, -136.3f);
    g.rearLowerAxis = float3(0.05f, -0.011f, -0.999f);      // ~3 deg toe-in, ~0.6 deg anti-squat
    g.rearLowerOuter = mm(90.15f, 36.15f, -126.3f);
    g.rearUpperInner = mm(26.8f, 57.0f, -143.0f);
    g.rearUpperOuter = mm(76.1f, 61.5f, -129.6f);
    g.rearWheelCenter = mm(99.86f, 43.74f, -124.68f);
    g.rearAxle = float3(0.9933f, 0.105f, 0.0486f);
    g.rearShockTop = mm(34.7f, 98.0f, -160.7f);
    g.rearShockBottom = mm(68.0f, 35.0f, -137.0f);
    g.driveshaftInner = mm(28.0f, 43.75f, -124.7f);
    if (fourWD)
    {
        g.wheelWidthFront = 0.040f;
        g.wheelRadiusFront = 0.0430f;
    }
    chassis.Get<Components::RCBuggyGeometry>() = g;

    // Moving parts. Names follow the corner, so the hierarchy reads like the setup sheet.
    const char* cornerNames[4] = { "Front Left", "Front Right", "Rear Left", "Rear Right" };
    const char* partNames[10] = { "Wheel", "Lower Arm", "Caster Block", "Steering Block", "Camber Link", "Steering Link", "Shock Body", "Driveshaft", "Bellcrank", "Shock Shaft" };
    for (uint corner = 0; corner < 4; corner++)
    {
        bool front = corner < 2;
        for (uint part = 0; part < 10; part++)
        {
            if (!front && (part == 3 || part == 5 || part == 8))
                continue;                                   // no kingpin, steering link or bellcrank at the rear
            if (front && part == 7 && !fourWD)
                continue;                                   // 2WD: no front driveshafts
            String name = String(cornerNames[corner]) + " " + (!front && part == 2 ? "Hub Carrier" : partNames[part]);

            World::Entity e;
            e.Make(Components::Transform::mask | Components::Parent::mask | Components::RCBuggyPart::mask, name);
            e.Get<Components::Parent>().entity.Set(chassis);
            auto& t = e.Get<Components::Transform>();
            t.position = float3(0, 0, 0);
            t.rotation = quaternion::identity();
            t.scale = float3(1);
            t.version = 0;
            auto& info = e.Get<Components::RCBuggyPart>();
            info.corner = corner;
            info.part = part;
        }
    }

    editorState.selectedObject = chassis;
    editorState.dirtyHierarchy = true;
}
