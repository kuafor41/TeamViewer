#include "deskawake/motion.hpp"
#include "deskawake/scheduler.hpp"
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void require(bool c,const std::string& m){if(!c)throw std::runtime_error(m);}
void testBezier(){
    const deskawake::Point a{2,4},b{10,3},c{12,18},d{30,20};
    const auto p0=deskawake::cubicBezier(a,b,c,d,0),p1=deskawake::cubicBezier(a,b,c,d,1);
    require(std::abs(p0.x-a.x)<1e-12&&std::abs(p0.y-a.y)<1e-12,"Bezier start");
    require(std::abs(p1.x-d.x)<1e-12&&std::abs(p1.y-d.y)<1e-12,"Bezier end");
}
void testSCurve(){
    require(deskawake::sCurve(-1)==0&&deskawake::sCurve(0)==0&&deskawake::sCurve(1)==1&&deskawake::sCurve(2)==1,"S-curve bounds");
    double previous=0;
    for(int i=1;i<=1000;++i){double v=deskawake::sCurve(i/1000.0);require(v>=previous,"S-curve monotonic");previous=v;}
}
void testPerlin(){
    const double a=deskawake::perlin2D(1.25,0.75,12345);
    require(a==deskawake::perlin2D(1.25,0.75,12345),"Perlin deterministic");
    require(a>=-1&&a<=1,"Perlin bounded");
    require(std::abs(deskawake::perlin2D(1.25,0.75,54321)-a)>1e-9,"Perlin seeded");
}
void testTrajectory(){
    const deskawake::Point a{20,40},b{420,240};
    deskawake::TrajectoryOptions o{};o.sample_count=64;o.seed=42;
    const auto p=deskawake::generateHumanTrajectory(a,b,o);
    require(p.size()==64,"trajectory count");
    require(p.front().x==a.x&&p.front().y==a.y,"trajectory start");
    require(p.back().x==b.x&&p.back().y==b.y,"trajectory end");
    for(const auto& v:p)require(std::isfinite(v.x)&&std::isfinite(v.y),"trajectory finite");
    const auto repeat=deskawake::generateHumanTrajectory(a,b,o);
    for(std::size_t i=0;i<p.size();++i)require(p[i].x==repeat[i].x&&p[i].y==repeat[i].y,"trajectory seeded");
}
void testPoisson(){
    deskawake::Scheduler s({},0xC0FFEEu);
    constexpr int n=50000;double sum=0;
    for(int i=0;i<n;++i){auto d=s.nextDelaySeconds();require(d>=10&&d<=89,"Poisson clamp");sum+=d;}
    const double mean=sum/n;require(mean>41&&mean<43,"Poisson lambda mean");
}
void testRouter(){
    deskawake::Scheduler s({},0xABCDEFu);constexpr int n=20000;int micro=0;
    for(int i=0;i<n;++i)if(s.nextAction()==deskawake::ActionKind::Micro)++micro;
    const double ratio=static_cast<double>(micro)/n;require(ratio>0.68&&ratio<0.72,"70/30 routing");
}
void testValidation(){
    bool threw=false;
    try{deskawake::TrajectoryOptions o{};o.sample_count=1;(void)deskawake::generateHumanTrajectory({0,0},{1,1},o);}
    catch(const std::invalid_argument&){threw=true;}require(threw,"sample count validation");
    threw=false;try{deskawake::SchedulerSettings s{};s.maximum_seconds=90;(void)deskawake::Scheduler(s,1);}
    catch(const std::invalid_argument&){threw=true;}require(threw,"maximum validation");
    threw=false;try{deskawake::SchedulerSettings s{};s.poisson_lambda=0;(void)deskawake::Scheduler(s,1);}
    catch(const std::invalid_argument&){threw=true;}require(threw,"lambda validation");
}
}
int main(){try{testBezier();testSCurve();testPerlin();testTrajectory();testPoisson();testRouter();testValidation();std::cout<<"All Acrobat core tests passed.\\n";return 0;}
catch(const std::exception& e){std::cerr<<"Test failure: "<<e.what()<<'\\n';return 1;}}
