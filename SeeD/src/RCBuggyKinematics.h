#pragma once

// Suspension kinematics of a 1/10 RC buggy corner, solved once at build time into lookup tables.
//
// Pure math on purpose -- no engine, no Jolt -- so it compiles on its own in a test harness and the
// numbers it produces (static pose, camber/toe curves, motion ratio, Ackermann) can be checked
// without launching the editor.
//
// THE MODEL. Every RC10-family buggy (B5M, B6, B7, B74, B84) hangs each wheel the same way:
//   * the lower arm pivots on an INNER hinge pin fixed to the chassis (bulkhead / arm mount);
//   * the caster block (front) or hub carrier (rear) pivots on an OUTER hinge pin through the end of
//     the arm, PARALLEL to the inner one -- so arm + carrier move in one plane, a 4-bar linkage;
//   * a camber link (turnbuckle, ball ended) from the shock tower to the carrier fixes the carrier's
//     rotation on that outer pin;
//   * front only: the steering block pivots on a kingpin inside the caster block, and the steering
//     link from the bellcrank fixes that last rotation. The rear hub carrier has no kingpin -- rear
//     toe comes from the angle of the hinge pins (the C/D arm mounts).
// The upshot is that a corner has exactly ONE free coordinate, the arm angle theta, plus the
// bellcrank angle beta at the front. Everything else -- carrier angle phi, kingpin angle psi, wheel
// centre, axle direction, shock length -- is a function of (theta, beta), tabulated here.
//
// Frames: chassis MODEL frame, metres. +X right, +Y up, +Z forward (SeeD's left-handed convention;
// the math below is handedness-agnostic, only the meaning of the axes matters). Geometry is authored
// for the RIGHT side; the left side is its mirror image (x -> -x).
//
// Every pose comes out as a delta from the DESIGN pose (the hardpoints exactly as authored), in the
// form p' = q * p + t. That is what lets a mesh exported in place -- vertices in the same model
// frame as the hardpoints, no re-pivoting -- be animated by giving its entity that Transform.

#include <cmath>
#include <vector>
#include <algorithm>

namespace RCKin
{
    struct V3
    {
        double x = 0, y = 0, z = 0;
        V3() = default;
        V3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
        V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
        V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
        V3 operator*(double s) const { return V3(x * s, y * s, z * s); }
        V3 operator-() const { return V3(-x, -y, -z); }
    };
    inline double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline V3 Cross(const V3& a, const V3& b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
    inline double Length(const V3& a) { return std::sqrt(Dot(a, a)); }
    inline V3 Normalize(const V3& a) { double l = Length(a); return l > 1e-12 ? a * (1.0 / l) : V3(0, 1, 0); }
    inline V3 MirrorX(const V3& a) { return V3(-a.x, a.y, a.z); }

    struct Q
    {
        double x = 0, y = 0, z = 0, w = 1;
        Q() = default;
        Q(double ax, double ay, double az, double aw) : x(ax), y(ay), z(az), w(aw) {}
        static Q AxisAngle(const V3& axis, double angle)
        {
            V3 n = Normalize(axis);
            double s = std::sin(0.5 * angle);
            return Q(n.x * s, n.y * s, n.z * s, std::cos(0.5 * angle));
        }
        Q operator*(const Q& b) const
        {
            return Q(w * b.x + x * b.w + y * b.z - z * b.y,
                     w * b.y - x * b.z + y * b.w + z * b.x,
                     w * b.z + x * b.y - y * b.x + z * b.w,
                     w * b.w - x * b.x - y * b.y - z * b.z);
        }
        V3 Rotate(const V3& v) const
        {
            V3 u(x, y, z);
            V3 t = Cross(u, v) * 2.0;
            return v + t * w + Cross(u, t);
        }
        Q Normalized() const
        {
            double l = std::sqrt(x * x + y * y + z * z + w * w);
            return l > 1e-12 ? Q(x / l, y / l, z / l, w / l) : Q();
        }
        // The same rotation seen in the mirror x -> -x.
        Q MirrorX() const { return Q(x, -y, -z, w); }
    };

    // Rotation that takes unit vector a onto unit vector b along the shortest arc.
    inline Q FromTo(const V3& a, const V3& b)
    {
        V3 c = Cross(a, b);
        double d = Dot(a, b);
        if (d < -0.999999)
        {
            V3 axis = Cross(V3(1, 0, 0), a);
            if (Dot(axis, axis) < 1e-8)
                axis = Cross(V3(0, 1, 0), a);
            return Q::AxisAngle(axis, 3.14159265358979);
        }
        return Q(c.x, c.y, c.z, 1.0 + d).Normalized();
    }

    // Hardpoints of ONE end (front or rear), right side, model frame, metres.
    struct EndGeometry
    {
        V3 lowerInner;          // a point on the inner hinge pin axis
        V3 lowerAxis;           // inner (and outer) hinge pin direction, any sign
        V3 lowerOuter;          // a point on the outer hinge pin axis, in the same plane as lowerInner
        V3 upperInner;          // camber link ball, chassis side (shock tower / C mount)
        V3 upperOuter;          // camber link ball, carrier side (caster block / hub carrier)
        V3 wheelCenter;         // design pose
        V3 axle;                // design pose, pointing outward
        V3 shockTop;            // shock eyelet, chassis side
        V3 shockBottom;         // shock eyelet, arm side
        double wheelRadius = 0.0425;

        bool steered = false;   // front: kingpin + bellcrank + steering link
        V3 kingpinTop;          // two points on the kingpin axis (in the caster block)
        V3 kingpinBottom;
        V3 tieInner;            // steering link ball, bellcrank side
        V3 tieOuter;            // steering link ball, steering block side
        V3 bellcrankPivot;      // a point on the bellcrank post axis
        V3 bellcrankAxis;       // bellcrank post direction, any sign
    };

    // One table node. Everything the runtime needs at a given (theta, beta), in the corner's own side
    // (the left side's tables are the right side's, mirrored).
    struct Sample
    {
        float wheel[3];         // wheel centre
        float axle[3];          // axle direction, pointing outward
        float shockLength;      // eye to eye
        float armQ[4], armT[3];             // delta poses, design -> current: p' = q p + t
        float carrierQ[4], carrierT[3];     // caster block / rear hub carrier
        float knuckleQ[4], knuckleT[3];     // front steering block (== carrier at the rear)
        float upperOuter[3];    // camber link outer ball, for drawing / aiming the link
        float tieOuter[3];      // steering link outer ball
        float tieInner[3];      // steering link inner ball (moves with the bellcrank)
        float shockBottom[3];
        float valid;            // 1 = the linkage closed here, 0 = interpolated over a hole
    };

    struct CornerTable
    {
        int    nTheta = 0, nBeta = 1;
        double theta0 = 0, dTheta = 1, beta0 = 0, dBeta = 1;
        std::vector<Sample> samples;        // [beta][theta]

        // Solved at build time, kept for the runtime and for the property window.
        double thetaStatic = 0;
        double camberLinkLength = 0;
        double tieRodLength = 0;
        double shockLengthStatic = 0;
        double shockLengthMax = 0;          // full droop (limiter)
        double shockLengthMin = 0;          // full bump
        double thetaDroop = 0, thetaBump = 0;
        double bellcrankSign = 1;           // +beta always means "steer right"
        double steerAtFullLock[2] = { 0, 0 }; // right wheel's steer angle at +/- full bellcrank throw, rad
        bool   ok = false;
        const char* error = "";

        const Sample& At(int it, int ib) const { return samples[(size_t)ib * nTheta + it]; }
    };

    // ---------------------------------------------------------------------------------------------
    struct Linkage
    {
        EndGeometry g;
        V3 axis;            // lowerAxis normalised, signed so that +theta = bump
        double lc = 0;      // camber link length
        double lt = 0;      // steering link length
        double bellSign = 1;

        struct Pose
        {
            V3 armT; Q armQ;
            V3 carrierT; Q carrierQ;
            V3 knuckleT; Q knuckleQ;
            V3 wheel, axleDir, upperOuter, tieOuter, tieInner, shockBottom;
            double shockLength = 0;
        };

        V3 TieInner(double beta) const
        {
            if (!g.steered)
                return V3();
            Q r = Q::AxisAngle(g.bellcrankAxis, beta * bellSign);
            return g.bellcrankPivot + r.Rotate(g.tieInner - g.bellcrankPivot);
        }

        // Full pose for arm angle theta, carrier angle phi on the outer pin, kingpin angle psi.
        void Evaluate(double theta, double phi, double psi, double beta, Pose& p) const
        {
            p.armQ = Q::AxisAngle(axis, theta);
            p.armT = g.lowerInner - p.armQ.Rotate(g.lowerInner);
            V3 outer = p.armQ.Rotate(g.lowerOuter) + p.armT;

            // The outer pin is parallel to the inner one and stays so (a rotation about an axis
            // leaves that axis alone), so the carrier just turns further about the same direction.
            p.carrierQ = Q::AxisAngle(axis, theta + phi);
            p.carrierT = outer - p.carrierQ.Rotate(g.lowerOuter);

            if (g.steered)
            {
                V3 kpTop = p.carrierQ.Rotate(g.kingpinTop) + p.carrierT;
                V3 kpBottom = p.carrierQ.Rotate(g.kingpinBottom) + p.carrierT;
                Q steer = Q::AxisAngle(kpTop - kpBottom, psi);
                p.knuckleQ = (steer * p.carrierQ).Normalized();
                p.knuckleT = kpBottom - p.knuckleQ.Rotate(g.kingpinBottom);
            }
            else
            {
                p.knuckleQ = p.carrierQ;
                p.knuckleT = p.carrierT;
            }

            p.wheel = p.knuckleQ.Rotate(g.wheelCenter) + p.knuckleT;
            p.axleDir = Normalize(p.knuckleQ.Rotate(g.axle));
            p.upperOuter = p.carrierQ.Rotate(g.upperOuter) + p.carrierT;
            p.tieOuter = p.knuckleQ.Rotate(g.tieOuter) + p.knuckleT;
            p.tieInner = TieInner(beta);
            p.shockBottom = p.armQ.Rotate(g.shockBottom) + p.armT;
            p.shockLength = Length(p.shockBottom - g.shockTop);
        }

        // Negative = top in. The OUTWARD axle tilts UP when the top of the tire leans in (turn a
        // wheel's up (0,1) by phi towards -x: its outward axle (1,0) turns to (cos, +sin)).
        // Was asin(+y) until 2026-11-25: every "-2 deg" setup was solved as 2 deg top OUT.
        static double Camber(const V3& axleOut) { return -std::asin(std::clamp(axleOut.y, -1.0, 1.0)); }
        static double Toe(const V3& axleOut) { return std::atan2(axleOut.z, std::sqrt(axleOut.x * axleOut.x + axleOut.y * axleOut.y)); }  // positive = toe in (right side)

        // 1-D root of f on [lo, hi]: scan for the sign change closest to `guess`, then bisect.
        template <class F>
        static bool Root(F f, double lo, double hi, double guess, double& out)
        {
            const int steps = 160;
            double bestA = 0, bestB = 0, bestDist = 1e30;
            double prevX = lo, prevF = f(lo);
            for (int i = 1; i <= steps; i++)
            {
                double x = lo + (hi - lo) * i / steps;
                double fx = f(x);
                if (std::isfinite(prevF) && std::isfinite(fx) && ((prevF <= 0 && fx >= 0) || (prevF >= 0 && fx <= 0)))
                {
                    double d = std::fabs(0.5 * (prevX + x) - guess);
                    if (d < bestDist) { bestDist = d; bestA = prevX; bestB = x; }
                }
                prevX = x; prevF = fx;
            }
            if (bestDist >= 1e29)
                return false;
            double fa = f(bestA);
            for (int i = 0; i < 60; i++)
            {
                double m = 0.5 * (bestA + bestB);
                double fm = f(m);
                if ((fa <= 0 && fm <= 0) || (fa >= 0 && fm >= 0)) { bestA = m; fa = fm; }
                else bestB = m;
            }
            out = 0.5 * (bestA + bestB);
            return true;
        }

        // Carrier angle that closes the camber link, then kingpin angle that closes the steering
        // link. psi feeds back into nothing upstream (the camber link hangs off the caster block,
        // not the steering block), so one pass each is exact.
        bool Solve(double theta, double beta, double& phi, double& psi, Pose& p) const
        {
            auto fPhi = [&](double x) { Pose q; Evaluate(theta, x, 0, beta, q); return Length(q.upperOuter - g.upperInner) - lc; };
            double phiOut;
            if (!Root(fPhi, -1.2, 1.2, phi, phiOut))
                return false;
            phi = phiOut;
            if (g.steered)
            {
                V3 tieIn = TieInner(beta);
                auto fPsi = [&](double x) { Pose q; Evaluate(theta, phi, x, beta, q); return Length(q.tieOuter - tieIn) - lt; };
                double psiOut;
                if (!Root(fPsi, -1.4, 1.4, psi, psiOut))
                    return false;
                psi = psiOut;
            }
            else
            {
                psi = 0;
            }
            Evaluate(theta, phi, psi, beta, p);
            return true;
        }
    };

    struct BuildSettings
    {
        double wheelCenterHeight;   // static wheel-centre Y in the model frame (from ride height and loaded tire radius)
        double camber;              // rad, static, negative = top in
        double toe;                 // rad, static, positive = toe in. Front only; the rear's comes from the pins
        double shockLengthMax;      // eye to eye at full droop, 0 = auto (static + 35% of stroke)
        double shockStroke;         // m
        double steeringThrow;       // bellcrank rotation at full lock, rad
        int    nTheta = 48;
        int    nBeta = 33;
    };

    inline int itStaticClamp(const CornerTable& t, double theta)
    {
        return std::clamp((int)std::lround((theta - t.theta0) / t.dTheta), 0, t.nTheta - 1);
    }

    static void StoreV(float* d, const V3& v) { d[0] = (float)v.x; d[1] = (float)v.y; d[2] = (float)v.z; }
    static void StoreQ(float* d, const Q& q) { d[0] = (float)q.x; d[1] = (float)q.y; d[2] = (float)q.z; d[3] = (float)q.w; }

    // Solves the static pose against the setup (ride height, camber, toe), derives the link lengths
    // a racer would set with the turnbuckles to get there, then tabulates the corner.
    inline CornerTable BuildCorner(const EndGeometry& geometry, const BuildSettings& s)
    {
        CornerTable table;
        Linkage L;
        L.g = geometry;
        L.g.axle = Normalize(L.g.axle);
        if (L.g.axle.x < 0) L.g.axle = -L.g.axle;
        L.axis = Normalize(L.g.lowerAxis);
        if (L.g.steered)
            L.g.bellcrankAxis = Normalize(L.g.bellcrankAxis);

        // +theta = bump, whichever way the pin was authored.
        {
            Linkage::Pose a, b;
            L.Evaluate(0, 0, 0, 0, a);
            L.Evaluate(0.01, -0.01, 0, 0, b);   // carrier held level, only the arm moves
            if (b.wheel.y < a.wheel.y)
                L.axis = -L.axis;
        }

        // ---- static pose ------------------------------------------------------------------------
        // For a trial arm angle, turn the carrier until the camber matches and the kingpin until the
        // toe matches (link lengths are free at this point -- they are what we are solving for), and
        // read the wheel height. Bisect the arm angle on that height.
        auto poseForTheta = [&](double theta, double& phi, double& psi, Linkage::Pose& p) -> bool
        {
            phi = 0; psi = 0;
            for (int it = 0; it < 8; it++)
            {
                auto fCamber = [&](double x) { Linkage::Pose q; L.Evaluate(theta, x, psi, 0, q); return Linkage::Camber(q.axleDir) - s.camber; };
                if (!Linkage::Root(fCamber, -1.2, 1.2, phi, phi))
                    return false;
                if (L.g.steered)
                {
                    auto fToe = [&](double x) { Linkage::Pose q; L.Evaluate(theta, phi, x, 0, q); return Linkage::Toe(q.axleDir) - s.toe; };
                    if (!Linkage::Root(fToe, -1.2, 1.2, psi, psi))
                        return false;
                }
            }
            L.Evaluate(theta, phi, psi, 0, p);
            return true;
        };

        auto heightError = [&](double theta) { double phi, psi; Linkage::Pose p; if (!poseForTheta(theta, phi, psi, p)) return std::nan(""); return p.wheel.y - s.wheelCenterHeight; };
        double thetaStatic;
        if (!Linkage::Root(heightError, -0.9, 0.9, 0.0, thetaStatic))
        {
            table.error = "no arm angle puts the wheel at the requested ride height";
            return table;
        }
        double phiStatic, psiStatic;
        Linkage::Pose staticPose;
        poseForTheta(thetaStatic, phiStatic, psiStatic, staticPose);

        L.lc = Length(staticPose.upperOuter - L.g.upperInner);
        L.lt = L.g.steered ? Length(staticPose.tieOuter - L.TieInner(0)) : 0;
        table.thetaStatic = thetaStatic;
        table.camberLinkLength = L.lc;
        table.tieRodLength = L.lt;
        table.shockLengthStatic = staticPose.shockLength;

        // ---- shock travel -> arm range --------------------------------------------------------------
        table.shockLengthMax = s.shockLengthMax > 0 ? s.shockLengthMax : staticPose.shockLength + 0.35 * s.shockStroke;
        table.shockLengthMin = table.shockLengthMax - s.shockStroke;
        {
            auto fMax = [&](double t) { Linkage::Pose p; L.Evaluate(t, 0, 0, 0, p); return p.shockLength - table.shockLengthMax; };
            auto fMin = [&](double t) { Linkage::Pose p; L.Evaluate(t, 0, 0, 0, p); return p.shockLength - table.shockLengthMin; };
            // The shock hangs off the arm only, so the carrier angle does not matter here.
            if (!Linkage::Root(fMax, thetaStatic - 1.2, thetaStatic + 0.3, thetaStatic, table.thetaDroop))
                table.thetaDroop = thetaStatic - 0.5;
            if (!Linkage::Root(fMin, thetaStatic - 0.3, thetaStatic + 1.2, thetaStatic, table.thetaBump))
                table.thetaBump = thetaStatic + 0.5;
            if (table.thetaDroop > table.thetaBump)
                std::swap(table.thetaDroop, table.thetaBump);
        }

        // ---- steering --------------------------------------------------------------------------------
        if (L.g.steered)
        {
            // +beta must steer right: for the right wheel that is toe OUT (its front swings to +X).
            double phi = phiStatic, psi = psiStatic;
            Linkage::Pose p;
            if (L.Solve(thetaStatic, 0.05, phi, psi, p) && Linkage::Toe(p.axleDir) > s.toe)
                L.bellSign = -1;
            table.bellcrankSign = L.bellSign;
        }

        // ---- tabulate --------------------------------------------------------------------------------
        const double margin = 0.06;
        table.nTheta = std::max(8, s.nTheta);
        table.nBeta = L.g.steered ? std::max(3, s.nBeta | 1) : 1;
        table.theta0 = table.thetaDroop - margin;
        table.dTheta = (table.thetaBump + margin - table.theta0) / (table.nTheta - 1);
        double betaMax = L.g.steered ? std::fabs(s.steeringThrow) * 1.15 + 1e-3 : 0;
        table.beta0 = -betaMax;
        table.dBeta = table.nBeta > 1 ? 2 * betaMax / (table.nBeta - 1) : 1;
        table.samples.resize((size_t)table.nTheta * table.nBeta);

        int failures = 0;
        for (int ib = 0; ib < table.nBeta; ib++)
        {
            double beta = table.beta0 + ib * table.dBeta;
            if (table.nBeta == 1) beta = 0;
            // March outward from the static angle in both directions so each solve starts next to
            // the previous one -- Root picks the closing branch nearest its guess.
            int itStatic = (int)std::lround((thetaStatic - table.theta0) / table.dTheta);
            itStatic = std::clamp(itStatic, 0, table.nTheta - 1);
            for (int dir = 0; dir < 2; dir++)
            {
                double phi = phiStatic, psi = psiStatic;
                int start = dir == 0 ? itStatic : itStatic - 1;
                int step = dir == 0 ? 1 : -1;
                for (int it = start; it >= 0 && it < table.nTheta; it += step)
                {
                    double theta = table.theta0 + it * table.dTheta;
                    Linkage::Pose p;
                    Sample& smp = table.samples[(size_t)ib * table.nTheta + it];
                    if (!L.Solve(theta, beta, phi, psi, p))
                    {
                        smp.valid = 0;
                        failures++;
                        continue;
                    }
                    smp.valid = 1;
                    StoreV(smp.wheel, p.wheel);
                    StoreV(smp.axle, p.axleDir);
                    smp.shockLength = (float)p.shockLength;
                    StoreQ(smp.armQ, p.armQ);           StoreV(smp.armT, p.armT);
                    StoreQ(smp.carrierQ, p.carrierQ);   StoreV(smp.carrierT, p.carrierT);
                    StoreQ(smp.knuckleQ, p.knuckleQ);   StoreV(smp.knuckleT, p.knuckleT);
                    StoreV(smp.upperOuter, p.upperOuter);
                    StoreV(smp.tieOuter, p.tieOuter);
                    StoreV(smp.tieInner, p.tieInner);
                    StoreV(smp.shockBottom, p.shockBottom);
                }
            }
            // A node the linkage could not close (a link going over centre at full lock + full
            // droop, typically) copies its nearest valid neighbour along theta, so the runtime never
            // reads garbage. Flagged, and counted in the build log.
            for (int pass = 0; pass < 2; pass++)
            {
                for (int k = 0; k < table.nTheta; k++)
                {
                    int it = pass == 0 ? k : table.nTheta - 1 - k;
                    int from = pass == 0 ? it - 1 : it + 1;
                    if (from < 0 || from >= table.nTheta) continue;
                    Sample& smp = table.samples[(size_t)ib * table.nTheta + it];
                    const Sample& src = table.samples[(size_t)ib * table.nTheta + from];
                    if (smp.valid == 0 && src.valid != 0)
                    {
                        smp = src;
                        smp.valid = 0.5f;   // "filled": usable, not solved
                    }
                }
            }
        }

        if (L.g.steered)
        {
            const Sample& full = table.At(itStaticClamp(table, thetaStatic), table.nBeta - 1);
            const Sample& opposite = table.At(itStaticClamp(table, thetaStatic), 0);
            table.steerAtFullLock[0] = -(Linkage::Toe(V3(full.axle[0], full.axle[1], full.axle[2])) - s.toe);
            table.steerAtFullLock[1] = -(Linkage::Toe(V3(opposite.axle[0], opposite.axle[1], opposite.axle[2])) - s.toe);
        }

        table.ok = failures < table.nTheta * table.nBeta / 4;
        if (!table.ok)
            table.error = "the linkage does not close over most of the travel -- check the hardpoints";
        return table;
    }
}
