#include "game/adventure/frontier_world.hpp"
#include "game/adventure/world_definition.hpp"
#include "core/sha256.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
glm::dvec3 at(const terrain::lego::Surface& terrain,double x,double z,double radius=1.12) {
    return {x,double(terrain::lego::supportHeight(terrain,glm::vec2(x,z),float(radius)))+.005,z};
}
bool overlap(const AdventureSpatialQueries::Solid& a,const AdventureSpatialQueries::Solid& b) {
    return glm::all(glm::lessThan(a.minimum,b.maximum-glm::dvec3(.015)))
        &&glm::all(glm::lessThan(b.minimum,a.maximum-glm::dvec3(.015)));
}
AdventureSpatialQueries::Solid visualSolid(const FrontierVisual& piece,const GridBox& box) {
    auto lo=glm::dvec3(box.minimum.x,box.minimum.y,box.minimum.z)*.02*piece.scale;
    auto hi=glm::dvec3(box.maximum.x,box.maximum.y,box.maximum.z)*.02*piece.scale;
    for(uint8_t turn=0;turn<piece.yaw%4;++turn) {
        const auto a=lo,b=hi;lo={a.z,a.y,-b.x};hi={b.z,b.y,-a.x};
    }
    return {{},{},piece.feet+lo,piece.feet+hi};
}
}
bool FrontierWorld::initialize(const terrain::lego::Surface& terrain,AdventureContent& content,std::string& error) {
    if(!terrain.valid()){error="Dawnreach needs the installed landscape.";return false;}
    visuals_.clear();destinations_.clear();servicePoints_.clear();approachSamples_.clear();content={};content.frontier=true;
    start_=at(terrain,1180,-1100);resident_=at(terrain,1184,-1091);
    content.town={start_.x,start_.y,start_.z,-2.18};
    struct Site {uint32_t id;double x,z;const char* name;const char* region;FrontierSiteKind kind;};
    constexpr Site sites[]{
        {1,1225,-1082,"The Broken Stair","Meadow's Edge",FrontierSiteKind::Cache},
        {2,1308,-1052,"Dawnreach Beacon","The First Light",FrontierSiteKind::Beacon},
        {3,1390,-946,"Stormwatch Beacon","Stormwatch Ridge",FrontierSiteKind::Beacon},
        {4,1210,-914,"The Old Quarry","The Elderwood",FrontierSiteKind::Quarry},
        {5,1120,-955,"The Last Ember","Emberwood Sanctuary",FrontierSiteKind::Beacon},
    };
    constexpr glm::dvec2 enemyCamps[]{{1264,-1040},{1288,-1036},{1340,-985},{1360,-952},{1163,-958},{1148,-938}};
    const auto lane=[&](glm::dvec2 from,glm::dvec2 to) {
        const int count=std::max(1,int(std::ceil(glm::length(to-from)/1.5)));
        for(int i=0;i<=count;++i) {
            const auto p=glm::mix(from,to,double(i)/count);
            approachSamples_.push_back(at(terrain,p.x,p.y));
        }
    };
    lane({1180,-1100},{1208,-1086});
    approachSamples_.push_back(resident_);
    for(const auto& site:sites) {
        const auto p=at(terrain,site.x,site.z,8);
        destinations_.push_back({site.id,site.name,site.region,p});
        auto target=at(terrain,p.x,p.z-10);
        if(site.kind==FrontierSiteKind::Cache)target=p+glm::dvec3(0,5.76,0);
        else {
            servicePoints_.push_back(target);
            lane({target.x,target.z-10},{target.x,target.z});
        }
        content.frontierSites.push_back({site.id,{target.x,target.y,target.z,0},site.kind,
            site.kind==FrontierSiteKind::Cache?ItemStack{ItemKind::Scrap,18}:ItemStack{}});
        if(site.id==2)content.frontierSites.back().defenders={1,2};
        if(site.id==3)content.frontierSites.back().defenders={3,4};
        if(site.id==5)content.frontierSites.back().requiresQuarryCharge=true;
    }
    for(size_t i=0;i<std::size(enemyCamps);++i) {
        const auto p=at(terrain,enemyCamps[i].x,enemyCamps[i].y);
        approachSamples_.push_back(p);
        content.frontierEnemies.push_back({uint32_t(i+1),1,{p.x,p.y,p.z,0},uint16_t(i%2?100:70),{ItemKind::Scrap,uint16_t(i%2?12:8)}});
        content.frontierEnemies.back().archetype=i%2?FrontierEnemyArchetype::Brute:FrontierEnemyArchetype::Scout;
    }
    // A shallow footing begins at the lowest actual cell under its footprint.
    // Tall terrain relief is filled with masonry, never a floating shared slab.
    const auto ground=[&](double x,double z,double width,double depth) {
        const auto origin=terrain.origin();
        const int x0=std::max(0,int(std::floor((x-width*.5+double(origin.x))/double(terrain.cellScale))));
        const int z0=std::max(0,int(std::floor((z-depth*.5+double(origin.y))/double(terrain.cellScale))));
        const int x1=std::min(int(terrain.width)-2,int(std::floor((x+width*.5+double(origin.x))/double(terrain.cellScale))));
        const int z1=std::min(int(terrain.height)-2,int(std::floor((z+depth*.5+double(origin.y))/double(terrain.cellScale))));
        double low=600;
        for(int iz=z0;iz<=z1;++iz)for(int ix=x0;ix<=x1;++ix)low=std::min(low,double(terrain.cellTop(ix,iz)));
        return low;
    };
    const auto add=[&](PieceKind kind,glm::dvec3 feet,glm::dvec3 scale,uint32_t paint,
        uint32_t resource=0,uint32_t site=0,bool light=false,bool solid=true,uint8_t yaw=0) {
        if(visuals_.size()>=maximumVisuals)return;
        FrontierVisual piece{kind,feet,scale,paint,yaw,resource,site,light,solid};
        if(solid&&!resource) {
            const auto* definition=buildingDefinition(kind);
            for(const auto& box:definition->solids) {
                const auto bounds=visualSolid(piece,box);
                for(const auto sample:approachSamples_) {
                    const AdventureSpatialQueries::Solid space{{},{},sample-glm::dvec3(1.65,.05,1.65),sample+glm::dvec3(1.65,5.3,1.65)};
                    if(overlap(bounds,space))return;
                }
            }
        }
        visuals_.push_back(piece);
    };
    const auto footing=[&](double x,double z,double width,double depth,double top,uint32_t paint) {
        const double low=ground(x,z,width,depth)-.025;
        if(top>low+.02)add(PieceKind::Brick2x4,{x,low,z},{width/4,(top-low)/.96,depth/2},paint);
    };
    const auto grounded=[&](PieceKind kind,double x,double z,glm::dvec3 scale,uint32_t paint,
                            bool solid=true,uint8_t yaw=0,uint32_t resource=0) {
        const auto* definition=buildingDefinition(kind);
        auto width=double(definition->bounds.maximum.x-definition->bounds.minimum.x)*.02*scale.x;
        auto depth=double(definition->bounds.maximum.z-definition->bounds.minimum.z)*.02*scale.z;
        if(yaw%2)std::swap(width,depth);
        const auto p=glm::dvec3(x,ground(x,z,width,depth),z);
        add(kind,p,scale,paint,resource,0,false,solid,yaw);return p;
    };
    const auto pier=[&](double x,double z,double base,double height,double width,uint32_t stone,uint32_t cap) {
        footing(x,z,width,width,base,stone);
        for(double y=0;y<height-.01;y+=.96)
            add(PieceKind::Brick2x2,{x,base+y,z},{width/2,std::min(.96,height-y)/.96,width/2},stone);
        add(PieceKind::Floor,{x,base+height,z},{width*.65,1,width*.65},cap);
    };
    const auto deck=[&](glm::dvec3 p,int halfX,int halfZ,uint32_t stone,uint32_t trim) {
        for(int x=-halfX;x<=halfX;x+=2)for(int z=-halfZ;z<=halfZ;z+=2) {
            footing(p.x+x,p.z+z,2,2,p.y,stone);
            add(PieceKind::Floor,p+glm::dvec3(x,0,z),{1,1,1},((x+z)/2)%3?stone:trim);
        }
    };
    const auto ring=[&](glm::dvec3 p,double radius,uint32_t paint,double height=.96) {
        for(double x=-radius;x<=radius;x+=2)for(int side:{-1,1})
            add(PieceKind::Brick1x2,p+glm::dvec3(x,0,side*radius),{1,height/.96,1},paint);
        for(double z=-radius+2;z<radius;z+=2)for(int side:{-1,1})
            add(PieceKind::Brick1x2,p+glm::dvec3(side*radius,0,z),{1,height/.96,1},paint,0,0,false,true,1);
    };
    const auto banner=[&](double x,double z,double base,double height,uint32_t color,uint8_t yaw=0) {
        footing(x,z,.45,.45,base,0x68533d);
        add(PieceKind::Pier,{x,base,z},{.9,height/.96,.9},0x68533d);
        add(PieceKind::Wall,{x,base+height-3.8,z},{1.4,1.1,.5},color,0,0,false,false,yaw);
        add(PieceKind::Brick1x2,{x,base+height-4,z},{1.15,.20,.14},0xe9be76,0,0,false,false,yaw);
    };
    // An open expedition camp, with a stepped canvas roof that reads from the
    // first camera position. Its central lane and recovery position stay clear.
    camp_=at(terrain,1187,-1119,5);const auto camp=camp_;
    deck(camp,2,2,0x916c45,0xac8555);
    for(int side:{-1,1}) {
        pier(camp.x+side*3.2,camp.z+2.4,camp.y,5.4,.48,0x735135,0x735135);
        pier(camp.x+side*3.2,camp.z-2.4,camp.y,5.4,.48,0x735135,0x735135);
        for(int step=0;step<5;++step)
                add(PieceKind::Floor,camp+glm::dvec3(side*(3.1-step*.64),5.4+step*.57,0),{.42,1.85,3.15},step%2?0xba573f:0xd77948);
    }
    add(PieceKind::Wall,camp+glm::dvec3(0,0,3),{3.1,1.9,.6},0xb85e42);
    for(int i=0;i<3;++i) {
        const auto supply=grounded(PieceKind::Brick2x2,camp.x+i*2.1,camp.z-6,{.85,1.4+i*.15,.85},i==1?0x477880:0xa27b4c);
        add(PieceKind::Beam,supply+glm::dvec3(0,1.36+i*.144,0),{.85,.3,5.4},0xd6bf87,0,0,false,false);
    }
    const auto canvas=grounded(PieceKind::Floor,camp.x+5,camp.z-3,{1.7,1,1.2},0x806244);
    for(int roll=0;roll<3;++roll)add(PieceKind::Brick1x2,canvas+glm::dvec3(0,.32,roll*.55-.55),{1.3,.7,.5},roll%2?0xc6a16b:0x9e7650);
    const auto fire=at(terrain,1173,-1092,.8);
    for(int i=0;i<8;++i) {
        const double angle=i*std::numbers::pi/4;
        grounded(PieceKind::Brick1x2,fire.x+std::cos(angle)*1.7,fire.z+std::sin(angle)*1.7,{.55,.5,.75},i%2?0x6d7473:0xa3a099,true,uint8_t(i%2));
    }
    grounded(PieceKind::Brick1x2,fire.x,fire.z,{1.1,.32,.6},0x5b3c28,true,1);
    add(PieceKind::Brick2x2,fire+glm::dvec3(0,.35,0),{.48,1.4,.48},0xed9e39,0,0,true,false);
    add(PieceKind::Brick2x2,fire+glm::dvec3(.2,1.45,-.1),{.23,.8,.23},0xffd277,0,0,true,false);
    grounded(PieceKind::Brick2x4,1169,-1092,{1.3,.7,.45},0x74583b,true);
    banner(camp.x-3,camp.z+6,camp.y,9,0x387b83);

    for(const auto& site:sites) {
        const auto p=destinations_[site.id-1].feet;
        if(site.kind==FrontierSiteKind::Cache) {
            // A severed aqueduct: the cache platform remains 5.76 above its
            // original anchor, and the missing western span needs real building.
            for(int side:{-1,1})pier(p.x+side*2,p.z,p.y,5.44,2,0x9c9484,0xcebd96);
            for(int x=-2;x<=2;x+=2)for(int z=-2;z<=2;z+=2)
                add(PieceKind::Floor,p+glm::dvec3(x,5.44,z),{1,1,1},0xd2c39c);
            const auto step=at(terrain,p.x,p.z-5,2);
            footing(step.x,step.z,3,3,step.y,0x918b7c);
            add(PieceKind::Stair,step,{1.5,1,1.5},0xaba18c);
            add(PieceKind::Chest,p+glm::dvec3(0,5.76,0),{1.4,1.4,1.4},0xd9a44d,0,site.id,false,false);
            for(int side:{-1,1}) {
                const double x=p.x+side*14;
                const double base=at(terrain,x,p.z,3).y;
                pier(x,p.z,base,7.68,2.4,0x8e9489,0xcebd96);
                pier(x+side*5,p.z,base,7.68,2.4,0x8e9489,0xcebd96);
                for(int n=0;n<4;++n)
                    add(PieceKind::Floor,{x+side*n*2,base+8,p.z},{1,1,2},0xb8b096);
                add(PieceKind::Brick1x2,{x,base+8.32,p.z+1.7},{2,.7,1},0xbbb39b);
                for(int n=0;n<4;++n)grounded(PieceKind::Brick2x4,x+side*(n-1)*2,p.z+5+n*.8,{.6+n*.08,.5+(n%2)*.3,.65},0xaaa18d,true,uint8_t(n%2));
            }
        } else if(site.id==2) {
            // Dawnreach: broad observatory bowl, asymmetric broken wall wings,
            // and pale/gold masonry with turquoise instrument inlays.
            deck(p,4,4,0xa99f86,0xd1c099);
            for(int side:{-1,1}) {
                for(int n=0;n<3;++n) {
                    const double x=p.x+side*(8+n*2);
                    const double base=at(terrain,x,p.z+4,2).y;
                    pier(x,p.z+4,base,side<0?7.68-n*1.92:5.76+n*.96,2,0x9a9788,0xe1c79a);
                }
                pier(p.x+side*4,p.z+1,p.y+.32,10.56,2.4,0xa7a18e,0xddc59a);
                add(PieceKind::Wall,p+glm::dvec3(side*4,4.16,.95),{1.22,1.7,.45},0x4d8c91,0,0,false,false);
            }
            footing(p.x,p.z+1,6,4,p.y+9.6,0x898f85);
            for(int tier=0;tier<4;++tier) {
                const int radius=2+tier*2;
                const double level=9.6+tier*.32;
                for(int x=-radius;x<=radius;x+=2)for(int z=-radius;z<=radius;z+=2)
                    add(PieceKind::Floor,p+glm::dvec3(x,level,z+1),{1,1,1},tier==3?0x355e68:0xa7a18e);
            }
            ring(p+glm::dvec3(0,10.88,1),8,0xe5c98d,1.6);
            for(int side:{-1,1})for(int z:{-1,1}) {
                add(PieceKind::Brick2x2,p+glm::dvec3(side*8,12.48,1+z*8),{.75,2.5,.75},0xe1c79a);
                add(PieceKind::Floor,p+glm::dvec3(side*8,14.88,1+z*8),{1,1,1},0x4d8c91);
            }
            pier(p.x,p.z+1,p.y+9.92,7.68,1.2,0xe1c79a,0xeacb84);
            add(PieceKind::Brick2x2,p+glm::dvec3(0,17.92,1),{1.4,2.3,1.4},0xffcf76,0,site.id,true,false);
        } else if(site.id==3) {
            // Stormwatch: a cold, offset keep with crenellations and a surviving
            // side bridge. Its south service approach remains on real terrain.
            const uint32_t stone=0x6f858e,trim=0xb2c7cb;
            double signalTop=p.y;
            for(int tower=0;tower<3;++tower) {
                const double x=p.x+(tower==0?-7:tower==1?7:1),z=p.z+(tower==2?9:2);
                const double base=at(terrain,x,z,3).y;
                const double height=tower==0?16.32:tower==1?10.56:20.16;
                if(tower==2)signalTop=base+height+.32;
                footing(x,z,5,5,base,stone);
                for(double y=0;y<height;y+=.96)
                    add(PieceKind::Brick2x4,{x,base+y,z},{1.25,1,2.5},stone);
                add(PieceKind::Floor,{x,base+height,z},{3,1,3},trim);
                for(int sx:{-1,1})for(int sz:{-1,1})
                    add(PieceKind::Brick2x2,{x+sx*2,base+height+.32,z+sz*2},{.7,1.5,.7},trim);
                add(PieceKind::Wall,{x,base+height-7,z-2.52},{.65,1.4,.18},0x263f56,0,0,false,false);
                if(tower!=1)banner(x+3,z,base,height-1,0x386780,1);
            }
            for(int side:{-1,1}) {
                const double x=p.x+side*11;
                const double base=at(terrain,x,p.z+8,2).y;
                footing(x,p.z+8,4,2,base+5.76,stone);
                for(int n=0;n<3;++n)add(PieceKind::Brick2x2,{x-2+n*2,base+5.76,p.z+8},{.7,1,.7},trim);
            }
            // A timber bridge crossing the western wall's drainage hollow.
            const double bridgeX=p.x-10,bridgeZ=p.z+8;
            const double bridgeY=std::max(at(terrain,bridgeX-6,bridgeZ,2).y,at(terrain,bridgeX+6,bridgeZ,2).y)+.32;
            for(int n=-3;n<=3;++n) {
                const double x=bridgeX+n*2;
                footing(x,bridgeZ,1,3,bridgeY,0x596a6b);
                add(PieceKind::Floor,{x,bridgeY,bridgeZ},{1,1,2},0x8a9fa0);
                for(int side:{-1,1}) {
                    add(PieceKind::Beam,{x,bridgeY+1.8,bridgeZ+side*1.85},{1,1,1.2},0x687e87);
                    if(n%2==0)add(PieceKind::Pier,{x,bridgeY+.32,bridgeZ+side*1.85},{.8,1.55,.8},0x687e87);
                }
            }
            add(PieceKind::Brick2x2,{p.x+1,signalTop,p.z+9},{1.4,2.1,1.4},0xa7e0ef,0,site.id,true,false);
        } else if(site.id==5) {
            // Last Ember: an open, octagonal sanctuary under concentric warm
            // canopy tiers. There is no repeated tower silhouette here.
            deck(p,4,4,0x9b7951,0xc1a174);
            for(int i=0;i<8;++i) {
                const double angle=i*std::numbers::pi/4;
                const double x=p.x+std::round(std::cos(angle)*7),z=p.z+std::round(std::sin(angle)*7);
                if(z<p.z-4&&std::abs(x-p.x)<4)continue;
                pier(x,z,p.y+.32,8.64,1.2,0x795438,0xc8a169);
            }
            for(int tier=0;tier<4;++tier) {
                const int r=8-tier*2;
                for(int x=-r;x<=r;x+=2)for(int z=-r;z<=r;z+=2) {
                    if(std::abs(x)+std::abs(z)>r+4)continue;
                    add(PieceKind::Roof,p+glm::dvec3(x,8.96+tier*.32,z),{1,1,1},tier%2?0xd4a050:0x9d493b);
                }
            }
            add(PieceKind::Roof,p+glm::dvec3(0,10.24,0),{2,1,2},0xc58242);
            for(int side:{-1,1})banner(p.x+side*8,p.z-4,p.y,8.8,0xb7523e);
            footing(p.x,p.z,3,3,p.y+2.88,0xa58b66);
            ring(p+glm::dvec3(0,2.88,0),2,0xdcad62,.64);
            add(PieceKind::Brick2x2,p+glm::dvec3(0,3.52,0),{1.2,3,1.2},0xffbd5f,0,site.id,true,false);
        } else {
            // Quarry rails, cut blocks and a broken winch replace an empty dot.
            for(int side:{-1,1})grounded(PieceKind::Beam,p.x+side*3,p.z+2,{.35,1,36},0x73868a,true);
            for(int n=0;n<5;++n)grounded(PieceKind::Brick1x2,p.x,p.z-3+n*2.4,{3,.3,.6},0x8a7257,true);
            const auto crane=at(terrain,p.x+9,p.z+4,2);
            pier(crane.x,crane.z,crane.y,8.64,1.2,0x73624d,0x9c8b6c);
            add(PieceKind::Beam,crane+glm::dvec3(-2,8.96,0),{4,2.2,2},0x9c8b6c);
            grounded(PieceKind::Brick2x2,p.x-7,p.z+4,{1.2,1.3,1},0x577b82);
        }
        if(site.kind==FrontierSiteKind::Beacon)
            add(PieceKind::Beam,p+glm::dvec3(0,24.32,1),{.11,170,.65},site.id==3?0xa7dff2:0xffcf76,0,site.id,true,false);
    }
    // The departure arch is a broken gatehouse with unequal piers, cornice,
    // ivy-colored fragments and a fallen wing, rather than two striped posts.
    const auto gate=at(terrain,1199,-1090,6);
    for(int side:{-1,1}) {
        const auto base=at(terrain,gate.x,gate.z+side*6,2);
        pier(base.x,base.z,base.y,side<0?10.56:8.64,2.4,0x968c76,0xd6c39c);
        footing(base.x+3,base.z+side*2,4,2,base.y+3.84,0x9b947f);
    }
    for(int n=0;n<6;++n)add(PieceKind::Brick2x2,gate+glm::dvec3(0,9.6,-5+n*2),{1.35,1.1,1},0xccbb98);
    for(int i=0;i<5;++i)grounded(PieceKind::Brick2x4,1204+i*1.4,-1079+i*.8,{.8,.5+(i%2)*.4,.6},i%2?0x8b936e:0xb6aa8f,true,uint8_t(i%2));

    // Stable resource IDs and locations, with distinct regional assemblies.
    // Every trunk/log/rock/crate component shares its depletion identity.
    uint32_t id=1;
    constexpr glm::dvec2 clusters[]{{1193,-1074},{1260,-1070},{1330,-1005},{1370,-958},{1220,-938},{1150,-986}};
    for(size_t cluster=0;cluster<std::size(clusters);++cluster)for(int i=0;i<9;++i,++id) {
        const auto center=clusters[cluster];const double angle=double(i)*2.3999632297;
        const auto p=at(terrain,center.x+std::cos(angle)*(7.+i*1.3),center.y+std::sin(angle)*(7.+i*1.3),1.5);
        const auto kind=ItemKind(1+i%3);const int variant=(int(cluster)+i/3)%3;
        content.resourceNodes.push_back({id,{p.x,p.y,p.z,0},{kind,uint16_t(kind==ItemKind::Wood?16:12)}});
        if(kind==ItemKind::Wood) {
            const uint32_t bark=cluster>=4?0x714b35:0x806044;
            if(variant==0) {
                const auto base=grounded(PieceKind::Brick2x2,p.x,p.z,{.65,1.5,.65},bark,true,0,id);
                add(PieceKind::Brick2x4,base+glm::dvec3(.8,1.2,.2),{1.6,.7,.65},bark,id,0,false,true,uint8_t(cluster%2));
                add(PieceKind::Floor,base+glm::dvec3(-.3,1.44,0),{.58,.7,.58},0xc4a36a,id,0,false,false);
            } else {
                const double height=variant==1?5.76:7.68;
                const auto base=grounded(PieceKind::Brick2x2,p.x,p.z,{.5,height/.96,.5},bark,true,0,id);
                for(int h=0;h<4;++h) {
                    const double width=variant==1?2.6-h*.45:1.8-h*.3;
                    const uint32_t green=cluster>=4?(h%2?0xb18743:0x747a3e):(h%2?0x789855:0x486d42);
                    add(PieceKind::Brick2x2,base+glm::dvec3((h%2)*.35,height-1.92+h*.8,0),{width,.85,width},green,id,0,false,false);
                }
            }
        } else if(kind==ItemKind::Stone) {
            const uint32_t stone=cluster==3?0x728b99:cluster>=4?0x8b9a94:0xa7a397;
            const auto base=grounded(PieceKind::Brick2x4,p.x,p.z,{.8+variant*.2,.8+variant*.3,1.1},stone,true,uint8_t(variant%2),id);
            add(PieceKind::Brick2x2,base+glm::dvec3(.3,.77+variant*.28,-.2),{.9,.65,.9},0xc5c5b3,id);
            if(variant==2)add(PieceKind::Brick1x2,base+glm::dvec3(-1.7,.1,.8),{.7,.5,.8},stone,id);
        } else {
            const auto base=grounded(PieceKind::Chest,p.x,p.z,{variant==1?2.2:1.6,1.2+variant*.25,1.7},cluster==3?0x607e89:0xb4814b,true,0,id);
            if(variant==0) {
                add(PieceKind::Beam,base+glm::dvec3(0,1.15,0),{1,.5,3},0x537a7c,id,0,false,false);
            } else if(variant==1) {
                for(int side:{-1,1})add(PieceKind::Brick2x2,base+glm::dvec3(side*1.5,.1,0),{.45,.65,.45},0x596468,id);
                add(PieceKind::Beam,base+glm::dvec3(0,.45,1.2),{.4,1,5},0x7f6546,id,0,false,true);
            } else {
                add(PieceKind::Chest,base+glm::dvec3(.2,1.63,.1),{1,1,1},0x587981,id);
                add(PieceKind::Floor,base+glm::dvec3(-.2,2.59,.1),{.8,.45,.6},0xc0a272,id,0,false,false);
            }
        }
    }
    for(int i=0;i<6;++i,++id) {
        const auto p=at(terrain,1202.+i*5,-920.+(i%2)*5,1.5);
        content.resourceNodes.push_back({id,{p.x,p.y,p.z,0},{ItemKind::CutStone,8}});
        const auto base=grounded(PieceKind::Brick2x4,p.x,p.z,{1.1,1.8,1.2},i%2?0x718996:0x899ca2,true,0,id);
        add(PieceKind::Brick2x2,base+glm::dvec3(0,1.73,0),{.7,.7,.7},0xd6c19a,id,0,false,false);
    }
    // Irregular, sparse ground cover. These shallow colored plates and foliage
    // are cosmetic; all walking remains on the exact installed terrain surface.
    const auto patch=[&](double x,double z,uint32_t color,int seed) {
        for(int n=0;n<4;++n) {
            const double angle=(seed+n)*2.3999632297,r=.7+n*.63;
            grounded(PieceKind::Floor,x+std::cos(angle)*r,z+std::sin(angle)*r,
                {.45+(n%2)*.25,.09,.4+(n%3)*.2},color,false,uint8_t(n%2));
        }
    };
    const auto verge=[&](glm::dvec2 from,glm::dvec2 to,int count,uint32_t color,int seed) {
        const auto direction=glm::normalize(to-from);const glm::dvec2 normal{-direction.y,direction.x};
        for(int i=0;i<count;++i) {
            const double t=(i+.3)/count;
            const auto p=glm::mix(from,to,t)+normal*((i%2?1:-1)*(3.6+(i%3)*.7));
            patch(p.x,p.y,color,seed+i);
            if(i%3==0) {
                const auto f=at(terrain,p.x+1.3,p.y-1.2,.3);
                add(PieceKind::Brick1x2,f,{.24,.55,.16},0x52784b,0,0,false,false,uint8_t(i%2));
                add(PieceKind::Floor,f+glm::dvec3(0,.5,0),{.22,.22,.22},i%2?0xe9bc69:0xd8a6a0,0,0,false,false);
            }
        }
    };
    verge({1178,-1099},{1236,-1079},13,0xb5a375,1);
    verge({1240,-1077},{1304,-1065},11,0xb5aa83,4);
    verge({1310,-1046},{1387,-961},12,0x7f9691,8);
    verge({1206,-930},{1154,-960},10,0xa69761,12);
    verge({1154,-966},{1121,-971},9,0xbb8654,17);
    for(const auto& destination:destinations_)for(int i=0;i<7;++i) {
        const double angle=i*2.3999632297,r=15+(i%3)*2;
        const double x=destination.feet.x+std::cos(angle)*r,z=destination.feet.z+std::sin(angle)*r;
        patch(x,z,destination.id==3?0x81979d:destination.id==5?0xaa7848:0xa79c77,i+int(destination.id));
        if(i%3==0)grounded(PieceKind::Brick1x2,x,z,{.8,.5,.8},destination.id==3?0x71858e:0x9d927b,true,uint8_t(i%2));
    }
    core::Sha256 hash;hash.string("voxys.frontier.dawnreach.v1");hash.string(installedWorld().samplesSha256);
    hash.string(frontierContentFingerprint());hash.string(frontierBuildingCatalogFingerprint());hash.string("frontier-kit-r01;dawnreach-world-r02");
    content.identity=hash.finish();error.clear();return true;
}
bool FrontierWorld::visible(const FrontierVisual& piece,const AdventureState& state) const noexcept {
    if(piece.resource&&std::binary_search(state.depletedNodes.begin(),state.depletedNodes.end(),piece.resource))return false;
    if(piece.site&&piece.kind==PieceKind::Chest) {
        const auto* site=frontierSite(state,piece.site);if(site&&site->rewardClaimRevision)return false;
    }
    return true;
}
bool FrontierWorld::appendSolids(const AdventureState& state,std::vector<AdventureSpatialQueries::Solid>& solids) const {
    uint64_t index=0;
    for(const auto& p:visuals_) {
        ++index;if(!p.solid||!visible(p,state))continue;
        const auto* definition=buildingDefinition(p.kind);if(!definition)return false;
        for(const auto& box:definition->solids) {
            if(solids.size()>=AdventureSpatialQueries::maximumSolids)return false;
            auto solid=visualSolid(p,box);
            solid.structure={state.world,frontierSceneryId};
            solid.part={state.world,p.resource?resourcePart(p.resource):frontierSceneryId-index};
            solids.push_back(solid);
        }
    }
    return true;
}
std::vector<AdventureSpatialQueries::Solid> FrontierWorld::clearance() const {
    std::vector<AdventureSpatialQueries::Solid> out;
    for(const auto& destination:destinations_)
        out.push_back({{},{},destination.feet-glm::dvec3(22,8,22),destination.feet+glm::dvec3(22,40,22)});
    out.push_back({{},{},start_-glm::dvec3(22,8,22),start_+glm::dvec3(22,20,22)});
    out.push_back({{},{},camp_-glm::dvec3(10,8,10),camp_+glm::dvec3(10,20,10)});
    // Clear the first sight line and a navigable departure corridor.
    out.push_back({{},{},{1170,-5,-1110},{1325,60,-1070}});
    return out;
}
bool FrontierWorld::protectedEdit(const AdventureState& before,const AdventureState& after,std::string& error) const {
    for(const auto& structure:after.structures)for(const auto& part:structure.parts) {
        if(const auto* old=AdventureSession::findPart(before,part.id);old&&*old==part)continue;
        const auto* def=buildingDefinition(part.kind);if(!def)continue;
        const glm::dvec3 position=glm::dvec3(part.position.x,part.position.y,part.position.z)*.02;
        // The original camp remains a guaranteed recovery destination even
        // after a bed is moved or dismantled. A player cannot brick it shut.
        const AdventureSpatialQueries::Solid recoverySpace{{},{},start_-glm::dvec3(1.4,.1,1.4),start_+glm::dvec3(1.4,5.1,1.4)};
        for(const auto& box:def->solids) {
            auto a=glm::dvec3(box.minimum.x,box.minimum.y,box.minimum.z)*.02;
            auto b=glm::dvec3(box.maximum.x,box.maximum.y,box.maximum.z)*.02;
            for(uint8_t turn=0;turn<part.yawQuarterTurns;++turn){const auto lo=a,hi=b;a={lo.z,lo.y,-hi.x};b={hi.z,hi.y,-lo.x};}
            if(overlap({{},{},position+a,position+b},recoverySpace)) {
                error="Leave the starting camp's recovery space clear.";return false;
            }
        }
        for(const auto service:servicePoints_) {
            // Reserve the exact terrain-grounded service point used by E.
            const AdventureSpatialQueries::Solid protectedSpace{{},{},service-glm::dvec3(1.4,.1,1.4),service+glm::dvec3(1.4,5.1,1.4)};
            for(const auto& box:def->solids) {
                auto a=glm::dvec3(box.minimum.x,box.minimum.y,box.minimum.z)*.02;
                auto b=glm::dvec3(box.maximum.x,box.maximum.y,box.maximum.z)*.02;
                for(uint8_t turn=0;turn<part.yawQuarterTurns;++turn){auto lo=a,hi=b;a={lo.z,lo.y,-hi.x};b={hi.z,hi.y,-lo.x};}
                if(overlap({{},{},position+a,position+b},protectedSpace)) {
                    error="Leave the beacon's service point clear. Build your camp beside it.";return false;
                }
            }
        }
    }
    return true;
}
} // namespace voxy::game::adventure
