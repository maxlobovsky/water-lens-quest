#include "../src/panel_mesh.h"
#include "../src/water_surface.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv){
    PanelControls ui;EffectSettings s;PanelInput p[2];double time=1;
    assert(!ui.open); // Starts unobstructed with only the compact tab.
    ui.open=true;
    auto aim=[&](int id,int h=0){const auto r=PanelControls::rect(id);p[h]={true,false,true,id==PanelControls::Open?1:0,r.x+r.w*.5f,r.y+r.h*.5f};};
    auto tick=[&](){time+=.15;return ui.update(p,time,s);};
    auto click=[&](int id){aim(id);tick();p[0].pinch=true;tick();p[0].pinch=false;return tick();};
    for(int id=0;id<=6;id++){aim(id);assert(PanelControls::hit(p[0],true)==id);}
    aim(PanelControls::Open);assert(PanelControls::hit(p[0],false)==PanelControls::Open);
    assert(PanelControls::hit(p[0],true)==PanelControls::None); // Hidden launcher cannot intercept main-panel clicks.
    for(int id=PanelControls::RowBase;id<PanelControls::RowEnd;id++){aim(id);assert(PanelControls::hit(p[0],true)==id);}
    aim(PanelControls::Choppy);assert(PanelControls::hit(p[0],true)==PanelControls::Choppy);
    assert(click(PanelControls::Water)&PanelControls::Changed);assert(s.clear);
    tick();assert(s.clear); // No repeat while hovering.
    assert(click(PanelControls::Auto)&PanelControls::CycleChanged);assert(!s.cycling);
    click(PanelControls::Deep);assert(!s.clear&&!s.cycling&&s.strength==.85f);
    click(PanelControls::Choppy);assert(s.speed==7.f&&s.sharpness==.9f&&!s.cycling);
    click(PanelControls::Calm);assert(s.strength==.2f&&s.speed==.3f&&s.sharpness==0);
    click(PanelControls::RowBase+2);assert(std::abs(s.strength-.25f)<.0001f);
    click(PanelControls::RowBase);assert(std::abs(s.strength-.20f)<.0001f);
    for(int row=0;row<PanelControls::RowCount;row++){
        const int id=PanelControls::RowBase+row*3+1;
        aim(id);tick();p[0].pinch=true;tick();
        p[0].x=1;tick();assert(PanelControls::value(s,row)==PanelControls::maximum(row));
        p[0].x=-1;tick();assert(PanelControls::value(s,row)==PanelControls::minimum(row));
        p[0].tracked=false;tick();assert(ui.owner==-1);
    }
    assert(PanelControls::maximum(1)==12.f);
    PanelControls::set(s,5,2.4f);assert(s.coverage==2.4f);
    PanelControls::set(s,5,4.f);assert(s.coverage==3.f);
    assert(!PanelControls::launcher.contains(0,0)); // Keep the center clear.
    assert(PanelControls::launcher.w/PanelControls::launcherDepth<.18f);
    s.cycling=true;click(PanelControls::RowBase+4*3+2);assert(s.sharpness>.049f&&!s.cycling);
    // Tracking loss cancels clicks and already-pinched reacquisition cannot click.
    aim(PanelControls::Water);tick();p[0].pinch=true;tick();p[0].tracked=false;tick();
    p[0].tracked=true;tick();p[0].pinch=false;tick();assert(!s.clear);
    // A button press released elsewhere is cancelled.
    aim(PanelControls::Water);tick();p[0].pinch=true;tick();p[0].x=0;p[0].y=-.56f;p[0].pinch=false;tick();assert(!s.clear);
    // Only one hand owns a control until released; the second must open to rearm.
    aim(PanelControls::Water);aim(PanelControls::Auto,1);tick();p[0].pinch=p[1].pinch=true;tick();
    assert(ui.owner==0);p[0].pinch=false;tick();assert(s.clear);p[1].pinch=false;tick();assert(!s.cycling);
    p[1]={};click(PanelControls::Hide);assert(!ui.open);
    aim(PanelControls::Water);assert(PanelControls::hit(p[0],ui.open)==PanelControls::None);
    assert(click(PanelControls::Open)&PanelControls::Recenter);assert(ui.open);
    click(PanelControls::Reset);assert(!s.clear&&!s.cycling&&s.coverage==1.f&&s.strength==.35f);
    s.fullViewAvailable=true;click(PanelControls::Mode);assert(s.fullView&&s.coverage==1);
    click(PanelControls::RowBase+17);assert(s.coverage==1);
    PanelControls::set(s,5,3);assert(s.coverage==1); // Full view cannot silently zoom.
    click(PanelControls::Reset);assert(s.fullView&&s.fullViewAvailable);
    click(PanelControls::Mode);assert(!s.fullView&&s.coverage==1);
    s.fullViewAvailable=false;click(PanelControls::Mode);assert(!s.fullView);
    // The mutable mesh stays finite, covers the measured Quest frusta with
    // margin, and never changes its angular sampling or triangle topology.
    struct Vertex {float x,y,z;};
    std::vector<Vertex> surface(WaterSurface::vertexCount);
    std::vector<uint32_t> indices(WaterSurface::triangleCount*3);
    WaterSurface::indices(indices.data());
    for(auto i:indices)assert(i<surface.size());
    WaterSurface::vertices(surface.data(),0,.8f,1,.5f,0);
    WaterSurface::diagnosticWindow(surface.data(),0);
    assert(std::abs(surface.back().x/-surface.back().z-.44f)<.00001f);
    WaterSurface::vertices(surface.data(),0,.8f,1,.5f,0);
    WaterSurface::diagnosticWindow(surface.data(),1.5707963f);
    assert(std::abs(surface.back().x/-surface.back().z-.572f)<.00001f);
    for(float strength:{0.f,.35f,1.f})for(float sharp:{0.f,1.f})for(float phase:{0.f,.4f,628.f})for(float density:{.3f,2.5f}){
        WaterSurface::vertices(surface.data(),phase,strength,density,.8f,sharp);
        for(uint32_t y=0;y<=WaterSurface::rows;y++)for(uint32_t x=0;x<=WaterSurface::columns;x++){
            const auto v=surface[y*(WaterSurface::columns+1)+x];
            assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
            assert(-v.z>=.799f&&-v.z<=20.01f);
            assert(std::abs(v.x/-v.z-WaterSurface::extent*(2.f*x/WaterSurface::columns-1))<.00001f);
            assert(std::abs(v.y/-v.z-WaterSurface::extent*(2.f*y/WaterSurface::rows-1))<.00001f);
        }
        for(size_t i=0;i<indices.size();i+=3){
            auto a=surface[indices[i]],b=surface[indices[i+1]],c=surface[indices[i+2]];
            const float area=(b.x/-b.z-a.x/-a.z)*(c.y/-c.z-a.y/-a.z)-(b.y/-b.z-a.y/-a.z)*(c.x/-c.z-a.x/-a.z);
            assert(area>0); // Facing the wearer, with no angular holes or folds.
        }
    }
    // Every control owns a nonoverlapping, visible target of at least 6 cm height.
    for(int id=0;id<=6;id++)assert(PanelControls::rect(id).h>=.06f);
    for(int id=PanelControls::RowBase;id<PanelControls::RowEnd;id++)assert(PanelControls::rect(id).h>=.06f);
    PanelMesh mesh;ui.resetInput();s={};mesh.build(s,ui,true);
    assert(mesh.mainCount>0&&mesh.vertices.size()%12==0);
    for(float v:mesh.vertices)assert(std::isfinite(v));
    // Hovering or reacquiring a hand already pinched must not reopen the tab.
    ui.open=false;ui.resetInput();aim(PanelControls::Open);tick();tick();assert(!ui.open);
    p[0].pinch=true;tick();p[0].tracked=false;tick();p[0].tracked=true;tick();
    p[0].pinch=false;tick();assert(!ui.open);
    assert(click(PanelControls::Open)&PanelControls::Recenter);assert(ui.open);
    ui.resetInput();mesh.build(s,ui,true);
    if(argc>1&&std::strcmp(argv[1],"--preview-closed")==0){
        ui.open=false;mesh.build(s,ui,true);
        assert(mesh.mainCount==0&&!mesh.vertices.empty());
    }
    if(argc>1){
        s.fullViewAvailable=true;s.fullView=false;mesh.build(s,ui,true);
        std::printf("{\"mainCount\":%d,\"vertices\":[",mesh.mainCount);
        for(size_t i=0;i<mesh.vertices.size();i++)std::printf(i?",%.7g":"%.7g",mesh.vertices[i]);
        std::puts("]}");return 0;
    }
    std::puts("PASS: panel controls, mode switching, no zoom in full view, tracking loss, ownership, hide/reopen, projected mesh bounds/winding/angular coverage");
}
