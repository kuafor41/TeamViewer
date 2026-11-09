#include "deskawake/motion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
namespace deskawake {
namespace {
double clamp01(double v) noexcept { return std::clamp(v, 0.0, 1.0); }
double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }
double fade(double t) noexcept { return t*t*t*(t*(t*6.0-15.0)+10.0); }
std::uint32_t mixHash(std::uint32_t v) noexcept {
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15;
    v *= 0x846ca68bu; v ^= v >> 16; return v;
}
double gradientDot(int h, double x, double y) noexcept {
    constexpr double q=0.7071067811865475244;
    switch(h&7) {
    case 0:return x; case 1:return -x; case 2:return y; case 3:return -y;
    case 4:return (x+y)*q; case 5:return (x-y)*q;
    case 6:return (-x+y)*q; default:return (-x-y)*q;
    }
}
}
Point cubicBezier(Point p0, Point p1, Point p2, Point p3, double t) noexcept {
    const double tc=clamp01(t), u=1.0-tc;
    const double b0=u*u*u, b1=3.0*u*u*tc, b2=3.0*u*tc*tc, b3=tc*tc*tc;
    return {b0*p0.x+b1*p1.x+b2*p2.x+b3*p3.x,
            b0*p0.y+b1*p1.y+b2*p2.y+b3*p3.y};
}
double sCurve(double t) noexcept {
    const double v=clamp01(t); return v*v*(3.0-2.0*v);
}
double perlin2D(double x, double y, std::uint32_t seed) noexcept {
    if(!std::isfinite(x)||!std::isfinite(y)) return 0.0;
    const double fx0=std::floor(x), fy0=std::floor(y);
    const auto ix=static_cast<std::int32_t>(fx0), iy=static_cast<std::int32_t>(fy0);
    const double fx=x-fx0, fy=y-fy0;
    const auto hashAt=[seed](std::int32_t gx,std::int32_t gy) {
        return static_cast<int>(mixHash(seed ^ mixHash(static_cast<std::uint32_t>(gx)+0x9e3779b9u)
            ^ mixHash(static_cast<std::uint32_t>(gy)+0x85ebca6bu)) & 7u);
    };
    const double n00=gradientDot(hashAt(ix,iy),fx,fy);
    const double n10=gradientDot(hashAt(ix+1,iy),fx-1.0,fy);
    const double n01=gradientDot(hashAt(ix,iy+1),fx,fy-1.0);
    const double n11=gradientDot(hashAt(ix+1,iy+1),fx-1.0,fy-1.0);
    const double u=fade(fx),v=fade(fy);
    return std::clamp(lerp(lerp(n00,n10,u),lerp(n01,n11,u),v)*1.65,-1.0,1.0);
}
std::vector<Point> generateHumanTrajectory(Point start, Point target, const TrajectoryOptions& o) {
    if(o.sample_count<2) throw std::invalid_argument("sample_count must be at least 2");
    if(!std::isfinite(start.x)||!std::isfinite(start.y)||!std::isfinite(target.x)||!std::isfinite(target.y))
        throw std::invalid_argument("trajectory points must be finite");
    if(!std::isfinite(o.overshoot_fraction)||!std::isfinite(o.perlin_amplitude_pixels)||
       !std::isfinite(o.gaussian_sigma_pixels)||o.gaussian_sigma_pixels<0.0)
        throw std::invalid_argument("trajectory amplitudes must be finite and sigma non-negative");
    std::vector<Point> result; result.reserve(o.sample_count);
    const double dx=target.x-start.x,dy=target.y-start.y,distance=std::hypot(dx,dy);
    if(distance<1e-9) { result.assign(o.sample_count,start); result.front()=start; result.back()=target; return result; }
    const Point tangent{dx/distance,dy/distance},normal{-tangent.y,tangent.x};
    const double over=distance*std::clamp(o.overshoot_fraction,0.0,0.20);
    const Point overshoot{target.x+tangent.x*over,target.y+tangent.y*over};
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> bendDist(-0.12,0.12);
    std::normal_distribution<double> gaussian(0.0,o.gaussian_sigma_pixels);
    const double bend=distance*bendDist(rng);
    const Point c1{start.x+tangent.x*distance*0.30+normal.x*bend,
                   start.y+tangent.y*distance*0.30+normal.y*bend};
    const Point c2{overshoot.x-tangent.x*distance*0.28+normal.x*bend*0.55,
                   overshoot.y-tangent.y*distance*0.28+normal.y*bend*0.55};
    const Point r1{overshoot.x-tangent.x*std::max(1.0,over*0.25),
                   overshoot.y-tangent.y*std::max(1.0,over*0.25)};
    const Point r2{target.x-tangent.x*std::max(1.0,distance*0.08),
                   target.y-tangent.y*std::max(1.0,distance*0.08)};
    constexpr double split=0.88;
    const double perlinAmp=std::clamp(o.perlin_amplitude_pixels,0.0,3.0);
    for(std::size_t i=0;i<o.sample_count;++i) {
        const double raw=static_cast<double>(i)/static_cast<double>(o.sample_count-1);
        const double eased=sCurve(raw);
        Point p{};
        if(eased<=split) p=cubicBezier(start,c1,c2,overshoot,eased/split);
        else p=cubicBezier(overshoot,r1,r2,target,(eased-split)/(1.0-split));
        p.x+=perlin2D(eased*7.0,0.37,o.seed)*perlinAmp+gaussian(rng);
        p.y+=perlin2D(eased*7.0,12.91,o.seed^0x9e3779b9u)*perlinAmp+gaussian(rng);
        result.push_back(p);
    }
    result.front()=start; result.back()=target; return result;
}
} // namespace deskawake
