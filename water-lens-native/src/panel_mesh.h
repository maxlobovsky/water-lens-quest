#pragma once
#include <cstdio>
#include <vector>
#include <cstring>
#include "panel_controls.h"
#include "font_metrics.h"

// The renderer and preview use this same geometry; controls and hit tests share rects.
struct PanelMesh {
    struct Color {float r,g,b,a;};
    std::vector<float> vertices;
    int mainCount=0;
    static constexpr Color ink{.85f,.94f,.97f,1},muted{.42f,.59f,.64f,1},accent{.11f,.77f,.64f,1};
    void quad(UiRect r,Color c,float kind,float radius,float u=0,float v=0,float uw=1,float vh=1){
        const float xyuv[]{r.x,r.y,u,v+vh,r.x+r.w,r.y,u+uw,v+vh,r.x,r.y+r.h,u,v,
            r.x,r.y+r.h,u,v,r.x+r.w,r.y,u+uw,v+vh,r.x+r.w,r.y+r.h,u+uw,v};
        for(int i=0;i<6;i++){
            const float vertex[]{xyuv[i*4],xyuv[i*4+1],xyuv[i*4+2],xyuv[i*4+3],c.r,c.g,c.b,c.a,kind,r.w,r.h,radius};
            vertices.insert(vertices.end(),vertex,vertex+12);
        }
    }
    void box(UiRect r,Color c,float radius=.012f){quad(r,c,0,radius);}
    static float textWidth(const char* str,float height){float w=0;for(const char* p=str;*p;p++)if(*p>=32&&*p<127)w+=glyphAdvance[*p-32]*height/44;return w;}
    void text(const char* str,float x,float top,float height,Color c=ink){
        for(const char* p=str;*p;p++)if(*p>=32&&*p<127){
            const int ch=*p-32;
            quad({x,top-height,32*height/44,height},c,1,0,float(ch%16*32)/512,float(ch/16*44)/264,32.f/512,44.f/264);
            x+=glyphAdvance[ch]*height/44;
        }
    }
    void button(const PanelControls& ui,int id,const char* label,bool active=false){
        auto r=PanelControls::rect(id);
        const bool hover=ui.hovered(id),pressed=ui.pressed(id);
        box(r,hover?accent:Color{.11f,.17f,.20f,1});
        box({r.x+.002f,r.y+.002f,r.w-.004f,r.h-.004f},pressed?Color{.10f,.55f,.45f,1}:active?Color{.025f,.24f,.19f,1}:Color{.045f,.075f,.09f,1});
        const float height=id>=PanelControls::RowBase?.043f:.033f;
        text(label,r.x+(r.w-textWidth(label,height))*.5f,r.y+r.h*.5f+height*.58f,height,ink);
    }
    void cursors(const PanelControls& ui,int surface){
        for(const auto& p:ui.pointers)if(p.tracked&&p.onSurface&&p.surface==surface){
            const float size=p.pinch?.025f:.031f;
            box({p.x-size*.5f,p.y-size*.5f,size,size},ink,size*.5f);
            box({p.x-size*.32f,p.y-size*.32f,size*.64f,size*.64f},p.pinch?accent:Color{.01f,.045f,.05f,1},size*.32f);
        }
    }
    void build(const EffectSettings& s,const PanelControls& ui,bool available,bool handAim=true){
        vertices.clear();vertices.reserve(24000);
        if(ui.open){
            box(PanelControls::background,{.12f,.20f,.23f,.98f},.026f);
            box({-.487f,-.667f,.974f,1.004f},{.015f,.029f,.04f,.97f},.024f);
            text("Water Lens",-.44f,.307f,.049f);
            button(ui,PanelControls::Hide,"Hide");
            const bool seen=ui.pointers[0].tracked||ui.pointers[1].tracked;
            text(!available?"Hand pointer unavailable. Check Quest hand tracking.":seen?(handAim?"Point to highlight. Pinch to click. Hold to slide.":"Look at a control, then pinch to select."):"Hold an open hand in view. Point, then pinch to select.",-.44f,.237f,.026f,muted);
            button(ui,PanelControls::Water,s.clear?"Water: OFF":"Water: ON",!s.clear);
            button(ui,PanelControls::Auto,s.cycling?"Auto cycle: ON":"Auto cycle: OFF",s.cycling);
            button(ui,PanelControls::Calm,"Calm");button(ui,PanelControls::Ripples,"Ripples");button(ui,PanelControls::Deep,"Deep water");button(ui,PanelControls::Choppy,"Choppy");
            const char* labels[]{"Strength","Speed","Ripples","Cross waves","Sharpness","View size"};
            for(int row=0;row<PanelControls::RowCount;row++){
                float y=PanelControls::rowY(row),t=PanelControls::fraction(s,row);
                if(row==5&&s.fullView){
                    text("Surface test",-.44f,y+.023f,.035f);
                    text("Experimental",-.10f,y+.023f,.031f,accent);continue;
                }
                text(labels[row],-.44f,y+.023f,.035f);
                const int id=PanelControls::RowBase+row*3;
                button(ui,id,"-");button(ui,id+2,"+");
                box({PanelControls::trackLeft,y-.005f,PanelControls::trackWidth,.01f},{.14f,.22f,.25f,1},.005f);
                if(t>0)box({PanelControls::trackLeft,y-.005f,PanelControls::trackWidth*t,.01f},accent,.005f);
                float x=PanelControls::trackLeft+PanelControls::trackWidth*t;
                bool hot=ui.hovered(id+1)||ui.pressed(id+1);
                box({x-.017f,y-.023f,.034f,.046f},hot?accent:ink,.012f);
                char value[24];
                if(row==1||row==2)std::snprintf(value,sizeof(value),"%.1f",PanelControls::value(s,row));
                else std::snprintf(value,sizeof(value),"%d%%",int(PanelControls::value(s,row)*100+.5f));
                text(value,.365f,y+.023f,.031f);
            }
            button(ui,PanelControls::Reset,"Reset");
            if(s.fullViewAvailable)button(ui,PanelControls::Mode,s.fullView?"Back to Camera":"Try surface test (experimental)",s.fullView);
            text(s.fullView?"This test has not produced visible water distortion.":"View above 100% magnifies the camera image.",-.44f,-.545f,.023f,muted);
            cursors(ui,0);
        }
        mainCount=int(vertices.size()/12);
        if(!ui.open){
            auto r=PanelControls::launcher;
            box(r,ui.hovered(PanelControls::Open)?accent:Color{.10f,.20f,.22f,.78f},.012f);
            box({r.x+.002f,r.y+.002f,r.w-.004f,r.h-.004f},ui.pressed(PanelControls::Open)?Color{.05f,.32f,.27f,1}:Color{.015f,.045f,.055f,.88f});
            constexpr float height=.029f;
            text("Controls",r.x+(r.w-textWidth("Controls",height))*.5f,r.y+r.h*.5f+height*.58f,height);
            cursors(ui,1);
        }
    }
};
